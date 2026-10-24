#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Command {
constexpr size_t maxPacketSize = 1024;
enum class Protocol { Osc, RawUdp, UsbMidi };
enum class ArgumentType { Int32, Float32, String };

struct Argument {
  ArgumentType type;
  int32_t integer;
  float real;
  const char* text;
  static constexpr Argument int32(int32_t value) {
    return {ArgumentType::Int32, value, 0, nullptr};
  }
  static constexpr Argument float32(float value) {
    return {ArgumentType::Float32, 0, value, nullptr};
  }
  static constexpr Argument string(const char* value) {
    return {ArgumentType::String, 0, 0, value};
  }
};

struct Destination {
  const char* ipv4;  // Empty/null means unset; numeric IPv4 only, no DNS lookup.
  uint32_t port;     // Wide enough to validate out-of-range development settings.
};

struct Action {
  Protocol protocol;
  const char* oscAddress;
  const Argument* arguments;
  size_t argumentCount;
  const uint8_t* rawBytes;
  size_t rawLength;
};

struct IPv4 { uint8_t octets[4] = {}; };
struct Packet {
  uint8_t bytes[maxPacketSize];
  size_t length = 0;
};
enum class DestinationStatus { Valid, Unset, Invalid };
enum class EncodeStatus { Ok, Invalid, TooLarge };

DestinationStatus validateDestination(const Destination& destination, IPv4& address);
EncodeStatus encode(const Action& action, Packet& packet);
const char* protocolText(Protocol protocol);
}  // namespace Command
