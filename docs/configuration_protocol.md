# Milestone 5 — configuration schema 1 and authenticated BLE transport 1

Firmware is `M5 / 0.5.0`. This document is the contract for firmware and CueLink
Config. The controller remains a general-purpose OSC/raw UDP/USB MIDI tool.
There is no remote cue-trigger command. Existing BLE information/status UUIDs,
version byte and 12-byte formats remain compatible. Their firmware metadata is
now 0.5.0; capability bits 5 (authenticated configuration) and 6 (network staging
and scan) extend the existing bitmask to `0x007f`.

## Authentication and physical authorization

The installed NimBLE-Arduino 2.3.9 supports LE Secure Connections passkey entry.
Firmware selects Display Only, bonding, MITM and SC. Build flags explicitly
compile out legacy pairing (`MYNEWT_VAL_BLE_SM_LEGACY=0`), require security level
4 and SC-only mode. Configuration characteristics require encrypted,
authenticated ATT access. Application checks additionally require a 16-byte
key and physical session authorization. There is no Just Works fallback and
no custom cryptography. CRC32 below is transfer/storage integrity, not authentication.

Hold B4 five seconds, use B1/B2 to select **App connection**, then press B3.
The existing 120-second discovery window opens/renews; B3 also authorizes this
connection or the next connection in the window. Before authentication, the
OLED shows a fresh six-digit passkey with leading zeros. In the app connect
normally, open **Buttons, routing & network**, then **Load / query device**.
Reading the protected response characteristic lets iOS/CoreBluetooth perform
standard pairing. Enter the OLED value in the system pairing dialog. The app
never handles or logs that passkey. Firmware disables NimBLE security logging.
An existing authenticated bond may avoid the dialog, but still needs B3.

Authorization is revoked on every disconnect, independently of the remaining
discovery window and stored bond. Reconnect and press B3 again. A session that
was physically authorized can continue after discovery expires. Wrong passkey,
insufficient encryption/authentication or short keys never enable configuration.
The original public non-secret information/live status remain readable without
configuration authorization. Physical actions remain diagnostic events, not cues.
No bond-erasure operation or authentication bypass is implemented.

`flutter_reactive_ble` remains pinned to 5.6.0. Its iOS CoreBluetooth protected
read/write operations use OS pairing; no explicit pairing API, custom passkey
UI, crypto package or new app dependency is needed. Verify this interaction on
physical iOS hardware; host tests cannot prove a radio/security exchange.

## Configuration document

The exact cross-language example is [configuration_vector.json](configuration_vector.json).
JSON uses UTF-8, with a 12288-byte maximum serialized configuration and a
16384-byte maximum transfer/request/response. The extra space accommodates
receipts around a configuration. JSON nesting is bounded at 12; firmware JSON
document capacity is 32768 bytes. Unknown configuration fields/protocols,
missing required fields, wrong types and unsupported schemas are rejected.
Disabled contents receive the same validation and remain stored in order.

Root fields are `schema: 1`, `revision: uint32`, `buttons`, `routing`, `network`.
Exactly four buttons are stored in physical B1–B4 order. Each button has:

- `label`: 1–12 printable ASCII characters, U+0020 through U+007E. No trimming.
  The app displays the full label. OLED indicators show up to four characters;
  longer labels use the first three plus `.`. Diagnostic button pages show the
  full label. This range is supported by the existing OLED fonts.
- `enabled`: boolean, independent of the slots.
- `slots`: ordered array of zero through four triggers, including disabled ones.
  A fifth slot is rejected, never truncated.

Every slot has `enabled`, `protocol` (`osc`, `udp` or `midi`) and exactly the
matching typed settings object. Protocol fields cannot be mixed.

