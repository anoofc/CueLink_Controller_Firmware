#include "command.h"

#include <limits>
#include <string.h>

namespace Command {
const char* protocolText(Protocol protocol) {
  switch (protocol) {
    case Protocol::Osc: return "OSC";
    case Protocol::RawUdp: return "UDP";
    case Protocol::UsbMidi: return "MIDI";
  }
  return "Invalid";
}

DestinationStatus validateDestination(const Destination& destination, IPv4& address) {
  address = IPv4{};
  const char* text = destination.ipv4;
  if (!text || !*text) return DestinationStatus::Unset;
  if (destination.port == 0 || destination.port > 65535) return DestinationStatus::Invalid;
  IPv4 parsed;
  for (uint8_t part = 0; part < 4; ++part) {
    unsigned value = 0;
    uint8_t digits = 0;
    while (*text >= '0' && *text <= '9') {
      if (++digits > 3) return DestinationStatus::Invalid;
      value = value * 10 + unsigned(*text++ - '0');
    }
    if (!digits || value > 255) return DestinationStatus::Invalid;
    parsed.octets[part] = value;
    if (part < 3) {
      if (*text++ != '.') return DestinationStatus::Invalid;
    } else if (*text != '\0') return DestinationStatus::Invalid;
  }
  // This milestone targets individual LAN receivers, not loopback, multicast,
  // unspecified, or limited-broadcast destinations.
  if (parsed.octets[0] == 0 || parsed.octets[0] == 127 || parsed.octets[0] >= 224) {
    return DestinationStatus::Invalid;
  }
  address = parsed;
  return DestinationStatus::Valid;
}

namespace {
class Writer {
 public:
  explicit Writer(Packet& packet) : packet_(packet) {}
  EncodeStatus status = EncodeStatus::Ok;
  size_t used = 0;

  void byte(uint8_t value) {
    if (status != EncodeStatus::Ok) return;
    if (used == maxPacketSize) { status = EncodeStatus::TooLarge; return; }
    packet_.bytes[used++] = value;
  }
  void word(uint32_t value) {
    byte(uint8_t(value >> 24));
    byte(uint8_t(value >> 16));
    byte(uint8_t(value >> 8));
    byte(uint8_t(value));
  }
  void pad() {
    while (status == EncodeStatus::Ok && used % 4) byte(0);
  }
  void string(const char* text, bool address = false) {
    if (!text || (address && (text[0] != '/' || !text[1]))) {
      status = EncodeStatus::Invalid;
      return;
    }
    // Read only a bounded string prefix; oversized settings never overflow the
    // output buffer. OSC 1.0 strings are ASCII, NUL terminated, and padded.
    while (status == EncodeStatus::Ok) {
      const uint8_t ch = uint8_t(*text++);
      if (ch >= 128 || (address && ch && ch <= 32)) {
        status = EncodeStatus::Invalid;
        return;
      }
      byte(ch);
      if (!ch) break;
    }
    pad();
  }
 private:
  Packet& packet_;
};

char typeTag(ArgumentType type) {
  switch (type) {
    case ArgumentType::Int32: return 'i';
    case ArgumentType::Float32: return 'f';
    case ArgumentType::String: return 's';
  }
  return '\0';
}
}  // namespace

EncodeStatus encode(const Action& action, Packet& packet) {
  packet.length = 0;  // A failed encode never exposes a partial packet for send.
  if (action.protocol == Protocol::RawUdp) {
    if (!action.rawBytes || !action.rawLength) return EncodeStatus::Invalid;
    if (action.rawLength > maxPacketSize) return EncodeStatus::TooLarge;
    memcpy(packet.bytes, action.rawBytes, action.rawLength);
    packet.length = action.rawLength;
    return EncodeStatus::Ok;
  }
  if (action.protocol != Protocol::Osc || (action.argumentCount && !action.arguments)) {
    return EncodeStatus::Invalid;
  }
  if (action.argumentCount > maxPacketSize - 2) return EncodeStatus::TooLarge;
  Writer writer(packet);
  writer.string(action.oscAddress, true);
  writer.byte(',');
  for (size_t i = 0; i < action.argumentCount && writer.status == EncodeStatus::Ok; ++i) {
    const char tag = typeTag(action.arguments[i].type);
    if (!tag) return EncodeStatus::Invalid;
    writer.byte(uint8_t(tag));
  }
  writer.byte(0);
  writer.pad();
  for (size_t i = 0; i < action.argumentCount && writer.status == EncodeStatus::Ok; ++i) {
    const Argument& argument = action.arguments[i];
    switch (argument.type) {
      case ArgumentType::Int32:
        writer.word(static_cast<uint32_t>(argument.integer));
        break;
      case ArgumentType::Float32: {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "OSC requires IEEE-754 float32");
        uint32_t bits;
        memcpy(&bits, &argument.real, sizeof(bits));
        writer.word(bits);
        break;
      }
      case ArgumentType::String:
        writer.string(argument.text);
        break;
      default: return EncodeStatus::Invalid;
    }
  }
  if (writer.status == EncodeStatus::Ok) packet.length = writer.used;
  return writer.status;
}
}  // namespace Command
