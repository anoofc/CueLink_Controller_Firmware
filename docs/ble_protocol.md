# CueLink BLE protocol — version 1 (compatible through Milestone 5)

CueLink is a general-purpose OSC/raw UDP/USB MIDI controller. The two public
read-only version-1 status formats below remain compatible. Milestone 5 adds
separate authenticated configuration characteristics and physical authorization;
see [the configuration protocol](configuration_protocol.md). They never return
Wi-Fi passwords or offer remote cue triggering.

## Service and characteristics

| Item | Fixed UUID | Properties | Value size |
| --- | --- | --- | --- |
| CueLink service | `c0e10001-7a6b-4c8d-9e10-5b2f034a0001` | Custom primary service | — |
| Device information | `c0e10002-7a6b-4c8d-9e10-5b2f034a0001` | READ | 12 bytes |
| Live status | `c0e10003-7a6b-4c8d-9e10-5b2f034a0001` | READ, NOTIFY | 12 bytes |

These UUIDs are also constants in Flutter's `lib/ble/protocol.dart`. The service
UUID is in legacy advertising data; the local name is in the scan response,
so both fit within their separate 31-byte limits. Default ATT MTU 23 gives a
20-byte characteristic payload, sufficient for both values. Neither endpoint
requests an MTU change or chunks values. Standard GAP/GATT services and the
notification CCCD exist as required by BLE; the CCCD only enables notifications.
The two legacy characteristics have no WRITE permission. M5 protected request/response
characteristics use the separate configuration transport.

### Device information

| Byte offset | Length | Field / encoding |
| --- | --- | --- |
| 0 | 1 | Protocol version, unsigned byte: `1` |
| 1–6 | 6 | Stable identity: factory-derived station MAC, in canonical displayed byte order |
| 7 | 1 | Firmware major, unsigned byte (`0`) |
| 8 | 1 | Firmware minor, unsigned byte (`5`) |
| 9 | 1 | Firmware patch, unsigned byte (`0`); together displayed as `0.5.0` |
| 10–11 | 2 | Capability bitmask, unsigned **little-endian** uint16 |

Capability bits: bit 0 Wi-Fi station, bit 1 OSC-over-UDP, bit 2 raw UDP, bit 3
read-only BLE live status, bit 4 USB MIDI, bit 5 authenticated configuration, bit 6
network staging/scanning. Current value `0x007f`. Remaining bits are reserved;
a version-1 reader may preserve/ignore unknown capability bits. These flags
indicate firmware features, not services available for invocation over BLE.

The full identity is displayed as 12 uppercase hexadecimal digits. The advertised
name is `CueLink-XXXX`, using the last two identity bytes (identity indices 4–5, characteristic offsets 5–6),
for example identity `021122334455` gives `CueLink-4455`. Names can collide; the
full identifier is authoritative. The ID is stable across reboots and firmware
updates. iOS's discovered peripheral UUID is a transport identifier, not this ID.

### Live status

| Byte offset | Length | Field / encoding |
| --- | --- | --- |
| 0 | 1 | Protocol version, unsigned byte: `1` |
| 1 | 1 | Wi-Fi state: `0` Not configured, `1` Connecting, `2` Connected/IP-ready, `3` Reconnecting |
| 2–5 | 4 | IPv4 octets in network/display order (e.g. `192,0,2,1`); all zero unless Wi-Fi state is 2 |
| 6 | 1 | Signed two's-complement int8 Wi-Fi RSSI in dBm; `0x80` (-128) means unavailable |
| 7–10 | 4 | Uptime in whole seconds, unsigned **little-endian** uint32 |
| 11 | 1 | Controller mode: `0` Run, `1` Diagnostics |

Uptime accumulates unsigned `millis()` deltas across its rollover, and the wire
seconds field wraps modulo 2^32 (about 136 years). RSSI is clamped to -127..127;
disconnected states always encode the unavailable sentinel and zero addresses.
Live status contains **Wi-Fi RSSI**, not BLE RSSI. BLE scan RSSI is provided by
iOS at discovery and is labeled separately in the app.

