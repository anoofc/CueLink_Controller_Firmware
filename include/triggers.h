#pragma once
#include "command.h"
#include "midi.h"

namespace Triggers {
constexpr size_t maxTriggers = 4, maxDestinations = 8;
struct Destinations { const Command::Destination* items; size_t count; };
struct Osc { const char* address; const Command::Argument* arguments; size_t argumentCount; Destinations destinations; };
struct Udp { const uint8_t* bytes; size_t length; Destinations destinations; };

// A private tagged union of typed settings pointers. A UDP payload can never be
// interpreted as MIDI data through this API. Settings must outlive the engine.
class Trigger {
 public:
  static constexpr Trigger osc(const Osc* settings, bool enabled = true) { return Trigger(settings, enabled); }
  static constexpr Trigger udp(const Udp* settings, bool enabled = true) { return Trigger(settings, enabled); }
  static constexpr Trigger midi(const Midi::Message* settings, bool enabled = true) { return Trigger(settings, enabled); }
  Command::Protocol protocol() const { return protocol_; }
  bool enabled() const { return enabled_; }
  const Osc* osc() const { return protocol_ == Command::Protocol::Osc ? settings_.osc : nullptr; }
  const Udp* udp() const { return protocol_ == Command::Protocol::RawUdp ? settings_.udp : nullptr; }
  const Midi::Message* midi() const { return protocol_ == Command::Protocol::UsbMidi ? settings_.midi : nullptr; }
 private:
  union Settings {
    const Osc* osc; const Udp* udp; const Midi::Message* midi;
    constexpr Settings(const Osc* value) : osc(value) {}
    constexpr Settings(const Udp* value) : udp(value) {}
    constexpr Settings(const Midi::Message* value) : midi(value) {}
  };
  constexpr Trigger(const Osc* value, bool enabled) : protocol_(Command::Protocol::Osc), enabled_(enabled), settings_(value) {}
  constexpr Trigger(const Udp* value, bool enabled) : protocol_(Command::Protocol::RawUdp), enabled_(enabled), settings_(value) {}
  constexpr Trigger(const Midi::Message* value, bool enabled) : protocol_(Command::Protocol::UsbMidi), enabled_(enabled), settings_(value) {}
  Command::Protocol protocol_;
  bool enabled_;
  Settings settings_;
};
struct Button { const char* label; const Trigger* triggers; size_t count; };
struct Configuration {
  const Button* buttons;
  bool useMaster;
  Destinations master;
  constexpr Configuration(const Button (&values)[4], bool overrideDestinations = false,
                          Destinations destinations = {nullptr, 0})
      : buttons(values), useMaster(overrideDestinations), master(destinations) {}
};
Command::EncodeStatus encode(const Trigger& trigger, Command::Packet& packet);
Destinations individualDestinations(const Trigger& trigger);
bool validLabel(const char* label);
}  // namespace Triggers
