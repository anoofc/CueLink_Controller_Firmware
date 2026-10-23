# CueLink Controller — milestone 3

Version: `M3 / 0.3.0`. Local buttons, SSD1306 display, diagnostics, Wi-Fi
station/DHCP, and binary OSC-over-UDP or exact raw UDP output. Board, USB,
monitor, and upload settings are unchanged. BLE and persistent configuration
are outside this milestone. No internet access is needed or checked.

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

## Development command configuration

Edit **`include/commands.dev.h`**. Set `DevelopmentCommands::destinationIpv4`
to the receiving computer's actual LAN IPv4 address. It defaults to the empty
string, disabling transmission; no server IP is assumed. Do not enter the
controller's address, `127.0.0.1`, a hostname, or a URL. Ports and actions are
editable in the same file. Each button has its own destination entry; replace
its reference to `destinationIpv4` with another numeric IP if needed.
Rebuild and manually upload after editing. These are development examples,
not permanent presets or saved settings.

| Button | Protocol | OSC address / raw bytes | Arguments | Remote port |
| --- | --- | --- | --- | --- |
| B1 | OSC over UDP | `/composition/columns/1/connect` | int32 `1` | 7000 |
| B2 | OSC over UDP | `/composition/columns/2/connect` | int32 `1` | 7000 |
| B3 | Raw UDP | `CUELINK_B3` | — | 9000 |
| B4 | Raw UDP | `CUELINK_B4` | — | 9000 |

