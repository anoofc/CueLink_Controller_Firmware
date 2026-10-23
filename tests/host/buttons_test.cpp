#include <assert.h>
#include <stdio.h>

#include "Arduino.h"
#include "buttons.h"
#include "pins.h"

static bool levels[8];
int digitalRead(uint8_t pin) { return levels[pin] ? LOW : HIGH; }
void pinMode(uint8_t, int mode) { assert(mode == INPUT_PULLUP); }

struct Fixture {
  ButtonController buttons;
  uint32_t now;

  explicit Fixture(uint32_t start = 0, uint8_t held = 0) : now(start) {
    for (uint8_t i = 0; i < 4; ++i) levels[Pins::buttons[i]] = held & (1U << i);
    buttons.begin(now);
  }
  ButtonController::Events tick(uint32_t elapsed) {
    now += elapsed;
    return buttons.update(now);
  }
  ButtonController::Events edge(uint8_t index, bool down) {
    levels[Pins::buttons[index]] = down;
    assert(tick(1).shortPressMask == 0);
    return tick(ButtonController::debounceMs);
  }
  void arm() { tick(30); }
  void enterDiagnostics() {
    assert(edge(3, true).shortPressMask == 0);
    assert(!tick(4999).modeChanged);
    assert(tick(1).modeChanged);
    assert(buttons.mode() == Mode::Diagnostics);
  }
};

int main() {
  // Each run button fires once on press, including simultaneous presses.
  {
    Fixture f;
    f.arm();
    for (uint8_t i = 0; i < 3; ++i) {
      assert(f.edge(i, true).shortPressMask == (1U << i));
      assert(f.tick(6000).shortPressMask == 0);
      assert(f.edge(i, false).shortPressMask == 0);
      assert(f.buttons.shortPresses(i) == 1);
    }
    for (uint8_t i = 0; i < 3; ++i) levels[Pins::buttons[i]] = true;
    f.tick(1);
    assert(f.tick(30).shortPressMask == 7);
  }
  // Contact bounce cannot create events before a stable 30 ms interval.
  {
    Fixture f;
    f.arm();
    levels[Pins::buttons[0]] = true;
    assert(f.tick(1).shortPressMask == 0);
    assert(f.tick(20).shortPressMask == 0);
    levels[Pins::buttons[0]] = false;
    assert(f.tick(1).shortPressMask == 0);
    levels[Pins::buttons[0]] = true;
    assert(f.tick(1).shortPressMask == 0);
    assert(f.tick(29).shortPressMask == 0);
    assert(f.tick(1).shortPressMask == 1);
    assert(f.buttons.pressed(0));
  }
  // Startup holds are visible but inert until released, for every button.
  for (uint8_t i = 0; i < 4; ++i) {
    Fixture f(0, 1U << i);
    assert(f.buttons.pressed(i));
    auto events = f.tick(10000);
    assert(events.shortPressMask == 0 && !events.modeChanged);
    assert(f.edge(i, false).shortPressMask == 0);
    const auto press = f.edge(i, true);
    if (i < 3) assert(press.shortPressMask == (1U << i));
    else {
      assert(press.shortPressMask == 0);
      assert(f.edge(i, false).shortPressMask == 8);
    }
  }
  // B4 short events occur only on release; release at the threshold is long.
  {
    Fixture f;
    f.arm();
    assert(f.edge(3, true).shortPressMask == 0);
    f.tick(100);
    assert(f.edge(3, false).shortPressMask == 8);
    assert(f.buttons.shortPresses(3) == 1);
    f.edge(3, true);
    f.tick(4969);
    const auto events = f.edge(3, false);  // Release settles at exactly 5000 ms.
    assert(events.modeChanged && events.shortPressMask == 0);
    assert(f.buttons.shortPresses(3) == 1);
  }
  // Mode transitions lock all input until every button is stably released.
  {
    Fixture f;
    f.arm();
    f.edge(0, true);
    f.enterDiagnostics();
    assert(!f.tick(10000).modeChanged);  // Holding B4 cannot transition twice.
    f.edge(3, false);
    assert(f.buttons.waitingForRelease());
    assert(f.edge(1, true).shortPressMask == 0);
    assert(f.buttons.page() == 0);
    f.edge(1, false);
    f.edge(0, false);
    assert(!f.buttons.waitingForRelease());
    f.edge(0, true);
    assert(f.buttons.page() == ButtonController::pageCount - 1);
    f.edge(0, false);
    f.edge(1, true);
    assert(f.buttons.page() == 0);
    f.edge(1, false);
    f.edge(1, true);
    assert(f.buttons.page() == 1);
    f.edge(1, false);
    f.edge(2, true);
    assert(f.buttons.page() == 1);
    f.edge(2, false);
    assert(f.buttons.shortPresses(0) == 1);
    assert(f.buttons.shortPresses(1) == 0);
    assert(f.buttons.shortPresses(2) == 0);
    // Visit every diagnostic page and wrap, without creating run events.
    for (uint8_t i = 0; i < ButtonController::pageCount; ++i) {
      const uint8_t previous = f.buttons.page();
      assert(f.edge(1, true).shortPressMask == 0);
      assert(f.buttons.page() == (previous + 1) % ButtonController::pageCount);
      f.edge(1, false);
    }
    f.edge(3, true);
    assert(f.edge(3, false).shortPressMask == 0);
    f.edge(3, true);
    const auto exit = f.tick(5000);
    assert(exit.modeChanged && exit.shortPressMask == 0);
    assert(f.buttons.mode() == Mode::Run);
    assert(f.edge(0, true).shortPressMask == 0);
    f.edge(3, false);
    assert(f.buttons.waitingForRelease());
    f.edge(0, false);
    assert(!f.buttons.waitingForRelease());
    assert(f.edge(0, true).shortPressMask == 1);
    assert(f.buttons.shortPresses(3) == 0);
  }
  // B3 enables discovery only on App connection, never as a run short action.
  {
    Fixture f;
    f.arm();
    f.enterDiagnostics();
    f.edge(3, false);
    f.edge(0, true);  // Wrap directly to the new last page.
    f.edge(0, false);
    assert(f.buttons.page() == ButtonController::appConnectionPage);
    const auto request = f.edge(2, true);
    assert(request.appConnectionRequested && request.shortPressMask == 0);
    assert(!f.tick(6000).appConnectionRequested);
    assert(f.buttons.shortPresses(2) == 0);
    f.edge(2, false);
    f.edge(1, true);
    f.edge(1, false);
    assert(!f.edge(2, true).appConnectionRequested);
    assert(f.buttons.shortPresses(2) == 0);
  }
  // Both debounce and the five-second hold survive uint32_t millis rollover.
  {
    Fixture f(UINT32_MAX - 40);
    f.arm();
    assert(f.edge(0, true).shortPressMask == 1);
  }
  {
    Fixture f(UINT32_MAX - 2000);
    f.arm();
    f.enterDiagnostics();
    assert(f.buttons.shortPresses(3) == 0);
  }
  puts("All button state-machine checks passed.");
}
