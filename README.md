# CueLink Controller — milestone 1

Version: `M1 / 0.1.1`. Local buttons, serial logging, and SSD1306 display only.
No Wi-Fi, BLE, OSC, or UDP is initialized or implemented.

### Blank-display startup fix (0.1.1)

The probe bus is now stopped before U8g2 initializes it again. U8g2 configures
its explicit I2C pins as GPIO outputs first; Arduino-ESP32 2.x skips restoring
the hardware pin routing if `Wire` is already initialized. This previously
allowed address detection to succeed followed by failure during OLED startup.
Serial output captured from the connected board stopped immediately after the
address probes (`0x3C: ACK`, `0x3D: no ACK`), consistent with this problem.
The correction has been built but still needs to be flashed and checked visually.
The board and upload settings are unchanged. Monitor DTR and RTS are set to zero,
and a ready/version/OLED summary prints whenever the USB monitor connects, so
opening a monitor after startup does not leave an otherwise idle console blank.

For the currently connected board, close any other serial monitor, then use:

```sh
~/.platformio/penv/bin/pio run -e seeed_xiao_esp32s3 -t upload --upload-port /dev/cu.usbmodem101
~/.platformio/penv/bin/pio device monitor -p /dev/cu.usbmodem101 -b 115200
```

Press RESET once if necessary. Look for version `M1 / 0.1.1`, `[READY]`, and
the OLED address `0x3C`. If the port changes, use `pio device list` to find it.

## Hardware and assumptions

The existing `seeed_xiao_esp32s3` PlatformIO board definition, Arduino framework,
platform selection, and upload defaults are preserved. This milestone assumes
that this definition is suitable for the XIAO ESP32-S3 Plus for the requested
GPIOs and basic firmware. It uses the definition's 8 MB flash layout and does
not attempt to use any additional Plus storage or peripherals.

| Connection | GPIO | Configuration |
| --- | --- | --- |
| B1 / D8 | 7 | INPUT_PULLUP, switch to GND |
| B2 / D3 | 4 | INPUT_PULLUP, switch to GND |
| B3 / D1 | 2 | INPUT_PULLUP, switch to GND |
| B4 / D0 | 1 | INPUT_PULLUP, switch to GND |
| OLED SDA / D4 | 5 | Hardware I2C |
| OLED SCL / D5 | 6 | Hardware I2C |
| OLED VCC / GND | — | 3.3 V / common GND |

The OLED is assumed to be a standard SSD1306 128×64 module compatible with
U8g2's NONAME driver, with I2C pull-ups to 3.3 V and support for 400 kHz I2C.
Both 7-bit addresses `0x3C` and `0x3D` are probed at boot. If both acknowledge,
`0x3C` wins; the chosen address is shifted left once for U8g2. Address ACK is
presence detection, not verification of the controller model. There is no
automatic hot-plug reinitialization; reconnect then reset if needed.

## Behavior

Buttons debounce for 30 ms. B1–B3 generate a run event on a stable press;
B4 generates one on a stable release before five seconds. Holding B4 for
five seconds changes modes once, with no short event. Timing starts at the
debounced press. A held startup button remains inert until stably released.
Every mode change locks input until all four buttons are stably released.

Diagnostics opens on page 1. B1 selects the previous page, B2 the next, with
wrapping. B3 does nothing. The pages show live states, run-mode short-press
counts, and uptime/version/free heap. Diagnostic navigation and B4 long holds
never increment counters. Counts and the last event reset on reboot; counts
use unsigned 32-bit values. The most recent event among simultaneous presses
uses the highest button number. Uptime accumulates across `millis()` rollover.

Physical indicators continue updating during input lockout. The display starts
at most 25 frames per second, sending one tile row per loop so input is sampled
between I2C transfers. Hardware I2C transfers are synchronous and brief; the loop
has no `delay()`. A missing OLED leaves button processing and serial operational.
Serial logs contain individual run short events and mode changes, not a stream
of physical button states.

## Build and flash

Run these commands in `firmware/`. On the development machine PlatformIO is at
`~/.platformio/penv/bin/pio`; if `pio` is not on PATH, use that full path for each
command below.

1. Build:

   ```sh
   pio run -e seeed_xiao_esp32s3
   ```

2. Connect the board using a USB data cable and list serial ports:

   ```sh
   pio device list
   ```

3. Substitute the actual board port from that list for `PORT` and upload:

   ```sh
   pio run -e seeed_xiao_esp32s3 -t upload --upload-port PORT
   ```

   If the board cannot enter the bootloader automatically, hold BOOT, press
   and release RESET, then release BOOT. List ports again and retry using the
   bootloader port. Press RESET after upload if the application does not start.

4. List ports again if needed, then open the application serial port:

   ```sh
   pio device monitor -p PORT -b 115200
   ```

   Press RESET with the monitor open to observe startup and both OLED probe
   results. Exit the monitor with Ctrl+C before another upload.

## Manual verification on hardware

1. Boot with buttons released. Check the CueLink run header, four outlined
   indicators, empty last-event footer, and serial OLED probe results.
2. Press each of B1–B3. Each indicator should fill after debounce; exactly one
   `[SHORT]` line and footer update should occur on press. Hold for over five
   seconds, then release: no repeated event and no release event.
3. Tap B4. Its indicator should fill on press; exactly one short event should
   appear on release. Hold it just under five seconds and release: one short
   event. Hold beyond five seconds: watch progress, then exactly one transition
   into diagnostics. Keep holding for another five seconds: no second transition.
   Release: no short event.
4. After releasing all buttons, use B1 to wrap page 1 → 3, and B2 to wrap page
   3 → 1. Visit all pages. Confirm readable live states, expected run counters,
   advancing uptime, version, and heap. B3 should change only its physical
   indicator on page 1. Navigation must produce no `[SHORT]` lines or increments.
5. Hold B4 five seconds to exit. Verify one transition, no short event on release,
   and retained run counters. Test exit while on each diagnostics page.
6. Repeat entry and exit while holding another button across the transition.
   Release B4 and press other buttons: input must stay locked until every button
   has been released together for at least 30 ms. A fresh press after unlock
   should work normally.
7. Reset while holding each button in turn. Keep it held for over five seconds:
   no event or mode change. Release: no event. Press again: normal behavior.
   Buttons that were not held at boot should still work independently.
8. Disconnect power, disconnect the OLED, then restart. With the serial monitor
   open, reset and confirm the missing-OLED message. Repeat short presses and
   B4 mode changes and verify serial events still work. Reconnect with power off.
9. If the OLED supports address selection, repeat startup at `0x3D` and verify
   detection and rendering. Check that fast presses do not produce duplicate
   logs and that the display remains responsive while several buttons are held.

These are instructions for physical checks. During troubleshooting, serial
output was read from the connected board. No uploads, physical button checks,
or visual confirmation of OLED operation were performed by the coding agent.

## Host state-machine checks

The tests compile the actual button implementation with a minimal GPIO stub.
They cover debounce, repeated holds, simultaneous events, startup arming,
B4 short/long separation (including a release exactly at the threshold),
entry/exit release gates, diagnostic navigation and counter isolation, and
unsigned timer rollover. No board or serial port is required:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -Itests/host -Iinclude src/buttons.cpp tests/host/buttons_test.cpp -o /tmp/cuelink-buttons-test
/tmp/cuelink-buttons-test
```

They do not verify electrical wiring, OLED pixels, real switch bounce, or actual
bus timing.
