#pragma once

#include <stdint.h>
#include "display_home.h"

namespace DisplayBoot {
constexpr uint32_t durationMs = 5000, frameMs = 40, stepMs = 250;

// The first full frame must be on the display before any of its five seconds
// elapse. Completion is latched: link changes and repeated arm calls cannot replay it.
class Animation {
 public:
  enum class State { Idle, FirstFrame, Playing, Complete };
  void arm() { if (state_ == State::Idle) state_ = State::FirstFrame; }
  void firstFrameDisplayed(uint32_t now) {
    if (state_ != State::FirstFrame) return;
    startedAt_ = now;
    state_ = State::Playing;
  }
  bool advance(uint32_t now) {
    if (state_ != State::Playing || uint32_t(now - startedAt_) < durationMs) return false;
    state_ = State::Complete;
    return true;
  }
  bool ownsDisplay() const { return state_ == State::FirstFrame || state_ == State::Playing; }
  uint32_t elapsed(uint32_t now) const {
    return state_ == State::Playing ? uint32_t(now - startedAt_)
        : state_ == State::Complete ? durationMs : 0;
  }
  State state() const { return state_; }
 private:
  State state_ = State::Idle;
  uint32_t startedAt_ = 0;
};

struct Frame {
  uint8_t visible, filled, indicator;
  bool wordmark;
};
inline Frame frameAt(uint32_t elapsed) {
  if (elapsed >= durationMs) elapsed = durationMs - 1;
  // Row-major button indices, visited clockwise: TL, TR, BR, BL.
  const uint8_t clockwise[] = {0, 1, 3, 2};
  Frame frame = {0x0f, 0x0f, uint8_t((elapsed / stepMs) % 4), elapsed >= 1000};
  if (elapsed < 1000) {
    frame.visible = 0;
    for (unsigned i = 0; i <= elapsed / stepMs; ++i) frame.visible |= 1U << clockwise[i];
    frame.filled = frame.visible;
  } else if (elapsed < 4000) {
    frame.filled = 1U << clockwise[((elapsed - 1000) / stepMs) % 4];
  }
  return frame;
}

inline void draw(u8g2_t* oled, uint32_t elapsed) {
  u8g2_ClearBuffer(oled);
  u8g2_SetDrawColor(oled, 1);
  const Frame frame = frameAt(elapsed);
  // Approved mark proportions: 124-unit buttons, 12-unit gaps, 40-unit
  // corner radii become crisp 12px buttons, 2px gaps, and 4px radii.
  constexpr int side = 12, gap = 2, markWidth = 2 * side + gap;
  constexpr int left = (128 - markWidth) / 2, top = 7;
  for (unsigned i = 0; i < 4; ++i) {
    if (!(frame.visible & (1U << i))) continue;
    const int x = left + (i % 2) * (side + gap), y = top + (i / 2) * (side + gap);
    if (frame.filled & (1U << i)) u8g2_DrawRBox(oled, x, y, side, side, 4);
    else u8g2_DrawRFrame(oled, x, y, side, side, 4);
  }
  if (frame.wordmark) {
    u8g2_SetFont(oled, u8g2_font_helvB10_tr);
    DisplayHome::centeredLine(oled, "CueLink", 36, 12);
  }
  // Four dots loop once per second, finishing the last cycle at five seconds.
  // This is activity, not a measurement of initialization progress.
  for (unsigned i = 0; i < 4; ++i)
    u8g2_DrawDisc(oled, 55 + i * 6, 55, i == frame.indicator ? 1 : 0, U8G2_DRAW_ALL);
}
}  // namespace DisplayBoot
