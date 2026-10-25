#include "config_manager.h"

#include <WiFi.h>

#include <new>

#include "serial_log.h"
#ifdef ARDUINO
#include <esp_wifi.h>
#endif
#ifdef ARDUINO
std::string NvsConfigStorage::read(unsigned slot) {
  const char* key = slot ? "record1" : "record0";
  size_t length = preferences_.getBytesLength(key);
  if (length < 32 || length > Config::maxTransfer + 95) return {};
  std::string result(length, '\0');
  if (preferences_.getBytes(key, &result[0], length) != length) return {};
  return result;
}
bool NvsConfigStorage::write(unsigned slot, const std::string& bytes) {
  return preferences_.putBytes(slot ? "record1" : "record0", bytes.data(), bytes.size()) ==
         bytes.size();
}
#endif
NetworkSettings ConfigManager::networkSettings(const Config::Snapshot& snapshot,
                                               const std::string& password) {
  auto n = snapshot.document["network"];
  NetworkSettings s;
  s.ssid = n["ssid"].as<const char*>();
  s.password = password;
  s.dhcp = n["dhcp"];
  s.ip = n["ip"].as<const char*>();
  s.subnet = n["subnet"].as<const char*>();
  s.gateway = n["gateway"].as<const char*>();
  s.dns1 = n["dns1"].as<const char*>();
  s.dns2 = n["dns2"].as<const char*>();
  return s;
}
void ConfigManager::begin(Network& network, Commands& commands, uint32_t now) {
  network_ = &network;
  commands_ = &commands;
  active_.reset(new Config::Snapshot);
  std::string error;
  if (journal_.load(record_))
    active_->parse(record_.configuration, error);
  else {
    active_->development();
    auto settings = network.developmentSettings();
    active_->document["network"]["ssid"] = settings.ssid;
    active_->document["network"]["passwordConfigured"] = !settings.password.empty();
    record_.password = settings.password;
    if (!active_->build(error)) {
      // Invalid compiled examples must not become a partially active snapshot.
      // Leave storage untouched and offer a valid, silent recovery editor.
      auto buttons = active_->document["buttons"].to<JsonArray>();
      for (unsigned b = 0; b < 4; ++b) {
        auto button = buttons.createNestedObject();
        button["label"] = std::string("B") + char('1' + b);
        button["enabled"] = false;
        button.createNestedArray("slots");
      }
      auto routing = active_->document["routing"].to<JsonObject>();
      routing["master"] = false;
      routing.createNestedArray("destinations");
      if (!Config::validSsid(settings.ssid)) active_->document["network"]["ssid"] = "";
      active_->build(error);
      FirmwareSerial.println(
          "[Config] Invalid development mappings; recovery settings loaded without saving.");
    }
    record_.configuration = Config::json(active_->document.as<JsonVariantConst>());
  }
  active_->document.shrinkToFit();
  active_->build(error);
  commands_->configure(active_->view);
  network_->configure(networkSettings(*active_, record_.password), now);
}
std::string ConfigManager::response(bool ok, const char* error, bool includeConfiguration) {
  DynamicJsonDocument out(includeConfiguration ? active_->document.memoryUsage() + 2048 : 1536);
  out["ok"] = ok;
  out["error"] = error;
  out["revision"] = record_.revision;
  out["lastRequest"] = record_.request;
  out["networkRequest"] = lastRequest_;
  out["networkError"] = lastError_;
  out["pending"] = pending_;
  if (includeConfiguration) out["configuration"].set(active_->document.as<JsonVariantConst>());
  return Config::json(out.as<JsonVariantConst>());
}
std::string ConfigManager::commit(uint32_t request, uint32_t fingerprint) {
  if (Config::json(candidate_->document.as<JsonVariantConst>()) == record_.configuration &&
      candidatePassword_ == record_.password) {
    candidate_.reset();
    return response(true);
  }
  if (record_.revision == UINT32_MAX) {
    candidate_.reset();
    return response(false, "Revision exhausted");
  }
  candidate_->document.shrinkToFit();
  std::string validationError;
  if (!candidate_->build(validationError)) {
    candidate_.reset();
    return response(false, "Configuration allocation failed");
  }
  Config::Record next;
  next.revision = record_.revision + 1;
  next.request = request;
  next.fingerprint = fingerprint;
  candidate_->document["revision"] = next.revision;
  next.configuration = Config::json(candidate_->document.as<JsonVariantConst>());
  next.password = candidatePassword_;
#ifdef ARDUINO
  const uint32_t persistAt = millis();
#endif
  const bool persisted = journal_.save(next);
#ifdef ARDUINO
  FirmwareSerial.printf("[ConfigTiming] request=%lu persist_ms=%lu bytes=%u ok=%u\n",
    static_cast<unsigned long>(request), static_cast<unsigned long>(millis() - persistAt),
    unsigned(next.configuration.size()), persisted);
#endif
  if (!persisted) {
    candidate_.reset();
    return response(false, "Persistence failed; previous settings retained");
  }
  active_.swap(candidate_);
  commands_->configure(active_->view);
  candidate_.reset();
  record_ = std::move(next);
  return response(true);
}
std::string ConfigManager::execute(uint32_t request, uint32_t fingerprint, const std::string& bytes,
                                   uint32_t now) {
#ifdef ARDUINO
  if (ESP.getFreeHeap() < 65536 + std::min(bytes.size() * 2, size_t(32768)) ||
      ESP.getMaxAllocHeap() < 32768)
    return "{\"ok\":false,\"error\":\"Configuration memory capacity unavailable; query when "
           "idle\"}";
#endif
  DynamicJsonDocument input(std::min(Config::documentCapacity, bytes.size() * 2 + 1024));
  if (bytes.size() > Config::maxTransfer ||
      deserializeJson(input, bytes, DeserializationOption::NestingLimit(12)) ||
      (!input["schema"].is<unsigned>() || input["schema"] != Config::schema) ||
      !input["op"].is<const char*>())
    return response(false, "Invalid request/schema");
  const std::string op = input["op"].as<const char*>();
  for (JsonPairConst field : input.as<JsonObjectConst>()) {
    const std::string key = field.key().c_str();
    if (key != "schema" && key != "op" && key != "revision" && key != "button" && key != "value" &&
        key != "credential" && key != "scope")
      return response(false, "Unsupported request field");
  }

  if (op == "read_scope") {
    const std::string scope = input["scope"] | "";
    DynamicJsonDocument out(active_->document.memoryUsage() + 1536);
    out["ok"] = true;
    out["revision"] = record_.revision;
    out["pending"] = pending_;
    out["scope"] = scope;
    if (scope == "button" && input["button"].is<unsigned>() && input["button"].as<unsigned>() < 4) {
      out["button"] = input["button"];
      out["value"].set(active_->document["buttons"][input["button"].as<unsigned>()]);
    } else if (scope == "routing" || scope == "network") out["value"].set(active_->document[scope]);
    else return response(false, "Invalid read scope");
    if (out.overflowed()) return response(false, "Scoped response capacity exceeded");
    return Config::json(out.as<JsonVariantConst>());
  }
  if (op == "read") return response(true, "", true);
  if (op == "query") return completed_ ? reply_ : response(true);
  if (record_.request == request) {
    return response(record_.fingerprint == fingerprint,
                    record_.fingerprint == fingerprint ? "" : "Request identifier reused");
  }
  if (pending_) return response(false, "Operation busy; query before retry");
  completed_ = false;
  if (op == "scan") {
#ifdef ARDUINO
    WiFi.persistent(false);
    if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    int result = WiFi.scanNetworks(true, true);
    if (result == WIFI_SCAN_FAILED) return response(false, "Scan could not start");
    pending_ = scan_ = true;
    started_ = now;
    return {};
#else
    return response(false, "Scan unavailable");
#endif
  }
  if (!input["revision"].is<uint32_t>() || input["revision"].as<uint32_t>() != record_.revision)
    return response(false, "Revision conflict; reload and reconcile drafts");
  candidate_.reset(new (std::nothrow) Config::Snapshot);
  if (!candidate_) return response(false, "Configuration memory capacity unavailable");
  candidate_->document.set(active_->document.as<JsonVariantConst>());
  candidatePassword_ = record_.password;
  if (op == "button" && input["button"].is<unsigned>() && input["button"].as<unsigned>() < 4)
    candidate_->document["buttons"][input["button"].as<unsigned>()].set(input["value"]);
  else if (op == "routing")
    candidate_->document["routing"].set(input["value"]);
  else if (op == "network") {
    if (!Config::credential(input["credential"], record_.password, candidatePassword_)) {
      candidate_.reset();
      return response(false, "Invalid credential operation");
    }
    candidate_->document["network"].set(input["value"]);
    candidate_->document["network"]["passwordConfigured"] = !candidatePassword_.empty();
    if (candidate_->document["network"]["ssid"] == "") {
      candidate_.reset();
      candidatePassword_.clear();
      return response(false, "Apply network requires an SSID");
    }
  } else {
    candidate_.reset();
    return response(false, "Unknown operation or button index");
  }
  std::string error;
#ifdef ARDUINO
  const uint32_t validateAt = millis();
#endif
  const bool valid = candidate_->build(error);
#ifdef ARDUINO
  FirmwareSerial.printf("[ConfigTiming] request=%lu validation_ms=%lu bytes=%u\n",
    static_cast<unsigned long>(request), static_cast<unsigned long>(millis() - validateAt), unsigned(bytes.size()));
#endif
  if (!valid) {
    candidate_.reset();
    candidatePassword_.clear();
    return response(false, error.c_str());
  }
  if (op != "network") return commit(request, fingerprint);
  if (Config::json(candidate_->document.as<JsonVariantConst>()) == record_.configuration &&
      candidatePassword_ == record_.password) {
    candidate_.reset();
    return response(true);
  }
  pending_ = true;
  scan_ = false;
  started_ = now;
  pendingRequest_ = request;
  pendingFingerprint_ = fingerprint;
  lastRequest_ = request;
  lastError_.clear();
  network_->configure(networkSettings(*candidate_, candidatePassword_), now);
  return {};
}
void ConfigManager::update(uint32_t now) {
  if (!pending_) return;
  if (scan_) {
#ifdef ARDUINO
    int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING && uint32_t(now - started_) < 10000) return;
    pending_ = false;
    scan_ = false;
    DynamicJsonDocument out(4096);
    out["ok"] = count >= 0;
    out["error"] = count >= 0 ? "" : "Scan failed/timed out";
    auto list = out.createNestedArray("networks");
    for (int i = 0; i < count && i < 20; ++i) {
      auto item = list.createNestedObject();
      String name = WiFi.SSID(i);
      if (!Config::validSsid(std::string(name.c_str(), name.length()))) {
        list.remove(list.size() - 1);
        continue;
      }
      item["ssid"] = name;
      item["rssi"] = WiFi.RSSI(i);
      item["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    if (count == WIFI_SCAN_RUNNING) esp_wifi_scan_stop();
    WiFi.scanDelete();
    reply_ = Config::json(out.as<JsonVariantConst>());
    completed_ = true;
#endif
    return;
  }
  if (!network_->connected() && uint32_t(now - started_) < 30000) return;
  pending_ = false;
  if (network_->connected()) {
    reply_ = commit(pendingRequest_, pendingFingerprint_);
    if (record_.request != pendingRequest_) {
      lastError_ = "Persistence failed; previous network restored";
      network_->configure(networkSettings(*active_, record_.password), now);
    }
  } else {
    candidate_.reset();
    lastError_ = "Association/IP acquisition timed out; previous network restored";
    network_->configure(networkSettings(*active_, record_.password), now);
    reply_ = response(false, lastError_.c_str());
  }
  candidatePassword_.clear();
  completed_ = true;
}
