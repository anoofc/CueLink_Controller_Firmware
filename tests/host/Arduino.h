#pragma once

#include <stdint.h>

constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT_PULLUP = 2;
int digitalRead(uint8_t pin);
void pinMode(uint8_t pin, int mode);
