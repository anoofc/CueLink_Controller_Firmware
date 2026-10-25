#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
#include "display_home.h"

struct Bounds {
  int left = 128, right = -1, top = 64, bottom = -1;
  int width() const { return right - left + 1; }
  int height() const { return bottom - top + 1; }
};
static Bounds bounds(const uint8_t* pixels, int x0 = 0, int y0 = 0, int x1 = 128, int y1 = 64) {
  Bounds b;
  for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
    if (!(pixels[(y / 8) * 128 + x] & (1U << (y % 8)))) continue;
    if (x < b.left) b.left = x;
    if (x > b.right) b.right = x;
    if (y < b.top) b.top = y;
    if (y > b.bottom) b.bottom = y;
  }
  return b;
}
static int absolute(int n) { return n < 0 ? -n : n; }
static void centered(const Bounds& b, int x, int y, int w, int h) {
  assert(b.width() > 0 && b.height() > 0);
  assert(absolute((b.left + b.right) - (2 * x + w - 1)) <= 1);
  assert(absolute((b.top + b.bottom) - (2 * y + h - 1)) <= 1);
}
static void capture(const uint8_t* pixels, const char* directory, const char* name) {
  char path[512]; snprintf(path, sizeof(path), "%s/%s.pbm", directory, name);
  FILE* f = fopen(path, "w"); assert(f);
  fprintf(f, "P1\n128 64\n");
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x)
      fprintf(f, "%u ", !!(pixels[(y / 8) * 128 + x] & (1U << (y % 8))));
    fprintf(f, "\n");
  }
  fclose(f);
}

