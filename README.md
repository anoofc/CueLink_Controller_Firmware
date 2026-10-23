# CueLink Controller — milestone 2

Version: `M2 / 0.2.0`. Local buttons, SSD1306 display, diagnostics, and Wi-Fi
station connectivity with DHCP. Board, USB, monitor, and upload settings are
unchanged. BLE, OSC, UDP commands, and persistent configuration are outside
this milestone. No internet access is needed or checked.

## Local Wi-Fi configuration

Enter local credentials in `include/secrets.h`, using the macro names shown in
`include/secrets.example.h`: `CUELINK_WIFI_SSID` and `CUELINK_WIFI_PASSWORD`.
If the local file does not exist, copy the example to that path without
replacing an existing file. The local file is excluded from Git. Do not put
credentials in the example or commit them.

A missing header or empty SSID builds normally and leaves the radio inactive;
serial and the run screen show Wi-Fi not configured. An empty password supports
an open network. Rebuild and manually upload after changing the local file.
Credentials are compiled into the firmware; this is development configuration,
not provisioning or a configuration store.

## Network behavior

`Network` in `include/network.h` and `src/network.cpp` owns Wi-Fi separately from
button handling and rendering. The station hostname is `cuelink-controller`.
The initial attempt starts without a connection wait loop. Only station mode is
used; association alone is not ready: a nonzero IPv4 address and the framework's
IP-ready status are required.

Each attempt has a 15-second association/DHCP budget. A failed attempt is
cancelled before waiting 2, 4, 8, 16, then at most 30 seconds between attempts.
Successful IP acquisition resets backoff. Arduino-ESP32 2.0.17 can make one
internal first-connect retry despite disabling automatic reconnect; that retry
stays within the same 15-second budget. There is never a second application
attempt while one is active. Timing uses unsigned elapsed milliseconds, including
across rollover. Framework initialization and driver/I2C calls are synchronous,
but there are no connection waits, sleeps, or synchronous network scans in the
application loop.

Credential persistence is disabled before enabling Wi-Fi, so reconnects use RAM
storage. Disconnect/IP-loss callbacks only update a mailbox under a spinlock.
The main loop processes it, controls retries, logs state changes and disconnect
reason codes/names, and clears addressing on loss. A loss counts once per
previously observed IP-ready connection, including IP-lease loss; initial failures
and cancellation events do not increase the counter. Counters reset on reboot.
The mailbox combines disconnects between loop iterations and logs the latest
reason plus the number of events. Signal/address data refreshes once per second
while connected. Serial reports assigned IPv4, subnet, gateway, primary DNS,
RSSI, and channel on connection and IP changes, plus a summary on USB attachment.

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

## Buttons and display

The verified button state machine is unchanged except for its diagnostic page
count. Buttons debounce for 30 ms. B1–B3 generate a run event on stable press;
B4 generates one on stable release before five seconds. A five-second B4 hold
changes mode once without a short event. Startup-held buttons must be released
to arm. After a mode change all buttons must be stably released before input
resumes. Diagnostic navigation never increments run counters.

The run screen retains the CueLink header and four physical button indicators.
Dedicated network lines show Not configured, Connecting, Connected, or
Reconnecting, plus the current IPv4 address when connected. A short-event footer
appears for 1.2 seconds without replacing network information. The B4 progress
strip only appears during an active hold. The original OLED address probing,
initialization fix, and one-tile-row-per-loop transfers remain intact. Display
updates start at most 25 times per second; a missing OLED leaves other work active.

Hold B4 for five seconds to enter or exit diagnostics. B1 selects the previous
page and B2 the next, with wrapping; B3 does nothing. Pages are:

1. Live button states.
2. Run short-press counters.
3. Uptime, firmware version, and free heap.
4. Network state, safely truncated SSID, and current IPv4 address.
5. Signal: RSSI in dBm, channel, and connection-loss count.
6. Addressing: subnet mask, gateway, and primary DNS.

Disconnected addresses and channel show `--`; RSSI shows unavailable. SSID text
is limited to 19 display characters with an ellipsis on truncation; unsupported
or control bytes become `?`. Connected describes Wi-Fi/IP connectivity only.

