#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
#include "display_boot.h"
#include "buttons.h"
#include "pins.h"
#include "midi.h"
#include "../host/Arduino.h"
#include "../network/Arduino.h"

SerialStub Serial;
static bool down[8] = {};
int digitalRead(uint8_t pin) { return down[pin] ? LOW : HIGH; }
void pinMode(uint8_t, int mode) { assert(mode == INPUT_PULLUP); }
struct Port : Midi::Port {
  unsigned ons = 0, offs = 0;
  bool mounted() const override { return true; }
  bool ready() const override { return true; }
  uint32_t sessionEpoch() const override { return 0; }
  bool send(const Midi::Packet& p) override {
    if ((p.bytes[1] & 0xf0) == 0x90) ++ons;
    if ((p.bytes[1] & 0xf0) == 0x80) ++offs;
    return true;
  }
};
static bool pixel(const uint8_t* buffer, int x, int y) {
  return buffer[(y / 8) * 128 + x] & (1U << (y % 8));
}
static void capture(const uint8_t* pixels, const char* directory, unsigned elapsed) {
  char path[512]; snprintf(path, sizeof(path), "%s/boot-%04u.pbm", directory, elapsed);
  FILE* f = fopen(path, "w"); assert(f);
  fprintf(f, "P1\n128 64\n");
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) fprintf(f, "%u ", pixel(pixels, x, y));
    fprintf(f, "\n");
  }
  fclose(f);
}
int main(int argc, char** argv) {
  using DisplayBoot::Animation;
  Animation animation;
  assert(!animation.ownsDisplay()); // Missing OLED: the unarmed display cannot own anything.
  animation.arm();
  assert(animation.ownsDisplay() && animation.elapsed(9000) == 0);
  assert(!animation.advance(9000)); // Initialization time before the frame is displayed does not count.
  animation.firstFrameDisplayed(9000);
  animation.firstFrameDisplayed(10000); // Later transfers never restart the timer.
  animation.arm();
  assert(animation.elapsed(13999) == 4999 && !animation.advance(13999));
  assert(animation.advance(14000) && !animation.ownsDisplay());
  for (uint32_t now = 14000; now < 60000; now += 40) {
    animation.arm(); animation.firstFrameDisplayed(now);
    assert(!animation.advance(now) && !animation.ownsDisplay());
  }
  Animation reset; reset.arm(); reset.firstFrameDisplayed(0);
  assert(reset.ownsDisplay()); // A fresh instance (software/device reset) plays again.
  Animation rollover; rollover.arm();
  const uint32_t start = UINT32_MAX - 99;
  rollover.firstFrameDisplayed(start);
  assert(!rollover.advance(uint32_t(start + 4999)));
  assert(rollover.advance(uint32_t(start + 5000)));
  assert(DisplayBoot::frameMs == 40 && DisplayBoot::durationMs == 5000);

  const uint8_t reveal[] = {1, 3, 11, 15}, clockwise[] = {1, 2, 8, 4};
  for (unsigned i = 0; i < 4; ++i) {
    auto frame = DisplayBoot::frameAt(i * 250);
    assert(frame.visible == reveal[i] && frame.filled == reveal[i] && !frame.wordmark);
    frame = DisplayBoot::frameAt(1000 + i * 250);
    assert(frame.visible == 15 && frame.filled == clockwise[i] && frame.wordmark);
  }
  assert(DisplayBoot::frameAt(3999).filled == 4);
  assert(DisplayBoot::frameAt(4000).filled == 15);
  assert(DisplayBoot::frameAt(4999).indicator == 3);

  uint8_t guarded[1056]; memset(guarded, 0xa5, sizeof(guarded));
  uint8_t* pixels = guarded + 16;
  u8g2_t oled = {};
  u8x8_display_info_t info = {};
  info.tile_width = 16; info.tile_height = 8; info.pixel_width = 128; info.pixel_height = 64;
  oled.u8x8.display_info = &info;
  u8g2_SetupBuffer(&oled, pixels, 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  for (unsigned elapsed = 0; elapsed < 5000; elapsed += 40) {
    memset(pixels, 0xff, 1024); // Every frame must clear stale pixels itself.
    DisplayBoot::draw(&oled, elapsed);
    int left = 128, right = -1, top = 64, bottom = -1;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x) {
      if (!pixel(pixels, x, y)) continue;
      assert(x >= 4 && x < 124 && y >= 4 && y < 60);
      if (x < left) left = x;
      if (x > right) right = x;
      if (y < top) top = y;
      if (y > bottom) bottom = y;
      assert(!(y >= 33 && y < 36)); // Clear space between mark and wordmark.
      assert(!(y >= 48 && y < 54)); // Clear space before the indicator.
      if (elapsed < 1000) assert(y < 36 || y >= 54);
    }
    assert(top == 7 && bottom == 56 && top + bottom == 63);
    if (elapsed >= 1000) assert(left + right >= 126 && left + right <= 128);
    for (int i = 0; i < 16; ++i) assert(guarded[i] == 0xa5 && guarded[1040 + i] == 0xa5);
    if (argc > 1) capture(pixels, argv[1], elapsed);
  }
  // Clean handover: a home frame rendered after the last boot frame must exactly
  // match the same home rendered on a fresh buffer, including offline/connecting.
  uint8_t expected[1024];
  const char* const labels[] = {"AB", "B2", "Long label", "gj"};
  for (const char* state : {"Connected", "Connecting...", "Reconnecting...", "Offline"}) {
    u8g2_ClearBuffer(&oled);
    DisplayHome::draw(&oled, state, "No IP", labels, 0, "", 0);
    memcpy(expected, pixels, 1024);
    DisplayBoot::draw(&oled, 4999);
    u8g2_ClearBuffer(&oled); // Display::draw clears before drawing the current live values.
    DisplayHome::draw(&oled, state, "No IP", labels, 0, "", 0);
    assert(memcmp(expected, pixels, 1024) == 0);
  }
  if (argc > 1) for (unsigned elapsed : {0U, 250U, 500U, 750U, 1000U, 1250U, 1500U, 1750U, 4000U, 4750U}) {
    DisplayBoot::draw(&oled, elapsed); capture(pixels, argv[1], elapsed);
  }

  if (argc > 1) {
    u8g2_ClearBuffer(&oled);
    DisplayHome::draw(&oled, "Offline", "No IP", labels, 0, "", 0);
    capture(pixels, argv[1], 5000);
  }

  // Real button debounce and MIDI Note Off scheduling continue while boot owns
  // the OLED; the animation state introduces no gate or waiting loop.
  ButtonController buttons; buttons.begin(0);
  Port port; Midi::Engine midi(port); midi.begin();
  Animation responsive; responsive.arm(); responsive.firstFrameDisplayed(0);
  for (uint32_t now = 0; now < 5000; now += 10) {
    down[Pins::buttons[0]] = now >= 100 && now < 200;
    const auto events = buttons.update(now);
    midi.update(now);
    if (events.shortPressMask & 1) {
      assert(now == 130 && responsive.ownsDisplay());
      assert(midi.submit(Midi::Message::pulse(1, 60, 100, 100), true, now) == Midi::Result::Submitted);
    }
    responsive.advance(now);
    if (now == 230) assert(port.ons == 1 && port.offs == 1 && responsive.ownsDisplay());
  }
  assert(buttons.shortPresses(0) == 1 && midi.pendingCount() == 0);
  assert(responsive.advance(5000));
  puts("Boot timing, one-shot/reset/rollover, clockwise stages, 125 raster frames, clean handover, buttons and MIDI during boot passed.");
}
