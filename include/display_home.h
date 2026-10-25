#pragma once

#include <clib/u8g2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace DisplayHome {
constexpr int width = 128, height = 64, margin = 4;
constexpr int buttonCount = 4, buttonGap = 4;
constexpr int buttonWidth = (width - 2 * margin - (buttonCount - 1) * buttonGap) / buttonCount;
constexpr int buttonTop = 46, buttonHeight = height - margin - buttonTop;
constexpr int statusTop = 16, statusHeight = 10, ipTop = 27, ipHeight = 8;
constexpr int feedbackTop = 36, feedbackHeight = 9, holdTop = 61;

struct TextMetrics {
  int left = 0, width = 0, ascent = 0, descent = 0;
  int height() const { return ascent - descent; }
};

// U8g2's font-wide descent includes glyphs absent from e.g. "AB". Measure ink
// in a bounded, memory-only two-tile buffer to center the actual string. No I2C,
// waits, heap allocation, or changes to the live framebuffer are involved.
inline TextMetrics measure(u8g2_t* oled, const char* text) {
  uint8_t pixels[width * 2] = {};
  u8g2_t probe = *oled;
  const uint8_t* font = oled->font;
  u8g2_SetupBuffer(&probe, pixels, 2, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  u8g2_SetFont(&probe, font);
  u8g2_SetFontMode(&probe, 1);
  const int baseline = u8g2_GetAscent(&probe);
  const int stringWidth = u8g2_GetStrWidth(&probe, text);
  u8g2_DrawStr(&probe, margin, baseline, text);
  int left = width, right = -1, top = 16, bottom = -1;
  const int scanWidth = stringWidth + 2 * margin < width ? stringWidth + 2 * margin : width;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < scanWidth; ++x) {
      if (!(pixels[(y / 8) * width + x] & (1U << (y % 8)))) continue;
      if (x < left) left = x;
      if (x > right) right = x;
      if (y < top) top = y;
      if (y > bottom) bottom = y;
    }
  }
  TextMetrics result;
  if (right >= left) {
    result.left = left - margin;
    result.width = right - left + 1;
    result.ascent = baseline - top;
    result.descent = baseline - bottom - 1;
  }
  return result;
}

inline int centeredBaseline(const TextMetrics& text, int top, int rowHeight) {
  return top + (rowHeight - text.height()) / 2 + text.ascent;
}

inline void centeredLine(u8g2_t* oled, const char* value, int top, int rowHeight) {
  const TextMetrics text = measure(oled, value);
  if (!text.width) return;
  u8g2_DrawStr(oled, (width - text.width) / 2 - text.left,
               centeredBaseline(text, top, rowHeight), value);
}

inline void drawHeader(u8g2_t* oled) {
  u8g2_SetFont(oled, u8g2_font_7x13_tr);
  const TextMetrics brand = measure(oled, "CueLink"), mode = measure(oled, "RUN");
  const int baseline = margin + (brand.ascent > mode.ascent ? brand.ascent : mode.ascent);
  u8g2_DrawStr(oled, margin - brand.left, baseline, "CueLink");
  u8g2_DrawStr(oled, width - margin - mode.width - mode.left, baseline, "RUN");
  u8g2_DrawHLine(oled, margin, 14, width - 2 * margin);
}

inline void drawNetwork(u8g2_t* oled, const char* state, const char* ip) {
  u8g2_SetFont(oled, u8g2_font_helvR08_tr);
  centeredLine(oled, state, statusTop, statusHeight);
  centeredLine(oled, ip, ipTop, ipHeight);
}

inline void abbreviate(const char* label, char (&out)[5]) {
  snprintf(out, sizeof(out), strlen(label) > 4 ? "%.3s." : "%s", label);
}

inline void drawIndicator(u8g2_t* oled, unsigned index, const char* name, bool pressed,
                          int top, int boxHeight) {
  const int x = margin + index * (buttonWidth + buttonGap);
  if (pressed) u8g2_DrawRBox(oled, x, top, buttonWidth, boxHeight, 3);
  else u8g2_DrawRFrame(oled, x, top, buttonWidth, boxHeight, 3);
  char label[5];
  abbreviate(name, label);
  u8g2_SetFont(oled, strlen(label) > 2 ? u8g2_font_5x7_tr : u8g2_font_6x12_tr);
  const TextMetrics text = measure(oled, label);
  u8g2_SetDrawColor(oled, pressed ? 0 : 1);
  if (text.width) u8g2_DrawStr(oled, x + (buttonWidth - text.width) / 2 - text.left,
                              centeredBaseline(text, top, boxHeight), label);
  u8g2_SetDrawColor(oled, 1);
}

inline void draw(u8g2_t* oled, const char* state, const char* ip,
                 const char* const (&labels)[buttonCount], uint8_t pressed,
                 const char* feedback, unsigned holdWidth) {
  drawHeader(oled);
  drawNetwork(oled, state, ip);
  if (feedback && *feedback) {
    u8g2_SetFont(oled, u8g2_font_6x10_tr);
    char line[32];
    snprintf(line, sizeof(line), "%s", feedback);
    while (strlen(line) && u8g2_GetStrWidth(oled, line) > width - 2 * margin)
      line[strlen(line) - 1] = '\0';
    centeredLine(oled, line, feedbackTop, feedbackHeight);
  }
  if (holdWidth) u8g2_DrawHLine(oled, margin, holdTop,
                               holdWidth > width - 2 * margin ? width - 2 * margin : holdWidth);
  for (unsigned i = 0; i < buttonCount; ++i)
    drawIndicator(oled, i, labels[i], pressed & (1U << i), buttonTop, buttonHeight);
}
}  // namespace DisplayHome