| Settings | Required fields / limits |
| --- | --- |
| `osc` | `address`: 2–128 printable ASCII characters, begins `/`, no whitespace; `args`: 0–8 arguments; `destinations`: list below |
| OSC argument | `{ "type": "i", "value": int32 }`, `{ "type": "f", "value": finite float32-range number }`, or `{ "type": "s", "value": string }` |
| OSC string | 0–256 supported ASCII bytes; printable characters plus tab/CR/LF, no embedded NUL |
| `udp` | `format`: `text` or `hex`; `payload`: string; `lineEnding`: `none`, `lf` or `crlf`; `destinations` |
| UDP text | Printable ASCII plus tab/CR/LF; append only the explicitly selected line ending |
| UDP hex | Complete contiguous hex byte pairs, either case; `lineEnding` must be `none`; arbitrary bytes allowed |
| `midi` | `type` and user-facing `channel` 1–16, plus fields below; no destinations |
| MIDI `pulse` | `note` 0–127, `velocity` 1–127, `duration` 1–60000 ms; editor default 100 ms |
| MIDI `on` / `off` | `note`, `velocity`, both 0–127 |
| MIDI `cc` | `controller`, `value`, both 0–127 |
| MIDI `program` | `program` 0–127, consistently zero-based |

OSC arguments retain their type tag even when a float is numerically integral.
Both sides normalize float arguments to IEEE float32 for encoding/comparison.
OSC retains the working binary encoder. Encoded OSC and raw UDP are bounded
at 1024 bytes. Raw UDP must contain at least one byte; it has no implicit
terminator/padding. Hex input is at most 2048 characters. Switching text/hex
preserves actual bytes, including existing line endings; unsupported text
conversion is rejected. The representation and explicit line-ending choice
are persisted, not inferred from display text.

Every destination is `{ "ip": "numeric unicast IPv4", "port": 1..65535 }`.
Lists contain 0–8 entries. Empty lists are valid stored settings, but attempts
report missing destination. Loopback, unspecified, multicast and limited
broadcast addresses are rejected. No DNS lookup/internet/server probe occurs.
`routing` contains `master: bool` and `destinations`. ON replaces effective
routing for all network slots, preserving their individual lists. MIDI ignores it.

`network` contains `ssid` (0–32 UTF-8 bytes without control characters), `passwordConfigured`
(boolean indicator only), `dhcp` (boolean), and numeric IPv4 strings `ip`,
`subnet`, `gateway`, `dns1`, `dns2`. All fields are present even under DHCP;
`0.0.0.0` is allowed for inactive address fields/DNS. Static masks must be
contiguous, nonzero, leave at least two usable hosts (/1 through /30), and use
a usable unicast IP/gateway in the same subnet, with gateway different from IP.
No internet-reachability test is performed. Apply requires a nonempty SSID.
SSID discovery skips invalid UTF-8/control-byte names; manual entry
uses the same rules. WPA-Enterprise, IPv6 and hostname destinations are outside
this schema.

Passwords are absent from configuration documents and read receipts. Network
requests carry a separate `credential` object, only over the protected session:

- `{ "mode": "keep" }`: preserve the existing secret, including when SSID changes.
- `{ "mode": "replace", "value": "..." }`: 8–63 UTF-8 bytes without NUL; never trim.
- `{ "mode": "open" }`: explicitly clear the secret for an open network.

Keep/open reject a supplied `value`; an empty replacement is invalid. The
controller computes `passwordConfigured`; the app cannot set that indicator to
erase a secret. The app has no disk-backed drafts, password cache, or credential
logging. Password input is masked/revealable, cleared on disconnect, disposal,
credential-mode change and after Apply. Typed public drafts are retained only
in the current editor and require explicit Save/Discard/reconciliation.

## BLE characteristics and frames

Same service: `c0e10001-7a6b-4c8d-9e10-5b2f034a0001`.

| Characteristic | UUID | Properties |
| --- | --- | --- |
| Request | `c0e10004-7a6b-4c8d-9e10-5b2f034a0001` | WRITE with response, encryption + authentication required |
| Response | `c0e10005-7a6b-4c8d-9e10-5b2f034a0001` | READ, encryption + authentication required |

