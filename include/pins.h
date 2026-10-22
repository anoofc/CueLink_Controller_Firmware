#pragma once

#include <stdint.h>

namespace Pins {
constexpr uint8_t buttons[] = {7, 4, 2, 1};  // B1–B4: D8, D3, D1, D0
constexpr uint8_t sda = 5;                  // D4
constexpr uint8_t scl = 6;                  // D5
}  // namespace Pins
