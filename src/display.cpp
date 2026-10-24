#include "serial_log.h"
#include "display.h"

#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>
#include "pins.h"

void Display::begin() {
  if (!Wire.begin(Pins::sda, Pins::scl)) {
    FirmwareSerial.println("[OLED] I2C initialization failed; continuing without display.");
    return;
  }
  Wire.setClock(400000);
  Wire.setTimeOut(5);  // Bound a failed bus transaction if a display disconnects.
  uint8_t detectedAddress = 0;
  for (const uint8_t address : {0x3C, 0x3D}) {
    Wire.beginTransmission(address);
    const bool found = Wire.endTransmission() == 0;
    FirmwareSerial.printf("[OLED] 0x%02X: %s\n", address, found ? "ACK" : "no ACK");
    if (found && detectedAddress == 0) detectedAddress = address;
  }
  if (!detectedAddress) {
    FirmwareSerial.println("[OLED] Missing; continuing with buttons and serial.");
    return;
  }

  // U8g2's GPIO init configures its explicit SDA/SCL pins as outputs before
  // calling Wire.begin(). In Arduino-ESP32 2.x, Wire.begin() returns early if
  // already initialized, leaving those pins detached from hardware I2C.
  // Release the probe bus so U8g2's Wire.begin() restores the pin routing.
  Wire.end();
  FirmwareSerial.println("[OLED] Initializing SSD1306...");
  // U8g2 expects the 7-bit I2C address shifted left by one.
  oled_.setI2CAddress(detectedAddress << 1);
  oled_.setBusClock(400000);
  // The constructor supplies GPIO6/GPIO5 explicitly to U8g2's Wire.begin().
  oled_.begin();
  Wire.setTimeOut(5);
  address_ = detectedAddress;
  available_ = true;
  FirmwareSerial.printf("[OLED] SSD1306 initialized at 0x%02X\n", detectedAddress);
}

void Display::logStatus() const {
  if (available_) FirmwareSerial.printf("[OLED] Initialized at 0x%02X; SDA=5 SCL=6\n", address_);
  else FirmwareSerial.println("[OLED] Unavailable; buttons remain active.");
}

void Display::drawIndicators(const ButtonController& buttons, const Commands& commands, uint8_t y) {
  oled_.setFont(u8g2_font_6x12_tr);
  for (uint8_t i = 0; i < ButtonController::count; ++i) {
    const uint8_t x = 4 + i * 31;
    if (buttons.pressed(i)) oled_.drawRBox(x, y, 27, 19, 3);
    else oled_.drawRFrame(x, y, 27, 19, 3);
    oled_.setDrawColor(buttons.pressed(i) ? 0 : 1);
    const char* label = commands.buttonValid(i) ? commands.mapping(i).label : "?";
    oled_.setFont(strlen(label) > 2 ? u8g2_font_5x7_tr : u8g2_font_6x12_tr);
    oled_.drawStr(x + (27 - oled_.getStrWidth(label)) / 2, y + 13, label);
    oled_.setDrawColor(1);
  }
}