Every frame starts with: byte 0 transport version `1`; byte 1 kind; bytes 2–5
nonzero request/transaction ID (uint32 LE); bytes 6–7 sequence (uint16 LE).
The ATT value length defines the frame-body length. No application read exceeds
20 bytes. The app intentionally writes at most 20 bytes, compatible with ATT
MTU 23 and any larger negotiated MTU. Firmware accepts frames up to 128 bytes
(the sender must additionally obey negotiated MTU minus 3). Response chunks
are always up to 12 bytes. There are no long-GATT reads or mandatory MTU requests.

| Kind | Body and behavior |
| --- | --- |
| `0x01` BEGIN | Sequence 0; total UTF-8 request length uint32 LE then CRC32 uint32 LE; exactly 16 bytes total |
| `0x02` DATA | 1–120 body bytes within ATT limits; sequences 0,1,2…; may not exceed declared total |
| `0x03` END | No body; sequence equals number of DATA frames; complete length and CRC required |
| `0x04` CANCEL | No application commit by an incomplete transfer; discards transfer buffers |
| `0x05` FETCH | No body; sequence selects response byte offset `sequence * 12` |
| `0x80` ACK | Main-loop acceptance; sequence is next expected DATA sequence (or CANCEL sequence) |
| `0x81` RESPONSE | Response total length and CRC32, each uint32 LE; 16-byte manifest |
| `0x82` CHUNK | Requested response slice, up to 12 bytes; same request ID/sequence |
| `0x83` REJECTED | Malformed, timed-out, cancelled, mismatched-ID, out-of-order or incomplete transfer |
| `0x84` UNAUTHORIZED | Missing physical authorization or security; no configuration data |
| `0x85` PROCESSING | Complete input accepted for main-loop parsing; not a save acknowledgment |

CRC is standard reflected CRC-32/ISO-HDLC (polynomial `0xedb88320`, initial/final
XOR `0xffffffff`) over the exact complete JSON bytes. Request ID binds manifest,
chunks and response. Each direction verifies its complete length/integrity.
The app waits for each application ACK before advancing; it polls READ for the
matching ID/kind/sequence without retransmitting cues or commit operations.
Receivers reject duplicate/out-of-order DATA, overflow and unsupported frames.
FETCH is explicitly indexed so response slices may be read again safely.

One bounded 128-byte callback mailbox is allowed. A second outstanding write
rejects the transfer rather than queueing it. BLE callbacks only check security,
copy/clear bounded frames or return a bounded cached response; they do not parse
configuration, save flash, scan Wi-Fi or render. Main loop owns all those actions.
Transfers expire after 15 seconds without an accepted frame, including response
fetches; elapsed timing is rollover-safe. Disconnect cancels the transfer and
revokes authorization. App operations have bounded per-step/overall timeouts
and a Cancel action. Cancelling/disconnecting after a network operation was
accepted does not undo it: query the controller before deciding what to do next.

## Requests, revisions and receipts

Every JSON request includes `schema: 1` and `op`. Supported operations:

| Operation | Additional fields | Scope |
| --- | --- | --- |
| `read` | none | Current authoritative configuration, revision and receipt, never password |
| `query` | none | Latest staged network/scan result or current receipt and pending state |
| `button` | `revision`, `button` (0–3), `value` (one complete button) | Only that button |
| `routing` | `revision`, `value` (routing object) | Only routing |
| `network` | `revision`, `value` (network object), `credential` | Stage/test then persist network only |
| `scan` | none | User-initiated asynchronous controller Wi-Fi scan; at most 20 supported SSIDs |

No command invokes a cue. Configuration mutations require the exact current
revision. A conflict does not merge/overwrite anything; reload and explicitly
reconcile the draft. Operations are serialized; while network/scan is pending,
mutations/scans are rejected busy but reads/queries remain available.

Successful mutation receipts contain `ok: true`, `revision`, `lastRequest`,
and `pending`. Only `read` adds the authoritative
`configuration` document; progress/commit receipts stay compact at default MTU. Other fields
include `error`, `networkRequest`, `networkError`. A network/scan start initially
returns `{ "ok": true, "pending": true }`; poll `query` until completion. Scan
completion returns `ok`, `error`, and `networks` entries (`ssid`, `rssi`, `open`).
Rejected operations return `ok: false` and a non-secret error. A saved revision
is sent only after verified storage success. Unchanged values do not write or
increment revision. Revision exhaustion is an explicit error, not rollover.

