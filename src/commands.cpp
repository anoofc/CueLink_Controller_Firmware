#include "commands.h"

#include <stdio.h>

void Commands::begin() {
  for (uint8_t i = 0; i < ButtonController::count; ++i) {
    Command::IPv4 address;
    if (Command::validateDestination(mappings_[i].destination, address) !=
        Command::DestinationStatus::Valid) {
      configResults_[i] = Result::InvalidDestination;
    } else if (Command::encode(mappings_[i].action, packet_) != Command::EncodeStatus::Ok) {
      configResults_[i] = Result::InvalidCommand;
    } else configResults_[i] = Result::None;
  }
  packet_.length = 0;
}

void Commands::update(const Network& network, uint32_t now) {
  if (feedbackActive_ && uint32_t(now - last_.at) >= 1500) feedbackActive_ = false;
  const bool online = network.connected();
  if (!online || online != wasConnected_ || network.ip() != previousIp_ ||
      network.losses() != previousLosses_) {
    transport_.stop();
  }
  wasConnected_ = online;
  previousIp_ = network.ip();
  previousLosses_ = network.losses();
  // Reopen lazily on a fresh valid press. Reconnection alone sends nothing.
}

void Commands::handleEvents(Mode mode, const ButtonController::Events& events,
                            const Network& network, bool onlineAtEvent, uint32_t now) {
  if (mode != Mode::Run || events.modeChanged) return;
  for (uint8_t i = 0; i < ButtonController::count; ++i) {
    if (events.shortPressMask & (1U << i)) trigger(i, network, onlineAtEvent, now);
  }
}

void Commands::trigger(uint8_t button, const Network& network, bool onlineAtEvent, uint32_t now) {
  const Command::ButtonMapping& mapping = mappings_[button];
  Command::IPv4 address;
  const auto destinationStatus = Command::validateDestination(mapping.destination, address);
  const auto encoding = Command::encode(mapping.action, packet_);
  last_.button = button;
  last_.protocol = mapping.action.protocol;
  last_.at = now;
  feedbackActive_ = true;
  last_.length = packet_.length;
  last_.sourcePort = 0;
  // Check both sides of network.update(): a press observed offline cannot be
  // replayed when that same loop iteration observes newly acquired Wi-Fi.
  if (!onlineAtEvent || !network.connected()) {
    last_.result = Result::Offline;
    ++counts_.offline;
  } else if (destinationStatus != Command::DestinationStatus::Valid) {
    last_.result = Result::InvalidDestination;
    ++counts_.invalid;
  } else if (encoding != Command::EncodeStatus::Ok) {
    last_.result = Result::InvalidCommand;
    ++counts_.invalid;
  } else {
    const bool opened = transport_.open(mappings_, ButtonController::count);
    last_.sourcePort = transport_.localPort();
    if (opened && transport_.send(address, uint16_t(mapping.destination.port), packet_)) {
      last_.result = Result::Submitted;
      ++counts_.submitted;
    } else {
      last_.result = Result::LocalFailure;
      ++counts_.localFailures;
      transport_.stop();
    }
  }
  char destination[20];
  if (destinationStatus == Command::DestinationStatus::Valid) {
    snprintf(destination, sizeof(destination), "%u.%u.%u.%u", address.octets[0],
             address.octets[1], address.octets[2], address.octets[3]);
  } else snprintf(destination, sizeof(destination), "%s",
                   destinationStatus == Command::DestinationStatus::Unset ? "unset" : "invalid");
  Serial.printf("[SEND] B%u %s dst=%s:%lu bytes=%u src=%u result=%s\n",
                button + 1, Command::protocolText(mapping.action.protocol), destination,
                static_cast<unsigned long>(mapping.destination.port),
                static_cast<unsigned>(last_.length), last_.sourcePort, resultText(last_.result));
  packet_.length = 0;  // Nothing survives this attempt for automatic transmission.
}

const char* Commands::resultText(Result result) {
  switch (result) {
    case Result::None: return "No send yet";
    case Result::Submitted: return "Local submission";
    case Result::Offline: return "Not sent: offline";
    case Result::InvalidDestination: return "Set/fix destination";
    case Result::InvalidCommand: return "Invalid/oversized cmd";
    case Result::LocalFailure: return "Local UDP failure";
  }
  return "Unknown result";
}

void Commands::feedback(char* text, size_t capacity) const {
  if (last_.result == Result::Submitted) {
    snprintf(text, capacity, "B%u %s sent", last_.button + 1, Command::protocolText(last_.protocol));
  } else if (last_.result == Result::InvalidDestination) {
    snprintf(text, capacity, "Set destination");
  } else snprintf(text, capacity, "%s", resultText(last_.result));
}
