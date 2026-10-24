#include "commands.h"
#include "serial_log.h"
#include <stdio.h>

namespace {
bool validShape(Triggers::Destinations list) {
  return list.count <= Triggers::maxDestinations && (!list.count || list.items);
}
}
void Commands::collectPorts(Triggers::Destinations list) {
  if (!validShape(list)) return;
  for (size_t i = 0; i < list.count; ++i) {
    if (list.items[i].port && list.items[i].port <= 65535 && excludedCount_ < 136) {
      excludedPorts_[excludedCount_++] = uint16_t(list.items[i].port);
    }
  }
}
void Commands::begin() {
  excludedCount_ = 0;
  collectPorts(configuration_.master);
  midi_.begin();
  for (uint8_t button = 0; button < 4; ++button) {
    const auto& mapping = configuration_.buttons[button];
    validButtons_[button] = Triggers::validLabel(mapping.label) && mapping.count <= 4 && (!mapping.count || mapping.triggers);
    for (uint8_t slot = 0; slot < 4; ++slot) {
      reports_[button][slot] = SlotReport{};
      validated_[button][slot] = Validation{};
      if (!validButtons_[button] || slot >= mapping.count) continue;
      const auto& trigger = mapping.triggers[slot];
      auto& validated = validated_[button][slot];
      const auto individual = Triggers::individualDestinations(trigger);
      collectPorts(individual);
      if (!trigger.enabled()) continue;
      if (trigger.protocol() == Command::Protocol::UsbMidi) {
        Midi::Packet packet;
        if (!trigger.midi() || !Midi::encode(*trigger.midi(), packet)) validated.result = Result::InvalidCommand;
      } else if (Triggers::encode(trigger, packet_) != Command::EncodeStatus::Ok) {
        validated.result = Result::InvalidCommand;
      } else {
        const auto effective = configuration_.useMaster ? configuration_.master : individual;
        // Bound all stored lists, even when master routing overrides their IPs.
        if (!validShape(individual) || !validShape(effective)) validated.result = Result::InvalidConfiguration;
        else if (!effective.count) validated.result = Result::InvalidDestination;
        else {
          validated.destinations = effective;
          for (size_t i = 0; i < effective.count; ++i) {
            if (Command::validateDestination(effective.items[i], validated.addresses[i]) != Command::DestinationStatus::Valid) {
              validated.entries[i] = Result::InvalidDestination;
            }
          }
        }
      }
    }
  }
  packet_.length = 0;
}
void Commands::update(const Network& network, uint32_t now) {
  midi_.update(now);  // Always runs, including Diagnostics and the release gate.
  if (feedbackActive_ && uint32_t(now - summary_.at) >= 1500) feedbackActive_ = false;
  const bool online = network.connected();
  if (!online || online != wasConnected_ || network.ip() != previousIp_ || network.losses() != previousLosses_) transport_.stop();
  wasConnected_ = online;
  previousIp_ = network.ip();
  previousLosses_ = network.losses();
}
void Commands::handleEvents(Mode mode, const ButtonController::Events& events, const Network& network,
                            bool onlineAtEvent, bool usbAtEvent, uint32_t now) {
  if (mode != Mode::Run || events.modeChanged) return;
  for (uint8_t button = 0; button < 4; ++button) {
    if (events.shortPressMask & (1U << button)) press(button, network, onlineAtEvent, usbAtEvent, now);
  }
}
void Commands::press(uint8_t button, const Network& network, bool onlineAtEvent, bool usbAtEvent, uint32_t now) {
  summary_ = Summary{};
  summary_.button = button;
  summary_.at = now;
  feedbackActive_ = true;
  if (!validButtons_[button]) {
    summary_.invalid = true;
    ++counts_.invalid;
    FirmwareSerial.printf("[TRIGGER] B%u invalid button configuration; no slots activated\n", button + 1);
    return;
  }
  const auto& mapping = configuration_.buttons[button];
  for (uint8_t slot = 0; slot < mapping.count; ++slot) {
    if (!mapping.triggers[slot].enabled()) continue;
    ++summary_.attempted;
    const Result result = execute(button, slot, network, onlineAtEvent, usbAtEvent, now);
    if (result == Result::Submitted) { ++summary_.sent; ++counts_.submitted; }
    else if (result == Result::Partial) { summary_.partial = true; ++counts_.partial; }
    else if (result == Result::WifiUnavailable || result == Result::UsbUnavailable) ++counts_.offline;
    else if (result == Result::LocalFailure || result == Result::SchedulerFull) ++counts_.localFailures;
    else ++counts_.invalid;
  }
  if (summary_.sent && summary_.sent < summary_.attempted) summary_.partial = true;
  if (!summary_.attempted) FirmwareSerial.printf("[TRIGGER] B%u no enabled triggers\n", button + 1);
}
Commands::Result Commands::execute(uint8_t button, uint8_t slot, const Network& network,
                                    bool onlineAtEvent, bool usbAtEvent, uint32_t now) {
  const auto& trigger = mapping(button).triggers[slot];
  const auto& validated = validated_[button][slot];
  auto& report = reports_[button][slot];
  report = SlotReport{};
  report.result = validated.result;
  last_ = LastAttempt{};
  last_.button = button; last_.slot = slot; last_.protocol = trigger.protocol(); last_.at = now;
  if (report.result == Result::None && trigger.protocol() == Command::Protocol::UsbMidi) {
    const auto result = midi_.submit(*trigger.midi(), usbAtEvent, now);
    switch (result) {
      case Midi::Result::Submitted: report.result = Result::Submitted; break;
      case Midi::Result::Invalid: report.result = Result::InvalidCommand; break;
      case Midi::Result::Unavailable: report.result = Result::UsbUnavailable; break;
      case Midi::Result::SchedulerFull: report.result = Result::SchedulerFull; break;
      case Midi::Result::TransportFailure: report.result = Result::LocalFailure; break;
    }
    report.length = 4;  // One USB MIDI event packet; pulse release is separate.
  } else if (report.result == Result::None) {
    if (Triggers::encode(trigger, packet_) != Command::EncodeStatus::Ok) report.result = Result::InvalidCommand;
    else {
      report.length = packet_.length;
      report.destinationCount = validated.destinations.count;
      Result firstFailure = Result::None;
      for (uint8_t i = 0; i < report.destinationCount; ++i) {
        Result result = validated.entries[i];
        if (result == Result::None) {
          if (!onlineAtEvent || !network.connected()) result = Result::WifiUnavailable;
          else if (!transport_.open(excludedPorts_, excludedCount_)) result = Result::LocalFailure;
          else {
            last_.sourcePort = transport_.localPort();
            result = transport_.send(validated.addresses[i], uint16_t(validated.destinations.items[i].port), packet_)
                         ? Result::Submitted : Result::LocalFailure;
            if (result == Result::LocalFailure) transport_.stop();
          }
        }
        report.destinations[i] = result;
        if (result == Result::Submitted) { ++report.destinationSuccess; ++counts_.destinationSuccess; }
        else { ++counts_.destinationFailures; if (firstFailure == Result::None) firstFailure = result; }
        const auto& destination = validated.destinations.items[i];
        char ip[20] = "invalid";
        if (validated.entries[i] == Result::None) {
          const auto& address = validated.addresses[i];
          snprintf(ip, sizeof(ip), "%u.%u.%u.%u", address.octets[0], address.octets[1], address.octets[2], address.octets[3]);
        }
        FirmwareSerial.printf("[DEST] B%u S%u D%u %s dst=%s:%lu bytes=%u result=%s\n", button + 1, slot + 1, i + 1,
                              Command::protocolText(trigger.protocol()), ip, static_cast<unsigned long>(destination.port),
                              static_cast<unsigned>(report.length), resultText(result));
      }
      report.result = firstFailure == Result::None ? Result::Submitted :
                      report.destinationSuccess ? Result::Partial : firstFailure;
    }
  }
  last_.result = report.result;
  last_.length = report.length;
  FirmwareSerial.printf("[TRIGGER] B%u S%u %s result=%s destinations=%u/%u\n", button + 1, slot + 1,
                        Command::protocolText(trigger.protocol()), resultText(report.result), report.destinationSuccess, report.destinationCount);
  packet_.length = 0;
  return report.result;
}
const char* Commands::resultText(Result result) {
  switch (result) {
    case Result::None: return "No attempt yet";
    case Result::Submitted: return "Local submission";
    case Result::Partial: return "Partial";
    case Result::WifiUnavailable: return "Wi-Fi unavailable";
    case Result::UsbUnavailable: return "USB MIDI unavailable";
    case Result::InvalidConfiguration: return "Invalid configuration";
    case Result::InvalidDestination: return "Set/fix destination";
    case Result::InvalidCommand: return "Invalid/oversized cmd";
    case Result::LocalFailure: return "Local transport fail";
    case Result::SchedulerFull: return "Note scheduler full";
  }
  return "Unknown";
}
const char* Commands::resultCode(Result result) {
  switch (result) {
    case Result::None: return "--";
    case Result::Submitted: return "OK";
    case Result::Partial: return "PT";
    case Result::WifiUnavailable: return "WF";
    case Result::UsbUnavailable: return "UF";
    case Result::LocalFailure: return "LF";
    case Result::SchedulerFull: return "SF";
    default: return "CF";
  }
}
void Commands::feedback(char* text, size_t capacity) const {
  if (summary_.invalid) snprintf(text, capacity, "B%u: Invalid config", summary_.button + 1);
  else if (!summary_.attempted) snprintf(text, capacity, "B%u: No triggers", summary_.button + 1);
  else if (summary_.partial) snprintf(text, capacity, "B%u: Partial %u/%u", summary_.button + 1, summary_.sent, summary_.attempted);
  else snprintf(text, capacity, "B%u: %u sent", summary_.button + 1, summary_.sent);
}
