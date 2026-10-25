#include <Arduino.h>
#include <assert.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "config_manager.h"
#include "config_transfer.h"
#include "configuration.h"
SerialStub Serial;
int digitalRead(uint8_t) { return HIGH; }
void pinMode(uint8_t, int) {}
static unsigned cues = 0;
void Network::begin(uint32_t) {}
void Network::update(uint32_t value) { state_ = value ? State::Connected : State::Connecting; }
void Network::configure(const NetworkSettings& s, uint32_t) {
  settings_ = s;
  state_ = State::Connecting;
}
NetworkSettings Network::developmentSettings() const {
  NetworkSettings n;
  n.ssid = "Test network";
  return n;
}
bool UdpTransport::open(const uint16_t*, size_t) {
  socket_ = 1;
  localPort_ = 50000;
  return true;
}
bool UdpTransport::send(const Command::IPv4&, uint16_t, const Command::Packet&) {
  ++cues;
  return true;
}
void UdpTransport::stop() {
  socket_ = -1;
  localPort_ = 0;
}
struct Port : Midi::Port {
  std::vector<Midi::Packet> packets;
  bool ready() const override { return true; }
  bool mounted() const override { return true; }
  uint32_t sessionEpoch() const override { return 0; }
  bool send(const Midi::Packet& p) override {
    packets.push_back(p);
    return true;
  }
};
struct Store : Config::Storage {
  std::string slots[2];
  bool fail = false, interrupted = false;
  unsigned writes = 0;
  std::string read(unsigned i) override { return slots[i]; }
  bool write(unsigned i, const std::string& value) override {
    ++writes;
    if (fail) return false;
    slots[i] = interrupted ? value.substr(0, value.size() / 2) : value;
    return !interrupted;
  }
};
static std::string encode(JsonVariantConst v) { return Config::json(v); }
static std::string golden() {
  std::ifstream f("docs/configuration_vector.json");
  std::stringstream s;
  s << f.rdbuf();
  auto out = s.str();
  if (out.back() == '\n') out.pop_back();
  return out;
}
static std::string frame(uint8_t kind, uint32_t id, uint16_t seq, const std::string& body = {}) {
  std::string s;
  s += char(1);
  s += char(kind);
  for (int i = 0; i < 4; ++i) s += char(id >> (i * 8));
  s += char(seq);
  s += char(seq >> 8);
  return s + body;
}
static void put(std::string& s, uint32_t v) {
  for (int i = 0; i < 4; ++i) s += char(v >> (i * 8));
}
int main() {
  assert(Config::validSsid(u8"Phone’s hotspot"));
  assert(!Config::validSsid(std::string("bad\0ssid", 8)));
  assert(!Config::validSsid(std::string(33, 'x')));
  assert(!Config::validSsid(std::string("\xc0\xaf", 2)));

  assert(Config::authorized(true, true, true, 16));
  assert(!Config::authorized(false, true, true, 16));
  assert(!Config::authorized(true, false, true, 16));
  assert(!Config::authorized(true, true, false, 16));
  assert(!Config::authorized(true, true, true, 7));
  std::string error, bytes = golden();
  std::unique_ptr<Config::Snapshot> snap(new Config::Snapshot);
  assert(snap->parse(bytes, error));
  assert(encode(snap->document.as<JsonVariantConst>()) == bytes);
  assert(snap->buttons[0].count == 3 && !snap->buttons[1].enabled);
  assert(snap->buttons[0].triggers[1].udp()->length == 5 &&
         snap->buttons[0].triggers[1].udp()->bytes[4] == 255);
  auto pristine = [&]() { assert(snap->parse(bytes, error)); };
  snap->document["buttons"][0]["slots"].as<JsonArray>().add(
      snap->document["buttons"][0]["slots"][0]);
  assert(snap->build(error));
  snap->document["buttons"][0]["slots"].as<JsonArray>().add(
      snap->document["buttons"][0]["slots"][0]);
  assert(!snap->build(error));
  pristine();
  snap->document["schema"] = 2;
  assert(!snap->build(error));
  pristine();
  snap->document["buttons"][0]["label"] = "1234567890123";
  assert(!snap->build(error));
  pristine();
  snap->document["buttons"][0]["slots"][2]["midi"]["channel"] = 17;
  assert(!snap->build(error));
  pristine();
  snap->document["buttons"][0]["slots"][1]["udp"]["payload"] = "f";
  assert(!snap->build(error));
  pristine();
  snap->document["buttons"][0]["slots"][0]["udp"].to<JsonObject>();
  assert(!snap->build(error));
  pristine();
  {
    Store store;
    Config::Journal j(store);
    Config::Record r;
    r.configuration = bytes;
    assert(j.save(r));
    Config::Record loaded;
    assert(j.load(loaded));
    r.revision = 1;
    snap->document["revision"] = 1;
    r.configuration = encode(snap->document.as<JsonVariantConst>());
    assert(j.save(r));
    assert(j.load(loaded) && loaded.revision == 1);
    store.slots[1][20] ^= 1;
    Config::Journal recovered(store);
    assert(recovered.load(loaded) && loaded.revision == 0);
    store.interrupted = true;
    assert(!recovered.save(r));
    Config::Journal afterLoss(store);
    assert(afterLoss.load(loaded) && loaded.revision == 0);
  }
  {
    Config::Transfer t;
    std::string beginBody;
    put(beginBody, bytes.size());
    put(beginBody, Config::crc32(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
    auto deliver = [&](const std::string& value, bool auth = true, uint32_t now = 0) {
      t.accept(reinterpret_cast<const uint8_t*>(value.data()), value.size(), auth, now);
    };
    deliver(frame(1, 7, 0, beginBody), false);
    assert(uint8_t(t.frame()[1]) == 0x84 && !t.complete());
    deliver(frame(1, 7, 0, beginBody));
    deliver(frame(2, 7, 1, "bad"));
    assert(uint8_t(t.frame()[1]) == 0x83 && !t.complete());
    deliver(frame(1, 7, 0, beginBody), true, UINT32_MAX - 100);
    t.update(14899);
    assert(uint8_t(t.frame()[1]) == 0x83);
    deliver(frame(1, 7, 0, beginBody));
    uint16_t seq = 0;
    for (size_t i = 0; i < bytes.size(); i += 12) deliver(frame(2, 7, seq++, bytes.substr(i, 12)));
    deliver(frame(3, 7, seq));
    assert(t.complete() && t.request() == bytes);
    t.reply(bytes);
    deliver(frame(5, 7, 0));
    assert(t.frame().substr(8) == bytes.substr(0, 12));
    deliver(frame(4, 7, 0));
    assert(!t.complete());
    deliver(frame(1, 7, 0, beginBody));
    deliver(frame(3, 7, 0));
    assert(!t.complete());
    deliver(frame(1, 7, 0, beginBody));
    std::string corrupted = bytes;
    corrupted[0] ^= 1;
    seq = 0;
    for (size_t i = 0; i < corrupted.size(); i += 12)
      deliver(frame(2, 7, seq++, corrupted.substr(i, 12)));
    deliver(frame(3, 7, seq));
    assert(!t.complete() && uint8_t(t.frame()[1]) == 0x83);
    std::string tooLarge;
    put(tooLarge, Config::maxTransfer + 1);
    put(tooLarge, 0);
    deliver(frame(1, 7, 0, tooLarge));
    assert(uint8_t(t.frame()[1]) == 0x83);
  }
  // V2 uses the agreed ATT value budget, including MTU 23, without changing persisted JSON.
  for (size_t budget : {size_t(20), size_t(182), size_t(244)}) {
    Config::Transfer transfer;
    Port port; Midi::Engine midi(port); midi.begin();
    assert(midi.submit(Midi::Message::pulse(1, 60, 100, 10), true, 0) == Midi::Result::Submitted);
    Network network; uint32_t tick = 0;
    const std::string data(Config::maxTransfer, 'x');
    std::string begin;
    put(begin, data.size()); put(begin, Config::crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    begin += char(244); begin += char(0);
    auto deliver = [&](uint8_t kind, uint16_t seq, const std::string& body = std::string()) {
      std::string packet = frame(kind, 77, seq, body); packet[0] = 2;
      ++tick; network.update(tick); midi.update(tick);
      transfer.accept(reinterpret_cast<const uint8_t*>(packet.data()), packet.size(), true, tick, budget);
    };
    deliver(1, 0, begin);
    assert(transfer.frame().size() == 10 && uint8_t(transfer.frame()[8]) == budget);
    const size_t payload = budget - 8;
    uint16_t seq = 0;
    for (size_t offset = 0; offset < data.size(); offset += payload) deliver(2, seq++, data.substr(offset, payload));
    deliver(3, seq);
    assert(transfer.complete() && transfer.request() == data);
    transfer.reply(data);
    std::string readback;
    for (uint16_t chunk = 0; readback.size() < data.size(); ++chunk) {
      deliver(5, chunk);
      assert(transfer.frame().size() <= budget && uint8_t(transfer.frame()[0]) == 2);
      const std::string first = transfer.frame();
      deliver(5, chunk); assert(first == transfer.frame()); // Safe idempotent recovery fetch.
      readback += first.substr(8);
    }
    assert(readback == data);
    assert(network.connected() && midi.pendingCount() == 0 && port.packets.size() == 2);
    assert(port.packets.back().bytes[1] == 0x80);
    deliver(4, 0);
    deliver(1, 0, begin);
    deliver(2, 1, "bad sequence");
    assert(uint8_t(transfer.frame()[1]) == 0x83 && !transfer.complete());
    deliver(1, 0, begin);
    seq = 0;
    std::string corrupt = data; corrupt[0] ^= 1;
    for (size_t offset = 0; offset < data.size(); offset += payload) deliver(2, seq++, corrupt.substr(offset, payload));
    deliver(3, seq);
    assert(!transfer.complete() && uint8_t(transfer.frame()[1]) == 0x83);
    deliver(1, 0, begin); deliver(2, 0, data.substr(0, payload));
    std::string fresh = frame(1, 88, 0, begin); fresh[0] = 2;
    transfer.accept(reinterpret_cast<const uint8_t*>(fresh.data()), fresh.size(), true, ++tick, budget);
    assert(transfer.id() == 88 && uint8_t(transfer.frame()[1]) == 0x80 && !transfer.complete());
    transfer.reset(); assert(transfer.request().empty() && !transfer.complete());
  }
  {
    Store store;
    Port port;
    Midi::Engine midi(port);
    midi.begin();
    pristine();
    Commands commands(snap->view, midi);
    Network network;
    ConfigManager manager(store);
    manager.begin(network, commands, 0);
    DynamicJsonDocument request(8192), result(Config::documentCapacity);
    auto run = [&](uint32_t id) {
      std::string b = encode(request.as<JsonVariantConst>());
      return manager.execute(
          id, Config::crc32(reinterpret_cast<const uint8_t*>(b.data()), b.size()), b, 0);
    };
    request["schema"] = 1;
    request["op"] = "button";
    request["revision"] = 0;
    request["button"] = 0;
    request["value"].set(snap->document["buttons"][0]);
    assert(midi.submit(Midi::Message::pulse(1, 60, 100), true, 0) == Midi::Result::Submitted);
    deserializeJson(result, run(10));
    assert(result["ok"] == true && manager.record().revision == 1 && midi.pendingCount() == 1);
    unsigned writes = store.writes;
    deserializeJson(result, run(10));
    assert(result["ok"] == true && store.writes == writes);  // duplicate after lost acknowledgment
    request["value"]["label"] = "Edited";
    deserializeJson(result, run(10));
    assert(result["ok"] == false && store.writes == writes);
    deserializeJson(result, run(11));
    assert(result["ok"] == false);  // stale revision
    request["revision"] = 1;
    store.fail = true;
    deserializeJson(result, run(12));
    assert(result["ok"] == false && std::string(commands.mapping(0).label) == "Main cue" &&
           manager.record().revision == 1);
    store.fail = false;
    deserializeJson(result, run(13));
    assert(result["ok"] == true && std::string(commands.mapping(0).label) == "Edited");
    request["revision"] = 2;
    writes = store.writes;
    deserializeJson(result, run(14));
    assert(result["ok"] == true && store.writes == writes && manager.record().revision == 2);
    midi.update(100);
    assert(port.packets.size() == 2 && port.packets.back().bytes[1] == 0x80 &&
           port.packets.back().bytes[2] == 60);
    assert(cues == 0);  // all operations above were configuration only
    request.clear();
    request["schema"] = 1;
    request["op"] = "network";
    request["revision"] = 2;
    request["value"].set(snap->document["network"]);
    request["value"]["ssid"] = "New network";
    request["credential"]["mode"] = "replace";
    request["credential"]["value"] = "  test key  ";
    assert(run(20).empty() && manager.pending());
    assert(network.settings().password == "  test key  ");
    manager.update(30000);
    assert(!manager.pending() && manager.record().revision == 2 &&
           network.settings().ssid == "Test network");
    assert(run(21).empty());
    network.update(1);
    manager.update(1);
    assert(manager.record().revision == 3 && manager.record().password == "  test key  ");
    assert(manager.record().configuration.find("test key") == std::string::npos);
    request["revision"] = 3;
    request["value"]["ssid"] = "Other network";
    request["credential"].remove("value");
    request["credential"]["mode"] = "keep";
    assert(run(22).empty() && network.settings().password == "  test key  ");
    network.update(1);
    manager.update(1);
    assert(manager.record().revision == 4);
    request["revision"] = 4;
    request["credential"]["mode"] = "open";
    assert(run(23).empty() && network.settings().password.empty());
    network.update(1);
    manager.update(1);
    assert(manager.record().revision == 5 && manager.record().password.empty());
    assert(cues == 0 && port.packets.size() == 2);
    {
      const unsigned before = store.writes;
      const std::string scoped = manager.execute(100, 0,
        "{\"schema\":1,\"op\":\"read_scope\",\"scope\":\"button\",\"button\":0}", 0);
      DynamicJsonDocument scope(8192); deserializeJson(scope, scoped);
      assert(scope["ok"] == true && scope["revision"] == 5 && scope["scope"] == "button");
      assert(scope["value"]["label"] == "Edited" && !scope.containsKey("configuration"));
      const auto networkScope = manager.execute(101, 0,
        "{\"schema\":1,\"op\":\"read_scope\",\"scope\":\"network\"}", 0);
      assert(networkScope.find("test key") == std::string::npos && store.writes == before);
    }
    request["revision"] = 5;
    request["value"]["ssid"] = "Persist failure test";
    request["credential"]["mode"] = "keep";
    store.fail = true;
    assert(run(24).empty());
    network.update(1);
    manager.update(1);
    store.fail = false;
    assert(manager.record().revision == 5 && network.settings().ssid == "Other network");
    ConfigManager rebooted(store);
    rebooted.begin(network, commands, 1);
    assert(rebooted.record().revision == 5 && std::string(commands.mapping(0).label) == "Edited");
  }
  std::cout << "Configuration parity, bounds, authenticated transfer gates, storage recovery, "
               "revisions/deduplication, rollback/credentials and MIDI snapshot checks passed.\n";
}
