#include "serial_log.h"
#include <Arduino.h>

#include "buttons.h"
#include "display.h"
#include "network.h"
#include "commands.dev.h"
#include "usb_midi.h"

ButtonController buttons;
Display display;
Network network;
UsbMidiPort midiPort;
Midi::Engine midi(midiPort);
Commands commands(DevelopmentCommands::configuration, midi);
AppConnection appConnection;
NvsConfigStorage configStorage;
ConfigManager configManager(configStorage);
bool serialWasConnected = false;

void setup() {
  midiPort.begin();
  FirmwareSerial.begin(115200);
  FirmwareSerial.printf("\nCueLink Controller %s\n", firmwareVersion);
  buttons.begin(millis());
  display.begin();
  commands.begin();
  configStorage.begin();
  configManager.begin(network,commands,millis());
  appConnection.attach(configManager);
  appConnection.begin(network, buttons.mode(), millis());
  FirmwareSerial.println("[MODE] RUN; held startup buttons must be released to arm.");
}

void loop() {
  FirmwareSerial.update();
  const uint32_t now = millis();
  const bool onlineAtEvent = network.connected();
  const bool usbAtEvent = midi.ready();
  const ButtonController::Events events = buttons.update(now);
  network.update(now);
  commands.update(network, now);
  commands.handleEvents(buttons.mode(), events, network, onlineAtEvent, usbAtEvent, now);
  configManager.update(now);
  appConnection.update(network, buttons.mode(), events.appConnectionRequested, now);
  const bool serialConnected = bool(FirmwareSerial);
  if (serialConnected && !serialWasConnected) {
    // USB monitors commonly attach after setup has already printed its banner.
    // Report once per connection without waiting for USB or streaming states.
    FirmwareSerial.printf("\n[READY] CueLink Controller %s; uptime=%lu ms\n", firmwareVersion,
                  static_cast<unsigned long>(now));
    display.logStatus();
    network.logStatus();
    FirmwareSerial.printf("[MODE] %s\n", buttons.mode() == Mode::Run ? "RUN" : "DIAGNOSTICS");
  }
  serialWasConnected = serialConnected;
  for (uint8_t i = 0; i < ButtonController::count; ++i) {
    if (events.shortPressMask & (1U << i)) {
      FirmwareSerial.printf("[SHORT] B%u count=%lu\n", i + 1,
                    static_cast<unsigned long>(buttons.shortPresses(i)));
    }
  }
  if (events.modeChanged) {
    FirmwareSerial.printf("[MODE] %s; release all buttons to continue.\n",
                  buttons.mode() == Mode::Run ? "RUN" : "DIAGNOSTICS");
  }
  if (events.triggerDetailRequested) display.advanceTriggerDetail();
  display.update(buttons, network, commands, appConnection, now);
}