int main(int argc, char** argv) {
  uint8_t guarded[1056]; memset(guarded, 0xa5, sizeof(guarded));
  uint8_t* pixels = guarded + 16;
  u8g2_t oled = {};
  u8x8_display_info_t info = {};
  info.tile_width = 16; info.tile_height = 8;
  info.pixel_width = 128; info.pixel_height = 64;
  oled.u8x8.display_info = &info;
  u8g2_SetupBuffer(&oled, pixels, 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);

  // Measure rendered pixels of the old and chosen fonts, rather than font names.
  for (const char* label : {"CueLink", "RUN", "Connected", "255.255.255.255"}) {
    const bool header = strcmp(label, "CueLink") == 0 || strcmp(label, "RUN") == 0;
    u8g2_SetFont(&oled, header ? u8g2_font_5x7_tr : u8g2_font_helvR10_tr);
    const auto before = DisplayHome::measure(&oled, label);
    u8g2_SetFont(&oled, header ? u8g2_font_7x13_tr : u8g2_font_helvR08_tr);
    const auto after = DisplayHome::measure(&oled, label);
    assert(header ? after.height() * 100 == before.height() * 150
                  : after.height() * 100 >= before.height() * 65 && after.height() * 100 <= before.height() * 75);
    printf("%s: %d -> %d visible pixels, new width %d\n", label, before.height(), after.height(), after.width);
  }

  memset(pixels, 0, 1024);
  DisplayHome::drawHeader(&oled);
  const Bounds brand = bounds(pixels, 0, 0, 64, 14), run = bounds(pixels, 64, 0, 128, 14);
  assert(brand.left == 4 && run.right == 123);
  assert(brand.top == 4 && brand.top == run.top && brand.bottom == run.bottom);
  assert(brand.height() == 9);

  const char* states[] = {"Connected", "Connecting...", "Reconnecting...", "Offline"};
  const char* const labels[] = {"AB", "B2", "Long label", "gj"};
  for (unsigned i = 0; i < 4; ++i) {
    // Test the longest address with EVERY state, plus the no-address rendering.
    for (const char* ip : {"255.255.255.255", "No IP"}) {
      memset(pixels, 0, 1024);
      DisplayHome::drawNetwork(&oled, states[i], ip);
      centered(bounds(pixels, 0, DisplayHome::statusTop, 128, DisplayHome::statusTop + DisplayHome::statusHeight),
               0, DisplayHome::statusTop, 128, DisplayHome::statusHeight);
      centered(bounds(pixels, 0, DisplayHome::ipTop, 128, DisplayHome::ipTop + DisplayHome::ipHeight),
               0, DisplayHome::ipTop, 128, DisplayHome::ipHeight);
      assert(bounds(pixels).left >= 4 && bounds(pixels).right <= 123);
    }
    for (unsigned pressed : {0U, 15U}) {
      for (const char* feedback : {"", "B1: 4 sent", "B1: Invalid config", "B2: No triggers", "Release buttons", "B4: Partial 1/4"}) {
        memset(pixels, 0, 1024);
        DisplayHome::draw(&oled, states[i], i == 0 ? "255.255.255.255" : "No IP",
                          labels, pressed, feedback, 120);
        const Bounds all = bounds(pixels);
        assert(all.left == 4 && all.right == 123 && all.top == 4 && all.bottom == DisplayHome::holdTop);
        assert(bounds(pixels, 0, 15, 128, 16).right == -1);
        assert(bounds(pixels, 0, 26, 128, 27).right == -1);
        assert(bounds(pixels, 0, 35, 128, 36).right == -1);
        assert(bounds(pixels, 0, 45, 128, 46).right == -1);
        assert(bounds(pixels, 0, 60, 128, 61).right == -1);
        if (*feedback) {
          const Bounds detail = bounds(pixels, 0, DisplayHome::feedbackTop, 128,
                                       DisplayHome::feedbackTop + DisplayHome::feedbackHeight);
          assert(detail.height() >= 7 && detail.height() <= DisplayHome::feedbackHeight);
          centered(detail, 0, DisplayHome::feedbackTop, 128, DisplayHome::feedbackHeight);
        }
        assert(bounds(pixels, 64, 0, 128, 14).height() == 9); // RUN remains present during feedback.
      }
    }
    memset(pixels, 0, 1024);
    DisplayHome::draw(&oled, states[i], i == 0 ? "255.255.255.255" : "No IP", labels,
                      1U << i, i == 0 ? "B1: 3 sent" : "", i == 2 ? 60 : 0);
    if (argc > 1) { char name[32]; snprintf(name, sizeof(name), "state-%u", i); capture(pixels, argv[1], name); }
  }

  // Readability regression: sent details use larger glyphs, not a scaled preview.
  u8g2_SetFont(&oled, u8g2_font_4x6_tr);
  const auto oldSent = DisplayHome::measure(&oled, "B1: 4 sent");
  u8g2_SetFont(&oled, u8g2_font_6x10_tr);
  const auto newSent = DisplayHome::measure(&oled, "B1: 4 sent");
  assert(newSent.height() >= 8 && newSent.height() > oldSent.height());
  printf("Sent details: %d -> %d visible pixels, width %d -> %d\n",
         oldSent.height(), newSent.height(), oldSent.width, newSent.width);

  // Label ink is centered independently of font-wide descenders and side bearings.
  const char* samples[] = {"AB", "A", "B4", "gj", "()", ".", "WXYZ", "long custom", "?", "_"};
  for (const char* sample : samples) {
    char label[5]; DisplayHome::abbreviate(sample, label);
    u8g2_SetFont(&oled, strlen(label) > 2 ? u8g2_font_5x7_tr : u8g2_font_6x12_tr);
    const auto text = DisplayHome::measure(&oled, label);
    assert(text.width <= DisplayHome::buttonWidth - 4 && text.height() <= DisplayHome::buttonHeight - 4);
    memset(pixels, 0, 1024);
    u8g2_DrawStr(&oled, (DisplayHome::buttonWidth - text.width) / 2 - text.left + 4,
                 DisplayHome::centeredBaseline(text, DisplayHome::buttonTop, DisplayHome::buttonHeight), label);
    centered(bounds(pixels), 4, DisplayHome::buttonTop, DisplayHome::buttonWidth, DisplayHome::buttonHeight);
  }
  char shortLabel[5], longLabel[5];
  DisplayHome::abbreviate("ABCD", shortLabel); DisplayHome::abbreviate("ABCDE", longLabel);
  assert(strcmp(shortLabel, "ABCD") == 0 && strcmp(longLabel, "ABC.") == 0);
  for (int i = 0; i < 16; ++i) assert(guarded[i] == 0xa5 && guarded[1040 + i] == 0xa5);
  assert(DisplayHome::margin * 2 + DisplayHome::buttonCount * DisplayHome::buttonWidth +
         (DisplayHome::buttonCount - 1) * DisplayHome::buttonGap == 128);
  puts("All home states, glyph centering, equal margins/gaps, feedback, and framebuffer bounds passed.");
}
