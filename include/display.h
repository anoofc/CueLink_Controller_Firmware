#pragma once

#include <U8g2lib.h>
#include "buttons.h"
#include "pins.h"
#include "network.h"
#include "commands.h"

constexpr char firmwareVersion[] = "M3 / 0.3.0";

class Display {
 public:
  void begin();
  void update(const ButtonController& buttons, const Network& network,
              const Commands& commands, uint32_t now);
  void logStatus() const;

 private:
  void draw(const ButtonController& buttons, const Network& network,
            const Commands& commands, uint32_t now);
  void drawIndicators(const ButtonController& buttons, uint8_t y);
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled_{U8G2_R0, U8X8_PIN_NONE,
                                          Pins::scl, Pins::sda};
  bool available_ = false;
  uint8_t address_ = 0;
  bool sending_ = false;
  uint8_t nextRow_ = 0;
  uint32_t lastFrameAt_ = 0;
  uint32_t previousTick_ = 0;
  uint64_t uptimeMs_ = 0;
};