Both decoders require exactly 12 bytes for version 1. Unsupported versions,
unknown state/mode values, invalid device identity, missing connected addressing,
and stale addresses/RSSI in disconnected states are explicit errors. A malformed
or unsupported peer is disconnected, rather than interpreted as current data.
Shared golden vectors are in `ble_protocol_vectors.json` and are checked by the
C++ encoder and Dart decoder tests.

## Availability and lifecycle

- At boot, BLE services initialize but discovery is **Off**. No advertising or
  app connection is active. The initialized BLE host is retained while off;
  Wi-Fi is never stopped/deinitialized by BLE lifecycle code.
- Hold B4 for five seconds to enter diagnostics. Use B1/B2 to reach page 13,
  **App connection**, then press B3 to enable a 120-second discovery window.
  A new debounced B3 press explicitly renews that window; holding B3 does not.
- B3 on this page generates a distinct diagnostic event, never a run short event
  or a UDP/OSC command. Leaving the page does not cancel discovery or a session.
- At most one peripheral connection is supported, enforced by
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`. Advertising stops on connection.
- A connected session remains open after the window expires. Disconnection
  resumes advertising only within the remaining original/explicitly renewed
  window. Outside the window it stays off until another B3 request.
- The main loop owns window expiry, advertising changes, status sampling, Serial,
  and OLED. Callbacks update a spinlock-protected connection/subscription mailbox.
- Status refreshes at about 1 Hz and on observed BLE link changes. Notifications
  are best effort and capped at one per second while connected and subscribed;
  no sample backlog or retry queue exists. READ returns the latest sample.
- Advertising failure is shown as Off/retry; another B3 request retries. Normal
  diagnostic navigation and B4 exit remain available in every BLE state.
- Public status remains non-secret/read-only. M5 configuration requires authenticated
  SC bonding/encryption plus physical session authorization; see the configuration protocol.

## iOS app

The app performs a user-initiated service-filtered 10-second scan, deduplicated
by the iOS peripheral UUID. Connection establishment has an 8-second library
timeout and a 12-second overall setup deadline. Information and initial status
are read before attaching one notification subscription. Individual reads time
out after five seconds. No received status for five seconds is labeled stale.

Bluetooth off, permission denied, unsupported hardware/protocol, empty scan,
scan failure, read timeout, and connection failure have explicit UI messages.
The app keeps last-known data marked stale after disconnect. Reconnection is
manual; it never automatically scans/reconnects indefinitely. Backgrounding
stops scans and disconnects, cancelling subscriptions. Returning to the foreground
does not reconnect; the user taps Reconnect. This policy needs no Bluetooth
background mode and prevents duplicated subscriptions after resume.

Only `NSBluetoothAlwaysUsageDescription` is added (existing iOS minimum is 15).
No location or local-network permission is requested. No Android target is added.

## Dependencies and licenses

- Firmware: **NimBLE-Arduino 2.3.9**, pinned in `platformio.ini`, Apache-2.0.
  Verified to compile with the installed Espressif32 6.12.0 / Arduino-ESP32 2.0.17.
  [Pinned upstream manifest](https://github.com/h2zero/NimBLE-Arduino/blob/2.3.9/library.properties),
  [license](https://github.com/h2zero/NimBLE-Arduino/blob/2.3.9/LICENSE).
  Its bundled NimBLE components retain their upstream notices. The single-connection
  build define affects BLE capacity only; board and upload defaults remain; M4B composite USB flags are documented in `triggers_and_usb_midi.md`.
- App: **flutter_reactive_ble 5.6.0**, pinned in `pubspec.yaml`, BSD-3-Clause
  (Signify's BSD license). `reactive_ble_mobile` and platform-interface dependencies
  are locked in `pubspec.lock`. The installed Flutter 3.47.7 / Dart 3.13.5 and iOS
  15 project are compatible; its SwiftPM integration is used by the existing project.
  [Published API/setup](https://pub.dev/packages/flutter_reactive_ble/versions/5.6.0),
  [license](https://github.com/PhilipsHue/flutter_reactive_ble/blob/master/LICENSE).
  SwiftProtobuf is a native transitive dependency (BSD-3-Clause); Xcode's
  `Package.resolved` records its resolved version. Redistribution must retain the
  respective dependencies' license notices.

Tests also explicitly declare **fake_async 1.3.3**, Apache-2.0 per its packaged
license, as a development-only dependency already resolved by `flutter_test`.
It provides deterministic timers for cancellation/reconnect tests and adds no
production dependency. Rapid reconnect waits for the previous native disconnect
before creating another session.

## Verification and physical iPhone test

Local checks (no firmware upload or app installation):

```sh
# From firmware/
~/.platformio/penv/bin/pio run -e seeed_xiao_esp32s3
sh tests/ble/run.sh
sh tests/network/run.sh
sh tests/commands/run.sh
c++ -std=c++11 -Wall -Wextra -Werror -Itests/host -Iinclude src/buttons.cpp tests/host/buttons_test.cpp -o /tmp/cuelink-buttons-test
/tmp/cuelink-buttons-test

