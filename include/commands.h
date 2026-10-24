#pragma once
#include "buttons.h"
#include "triggers.h"
#include "network.h"
#include "udp_transport.h"

class Commands {
 public:
  enum class Result { None, Submitted, Partial, WifiUnavailable, UsbUnavailable,
                      InvalidConfiguration, InvalidDestination, InvalidCommand, LocalFailure, SchedulerFull };
  struct SlotReport {
    Result result = Result::None;
    uint8_t destinationCount = 0, destinationSuccess = 0;
    Result destinations[Triggers::maxDestinations] = {};
    size_t length = 0;
  };
  struct Summary {
    uint8_t button = 0, attempted = 0, sent = 0;
    bool invalid = false, partial = false;
    uint32_t at = 0;
  };
  struct LastAttempt {
    Result result = Result::None;
    uint8_t button = 0, slot = 0;
    Command::Protocol protocol = Command::Protocol::RawUdp;
    size_t length = 0;
    uint16_t sourcePort = 0;
    uint32_t at = 0;
  };
  struct Counts {
    uint32_t submitted = 0, partial = 0, offline = 0, invalid = 0, localFailures = 0;
    uint32_t destinationSuccess = 0, destinationFailures = 0;
  };
  Commands(const Triggers::Configuration& configuration, Midi::Engine& midi) : configuration_(&configuration), midi_(midi) {}
  void begin(bool resetMidi = true);
  void configure(const Triggers::Configuration& configuration) { configuration_ = &configuration; transport_.stop(); begin(false); }
  void update(const Network& network, uint32_t now);
  void handleEvents(Mode mode, const ButtonController::Events& events, const Network& network,
                    bool onlineAtEvent, bool usbAtEvent, uint32_t now);
  const Triggers::Button& mapping(uint8_t button) const { return configuration_->buttons[button]; }
  bool buttonValid(uint8_t button) const { return validButtons_[button]; }
  const SlotReport& report(uint8_t button, uint8_t slot) const { return reports_[button][slot]; }
  Triggers::Destinations destinations(uint8_t button, uint8_t slot) const { return validated_[button][slot].destinations; }
  const LastAttempt& last() const { return last_; }
  const Summary& summary() const { return summary_; }
  const Counts& counts() const { return counts_; }
  const Midi::Engine& midi() const { return midi_; }
  bool feedbackActive() const { return feedbackActive_; }
  static const char* resultText(Result result);
  static const char* resultCode(Result result);
  void feedback(char* text, size_t capacity) const;
 private:
  struct Validation {
    Result result = Result::None;
    Triggers::Destinations destinations{nullptr, 0};
    Command::IPv4 addresses[Triggers::maxDestinations];
    Result entries[Triggers::maxDestinations] = {};
  };
  void collectPorts(Triggers::Destinations destinations);
  void press(uint8_t button, const Network& network, bool onlineAtEvent, bool usbAtEvent, uint32_t now);
  Result execute(uint8_t button, uint8_t slot, const Network& network, bool onlineAtEvent, bool usbAtEvent, uint32_t now);
  const Triggers::Configuration* configuration_;
  Midi::Engine& midi_;
  bool validButtons_[4] = {};
  Validation validated_[4][4];
  SlotReport reports_[4][4];
  uint16_t excludedPorts_[136];
  size_t excludedCount_ = 0;
  UdpTransport transport_;
  Command::Packet packet_;
  LastAttempt last_;
  Summary summary_;
  Counts counts_;
  bool wasConnected_ = false, feedbackActive_ = false;
  IPAddress previousIp_;
  uint32_t previousLosses_ = 0;
};
