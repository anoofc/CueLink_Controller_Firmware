#include "buttons.h"

#include <Arduino.h>
#include "pins.h"

void ButtonController::begin(uint32_t now) {
  for (uint8_t i = 0; i < count; ++i) {
    pinMode(Pins::buttons[i], INPUT_PULLUP);
    Button& button = buttons_[i];
    button.rawDown = digitalRead(Pins::buttons[i]) == LOW;
    button.down = button.rawDown;
    button.rawChangedAt = now;
    // Even a button already held at boot must first become stably released.
    button.armed = false;
  }
}

ButtonController::Events ButtonController::update(uint32_t now) {
  Events events;
  uint8_t pressEdges = 0;
  uint8_t releaseEdges = 0;
  bool allReleased = true;

  for (uint8_t i = 0; i < count; ++i) {
    Button& button = buttons_[i];
    const bool rawDown = digitalRead(Pins::buttons[i]) == LOW;
    if (rawDown != button.rawDown) {
      button.rawDown = rawDown;
      button.rawChangedAt = now;
    }
    const bool settled = uint32_t(now - button.rawChangedAt) >= debounceMs;
    if (settled && button.down != button.rawDown) {
      button.down = button.rawDown;
      if (button.armed) {
        if (button.down) pressEdges |= uint8_t(1U << i);
        else releaseEdges |= uint8_t(1U << i);
      }
    }
    if (settled && !button.down) button.armed = true;
    allReleased &= settled && !button.rawDown && !button.down;
  }

  if (releaseGate_) {
    if (allReleased) releaseGate_ = false;
    return events;  // Discard edges, including the release that opens the gate.
  }

  constexpr uint8_t b4Mask = 1U << 3;
  if (pressEdges & b4Mask) {
    holdingB4_ = true;
    b4PressedAt_ = now;
  }

  // Check before short events, including a release sampled at the threshold.
  if (holdingB4_ && uint32_t(now - b4PressedAt_) >= holdMs) {
    mode_ = mode_ == Mode::Run ? Mode::Diagnostics : Mode::Run;
    page_ = 0;
    holdingB4_ = false;
    releaseGate_ = true;
    events.modeChanged = true;
    return events;
  }

  if (mode_ == Mode::Run) {
    events.shortPressMask = pressEdges & 0x07;
    if ((releaseEdges & b4Mask) && holdingB4_) {
      events.shortPressMask |= b4Mask;
    }
    for (uint8_t i = 0; i < count; ++i) {
      if (events.shortPressMask & (1U << i)) {
        ++counters_[i];
        lastEvent_ = i;
      }
    }
  } else {
    if (pressEdges & 0x01) page_ = (page_ + pageCount - 1) % pageCount;
    if (pressEdges & 0x02) page_ = (page_ + 1) % pageCount;
  }

  if (releaseEdges & b4Mask) holdingB4_ = false;
  return events;
}

uint32_t ButtonController::holdElapsed(uint32_t now) const {
  if (!holdingB4_) return 0;
  const uint32_t elapsed = now - b4PressedAt_;
  return elapsed < holdMs ? elapsed : holdMs;
}