# From flutter/cuelink_config/
flutter analyze
flutter test
flutter build ios --no-codesign
```

The app tests use test-only fakes; production UI has no simulated devices/data.
They cover decoding/malformed data, discovery deduplication/timeout, permission
states, connection/read/notification lifecycle, unsupported protocol, cancellation,
and large-text UI. Firmware host checks do not verify actual BLE event ordering,
RF coexistence, radio timing, or display pixels. Native memory use reported by
the firmware build is static usage; BLE also allocates runtime heap.

Physical checks to perform yourself:

1. Manually upload the firmware. Verify the original buttons, Wi-Fi DHCP/recovery,
   OSC/raw UDP, OLED, and all existing diagnostic pages still work. CueLink remains
   general-purpose; Resolume is only one possible OSC receiver.
2. On App connection, confirm Off before B3 and Discoverable/name/countdown after
   a B3 press. Scan from a BLE inspector or the app; verify that the CueLink service
   and name appear. Confirm holding B3 does not repeat/renew or send a command.
3. Open `flutter/cuelink_config/ios/Runner.xcworkspace` in Xcode. Select your physical
   iPhone and use the existing signing/team/bundle settings to build/run manually.
   The unsigned build artifact alone cannot be installed on an iPhone. No signing
   settings were changed to bypass validation.
4. Allow Bluetooth, tap Scan controllers, and connect within the discovery window.
   Verify full identity, firmware, actual Wi-Fi state/IP/RSSI, uptime, and mode.
   Check that BLE scan RSSI is clearly separate from Wi-Fi RSSI. Watch notifications
   update about once per second, including a mode change and Wi-Fi loss/recovery.
5. Keep a session connected past 120 seconds. It must continue. Disconnect after
   expiry: no advertising until B3 is pressed again. Disconnect before expiry:
   advertising may resume for the remaining time only. Try a second app/device:
   only one app may connect at a time.
6. Navigate/exit diagnostics while connected and send normal run button commands;
   ensure input, OLED, Wi-Fi, and UDP remain responsive. BLE must never trigger a cue.
7. Test Stop, empty timeout, retry, Bluetooth off/on, denied permission (and later
   enable it in Settings), out-of-range/power-off disconnect, manual reconnect,
   and unsupported protocol using a test peripheral. Background/resume must mark
   data stale and require manual reconnect, with no duplicate subscriptions.
8. Check text scaling and safe areas on the iPhone. Verify that no configuration,
   credentials, command data, or write/trigger controls are present.

No real-device BLE testing has been performed by the coding agent.
