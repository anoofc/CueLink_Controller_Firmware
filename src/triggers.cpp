#include "triggers.h"

Command::EncodeStatus Triggers::encode(const Trigger& trigger, Command::Packet& packet) {
  if (const Osc* settings = trigger.osc()) {
    return Command::encode({Command::Protocol::Osc, settings->address, settings->arguments,
                            settings->argumentCount, nullptr, 0}, packet);
  }
  if (const Udp* settings = trigger.udp()) {
    return Command::encode({Command::Protocol::RawUdp, nullptr, nullptr, 0,
                            settings->bytes, settings->length}, packet);
  }
  packet.length = 0;
  return Command::EncodeStatus::Invalid;
}
Triggers::Destinations Triggers::individualDestinations(const Trigger& trigger) {
  if (const Osc* settings = trigger.osc()) return settings->destinations;
  if (const Udp* settings = trigger.udp()) return settings->destinations;
  return {nullptr, 0};
}
bool Triggers::validLabel(const char* label) {
  if (!label || !label[0]) return false;
  for (size_t i = 0; i <= 4; ++i) {
    const uint8_t ch = uint8_t(label[i]);
    if (!ch) return true;
    if (i == 4 || ch < 32 || ch > 126) return false;
  }
  return false;
}