The latest committed request ID and fingerprint are stored with the record.
The fingerprint is internal and never returned in configuration reads/receipts,
so a credential-update checksum is not exposed as a password verifier.
An identical duplicate returns the existing receipt without another commit;
reuse with different bytes is rejected. Older mutation retries retain their
old revision and conflict. After any uncertain outcome the app blocks new saves
until explicit **Load / query device**; it never automatically re-submits a draft.
After acknowledgment, the app performs another authoritative `read`, checks the
saved revision and scoped settings, then displays **Saved and verified**.
BLE write success alone is never displayed as Saved. Historical network failures
do not prevent loading authoritative settings to recover.

## Storage, migration and runtime application

`Config::Journal` alternates `record0` / `record1` blobs in Preferences namespace
`cuelink5` in the dedicated `cuelinkcfg` NVS partition. Record layout:

| Offset | Encoding |
| --- | --- |
| 0 | Four magic bytes `CLK5` |
| 4 | Schema uint32 LE |
| 8 | Revision uint32 LE |
| 12 | Last committed request ID uint32 LE |
| 16 | Request fingerprint uint32 LE |
| 20 | Configuration byte length uint32 LE |
| 24 | Password byte length uint32 LE |
| 28 | Configuration bytes, then password bytes |
| End − 4 | CRC32 over all preceding record bytes |

The inactive record is written with one NVS blob operation, read back and checked
before replacing the in-memory snapshot. NVS commits its blob index transactionally;
the untouched previous record remains available if the new write is interrupted.
At boot both records undergo bounds, schema, CRC, complete configuration,
revision and password-indicator consistency checks. Highest valid revision wins;
a corrupted/partial newer record falls back to the valid previous one. No namespace
clear, factory reset, bond deletion, unrelated NVS erase or identity change occurs.

Without a valid saved record, source development mappings and local secrets are
loaded into RAM at revision zero. Boot does not write defaults. The first explicit
changed save creates a record. Subsequent boots use saved settings, even after
source defaults change. Changing `commands.dev.h` or `secrets.h` does not overwrite
saved settings. All records invalid/missing falls back to source defaults; there
is no automatic repair write. Invalid compiled trigger defaults are deactivated
into silent recovery settings so they cannot become a partially active snapshot. No migration from an older persistent schema exists
because previous milestones did not persist configuration.

Configuration snapshots own their typed OSC/UDP/MIDI backing data. A fully
validated replacement is committed and bound between dispatches on the main loop,
so a press uses one coherent snapshot. Reloading the dispatcher retains its MIDI
scheduler: pending Offs keep their original channel/note/deadline. Saving/reading
never calls the button dispatcher. Disabled buttons/slots retain their contents.

Network Apply builds a candidate without overwriting the active/persisted network.
It associates/acquires IP for at most 30 seconds, with normal bounded attempts
and reconnect backoff; BLE and USB MIDI continue. Only a ready connection to the
requested SSID (and requested IP under static mode) may commit. Address setup
failures cannot silently succeed using a different DHCP configuration. On timeout
or persistence failure, the previous network is restored. Power loss before commit
boots the previous record. Accepted network work finishes even after BLE disconnect,
leaving a queryable result; it never queues network cues. Physical App connection
remains available regardless of saved Wi-Fi health. Scan is asynchronous,
user-initiated and bounded to 10 seconds; it does not periodically scan during use.

Firmware, app and serial never expose password contents or credential payloads.
The credential is stored locally in NVS to reconnect after power loss. This is
not encrypted-at-rest storage: secure boot/flash encryption and device ownership
reset policy are outside this milestone. Use authenticated BLE for all transfers.
SDK radio/I2C/flash calls retain their normal synchronous cost; parsing and saving
are bounded main-loop operations, not hard real-time guarantees. Physical timing,
heap headroom and RF coexistence must be checked under maximum intended settings.
Firmware checks heap/largest-block headroom and reports resource exhaustion before
parsing/activation. Active JSON pools shrink to fit; allocation failure never counts as Saved.

