#include "configuration.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "commands.dev.h"
namespace Config {
uint32_t crc32(const uint8_t* bytes, size_t size) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
std::string json(JsonVariantConst v) {
  std::string out;
  serializeJson(v, out);
  return out;
}
static bool keys(JsonVariantConst v, const char* allowed) {
  if (!v.is<JsonObjectConst>()) return false;
  for (JsonPairConst p : v.as<JsonObjectConst>()) {
    std::string key = std::string("|") + p.key().c_str() + "|";
    if (!strstr(allowed, key.c_str())) return false;
  }
  return true;
}
static bool text(JsonVariantConst v, size_t min, size_t max, bool lines = false) {
  if (!v.is<const char*>()) return false;
  const char* s = v.as<const char*>();
  size_t n = v.as<JsonString>().size();
  if (n < min || n > max || strlen(s) != n) return false;
  for (size_t i = 0; i < n; ++i)
    if ((uint8_t(s[i]) < 32 || uint8_t(s[i]) > 126) &&
        !(lines && (s[i] == '\n' || s[i] == '\r' || s[i] == '\t')))
      return false;
  return true;
}
static bool integer(JsonVariantConst v, int min, int max) {
  return v.is<int>() && v.as<int>() >= min && v.as<int>() <= max;
}
static bool quad(const char* s, uint32_t& value) {
  if (!s) return false;
  uint32_t result = 0;
  for (int part = 0; part < 4; ++part) {
    unsigned n = 0, digits = 0;
    while (*s >= '0' && *s <= '9') {
      if (++digits > 3) return false;
      n = n * 10 + unsigned(*s++ - '0');
    }
    if (!digits || n > 255) return false;
    result = (result << 8) | n;
    if (part < 3) {
      if (*s++ != '.') return false;
    } else if (*s)
      return false;
  }
  value = result;
  return true;
}
bool ipv4(const char* s, uint32_t& value, bool zero) {
  if (!quad(s, value)) return false;
  return (zero && value == 0) || ((value >> 24) > 0 && (value >> 24) != 127 && (value >> 24) < 224);
}
static bool destinations(JsonVariantConst v, Command::Destination* out) {
  if (!v.is<JsonArrayConst>() || v.size() > 8) return false;
  size_t i = 0;
  for (auto d : v.as<JsonArrayConst>()) {
    if (!keys(d, "|ip||port|") || !text(d["ip"], 7, 15) || !integer(d["port"], 1, 65535))
      return false;
    Command::Destination entry{d["ip"].as<const char*>(), d["port"].as<uint32_t>()};
    Command::IPv4 ip;
    if (Command::validateDestination(entry, ip) != Command::DestinationStatus::Valid) return false;
    out[i++] = entry;
  }
  return true;
}
bool validSsid(const std::string& value) {
  if (value.size() > 32) return false;
  for (size_t i = 0; i < value.size();) {
    uint8_t lead = uint8_t(value[i++]);
    uint32_t cp;
    unsigned extra;
    if (lead < 0x80) {
      cp = lead;
      extra = 0;
    } else if (lead >= 0xc2 && lead <= 0xdf) {
      cp = lead & 31;
      extra = 1;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      cp = lead & 15;
      extra = 2;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      cp = lead & 7;
      extra = 3;
    } else
      return false;
    if (i + extra > value.size()) return false;
    for (unsigned n = 0; n < extra; ++n) {
      uint8_t next = uint8_t(value[i++]);
      if ((next & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (next & 63);
    }
    if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 || (cp >= 127 && cp <= 159))
      return false;
  }
  return true;
}
bool validateNetwork(JsonVariantConst n, std::string& error) {
  if (!keys(n, "|ssid||passwordConfigured||dhcp||ip||subnet||gateway||dns1||dns2|") ||
      !n["ssid"].is<const char*>() || !validSsid(n["ssid"].as<std::string>()) ||
      strlen(n["ssid"].as<const char*>()) != n["ssid"].as<JsonString>().size() ||
      !n["passwordConfigured"].is<bool>() || !n["dhcp"].is<bool>()) {
    error = "Invalid network fields";
    return false;
  }
  uint32_t ip, mask, gateway, dns1, dns2;
  if (!ipv4(n["ip"], ip, true) || !ipv4(n["gateway"], gateway, true) ||
      !ipv4(n["dns1"], dns1, true) || !ipv4(n["dns2"], dns2, true)) {
    error = "Invalid IPv4 address";
    return false;
  }
  // Subnet masks are bit masks, not unicast destinations.
  if (!quad(n["subnet"], mask)) {
    error = "Invalid subnet";
    return false;
  }
  if (!n["dhcp"].as<bool>()) {
    uint32_t host = ~mask;
    if (!ip || !mask || !host || (host & (host + 1)) || host < 3 || !(ip & host) ||
        (ip & host) == host || !gateway || gateway == ip || (gateway & mask) != (ip & mask) ||
        !(gateway & host) || (gateway & host) == host) {
      error = "Invalid static subnet/IP/gateway";
      return false;
    }
  }
  return true;
}
bool credential(JsonVariantConst c, const std::string& old, std::string& next) {
  if (!keys(c, "|mode||value|") || !c["mode"].is<const char*>()) return false;
  std::string mode = c["mode"].as<const char*>();
  if (mode == "keep") {
    if (c.containsKey("value")) return false;
    next = old;
    return true;
  }
  if (mode == "open") {
    if (c.containsKey("value")) return false;
    next.clear();
    return true;
  }
  if (mode != "replace" || !c["value"].is<const char*>() ||
      c["value"].as<JsonString>().size() < 8 || c["value"].as<JsonString>().size() > 63 ||
      strlen(c["value"].as<const char*>()) != c["value"].as<JsonString>().size())
    return false;
  next = c["value"].as<const char*>();
  return true;  // Never trim.
}
Snapshot::Snapshot() {}
bool Snapshot::parse(const std::string& bytes, std::string& error) {
  if (bytes.size() > maxTransfer ||
      deserializeJson(document, bytes, DeserializationOption::NestingLimit(12))) {
    error = "Malformed/oversized configuration";
    return false;
  }
  return build(error);
}
bool Snapshot::build(std::string& error) {
  auto root = document.as<JsonVariantConst>();
  auto fail = [&](const char* message) {
    error = message;
    return false;
  };
  if (document.overflowed() || measureJson(document) > maxConfiguration ||
      !keys(root, "|schema||revision||buttons||routing||network|") ||
      (!root["schema"].is<unsigned>() || root["schema"] != schema) ||
      !root["revision"].is<uint32_t>())
    return fail("Unsupported configuration schema/revision");
  auto values = root["buttons"];
  if (!values.is<JsonArrayConst>() || values.size() != 4)
    return fail("Exactly four buttons required");
  auto routing = root["routing"];
  if (!keys(routing, "|master||destinations|") || !routing["master"].is<bool>() ||
      !destinations(routing["destinations"], master_))
    return fail("Invalid routing list");
  view.useMaster = routing["master"];
  view.master = {master_, routing["destinations"].size()};
  if (!validateNetwork(root["network"], error)) return false;
  for (size_t b = 0; b < 4; ++b) {
    auto button = values[b];
    auto list = button["slots"];
    if (!keys(button, "|label||enabled||slots|") || !text(button["label"], 1, 12) ||
        !button["enabled"].is<bool>() || !list.is<JsonArrayConst>() || list.size() > 4)
      return fail("Invalid button label/enabled/four-slot limit");
    buttons[b] = Triggers::Button(button["label"], triggers_[b], list.size(), button["enabled"]);
    for (size_t t = 0; t < list.size(); ++t) {
      auto trigger = list[t];
      auto& slot = slots_[b][t];
      if (!keys(trigger, "|enabled||protocol||osc||udp||midi|") || !trigger["enabled"].is<bool>() ||
          !trigger["protocol"].is<const char*>())
        return fail("Invalid typed trigger");
      const std::string protocol = trigger["protocol"].as<const char*>();
      bool enabled = trigger["enabled"];
      if (protocol == "osc") {
        auto osc = trigger["osc"];
        auto args = osc["args"];
        if (trigger.containsKey("udp") || trigger.containsKey("midi") ||
            !keys(osc, "|address||args||destinations|") || !text(osc["address"], 2, 128) ||
            !args.is<JsonArrayConst>() || args.size() > 8 ||
            !destinations(osc["destinations"], slot.destinations))
          return fail("Invalid OSC settings");
        size_t a = 0;
        for (auto arg : args.as<JsonArrayConst>()) {
          if (!keys(arg, "|type||value|") || !arg["type"].is<const char*>())
            return fail("Invalid argument");
          std::string type = arg["type"].as<const char*>();
          if (type == "i" && arg["value"].is<int32_t>())
            slot.args[a++] = Command::Argument::int32(arg["value"]);
          else if (type == "f" && arg["value"].is<double>() &&
                   std::isfinite(arg["value"].as<double>()) &&
                   std::abs(arg["value"].as<double>()) <= std::numeric_limits<float>::max()) {
            const float value = arg["value"].as<float>();
            slot.args[a] = Command::Argument::float32(value);
            document["buttons"][b]["slots"][t]["osc"]["args"][a]["value"] = value;
            ++a;
          } else if (type == "s" && text(arg["value"], 0, 256, true))
            slot.args[a++] = Command::Argument::string(arg["value"]);
          else
            return fail("Unsupported/range-invalid argument");
        }
        slot.osc = {osc["address"],
                    slot.args,
                    args.size(),
                    {slot.destinations, osc["destinations"].size()}};
        triggers_[b][t] = Triggers::Trigger::osc(&slot.osc, enabled);
      } else if (protocol == "udp") {
        auto udp = trigger["udp"];
        if (trigger.containsKey("osc") || trigger.containsKey("midi") ||
            !keys(udp, "|format||payload||lineEnding||destinations|") ||
            !destinations(udp["destinations"], slot.destinations) ||
            !udp["format"].is<const char*>() || !text(udp["payload"], 0, 2048, true) ||
            !udp["lineEnding"].is<const char*>())
          return fail("Invalid UDP settings");
        std::string format = udp["format"].as<const char*>(),
                    payload = udp["payload"].as<const char*>(),
                    ending = udp["lineEnding"].as<const char*>();
        slot.bytes.clear();
        if (format == "hex") {
          if (ending != "none" || payload.size() % 2) return fail("Invalid hex payload");
          auto nibble = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
          };
          for (size_t i = 0; i < payload.size(); i += 2) {
            int hi = nibble(payload[i]), lo = nibble(payload[i + 1]);
            if (hi < 0 || lo < 0) return fail("Invalid hex byte");
            slot.bytes.push_back(uint8_t(hi * 16 + lo));
          }
        } else if (format == "text") {
          if (ending != "none" && ending != "lf" && ending != "crlf")
            return fail("Invalid line ending");
          slot.bytes.assign(payload.begin(), payload.end());
          if (ending == "crlf") slot.bytes.push_back('\r');
          if (ending != "none") slot.bytes.push_back('\n');
        } else
          return fail("Unsupported UDP format");
        slot.udp = {
            slot.bytes.data(), slot.bytes.size(), {slot.destinations, udp["destinations"].size()}};
        triggers_[b][t] = Triggers::Trigger::udp(&slot.udp, enabled);
      } else if (protocol == "midi") {
        auto m = trigger["midi"];
        if (trigger.containsKey("osc") || trigger.containsKey("udp") ||
            !m["type"].is<const char*>() || !integer(m["channel"], 1, 16))
          return fail("Invalid MIDI settings");
        std::string type = m["type"].as<const char*>();
        int ch = m["channel"];
        if (type == "pulse" || type == "on" || type == "off") {
          if (!keys(m, type == "pulse" ? "|type||channel||note||velocity||duration|"
                                       : "|type||channel||note||velocity|") ||
              !integer(m["note"], 0, 127) || !integer(m["velocity"], type == "pulse" ? 1 : 0, 127))
            return fail("Invalid note values");
          if (type == "pulse") {
            if (!integer(m["duration"], 1, 60000)) return fail("Invalid pulse duration");
            slot.midi = Midi::Message::pulse(ch, m["note"], m["velocity"], m["duration"]);
          } else
            slot.midi = type == "on" ? Midi::Message::noteOn(ch, m["note"], m["velocity"])
                                     : Midi::Message::noteOff(ch, m["note"], m["velocity"]);
        } else if (type == "cc") {
          if (!keys(m, "|type||channel||controller||value|") || !integer(m["controller"], 0, 127) ||
              !integer(m["value"], 0, 127))
            return fail("Invalid CC values");
          slot.midi = Midi::Message::controlChange(ch, m["controller"], m["value"]);
        } else if (type == "program") {
          if (!keys(m, "|type||channel||program|") || !integer(m["program"], 0, 127))
            return fail("Invalid program");
          slot.midi = Midi::Message::programChange(ch, m["program"]);
        } else
          return fail("Unsupported MIDI type");
        triggers_[b][t] = Triggers::Trigger::midi(&slot.midi, enabled);
      } else
        return fail("Unsupported protocol");
      if (protocol != "midi") {
        Command::Packet packet;
        if (Triggers::encode(triggers_[b][t], packet) != Command::EncodeStatus::Ok)
          return fail("Invalid/oversized network command");
      }
    }
  }
  return true;
}
static void copyDest(JsonArray out, Triggers::Destinations list) {
  for (size_t i = 0; i < list.count; ++i) {
    auto d = out.createNestedObject();
    d["ip"] = list.items[i].ipv4;
    d["port"] = list.items[i].port;
  }
}
void Snapshot::development() {
  auto r = document.to<JsonObject>();
  r["schema"] = schema;
  r["revision"] = 0;
  auto bs = r.createNestedArray("buttons");
  for (size_t b = 0; b < 4; ++b) {
    auto button = bs.createNestedObject();
    const auto& source = DevelopmentCommands::buttons[b];
    button["label"] = source.label;
    button["enabled"] = source.enabled;
    auto list = button.createNestedArray("slots");
    for (size_t i = 0; i < source.count; ++i) {
      const auto& trigger = source.triggers[i];
      auto t = list.createNestedObject();
      t["enabled"] = trigger.enabled();
      if (auto* osc = trigger.osc()) {
        t["protocol"] = "osc";
        auto o = t.createNestedObject("osc");
        o["address"] = osc->address;
        auto args = o.createNestedArray("args");
        for (size_t a = 0; a < osc->argumentCount; ++a) {
          auto ar = args.createNestedObject();
          const auto& arg = osc->arguments[a];
          if (arg.type == Command::ArgumentType::Int32) {
            ar["type"] = "i";
            ar["value"] = arg.integer;
          } else if (arg.type == Command::ArgumentType::Float32) {
            ar["type"] = "f";
            ar["value"] = arg.real;
          } else {
            ar["type"] = "s";
            ar["value"] = arg.text;
          }
        }
        copyDest(o.createNestedArray("destinations"), osc->destinations);
      } else if (auto* udp = trigger.udp()) {
        t["protocol"] = "udp";
        auto u = t.createNestedObject("udp");
        u["format"] = "hex";
        std::string hex;
        const char* digits = "0123456789abcdef";
        for (size_t n = 0; n < udp->length; ++n) {
          hex += digits[udp->bytes[n] >> 4];
          hex += digits[udp->bytes[n] & 15];
        }
        u["payload"] = hex;
        u["lineEnding"] = "none";
        copyDest(u.createNestedArray("destinations"), udp->destinations);
      } else {
        t["protocol"] = "midi";
        auto m = t.createNestedObject("midi");
        const auto& msg = *trigger.midi();
        m["channel"] = msg.channel;
        switch (msg.type) {
          case Midi::Type::NotePulse:
            m["type"] = "pulse";
            m["duration"] = msg.durationMs;
            break;
          case Midi::Type::NoteOn:
            m["type"] = "on";
            break;
          case Midi::Type::NoteOff:
            m["type"] = "off";
            break;
          case Midi::Type::ControlChange:
            m["type"] = "cc";
            break;
          case Midi::Type::ProgramChange:
            m["type"] = "program";
            break;
        }
        if (msg.type == Midi::Type::ControlChange) {
          m["controller"] = msg.data1;
          m["value"] = msg.data2;
        } else if (msg.type == Midi::Type::ProgramChange)
          m["program"] = msg.data1;
        else {
          m["note"] = msg.data1;
          m["velocity"] = msg.data2;
        }
      }
    }
  }
  auto route = r.createNestedObject("routing");
  route["master"] = DevelopmentCommands::useMasterDestinations;
  copyDest(route.createNestedArray("destinations"), DevelopmentCommands::masterDestinations);
  auto n = r.createNestedObject("network");
  n["ssid"] = "";
  n["passwordConfigured"] = false;
  n["dhcp"] = true;
  for (const char* key : {"ip", "subnet", "gateway", "dns1", "dns2"}) n[key] = "0.0.0.0";
}
static void putWord(std::string& s, uint32_t n) {
  for (int b = 0; b < 4; ++b) s += char(n >> (b * 8));
}
static uint32_t word(const std::string& s, size_t at) {
  uint32_t n = 0;
  for (int b = 0; b < 4; ++b) n |= uint32_t(uint8_t(s[at + b])) << (b * 8);
  return n;
}
std::string encodeRecord(const Record& r) {
  std::string out = "CLK5";
  putWord(out, schema);
  putWord(out, r.revision);
  putWord(out, r.request);
  putWord(out, r.fingerprint);
  putWord(out, r.configuration.size());
  putWord(out, r.password.size());
  out += r.configuration;
  out += r.password;
  putWord(out, crc32(reinterpret_cast<const uint8_t*>(out.data()), out.size()));
  return out;
}
bool decodeRecord(const std::string& bytes, Record& r) {
  if (bytes.size() < 32 || bytes.size() > maxTransfer + 95 || bytes.substr(0, 4) != "CLK5" ||
      word(bytes, 4) != schema)
    return false;
  uint32_t length = word(bytes, 20), secret = word(bytes, 24);
  if (length > maxTransfer || secret > 63 || bytes.size() != 32 + length + secret ||
      word(bytes, bytes.size() - 4) !=
          crc32(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size() - 4))
    return false;
  r.revision = word(bytes, 8);
  r.request = word(bytes, 12);
  r.fingerprint = word(bytes, 16);
  r.configuration = bytes.substr(28, length);
  r.password = bytes.substr(28 + length, secret);
  return true;
}
bool Journal::load(Record& record) {
  Record a, b;
  bool va = decodeRecord(storage_.read(0), a), vb = decodeRecord(storage_.read(1), b);
  std::string error;
  if (va) {
    std::unique_ptr<Snapshot> check(new Snapshot);
    va = check->parse(a.configuration, error) && check->document["revision"] == a.revision &&
         check->document["network"]["passwordConfigured"] == !a.password.empty();
  }
  if (vb) {
    std::unique_ptr<Snapshot> check(new Snapshot);
    vb = check->parse(b.configuration, error) && check->document["revision"] == b.revision &&
         check->document["network"]["passwordConfigured"] == !b.password.empty();
  }
  if (!va && !vb) return false;
  bool useB = vb && (!va || b.revision > a.revision);
  record = useB ? b : a;
  next_ = useB ? 0 : 1;
  return true;
}
bool Journal::save(const Record& record) {
  std::string bytes = encodeRecord(record);
  Record verified;
  if (!decodeRecord(bytes, verified)) return false;
  if (!storage_.write(next_, bytes) || storage_.read(next_) != bytes) return false;
  next_ = 1 - next_;
  return true;
}
}  // namespace Config
