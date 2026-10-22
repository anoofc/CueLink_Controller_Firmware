#include <Arduino.h>

#include "buttons.h"
#include "display.h"

ButtonController buttons;
Display display;
bool serialWasConnected = false;

void setup() {
  Serial.begin(115200);
  Serial.printf("\nCueLink Controller %s\n", firmwareVersion);
  buttons.begin(millis());
  display.begin();
  Serial.println("[MODE] RUN; held startup buttons must be released to arm.");
}

void loop() {
  const uint32_t now = millis();
  const ButtonController::Events events = buttons.update(now);
  const bool serialConnected = bool(Serial);
  if (serialConnected && !serialWasConnected) {
    // USB monitors commonly attach after setup has already printed its banner.
    // Report once per connection without waiting for USB or streaming states.
    Serial.printf("\n[READY] CueLink Controller %s; uptime=%lu ms\n", firmwareVersion,
                  static_cast<unsigned long>(now));
    display.logStatus();
    Serial.printf("[MODE] %s\n", buttons.mode() == Mode::Run ? "RUN" : "DIAGNOSTICS");
  }
  serialWasConnected = serialConnected;
  for (uint8_t i = 0; i < ButtonController::count; ++i) {
    if (events.shortPressMask & (1U << i)) {
      Serial.printf("[SHORT] B%u count=%lu\n", i + 1,
                    static_cast<unsigned long>(buttons.shortPresses(i)));
    }
  }
  if (events.modeChanged) {
    Serial.printf("[MODE] %s; release all buttons to continue.\n",
                  buttons.mode() == Mode::Run ? "RUN" : "DIAGNOSTICS");
  }
  display.update(buttons, now);
}
