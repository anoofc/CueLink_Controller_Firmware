#pragma once

#include "command.h"

// DEVELOPMENT EXAMPLES ONLY. Set this to the receiving computer's actual LAN
// IPv4 address, then rebuild and manually upload. Empty disables all sends.
// Do not use the controller's IP or 127.0.0.1. No server IP is assumed.
// You can also replace destinationIpv4 in any mapping with a different IP.
namespace DevelopmentCommands {
constexpr char destinationIpv4[] = "10.128.151.148";

// Typed arguments: Argument::int32(...), ::float32(...), ::string("...").
// Confirm the OSC paths AND types against the actual Resolume installation.
constexpr Command::Argument columnConnect[] = {Command::Argument::int32(1)};
constexpr uint8_t b3Payload[] = "CUELINK_B3";
constexpr uint8_t b4Payload[] = "CUELINK_B4";

// Every button specifies its own destination port, protocol, and action.
// Raw lengths explicitly exclude the C string terminator; no newline is added.
constexpr Command::ButtonMapping buttons[] = {
    {{destinationIpv4, 7000},
     {Command::Protocol::Osc, "/composition/columns/1/connect",
      columnConnect, 1, nullptr, 0}},
    {{destinationIpv4, 7000},
     {Command::Protocol::Osc, "/composition/columns/2/connect",
      columnConnect, 1, nullptr, 0}},
    {{destinationIpv4, 9000},
     {Command::Protocol::RawUdp, nullptr, nullptr, 0,
      b3Payload, sizeof(b3Payload) - 1}},
    {{destinationIpv4, 9000},
     {Command::Protocol::RawUdp, nullptr, nullptr, 0,
      b4Payload, sizeof(b4Payload) - 1}},
};
}  // namespace DevelopmentCommands
