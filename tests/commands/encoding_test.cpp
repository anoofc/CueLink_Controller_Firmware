#include "command.h"
#include "fixtures.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

using namespace Command;

static Action osc(const char* address, const Argument* arguments, size_t count) {
  return {Protocol::Osc, address, arguments, count, nullptr, 0};
}

template <size_t N>
static void expect(const Action& action, const uint8_t (&golden)[N]) {
  Packet packet;
  memset(packet.bytes, 0xAA, sizeof(packet.bytes));
  assert(encode(action, packet) == EncodeStatus::Ok);
  assert(packet.length == N);
  assert(memcmp(packet.bytes, golden, N) == 0);
  assert(packet.length % 4 == 0);
}

int main() {
  const Argument integer[] = {Argument::int32(0x01020304), Argument::int32(-2)};
  const uint8_t intGolden[] = {
      '/', 'i', 0, 0, ',', 'i', 'i', 0, 1, 2, 3, 4, 0xff, 0xff, 0xff, 0xfe};
  expect(osc("/i", integer, 2), intGolden);

  const Argument real[] = {Argument::float32(1.0f), Argument::float32(-2.5f)};
  const uint8_t floatGolden[] = {
      '/', 'f', 0, 0, ',', 'f', 'f', 0, 0x3f, 0x80, 0, 0, 0xc0, 0x20, 0, 0};
  expect(osc("/f", real, 2), floatGolden);

  const Argument strings[] = {Argument::string("abcde"), Argument::string("")};
  const uint8_t stringGolden[] = {
      '/', 's', 't', 'r', 0, 0, 0, 0, ',', 's', 's', 0,
      'a', 'b', 'c', 'd', 'e', 0, 0, 0, 0, 0, 0, 0};
  expect(osc("/str", strings, 2), stringGolden);

  const Argument mixed[] = {Argument::int32(1), Argument::float32(0.5f), Argument::string("ok")};
  const uint8_t mixedGolden[] = {
      '/', 'm', 'i', 'x', 0, 0, 0, 0, ',', 'i', 'f', 's', 0, 0, 0, 0,
      0, 0, 0, 1, 0x3f, 0, 0, 0, 'o', 'k', 0, 0};
  expect(osc("/mix", mixed, 3), mixedGolden);
  const uint8_t emptyGolden[] = {'/', 'x', 0, 0, ',', 0, 0, 0};
  expect(osc("/x", nullptr, 0), emptyGolden);

  Packet packet;
  // Example input fixtures remain stable when local development settings change.
  for (uint8_t button = 2; button < 4; ++button) {
    assert(encode(TestCommands::buttons[button].action, packet) == EncodeStatus::Ok);
    assert(packet.length == 10);
    assert(memcmp(packet.bytes, button == 2 ? "CUELINK_B3" : "CUELINK_B4", 10) == 0);
    assert(memchr(packet.bytes, '\0', packet.length) == nullptr);
    assert(memchr(packet.bytes, '\n', packet.length) == nullptr);
  }
  for (uint8_t button = 0; button < 2; ++button) {
    const auto& action = TestCommands::buttons[button].action;
    assert(encode(action, packet) == EncodeStatus::Ok);
    assert(packet.length == 40);
    assert(memcmp(packet.bytes + 32, ",i\0\0\0\0\0\1", 8) == 0);
    assert(packet.bytes[21] == uint8_t('1' + button));
    assert(TestCommands::buttons[button].destination.port == 7000);
  }

  uint8_t bytes[maxPacketSize + 1] = {0, 1, 0, 255};
  Action raw{Protocol::RawUdp, nullptr, nullptr, 0, bytes, maxPacketSize};
  assert(encode(raw, packet) == EncodeStatus::Ok && packet.length == maxPacketSize);
  assert(memcmp(bytes, packet.bytes, maxPacketSize) == 0);
  raw.rawLength++;
  assert(encode(raw, packet) == EncodeStatus::TooLarge && packet.length == 0);
  raw.rawLength = 0;
  assert(encode(raw, packet) == EncodeStatus::Invalid);

  std::string text(1015, 'a');
  Argument boundary = Argument::string(text.c_str());
  assert(encode(osc("/x", &boundary, 1), packet) == EncodeStatus::Ok);
  assert(packet.length == maxPacketSize && packet.bytes[1023] == 0);
  text.push_back('a');
  boundary = Argument::string(text.c_str());
  assert(encode(osc("/x", &boundary, 1), packet) == EncodeStatus::TooLarge);
  assert(packet.length == 0);
  assert(encode(osc("bad", nullptr, 0), packet) == EncodeStatus::Invalid);
  assert(encode(osc("/bad path", nullptr, 0), packet) == EncodeStatus::Invalid);
  assert(encode(osc(nullptr, nullptr, 0), packet) == EncodeStatus::Invalid);
  assert(encode(osc("/x", nullptr, 1), packet) == EncodeStatus::Invalid);
  assert(encode(osc("/x", integer, maxPacketSize), packet) == EncodeStatus::TooLarge);
  Argument invalid = Argument::string(nullptr);
  assert(encode(osc("/x", &invalid, 1), packet) == EncodeStatus::Invalid);
  invalid.type = static_cast<ArgumentType>(99);
  assert(encode(osc("/x", &invalid, 1), packet) == EncodeStatus::Invalid);

  IPv4 address;
  assert(validateDestination({"192.0.2.42", 7000}, address) == DestinationStatus::Valid);
  assert(address.octets[0] == 192 && address.octets[3] == 42);
  assert(validateDestination({"", 7000}, address) == DestinationStatus::Unset);
  assert(validateDestination({nullptr, 7000}, address) == DestinationStatus::Unset);
  const char* invalidIps[] = {"0.0.0.0", "127.0.0.1", "224.0.0.1", "255.255.255.255",
                              "256.1.1.1", "1.2.3", "1.2.3.4.5", "host.local", "1.2.3.4x",
                              "-1.2.3.4", "1..2.3", "1000.2.3.4", " 1.2.3.4"};
  for (const char* ip : invalidIps) {
    assert(validateDestination({ip, 7000}, address) == DestinationStatus::Invalid);
    assert(address.octets[0] == 0);
  }
  assert(validateDestination({"192.0.2.42", 0}, address) == DestinationStatus::Invalid);
  assert(validateDestination({"192.0.2.42", 65536}, address) == DestinationStatus::Invalid);
  puts("OSC encoding, exact raw payload, size limits, and destination checks passed.");
}
