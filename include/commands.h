#pragma once

#include "buttons.h"
#include "command.h"
#include "network.h"
#include "udp_transport.h"

class Commands {
 public:
  enum class Result { None, Submitted, Offline, InvalidDestination, InvalidCommand, LocalFailure };
  struct LastAttempt {
    Result result = Result::None;
    uint8_t button = 0;
    Command::Protocol protocol = Command::Protocol::RawUdp;
    size_t length = 0;
    uint16_t sourcePort = 0;
    uint32_t at = 0;
  };
  struct Counts {
    uint32_t submitted = 0;
    uint32_t offline = 0;
    uint32_t invalid = 0;
    uint32_t localFailures = 0;
  };
  explicit Commands(const Command::ButtonMapping (&mappings)[ButtonController::count])
      : mappings_(mappings) {}
  void begin();
  void update(const Network& network, uint32_t now);
  void handleEvents(Mode mode, const ButtonController::Events& events,
                    const Network& network, bool onlineAtEvent, uint32_t now);
  const Command::ButtonMapping& mapping(uint8_t button) const { return mappings_[button]; }
  Result configurationResult(uint8_t button) const { return configResults_[button]; }
  const LastAttempt& last() const { return last_; }
  const Counts& counts() const { return counts_; }
  bool feedbackActive() const { return feedbackActive_; }
  static const char* resultText(Result result);
  void feedback(char* text, size_t capacity) const;

 private:
  void trigger(uint8_t button, const Network& network, bool onlineAtEvent, uint32_t now);
  const Command::ButtonMapping* mappings_;
  Result configResults_[ButtonController::count] = {};
  UdpTransport transport_;
  Command::Packet packet_;  // Fixed working buffer only, never a pending send.
  LastAttempt last_;
  Counts counts_;
  bool wasConnected_ = false;
  IPAddress previousIp_;
  uint32_t previousLosses_ = 0;
  bool feedbackActive_ = false;
};