## Dependencies and verification

Platform/core, U8g2, NimBLE and Flutter BLE versions remain pinned as in M4B.
Added **ArduinoJson 6.21.5 (MIT)** for bounded JSON parsing/serialization; no new
Flutter package. Standard ESP32 Preferences/NVS provides the record storage.
No signing, bundle identifier, board, pin, application-offset, upload-method or native USB mode changes.
The existing Bluetooth usage description now includes authenticated configuration.

The stock partition table reserves only 20 KB for NVS. M5 adds
`partitions_cuelink.csv`: a dedicated 64 KB configuration NVS partition at
`0x7e0000`, taken from the previously unused SPIFFS tail. SPIFFS retains its
start `0x670000` and shrinks from `0x180000` to `0x170000` bytes; this firmware
has no filesystem use. Default NVS, OTA data, both application offsets/sizes and
coredump retain their exact locations. BLE bonds stay in default NVS. This
provides room for both 12 KB records plus NVS replacement/garbage collection
headroom. Normal manual PlatformIO upload includes the new partition table;
no whole-flash erase or automatic filesystem format is performed. If this board
contains externally installed SPIFFS data, back it up before adopting the changed
filesystem geometry. A dedicated-partition initialization/storage error is
reported as a save failure, never repaired by erasing default NVS.


From `firmware/`:

```sh
~/.platformio/penv/bin/pio run -e seeed_xiao_esp32s3
sh tests/config/run.sh
sh tests/commands/run.sh
sh tests/network/run.sh
sh tests/midi/run.sh
sh tests/ble/run.sh
c++ -std=c++11 -Wall -Wextra -Werror -Itests/host -Iinclude src/buttons.cpp tests/host/buttons_test.cpp -o /tmp/cuelink-buttons-test
/tmp/cuelink-buttons-test
```

`tests/config/run.sh` uses the pinned ArduinoJson headers installed by PlatformIO.
It exercises production codec, journal, transfer gate and configuration manager
with simulated NVS/radio/transport boundaries. Shared vectors validate C++/Dart
serialization. Existing button/network/OSC/UDP/MIDI/BLE regressions remain relevant.

From `flutter/cuelink_config/`:

```sh
flutter analyze
flutter test
flutter build ios --no-codesign
```

The unsigned artifact is not an installed app. No firmware upload, signed app
installation or physical testing is performed automatically.

## Implementation files

- Firmware codec/model: `include/configuration.h`, `src/configuration.cpp`,
  `include/network_settings.h`; persistence/application: `include/config_manager.h`,
  `src/config_manager.cpp`; framed transport: `include/config_transfer.h`,
  `src/config_transfer.cpp`.
- Firmware integration: `include/app_connection.h`, `src/app_connection.cpp`,
  `include/ble_protocol.h`, `include/commands.h`, `src/commands.cpp`,
  `include/network.h`, `src/network.cpp`, `include/triggers.h`, `src/triggers.cpp`,
  `include/display.h`, `src/display.cpp`, `src/main.cpp`, `platformio.ini`,
  `partitions_cuelink.csv`.
- Firmware documentation/tests: `README.md`, `docs/ble_protocol.md`,
  `docs/ble_protocol_vectors.json`, this document, `docs/configuration_vector.json`,
  `tests/config/config_test.cpp`, `tests/config/partitions_test.py`,
  `tests/config/run.sh`, `tests/network/run.sh`.
- Flutter: `lib/config/models.dart`, `lib/config/client.dart`,
  `lib/config/screens.dart`, `lib/ble/controller.dart`, `lib/ble/transport.dart`,
  `lib/main.dart`, `ios/Runner/Info.plist`, `pubspec.yaml`,
  `test/config_test.dart`, `test/config_widget_test.dart`, `test/protocol_test.dart`.