void Display::draw(const ButtonController& buttons, const Network& network,
                    const Commands& commands, const AppConnection& app, uint32_t now) {
  oled_.clearBuffer();
  oled_.setFont(u8g2_font_6x12_tr);
  oled_.drawStr(4, 10, "CueLink");
  char text[32];
  if (buttons.mode() == Mode::Run) {
    oled_.drawStr(106, 10, "RUN");
  } else {
    oled_.setFont(u8g2_font_5x7_tr);
    snprintf(text, sizeof(text), "DIAG %u/%u", buttons.page() + 1,
             ButtonController::pageCount);
    oled_.drawStr(74, 10, text);
  }
  oled_.setFont(u8g2_font_6x12_tr);
  oled_.drawHLine(4, 13, 120);

  const uint32_t hold = buttons.holdElapsed(now);
  if (buttons.mode() == Mode::Run) {
    drawIndicators(buttons, commands, 16);
    oled_.setFont(u8g2_font_5x7_tr);
    if (network.state() == Network::State::NotConfigured) {
      snprintf(text, sizeof(text), "Wi-Fi not configured");
    } else snprintf(text, sizeof(text), "Wi-Fi %s", network.stateText());
    oled_.drawStr(4, 44, text);
    snprintf(text, sizeof(text), "IP %s",
             network.connected() ? network.ip().toString().c_str() : "--");
    oled_.drawStr(4, 53, text);
    // Network status and IP have dedicated lines even during event feedback.
    // Only a currently active B4 hold gets a progress strip.
    if (hold) oled_.drawBox(4, 55, (hold * 120U) / ButtonController::holdMs, 1);
    if (buttons.waitingForRelease()) {
      snprintf(text, sizeof(text), "Release all buttons");
    } else if (hold) {
      snprintf(text, sizeof(text), "Hold B4: diagnostics");
    } else if (commands.feedbackActive()) {
      commands.feedback(text, sizeof(text));
    } else {
      snprintf(text, sizeof(text), "Hold B4 5s: diagnostics");
    }
    oled_.drawStr(4, 62, text);
    return;
  }

  if (buttons.page() == 0) {
    oled_.drawStr(4, 25, "LIVE BUTTONS");
    drawIndicators(buttons, commands, 29);
  } else if (buttons.page() == 1) {
    oled_.setFont(u8g2_font_6x10_tr);
    // Counters are run-mode short events only; navigation never increments them.
    for (uint8_t i = 0; i < ButtonController::count; ++i) {
      snprintf(text, sizeof(text), "B%u %lu", i + 1,
               static_cast<unsigned long>(buttons.shortPresses(i)));
      oled_.drawStr(4, 24 + i * 10, text);
    }
    oled_.setFont(u8g2_font_5x7_tr);
    oled_.drawStr(89, 24, "SHORT");
    oled_.drawStr(89, 34, "RUN");
  } else if (buttons.page() == 2) {
    const uint64_t seconds = uptimeMs_ / 1000;
    snprintf(text, sizeof(text), "Up %luh %02lum %02lus",
             static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds / 60) % 60),
             static_cast<unsigned long>(seconds % 60));
    oled_.drawStr(4, 26, text);
    snprintf(text, sizeof(text), "FW %s", firmwareVersion);
    oled_.drawStr(4, 38, text);
    snprintf(text, sizeof(text), "Heap %lu B", static_cast<unsigned long>(ESP.getFreeHeap()));
    oled_.drawStr(4, 50, text);
  } else if (buttons.page() == 3) {
    oled_.setFont(u8g2_font_5x7_tr);
    oled_.drawStr(4, 24, "NETWORK");
    oled_.drawStr(4, 34, network.stateText());
    // SSIDs are at most 32 bytes, but the display has room for 19 here.
    // Replace non-ASCII/control bytes and mark truncation explicitly.
    char name[20] = {};
    const char* source = network.ssid();
    uint8_t length = 0;
    while (length < 19 && source[length]) {
      const uint8_t ch = static_cast<uint8_t>(source[length]);
      name[length++] = ch >= 32 && ch <= 126 ? char(ch) : '?';
    }
    if (length == 19 && source[length]) {
      name[16] = name[17] = name[18] = '.';
    }
    snprintf(text, sizeof(text), "SSID %s", name[0] ? name : "--");
    oled_.drawStr(4, 44, text);
    snprintf(text, sizeof(text), "IP %s",
             network.connected() ? network.ip().toString().c_str() : "--");
    oled_.drawStr(4, 54, text);
  } else if (buttons.page() == 4) {
    oled_.setFont(u8g2_font_5x7_tr);
    oled_.drawStr(4, 24, "SIGNAL");
    if (network.connected()) {
      snprintf(text, sizeof(text), "RSSI %ld dBm", static_cast<long>(network.rssi()));
    } else snprintf(text, sizeof(text), "RSSI unavailable");
    oled_.drawStr(4, 34, text);
    if (network.connected()) snprintf(text, sizeof(text), "Channel %u", network.channel());
    else snprintf(text, sizeof(text), "Channel --");
    oled_.drawStr(4, 44, text);
    snprintf(text, sizeof(text), "Losses %lu", static_cast<unsigned long>(network.losses()));
    oled_.drawStr(4, 54, text);
  } else if (buttons.page() == 5) {
    oled_.setFont(u8g2_font_5x7_tr);
    oled_.drawStr(4, 24, "ADDRESSING");
    snprintf(text, sizeof(text), "Mask %s",
             network.connected() ? network.subnet().toString().c_str() : "--");
    oled_.drawStr(4, 34, text);
    snprintf(text, sizeof(text), "GW   %s",
             network.connected() ? network.gateway().toString().c_str() : "--");
    oled_.drawStr(4, 44, text);
    snprintf(text, sizeof(text), "DNS  %s",
             network.connected() ? network.dns().toString().c_str() : "--");
    oled_.drawStr(4, 54, text);
  } else if (buttons.page() < 10) {
    oled_.setFont(u8g2_font_5x7_tr);
    const uint8_t button = buttons.page() - 6;
    const auto& mapping = commands.mapping(button);
    snprintf(text, sizeof(text), "B%u %s triggers: %u", button + 1,
             commands.buttonValid(button) ? mapping.label : "?", static_cast<unsigned>(mapping.count));
    oled_.drawStr(4, 22, text);
    if (!commands.buttonValid(button)) oled_.drawStr(4, 34, "Invalid; not activated");
    else for (uint8_t slot = 0; slot < 4; ++slot) {
      if (slot >= mapping.count) snprintf(text, sizeof(text), "%u -- empty", slot + 1);
      else {
        const auto& trigger = mapping.triggers[slot];
        const auto& report = commands.report(button, slot);
        snprintf(text, sizeof(text), "%u %-4s %s %u/%u", slot + 1,
                 Command::protocolText(trigger.protocol()), trigger.enabled() ? Commands::resultCode(report.result) : "off",
                 report.destinationSuccess, report.destinationCount);
      }
      oled_.drawStr(4, 30 + slot * 8, text);
    }
  } else if (buttons.page() == 10) {
    oled_.setFont(u8g2_font_5x7_tr);
    oled_.drawStr(4, 24, "LAST SEND (LOCAL ONLY)");
    const auto& last = commands.last();
    if (last.result != Commands::Result::None) {
      snprintf(text, sizeof(text), "B%u S%u %s", last.button + 1, last.slot + 1, Command::protocolText(last.protocol));
      oled_.drawStr(4, 34, text);
      snprintf(text, sizeof(text), "%uB source %u", static_cast<unsigned>(last.length), last.sourcePort);
      oled_.drawStr(4, 54, text);
    }
    oled_.drawStr(4, 44, Commands::resultText(last.result));
  } else if (buttons.page() == 11) {
    oled_.setFont(u8g2_font_5x7_tr);
    const auto& counts = commands.counts();
    snprintf(text, sizeof(text), "Submitted %lu", static_cast<unsigned long>(counts.submitted));
    oled_.drawStr(4, 24, text);
    snprintf(text, sizeof(text), "Offline drops %lu", static_cast<unsigned long>(counts.offline));
    oled_.drawStr(4, 34, text);
    snprintf(text, sizeof(text), "Local fails %lu", static_cast<unsigned long>(counts.localFailures));
    oled_.drawStr(4, 44, text);
    snprintf(text, sizeof(text), "Config drops %lu", static_cast<unsigned long>(counts.invalid));
    oled_.drawStr(4, 54, text);
  } else if (buttons.page() == ButtonController::appConnectionPage) {
    oled_.setFont(u8g2_font_5x7_tr);
    oled_.drawStr(4, 24, "APP CONNECTION");
    oled_.drawStr(4, 34, app.stateText());
    oled_.drawStr(4, 44, app.name());
    if (app.remainingSeconds(now)) {
      snprintf(text, sizeof(text), "B3: renew / %lus left",
               static_cast<unsigned long>(app.remainingSeconds(now)));
      oled_.drawStr(4, 54, text);
    } else oled_.drawStr(4, 54, "B3: discover for 120s");
  } else if (buttons.page() == ButtonController::usbMidiPage) {
    oled_.setFont(u8g2_font_5x7_tr);
    oled_.drawStr(4, 24, commands.midi().ready() ? "USB MIDI: Ready" :
                  commands.midi().mounted() ? "USB: mounted, not ready" : "USB MIDI: Not mounted");
    snprintf(text, sizeof(text), "Pending %u/16", static_cast<unsigned>(commands.midi().pendingCount()));
    oled_.drawStr(4, 34, text);
    snprintf(text, sizeof(text), "Off fail %lu", static_cast<unsigned long>(commands.midi().releaseFailures()));
    oled_.drawStr(4, 44, text);
    snprintf(text, sizeof(text), "Off dropped %lu", static_cast<unsigned long>(commands.midi().droppedReleases()));
    oled_.drawStr(4, 54, text);
  } else {
    oled_.setFont(u8g2_font_5x7_tr);
    const uint8_t button = triggerDetail_ / 8, slot = (triggerDetail_ / 2) % 4, half = triggerDetail_ % 2;
    const auto& mapping = commands.mapping(button);
    snprintf(text, sizeof(text), "B%u S%u / B3 next", button + 1, slot + 1);
    oled_.drawStr(4, 22, text);
    if (!commands.buttonValid(button) || slot >= mapping.count) oled_.drawStr(4, 34, "No active configuration");
    else {
      const auto& trigger = mapping.triggers[slot];
      const auto& report = commands.report(button, slot);
      if (trigger.protocol() == Command::Protocol::UsbMidi) {
        const auto* message = trigger.midi();
        const char* type = "?";
        if (message) switch (message->type) {
          case Midi::Type::NotePulse: type = "Pulse"; break;
          case Midi::Type::NoteOn: type = "On"; break;
          case Midi::Type::NoteOff: type = "Off"; break;
          case Midi::Type::ControlChange: type = "CC"; break;
          case Midi::Type::ProgramChange: type = "PC"; break;
        }
        // Program numbers stay 0-based, matching development settings/wire data.
        if (message && message->type == Midi::Type::ProgramChange)
          snprintf(text, sizeof(text), "PC ch%u program %u", message->channel, message->data1);
        else snprintf(text, sizeof(text), "%s ch%u %u/%u", type, message ? message->channel : 0,
                      message ? message->data1 : 0, message ? message->data2 : 0);
        oled_.drawStr(4, 30, text);
        oled_.drawStr(4, 38, Commands::resultText(report.result));
        const auto& release = commands.midi().lastRelease();
        if (release.valid) {
          snprintf(text, sizeof(text), "Off ch%u note%u", release.channel, release.note);
          oled_.drawStr(4, 46, text);
          oled_.drawStr(4, 54, release.result == Midi::Result::Submitted ? "Off: Local submission" : "Off: Local failure");
        }
      } else {
        const auto destinations = commands.destinations(button, slot);
        if (!destinations.count) oled_.drawStr(4, 34, "Set/fix destination list");
        for (uint8_t row = 0; row < 4; ++row) {
          const uint8_t index = half * 4 + row;
          if (index >= destinations.count) continue;
          const auto& destination = destinations.items[index];
          Command::IPv4 ip;
          if (Command::validateDestination(destination, ip) == Command::DestinationStatus::Valid) {
            snprintf(text, sizeof(text), "%u.%u.%u.%u:%lu %s", ip.octets[0], ip.octets[1], ip.octets[2], ip.octets[3],
                     static_cast<unsigned long>(destination.port), Commands::resultCode(report.destinations[index]));
          } else snprintf(text, sizeof(text), "D%u invalid/unset %s", index + 1, Commands::resultCode(report.destinations[index]));
          oled_.drawStr(4, 30 + row * 8, text);
        }
      }
    }
  }


  // A progress strip remains visible on diagnostic pages as B4 is held.
  if (hold) oled_.drawBox(4, 55, (hold * 120U) / ButtonController::holdMs, 1);
  oled_.setFont(u8g2_font_5x7_tr);
  oled_.drawStr(4, 62, buttons.waitingForRelease() ? "Release all buttons" : "B1< B2>   B4 hold: exit");
}

void Display::update(const ButtonController& buttons, const Network& network,
                     const Commands& commands, const AppConnection& app, uint32_t now) {
  uptimeMs_ += uint32_t(now - previousTick_);
  previousTick_ = now;
  if (!available_) return;
  if (!sending_) {
    if (uint32_t(now - lastFrameAt_) < 40) return;  // At most 25 frames/second.
    lastFrameAt_ = now;
    draw(buttons, network, commands, app, now);
    nextRow_ = 0;
    sending_ = true;
  }
  // One 128-byte tile row per loop: sample buttons between short I2C transfers.
  oled_.updateDisplayArea(0, nextRow_, 16, 1);
  if (++nextRow_ == 8) sending_ = false;
}