The raw examples are exactly 10 bytes each: no newline, NUL terminator, or
padding. `rawLength` controls the exact number of bytes, so arbitrary binary
arrays (including embedded zero bytes) also work. OSC arguments use
`Command::Argument::int32(...)`, `::float32(...)`, or `::string("...")` in an
argument array; update `argumentCount` when changing that array. Strings are
ASCII. OSC addresses must start with `/`, name a path, and contain no whitespace
or control bytes. The encoder emits address, comma-prefixed type tags, and
arguments with NUL termination, four-byte padding, and big-endian numeric data,
following the [OSC 1.0 specification](https://opensoundcontrol.stanford.edu/spec-1_0.html).
OSC is never sent as a plain-text command. Bundles and additional OSC types are
outside this milestone.

## Command transport and results

`command.cpp` handles validation/encoding, `commands.cpp` handles run events and
results, and `udp_transport.cpp` owns one reusable nonblocking socket using the
installed framework's lwIP socket API. There are no new library dependencies.
One button maps to one action and one destination; those are separate model
objects so future multi-action configuration need not change the encoder.

The source socket binds port zero, asking the OS to choose a local port.
Before sending, its selected port is checked against **all four remote ports**.
A collision causes a fresh bind, bounded to eight setup attempts. No packet is
sent during setup. The source is never deliberately bound to 7000 or 9000.
The socket is closed on disconnect, Wi-Fi loss-count changes, IP changes, or a
local failure, and reopened only for a new valid trigger. Wi-Fi reconnection
alone sends nothing. Device IPv4 addresses shown on network pages refer to the
controller, while the new mapping pages show the receiver.

Only the existing run-mode short-event mask can trigger output: B1–B3 once on
stable press and B4 once on short release. Five-second B4 holds, diagnostic
navigation, startup, mode changes, and held buttons send nothing. Simultaneous
short events each get one attempt, in button order. Only the most recent result
is shown in the brief footer; every attempt is logged and counted.

There are no automatic command retries or offline command queues. Offline
presses are discarded, including a press in the same loop that first observes
newly acquired Wi-Fi. A fixed 1024-byte working buffer is encoded per attempt
and immediately invalidated afterward. Packets over 1024 bytes, empty raw
payloads, unsupported types, malformed OSC settings, invalid ports (outside
1..65535), and invalid IPv4 destinations are rejected. Numeric unicast IPv4 is
supported; unspecified, loopback, multicast, and limited broadcast addresses
are rejected. There is no hostname lookup or server/internet probe.

Results distinguish:

- **Local submission**: the local UDP stack accepted the whole datagram.
- **Not sent: offline**: Wi-Fi/IP was unavailable when processing the event.
- **Set/fix destination**: the destination was unset or invalid.
- **Invalid/oversized cmd**: encoding/configuration failed or exceeded 1024 bytes.
- **Local UDP failure**: socket setup or the single `sendto` call failed.

When multiple problems exist, offline takes precedence, then destination, then
encoding, then transport. Serial emits exactly one `[SEND]` record per attempt
with button, protocol, validated destination (or unset/invalid), remote port,
encoded byte count, source port, and local result. An invalid encoding has a
zero byte count; source zero means socket setup was not completed. Existing
`[SHORT]` records continue to describe button events, not extra sends. Wi-Fi
passwords and command argument contents are never logged.

The footer uses `B1 OSC sent`, `B3 UDP sent`, `Not sent: offline`, or
`Set destination`, as appropriate, then returns to the normal hint after about
1.5 seconds. The Wi-Fi state and device IP remain visible throughout. **Sent
means local submission only: UDP does not acknowledge receipt or execution.**
Stopping Resolume or pointing at an unused port can still produce a local
submission. No result declares the receiver online or a cue executed.

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
Reconnecting, plus the current IPv4 address when connected. A command-result footer
appears for 1.5 seconds without replacing network information. The B4 progress
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
7. B1 protocol and destination IP/port, with configuration status.
8. B2 protocol and destination IP/port, with configuration status.
9. B3 protocol and destination IP/port, with configuration status.
10. B4 protocol and destination IP/port, with configuration status.
11. Last send result, button/protocol, encoded length, and source port used.
12. Successful local submissions, offline drops, local failures, and configuration drops.

The last-send page is historical, including its source port. It is not an
indication that a transport is currently open. Counts reset on reboot.

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

## Test raw UDP on a computer

1. Connect the computer and controller to a mutually reachable LAN (2.4 GHz
   Wi-Fi for the controller). Find the computer's LAN IPv4 address and enter it
   as `destinationIpv4` in `include/commands.dev.h`. Leave the B3/B4 remote
   ports at 9000. Rebuild and manually upload using the commands above.
2. On that receiving computer, from `firmware/`, run:

   ```sh
   python3 tools/udp_receiver.py --port 9000
   ```

   The script uses only Python's standard library and listens on all local
   IPv4 interfaces by default. Allow inbound UDP 9000 through the computer's
   firewall and check that AP/client isolation is disabled if packets do not
   arrive. Use `--bind COMPUTER_LAN_IP` to choose a specific local interface.
   Stop with Ctrl+C. For a bounded run use
   `--port 9000 --count 2 --timeout 60`.
3. Wait for the controller to show Connected and a device IP. Press B3, then
   briefly press/release B4. Expect sender IP/source port, byte count, hex, and
   readable text for each datagram. B3 must be exactly 10 bytes:
   `43 55 45 4c 49 4e 4b 5f 42 33`; B4 ends in `34` instead of `33`.
   Neither packet may have a final `00` or `0a`. The source port should remain
   the same across these sends and differ from both 7000 and 9000.
4. Hold B3: only its initial press sends. Hold B4 for five seconds: no B4 packet
   should arrive. Navigate diagnostics and exit: no packets. Check pages 7–12
   for mappings, last local result, byte count, and totals.
5. Turn off the AP, wait for Reconnecting, and press buttons: expect offline
   drops. Restore the AP: no earlier press may appear. Only a fresh short event
   should send, using a reinitialized socket (its numerical source port may be
   reused by the OS). With destination unset, connected presses must report
   Set destination and submit no packets.

## Test OSC with the actual Resolume installation

The B1/B2 OSC mappings are examples and **must be checked against the receiving
Resolume version and composition during physical testing**.

1. Set `destinationIpv4` to the Resolume computer's actual LAN address. In
   Resolume's Preferences, enable OSC input and select incoming UDP port 7000
   to match B1/B2, or edit their remote ports to match your installation.
2. Use **Shortcuts > Edit OSC** and select the desired column controls. Inspect
   the displayed addresses, type tags, and accepted values. Check both example
   paths and int32 value `1`; adjust the development mappings if your target
   requires different settings. Exit shortcut editing before the functional
   test. See the official [Resolume OSC guide](https://resolume.com/support/en/osc).
3. Rebuild and manually upload any configuration changes. Allow inbound UDP
   7000 on the receiving computer and make sure the LAN permits peer traffic.
   Enable Resolume's incoming OSC monitor using the monitor icon in its OSC
   preferences; see [Resolume preferences](https://resolume.com/support/preferences).
4. Press B1 and B2 separately. Confirm each expected address and int32 value in
   the incoming monitor, then visually confirm the intended column action.
   The example messages are 40-byte binary OSC packets with `,i` type tags and
   the final four bytes `00 00 00 01`. CueLink's sent feedback and local totals
   alone cannot establish that Resolume received or acted on them.
5. Check held buttons, B4 long holds, diagnostic navigation, and Wi-Fi loss/
   recovery again: no repeats or stale commands. To inspect raw OSC bytes
   independently, close/disable Resolume's OSC listener first and run the
   Python receiver on port 7000. Stop that receiver before re-enabling Resolume
   so they do not compete for the same incoming port.

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
5. Navigate all twelve pages with B1/B2, including wrapping in both directions.
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

No automatic upload or physical hardware testing was performed for milestone 3.
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

Command checks compile the actual encoder and transport without Arduino, then
compile the real dispatcher and button state machine with mocked radio/socket
boundaries. No local credentials are read. Run:

```sh
sh tests/commands/run.sh
```

This covers golden OSC int32/float32 network byte order and type tags, string
padding and alignment, mixed arguments, the 1024-byte boundary, invalid settings,
exact development raw payload lengths, native localhost datagrams and reusable
source ports, event gating, offline drops/no replay, transport reset, local
failure/no retries, and feedback timer rollover. It also runs a bounded
localhost-only check of the Python receiver (`--count 1 --timeout 2`). Native
host socket results do not verify ESP32 lwIP timing, actual radio delivery,
OLED pixels, or the receiving Resolume installation.
