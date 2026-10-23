#include "commands.h"
#include "fixtures.h"
#include "pins.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

SerialStub Serial;
static bool levels[8];
static unsigned opens = 0, sends = 0, stops = 0;
static bool openOk = true, sendOk = true;
static Command::Packet submitted;

int digitalRead(uint8_t pin) { return levels[pin] ? LOW : HIGH; }
void pinMode(uint8_t, int mode) { assert(mode == INPUT_PULLUP); }

// Mock only the radio and socket boundaries. The real button state machine,
// command dispatcher, validation, encoder, and event gating are compiled below.
void Network::begin(uint32_t) { state_ = State::Reconnecting; }
void Network::update(uint32_t signal) {
  if (!signal) {
    if (connected()) ++losses_;
    state_ = State::Reconnecting;
    ip_ = IPAddress();
  } else {
    state_ = State::Connected;
    ip_ = IPAddress(signal);
  }
}
bool UdpTransport::open(const Command::ButtonMapping*, size_t) {
  if (active()) return true;
  ++opens;
  if (!openOk) return false;
  socket_ = 1;
  localPort_ = 50000;
  return true;
}
bool UdpTransport::send(const Command::IPv4&, uint16_t, const Command::Packet& packet) {
  ++sends;
  submitted = packet;
  return sendOk;
}
void UdpTransport::stop() {
  if (active()) ++stops;
  socket_ = -1;
  localPort_ = 0;
}

struct Fixture {
  Command::ButtonMapping mappings[4];
  Commands commands;
  ButtonController buttons;
  Network network;
  uint32_t now = 0;
  Fixture(bool connected = true) : commands(mappings) {
    opens = sends = stops = 0;
    openOk = sendOk = true;
    Serial.output.clear();
    memset(levels, 0, sizeof(levels));
    memcpy(mappings, TestCommands::buttons, sizeof(mappings));
    for (auto& mapping : mappings) mapping.destination.ipv4 = "192.0.2.42";
    commands.begin();
    buttons.begin(now);
    if (connected) network.update(1);
    tick(30);
    assert(sends == 0);  // Boot/configuration/network readiness never sends.
  }
  void tick(uint32_t elapsed, bool reconnectDuringTick = false) {
    now += elapsed;
    const bool onlineAtEvent = network.connected();
    const auto events = buttons.update(now);
    if (reconnectDuringTick) network.update(1);
    commands.update(network, now);
    commands.handleEvents(buttons.mode(), events, network, onlineAtEvent, now);
  }
  void edge(uint8_t button, bool down) {
    levels[Pins::buttons[button]] = down;
    tick(1);
    tick(30);
  }
  void event(uint8_t mask) {
    ButtonController::Events events;
    events.shortPressMask = mask;
    commands.handleEvents(Mode::Run, events, network, network.connected(), ++now);
  }
};

int main() {
  {
    Fixture f;
    f.edge(0, true);
    assert(sends == 1 && f.commands.counts().submitted == 1);
    assert(submitted.length == 40);
    f.tick(6000);
    f.edge(0, false);
    assert(sends == 1 && opens == 1);
    f.edge(2, true);
    assert(sends == 2 && opens == 1 && submitted.length == 10);
    assert(memcmp(submitted.bytes, "CUELINK_B3", 10) == 0);
    f.edge(2, false);
    f.edge(3, true);
    assert(sends == 2);
    f.edge(3, false);
    assert(sends == 3 && memcmp(submitted.bytes, "CUELINK_B4", 10) == 0);
    f.edge(3, true);
    f.tick(5000);
    assert(f.buttons.mode() == Mode::Diagnostics && sends == 3);
    f.tick(6000);
    f.edge(3, false);
    for (uint8_t page = 0; page < ButtonController::pageCount; ++page) {
      f.edge(1, true);
      f.edge(1, false);
    }
    assert(sends == 3 && f.buttons.page() == 0);
    f.edge(3, true);
    f.tick(5000);
    f.edge(3, false);
    assert(f.buttons.mode() == Mode::Run && sends == 3);
  }
  {
    Fixture f(false);
    f.edge(0, true);
    assert(sends == 0 && opens == 0 && f.commands.counts().offline == 1);
    f.edge(0, false);
    f.network.update(1);
    f.tick(1);
    f.tick(10000);
    assert(sends == 0);  // Reconnection cannot replay the offline press.
    f.edge(0, true);
    assert(sends == 1 && opens == 1);
    f.network.update(0);
    f.tick(1);
    assert(stops == 1);
    f.edge(0, false);
    f.network.update(1);
    f.tick(1);
    assert(sends == 1);
    f.edge(0, true);
    assert(sends == 2 && opens == 2);
    f.network.update(2);  // IP change while connected also invalidates the socket.
    f.tick(1);
    assert(stops == 2);
  }
  {
    Fixture f(false);
    levels[Pins::buttons[0]] = true;
    f.tick(1);
    f.tick(30, true);  // DHCP ready in the same iteration as the short event.
    assert(sends == 0 && f.commands.counts().offline == 1);
    f.tick(5000);
    assert(sends == 0);
  }
  {
    Fixture f;
    openOk = false;
    f.event(1);
    assert(opens == 1 && sends == 0 && f.commands.counts().localFailures == 1);
    f.tick(30000);
    assert(opens == 1);  // No automatic retry on a local error.
    openOk = true;
    sendOk = false;
    f.event(1);
    assert(sends == 1 && stops == 1 && f.commands.counts().localFailures == 2);
    f.tick(30000);
    assert(sends == 1);
    sendOk = true;
    f.event(1);
    assert(sends == 2 && f.commands.counts().submitted == 1);
    assert(f.commands.last().result == Commands::Result::Submitted);
  }
  {
    Fixture f;
    f.now = UINT32_MAX - 500;
    f.event(1);
    assert(f.commands.feedbackActive());
    f.tick(1499);
    assert(f.commands.feedbackActive());
    f.tick(1);
    assert(!f.commands.feedbackActive());
    // An expired event cannot reappear when millis cycles back to its timestamp.
    f.now = f.commands.last().at;
    f.tick(1);
    assert(!f.commands.feedbackActive());
  }
  {
    Fixture f;
    f.mappings[0].destination.ipv4 = "";
    f.mappings[1].destination.port = 65536;
    f.mappings[2].action.rawLength = Command::maxPacketSize + 1;
    f.mappings[3].action.rawBytes = nullptr;
    f.commands.begin();
    f.event(15);
    assert(opens == 0 && sends == 0 && f.commands.counts().invalid == 4);
    assert(f.commands.configurationResult(0) == Commands::Result::InvalidDestination);
    assert(f.commands.configurationResult(2) == Commands::Result::InvalidCommand);
    assert(f.commands.last().result == Commands::Result::InvalidCommand);
    size_t logs = 0, offset = 0;
    while ((offset = Serial.output.find("[SEND]", offset)) != std::string::npos) {
      ++logs;
      offset += 6;
    }
    assert(logs == 4);  // One attempt log per triggered button, including rejects.
    ButtonController::Events modeChange;
    modeChange.modeChanged = true;
    modeChange.shortPressMask = 15;
    f.commands.handleEvents(Mode::Run, modeChange, f.network, true, f.now);
    modeChange.modeChanged = false;
    f.commands.handleEvents(Mode::Diagnostics, modeChange, f.network, true, f.now);
    assert(f.commands.counts().invalid == 4);
  }
  puts("Button-to-command gating, offline drops, reconnect lifecycle, and local failures passed.");
}
