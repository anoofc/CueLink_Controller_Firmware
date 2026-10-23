#include <Arduino.h>

#include "buttons.h"
#include "display.h"
#include "network.h"
#include "commands.dev.h"

ButtonController buttons;
Display display;
Network network;
Commands commands(DevelopmentCommands::buttons);
bool serialWasConnected = false;

void setup() {
  Serial.begin(115200);
  Serial.printf("\nCueLink Controller %s\n", firmwareVersion);
  buttons.begin(millis());
  display.begin();
  commands.begin();
  network.begin(millis());
  Serial.println("[MODE] RUN; held startup buttons must be released to arm.");
}

void loop() {
  const uint32_t now = millis();
  const bool onlineAtEvent = network.connected();
  const ButtonController::Events events = buttons.update(now);
  network.update(now);
  commands.update(network, now);
  commands.handleEvents(buttons.mode(), events, network, onlineAtEvent, now);
  const bool serialConnected = bool(Serial);
  if (serialConnected && !serialWasConnected) {
    // USB monitors commonly attach after setup has already printed its banner.
    // Report once per connection without waiting for USB or streaming states.
    Serial.printf("\n[READY] CueLink Controller %s; uptime=%lu ms\n", firmwareVersion,
                  static_cast<unsigned long>(now));
    display.logStatus();
    network.logStatus();
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
  display.update(buttons, network, commands, now);
}
