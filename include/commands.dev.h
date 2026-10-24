#pragma once
#include "triggers.h"

// Development settings, not permanent presets. Existing tested endpoint and
// OSC/UDP settings are retained. Empty IPv4 disables only network submissions.
namespace DevelopmentCommands {
constexpr char destinationIpv4[] = "10.128.151.148";
constexpr Command::Argument columnConnect[] = {Command::Argument::int32(1)};
constexpr uint8_t b1Payload[] = "CUELINK_B1";
constexpr uint8_t b3Payload[] = "CUELINK_B3";
constexpr uint8_t b4Payload[] = "CUELINK_B4";
constexpr Command::Destination oscDestinations[] = {{destinationIpv4, 7000}};
constexpr Command::Destination udpDestinations[] = {{destinationIpv4, 9000}};
constexpr Triggers::Osc column1{"/composition/columns/1/connect", columnConnect, 1, {oscDestinations, 1}};
constexpr Triggers::Osc column2{"/composition/columns/2/connect", columnConnect, 1, {oscDestinations, 1}};
constexpr Triggers::Udp udp1{b1Payload, sizeof(b1Payload) - 1, {udpDestinations, 1}};
constexpr Triggers::Udp udp3{b3Payload, sizeof(b3Payload) - 1, {udpDestinations, 1}};
constexpr Triggers::Udp udp4{b4Payload, sizeof(b4Payload) - 1, {udpDestinations, 1}};
constexpr Midi::Message pulse1 = Midi::Message::pulse(1, 60, 100, 100);
constexpr Midi::Message control3 = Midi::Message::controlChange(1, 1, 127);
constexpr Triggers::Trigger b1[] = {Triggers::Trigger::osc(&column1), Triggers::Trigger::udp(&udp1), Triggers::Trigger::midi(&pulse1)};
constexpr Triggers::Trigger b2[] = {Triggers::Trigger::osc(&column2)};
constexpr Triggers::Trigger b3[] = {Triggers::Trigger::udp(&udp3), Triggers::Trigger::midi(&control3)};
constexpr Triggers::Trigger b4[] = {Triggers::Trigger::udp(&udp4)};
// Labels are 1..4 printable ASCII characters; count is checked, never truncated.
// Disabled slots use factory(..., false). Zero triggers: {"B1", nullptr, 0}.
constexpr Triggers::Button buttons[] = {
    {"B1", b1, sizeof(b1) / sizeof(b1[0])}, {"B2", b2, sizeof(b2) / sizeof(b2[0])},
    {"B3", b3, sizeof(b3) / sizeof(b3[0])}, {"B4", b4, sizeof(b4) / sizeof(b4[0])},
};
// OFF by default. When ON, this list replaces routing for every OSC/UDP slot;
// individual lists above remain intact. Fill with up to 8 actual IP/port entries.
constexpr bool useMasterDestinations = false;
constexpr Triggers::Destinations masterDestinations{nullptr, 0};
constexpr Triggers::Configuration configuration(buttons, useMasterDestinations, masterDestinations);
}  // namespace DevelopmentCommands