## Build and manual upload

Run from `firmware/`. If `pio` is not on PATH, use
`~/.platformio/penv/bin/pio` instead.

```sh
pio run -e seeed_xiao_esp32s3
```

Upload only when ready to test hardware. List ports, substitute the actual port
for `PORT`, then open the serial monitor:

```sh
pio device list
pio run -e seeed_xiao_esp32s3 -t upload --upload-port PORT
pio device monitor -p PORT -b 115200
```

Close other serial monitors before uploading. If automatic bootloader entry
fails, hold BOOT, press and release RESET, then release BOOT and list ports again.
Press RESET if the application does not start. Monitor DTR/RTS remain zero; a
version/OLED/mode/network summary prints when USB serial attaches. Exit the
monitor with Ctrl+C before another upload.

## Manual verification on hardware

1. With no local header (back up your own file first), build and manually upload.
   Verify Wi-Fi not configured, no connection attempts, responsive buttons,
   diagnostics, and serial. Restore the local file afterward.
2. Configure a 2.4 GHz DHCP network, build, and manually upload. Verify Connecting
   followed by Connected only after an IPv4 address is assigned. Check the
   hostname in the router's DHCP lease list when supported. Test on a network
   without internet access.
3. Press B1–B3 while connecting and during backoff: one event per debounced press,
   with no repeated event while held. Check all four indicators and brief event
   feedback with network state/IP always visible. A short B4 press fires on release.
4. Hold B4 for five seconds: progress should appear only during the hold, followed
   by one mode change and no short event. Keep holding another five seconds;
   no additional mode change should occur. Release all buttons to unlock.
5. Navigate all six pages with B1/B2, including wrapping in both directions.
   Confirm the three original pages, truncated SSID, current IP, RSSI, channel,
   loss count, subnet, gateway, and DNS. Navigation must not increment counters.
   Check a long SSID and readability within 128×64. Test B4 exit from each page.
6. Turn off the access point while connected. Verify Reconnecting, cleared
   addresses, unavailable RSSI/channel, one added loss, and a serial disconnect
   reason. Keep testing buttons, page navigation, and B4 while the AP is absent.
   Confirm timeout/retry logs: at most 15 seconds per attempt followed by
   2/4/8/16/30-second waits. Restore the AP; verify automatic DHCP recovery and
   correct new addressing. Repeat to check one loss per outage and backoff reset.
7. Boot with the AP absent, or temporarily use invalid local credentials. Verify
   recovery without a reset when valid connectivity returns, and no connection
   losses counted before the first successful IP acquisition. If possible, test
   association with DHCP unavailable: it must time out without showing Connected.
8. Recheck startup-held buttons, simultaneous presses, debounce, B4 short/long
   separation, and the all-buttons-release gate on both entry and exit. Verify a
   missing OLED still permits serial events and Wi-Fi recovery. Reconnect wiring
   only with power removed; repeat at `0x3D` if address selection is available.

No automatic upload or physical hardware testing was performed for milestone 2.
Actual radio recovery, USB behavior, electrical wiring, OLED readability, and
loop latency require these hardware checks. The ESP32-S3 uses 2.4 GHz Wi-Fi;
enterprise/captive-portal provisioning and hostname-based discovery are not
implemented. Hostname advertisement does not start an mDNS responder.

## Host checks

The existing button tests use the production button implementation and cover
bounce, startup arming, short/long holds, release gates, navigation, counter
isolation, simultaneous inputs, and timer rollover:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -Itests/host -Iinclude src/buttons.cpp tests/host/buttons_test.cpp -o /tmp/cuelink-buttons-test
/tmp/cuelink-buttons-test
```

Network tests compile the production network implementation in an isolated
temporary directory against small driver stubs. They never read local credentials
and cover missing configuration, DHCP readiness, bounded attempts, capped backoff,
rollover, recovery, stale-address clearing, callback handoff, loss counting,
backoff reset, and RAM-only credential setup:

```sh
sh tests/network/run.sh
```

These checks simulate driver behavior; they do not test actual RF, DHCP,
framework event timing, cross-task synchronization, or OLED pixels.
