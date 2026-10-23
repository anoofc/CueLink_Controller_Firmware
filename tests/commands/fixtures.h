#pragma once
#include "command.h"

// Test inputs use documentation-only addresses and never a user's settings.
// Keep tests independent of the editable development command configuration.
namespace TestCommands {
constexpr Command::Argument connect[] = {Command::Argument::int32(1)};
constexpr uint8_t raw3[] = "CUELINK_B3";
constexpr uint8_t raw4[] = "CUELINK_B4";
constexpr Command::ButtonMapping buttons[] = {
    {{"192.0.2.42", 7000}, {Command::Protocol::Osc,
      "/composition/columns/1/connect", connect, 1, nullptr, 0}},
    {{"192.0.2.42", 7000}, {Command::Protocol::Osc,
      "/composition/columns/2/connect", connect, 1, nullptr, 0}},
    {{"192.0.2.42", 9000}, {Command::Protocol::RawUdp,
      nullptr, nullptr, 0, raw3, sizeof(raw3) - 1}},
    {{"192.0.2.42", 9000}, {Command::Protocol::RawUdp,
      nullptr, nullptr, 0, raw4, sizeof(raw4) - 1}},
};
}  // namespace TestCommands
