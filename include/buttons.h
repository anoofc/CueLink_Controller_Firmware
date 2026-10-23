#pragma once

#include <stdint.h>

enum class Mode { Run, Diagnostics };

class ButtonController {
 public:
  static constexpr uint8_t count = 4;
  static constexpr uint32_t debounceMs = 30;
  static constexpr uint32_t holdMs = 5000;
  static constexpr uint8_t pageCount = 12;

  struct Events {
    uint8_t shortPressMask = 0;
    bool modeChanged = false;
  };

  void begin(uint32_t now);
  Events update(uint32_t now);
  bool pressed(uint8_t index) const { return buttons_[index].down; }
  uint32_t shortPresses(uint8_t index) const { return counters_[index]; }
  Mode mode() const { return mode_; }
  uint8_t page() const { return page_; }
  int8_t lastEvent() const { return lastEvent_; }
  bool waitingForRelease() const { return releaseGate_; }
  uint32_t holdElapsed(uint32_t now) const;

 private:
  struct Button {
    bool rawDown = false;
    bool down = false;
    bool armed = false;
    uint32_t rawChangedAt = 0;
  };
  Button buttons_[count];
  uint32_t counters_[count] = {};
  Mode mode_ = Mode::Run;
  uint8_t page_ = 0;
  int8_t lastEvent_ = -1;
  bool releaseGate_ = false;
  bool holdingB4_ = false;
  uint32_t b4PressedAt_ = 0;
};