Validation on 2026-10-24: PlatformIO build passed (70964 bytes static RAM,
974065 bytes application flash); all host regression suites passed, including
AddressSanitizer/UndefinedBehaviorSanitizer configuration checks. Generated
partition binary verified against the intended offsets. Flutter analysis was
clean, all 27 tests passed, and unsigned iOS build produced `Runner.app` (20.7 MB).
No upload, signed installation or physical hardware testing was performed.

## Physical acceptance checklist

1. Review the partition change above, then manually upload following [USB upload/recovery](triggers_and_usb_midi.md).
   Run the existing buttons, B4 hold/release gate, OLED pages, Wi-Fi recovery,
   OSC/raw UDP and native USB MIDI checks before configuration testing.
2. From App connection press B3, connect the iPhone app, open settings and Load.
   Check the six-digit OLED value against the iOS passkey dialog. Try a wrong
   value: access must fail. Repeat B3 and authenticate correctly. Confirm public
   status still works separately. With a BLE inspector, verify configuration
   access fails without authentication or physical authorization; test an
   unauthenticated/legacy peer. Never record passkeys or credential packet data.
3. Disconnect, reconnect without another B3: configuration must be denied even
   for a bonded iPhone. Press B3 to authorize again. Leave connected beyond 120
   seconds and verify authorized configuration remains usable; exit diagnostics
   without generating a cue. Repeat with Wi-Fi unavailable to verify BLE recovery.
4. Load settings, check exact migrated development mappings and full labels.
   Edit a label longer than four characters and check OLED abbreviation/full
   diagnostic label. Save only B1; verify other buttons/routing/network unchanged.
   Disable/re-enable a button and slot, remove/reorder slots, test mixed/repeated
   protocols, four-slot Add disabled, zero slots and invalid input. Draft edits,
   Save, read and navigation must emit no cues. Discard must restore device values.
5. Verify OSC int32/float32/string bytes and raw UDP text/hex/line endings with
   receivers. Try a hex payload containing NUL/non-ASCII: conversion to unsupported
   text must fail without changing bytes. Test master routing ON/OFF, eight
   destinations and partial destination failures; MIDI must ignore master routing.
6. Configure a longer MIDI pulse, trigger it physically, then save changes to that
   slot and enter diagnostics before release. The original note/channel must
   receive its original Off on schedule. Confirm no notes on Save/reconnect/boot.
7. In Network, explicitly Scan from the controller; verify no iPhone scan or
   background scanning. Try hidden/manual SSID, scan error/timeout, password
   reveal, leading/trailing password spaces, keep, replace and explicit open.
   Empty replacement must fail, keep must preserve the secret, reads must show
   only the indicator. Disconnect/background during editing: password field clears.
8. Apply working DHCP settings and then valid static IP/mask/gateway/DNS1/DNS2.
   Observe Testing, saved revision acknowledgment and readback before Saved.
   Confirm actual current addresses, no internet requirement, and continuing USB
   MIDI/button/OLED/BLE operation. Try wrong credentials/unavailable AP and invalid
   static subnet: invalid input rejects immediately; connection failure rolls back
   after at most 30 seconds. Prior network must reconnect without manual repair.
9. Disconnect BLE immediately after Apply acceptance. Let it finish, then physically
   authorize/reconnect and query authoritative revision/result; do not blindly
   retry. Repeat around ACK/readback loss. With two stale edit sessions, save the
   newer version then attempt the old revision: it must conflict, preserving data.
10. Power-cycle after saves and rebuild with changed source defaults: saved values
    must win. Using controlled test hardware, interrupt a storage write; reboot must
    select either complete old or complete new settings. Corrupt the newer record
    in a test build: previous valid record must recover. Simulate persistence failure:
    active settings must stay unchanged and network candidates must roll back.
    Do not erase unrelated NVS/bonds to perform these checks.
11. Test maximum intended packet/configuration sizes, default ATT MTU 23 and higher
    negotiated MTUs, transfer cancellation/inactivity, malformed sequence/CRC/length,
    repeated request IDs, fast reconnect and sustained combined Wi-Fi/BLE/MIDI use.
    Check runtime heap and release timing, keyboard scrolling and large text on
    the physical iPhone. Confirm existing signing and device identity stay unchanged.
