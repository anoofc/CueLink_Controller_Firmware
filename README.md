# CueLink Controller — Milestone 5

Version `M5 / 0.5.0`: a general-purpose four-button controller for binary OSC,
exact raw UDP and class-compliant USB MIDI 1.0. Each button supports zero to four
independent enabled triggers, with up to eight destinations per network trigger
and optional master network routing. Read-only BLE status remains compatible.
The graphite/cyan Flutter design is preserved. Authenticated BLE now supports
button/routing editors, controller Wi-Fi scans and staged DHCP/static network
changes, backed by transactional persistent configuration. No remote cue trigger
or internet-reachability check is implemented.

See **[MTU-aware configuration transfer, timings, rebuild/install and acceptance steps](docs/configuration_performance.md)** for the v2 fast path and its legacy fallback. The ≤5-second target remains awaiting physical-device verification.

See **[Milestone 5 schema, authentication, storage and physical acceptance steps](docs/configuration_protocol.md)**.
On App connection press B3 to authorize, connect the app, open **Buttons, routing
& network**, then **Load settings / Refresh**. Enter the OLED passkey in the iOS system
pairing dialog. Configuration access requires authenticated encryption and a fresh
physical authorization after each disconnect. Saved settings take precedence over
source defaults; `commands.dev.h` and local secrets seed revision zero only when
no valid saved record exists. Changes require explicit Save button, Save routing
or Apply network, followed by device acknowledgment and readback.

Edit **`include/commands.dev.h`** for trigger settings. The user's tested endpoint
**10.128.151.148**, OSC port 7000 and raw UDP port 9000 are preserved. Confirm that
address is still your receiver. The example adds B1 UDP and a MIDI note pulse,
and B3 MIDI CC. All fourth slots remain empty.

See **[Trigger configuration, exact mappings, USB details and macOS testing](docs/triggers_and_usb_midi.md)**
for the full settings schema, bounded scheduler behavior, result semantics,
manual upload and BOOT/RESET recovery. Native composite MIDI + CDC requires the
documented USB mode change; no additional dependency or core upgrade is needed.

## Local Wi-Fi configuration

Enter local credentials in `include/secrets.h`, using the macro names shown in
`include/secrets.example.h`: `CUELINK_WIFI_SSID` and `CUELINK_WIFI_PASSWORD`.
If the local file does not exist, copy the example to that path without
replacing an existing file. The local file is excluded from Git. Do not put
credentials in the example or commit them.

A missing header or empty SSID builds normally and leaves the radio inactive;
serial and the run screen show Wi-Fi not configured. An empty password supports
an open network. On an unconfigured device, rebuild and manually upload after changing the local file.
Saved network settings take precedence; change those through the authenticated app.
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

## Buttons, display and BLE

Buttons retain 30 ms debounce. B1–B3 trigger once on stable press; B4 triggers
once on short release. Holding B4 five seconds enters/exits diagnostics without
a cue. Startup-held buttons and mode changes require stable release before arming.
The OLED initialization, four indicators, Wi-Fi information and held-only B4
progress strip are retained. Button labels come from development configuration.
Trigger summary feedback lasts 1.5 seconds and never hides network information.

B1/B2 navigate diagnostics; B4 long-press exits. Original button/counter/system,
network/signal/addressing, last-result and totals pages remain. Four mapping pages
now show all trigger slots. App connection remains page 13, with B3 enabling
120-second BLE discovery. New page 14 shows USB MIDI state and release counters;
page 15 provides trigger/destination details, cycled with B3. Navigation emits
no cues. Successful local submission does not establish receiver execution.

Read-only BLE protocol version 1 and both 12-byte formats are unchanged.
Firmware metadata is 0.5.0 with USB MIDI bit 4 and authenticated configuration/network bits 5/6. See
[BLE protocol and physical iPhone checklist](docs/ble_protocol.md).

## Build and checks

From `firmware/`:

```sh
~/.platformio/penv/bin/pio run -e seeed_xiao_esp32s3
sh tests/config/run.sh
sh tests/commands/run.sh
sh tests/midi/run.sh
sh tests/network/run.sh
sh tests/ble/run.sh
c++ -std=c++11 -Wall -Wextra -Werror -Itests/host -Iinclude src/buttons.cpp tests/host/buttons_test.cpp -o /tmp/cuelink-buttons-test
/tmp/cuelink-buttons-test
```

These check real production encoders, dispatcher, button gates, scheduler and
network lifecycle through host boundary fakes, plus native localhost UDP socket
and bounded Python receiver checks. They cannot establish physical USB, radio,
BLE coexistence, OLED pixels or receiving application behavior. No automatic
upload is performed. Manual upload/recovery and physical testing are documented
in [the M4B guide](docs/triggers_and_usb_midi.md).
