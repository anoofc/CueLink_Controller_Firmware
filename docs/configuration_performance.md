# Configuration transfer performance — v2

Implemented in both existing repositories after inspecting their clean working trees. The original 12-byte/read-acknowledgment transfer was still present; no parallel performance implementation was found. Persisted schema, atomic journal, partition layout, physical authorization, trigger behavior, OLED home/diagnostics, and five-second boot animation are unchanged. No push, firmware upload, or app installation was performed.

## Fast path and compatibility

Device-information capability **bit 7 (`0x0080`)** advertises v2 transport plus scoped verification. Current information capability mask is `0x00ff`; the public information/status protocol remains version 1 with the same UUIDs and 12-byte values. New apps use the legacy path if the bit is absent. Old apps remain supported by the new firmware's v1 path. Unexpected transport versions fail rather than silently changing protocols midway through a mutation.

The protected response characteristic now supports READ and NOTIFY with a maximum value of 244 bytes. Request characteristic limits and both mailbox/response buffers are also 244 bytes. Every write/read retains the physical-session grant, authenticated encryption, and 16-byte key requirement. Before each notification the main loop rechecks the current peer's handle, encryption, authentication, and key size; notifications target that peer, not a broadcast. Callbacks copy/clear bounded data only. Parsing, validation, persistence, and notification publication stay in the main loop.

Firmware prefers ATT MTU 247, but each BEGIN is bounded by the peer's **actual** MTU minus 3. Flutter queries the installed plugin's supported `requestMtu` API. In pinned `reactive_ble_mobile` 5.6.0, the iOS implementation retrieves CoreBluetooth's current `maximumWriteValueLength(.withoutResponse)` and returns that **value capacity**, rather than the ATT MTU implied by the Dart API name. The adapter uses that returned capacity directly on iOS, and subtracts the 3-byte ATT overhead from Android's actual returned MTU. It never assumes the requested 247 was granted. Errors retain a safe 20-byte frame budget.

### Framing

The eight-byte header is unchanged: version, kind, request uint32 LE, sequence uint16 LE. V1 retains its 16-byte BEGIN, 12-byte response chunks and legacy upload limit of 128 bytes, always additionally subject to the phone's ATT budget.

V2 changes only the following:

| Item | Definition |
| --- | --- |
| Version | byte 0 = 2 |
| BEGIN | 18 bytes: existing total-length/CRC words plus uint16 LE requested frame budget |
| BEGIN ACK | 10 bytes: header plus uint16 LE chosen frame budget |
| Chosen budget | min(requested budget, actual peer MTU − 3, 244), at least 20 |
| Payload | chosen budget − 8; e.g. 174 bytes at MTU 185, 236 at MTU 247 |
| Response manifest | 16 bytes, kind `0x81`, sequence 0, total bytes + CRC |
| FETCH | same request/sequence header; offset = sequence × negotiated payload |

All lengths, offsets, sequence checks, and CRC checks use this agreed budget. A fresh BEGIN with a new ID may abandon an incomplete, uncommitted upload for explicit recovery. It does not cancel/replay an already committed mutation or a staged network transaction. Duplicate/out-of-order DATA is rejected. FETCH remains idempotent.

Flutter subscribes before its first BEGIN and installs the request/kind/sequence matcher **before** writing each frame, so early notifications are retained. It waits for the ATT write to complete before starting recovery reads, allowing the first system pairing dialog to take longer than five seconds. Only one write is in flight, including a user-requested CANCEL. Stale, duplicate, delayed, wrong-ID, and wrong-sequence frames cannot advance the transfer. A missing response permits at most two snapshot recovery reads, after 300 ms waits; failure marks the outcome uncertain. No DATA/END mutation replay occurs. Notification errors and disconnects invalidate the whole operation, including the boundary after its final verification notification.

Normal loading uses **one `read` JSON operation**. If its receipt reports a pending network transaction, the app queries to completion and repeats the full read before publishing settings. An initial protected READ and an unconditional query are no longer required.

### Scoped verification

After a successful mutation, the app makes a separate authoritative `read_scope` request:

```json
{"schema":1,"op":"read_scope","scope":"button","button":0}
```

`scope` is `button`, `routing`, or `network`. The response includes `ok`, `revision`, `pending`, `scope`, optional button index, and authoritative `value`. It contains no password. The app verifies the committed revision, matching scope/index, all normalized saved values, and the expected password-configured state for keep/replace/open credential operations. Only then does it replace that scope in its cache and publish success. Legacy peers still use full-configuration readback. Initial load, reconnect recovery, and uncertain outcomes retain full refresh. Draft revisions and explicit conflict resolution remain intact.

Connection parameters request 15–30 ms interval, zero peripheral latency, and 4 s supervision timeout. The central's final scheduling decision is respected; actual parameters are logged, not assumed.

## Diagnostics

Flutter emits `[ConfigTiming]` after the complete logical operation, including transport setup, upload, persistence acknowledgment, verification, and subscription cleanup. `elapsedMs` uses a monotonic Stopwatch. It also records protocol/frame budget, JSON upload/download bytes, chunk counts, configuration GATT writes/reads/notifications, recovery reads, and response-wait retries. These counters describe the configuration characteristics; OS pairing exchanges, MTU control packets and CCCD management are not exposed as data-write counters by the plugin. Retries never mean automatic mutation replay.

Firmware emits candidate `validation_ms`, actual journal `persist_ms`/success/serialized-byte count, request/response byte and chunk counts, cumulative received writes/reads and sent notifications, actual MTU, chosen frame budget and accepted connection interval/latency. Interval units are 1.25 ms. Payloads, passwords, credentials and passkeys are never logged by these diagnostics. No-op saves skip persistence.

Network credential changes retain the existing association/DHCP test and rollback (up to 30 seconds); they are outside the ordinary five-second save target.

## Measured simulation results

These are **virtual-time client/peripheral simulations, not physical measurements**. The tests use the real client, framing, CRC, model validation, cache verification and timers, with an independent peripheral model. Each frame models a write leg plus a notification leg. Setup costs one modeled round trip per operation; a changed value adds an assumed 40 ms persistence delay. Real radio contention, iOS scheduling/CCCD timing and actual NVS/validation time remain hardware variables. The live Stopwatch diagnostic printed inside a fake-time test measures host execution; the `SIMULATED` lines separately report the virtual elapsed durations below.

| Configuration / link | Load | Button save + scoped verification | Routing save + scoped verification |
| --- | ---: | ---: | ---: |
| 12,286-byte configuration, MTU 185, 30 ms round trip | 2.250 s | 1.330 s | 0.310 s |
| Same configuration, MTU 247, 30 ms round trip | 1.710 s | 1.090 s | 0.310 s |
| Near-limit configuration, MTU 185, 60 ms round trip | 4.500 s | 2.620 s | tested at 30 ms above |
| Representative 933-byte mixed-protocol configuration, MTU 185, 60 ms round trip | 0.600 s | 0.880 s | tested separately |
| Near-limit configuration, safe MTU 23, 30 ms round trip | 31.020 s | 15.880 s | 0.940 s |

The supported configuration limit is 12,288 bytes; the framed request/response limit remains 16,384 bytes. Tests also round-trip a full 16,384-byte transfer in native C++ at 20/182/244-byte budgets. The MTU-23 row is intentionally reported: safe fallback/legacy compatibility does **not** guarantee five seconds for a near-limit document. That mode requires over a thousand serialized frames for a load. The ≤5 s target for healthy negotiated-MTU sessions is **awaiting iPhone/controller verification**, not declared achieved on hardware.

## Checks completed

- Flutter analysis: no issues; 58 Flutter tests passed, including 21 new fast-transfer tests.
- Signed iOS release build: passed with the existing bundle ID/signing settings.
- PlatformIO release build for existing `seeed_xiao_esp32s3`: passed; board/partition settings unchanged for the XIAO ESP32-S3 Plus workspace.
- Native configuration, commands/UDP, MIDI, network, BLE golden-vector, OLED home/boot, and button state-machine host suites: passed.
- Coverage includes MTU 23/185/247, legacy fallback, unsupported version, lost/duplicate/stale/delayed notifications, bounded missing-response recovery, notification errors, cancellation, disconnect during upload/after commit/during verification and after its last notification, CRC/revision failures, wrong scope/value rejection, no-op/repeated saves, pending network recovery, near-limit simulated latency, full-capacity native framing, and unchanged persistence/deduplication behavior.
- Real MIDI Note Off scheduling and network update calls are exercised between large-transfer frames in the host harness. Existing button/MIDI/boot tests continue to pass; main-loop ordering is unchanged. This does not establish real BLE/Wi-Fi coexistence or physical response latency.

At verification time, the iPhones were listed as unavailable and no controller USB serial port was exposed. No live load/save timing was measured.

## Changed files

Firmware: `include/config_transfer.h`, `src/config_transfer.cpp`, `include/app_connection.h`, `src/app_connection.cpp`, `include/ble_protocol.h`, `src/config_manager.cpp`, `tests/config/config_test.cpp`, `docs/ble_protocol_vectors.json`, the protocol documents/README, and this report.

Flutter: `lib/config/client.dart`, `lib/ble/transport.dart`, `test/config_fast_test.dart`, `test/protocol_test.dart`, and README. No widget/layout, model schema, plugin version, permission, icon, or firmware display change was needed.

## Rebuild and install

Firmware:

```sh
cd /Users/anoofc/Documents/PROJECTS_X/CueLink/firmware
/Users/anoofc/.platformio/penv/bin/pio run -e seeed_xiao_esp32s3
/Users/anoofc/.platformio/penv/bin/pio device list
# Connect the controller; use BOOT/RESET recovery if no upload port is available.
/Users/anoofc/.platformio/penv/bin/pio run -e seeed_xiao_esp32s3 --target upload
```

If multiple serial devices exist, append `--upload-port` followed by the controller's actual path from `pio device list`. Do not erase flash/NVS; the persisted schema and partition layout are compatible.

App:

```sh
cd /Users/anoofc/Documents/PROJECTS_X/CueLink/flutter/cuelink_config
/Users/anoofc/Documents/PROJECTS_X/Flutter/flutter/bin/flutter pub get
/Users/anoofc/Documents/PROJECTS_X/Flutter/flutter/bin/flutter analyze
/Users/anoofc/Documents/PROJECTS_X/Flutter/flutter/bin/flutter test
/Users/anoofc/Documents/PROJECTS_X/Flutter/flutter/bin/flutter build ios --release
# Reconnect/unlock the iPhone, then update the existing app without uninstalling:
xcrun devicectl device install app --device 00008150-000A5D8411F8C01C build/ios/iphoneos/Runner.app
xcrun devicectl device process launch --device 00008150-000A5D8411F8C01C --terminate-existing --console com.example.cuelinkConfig
```

The Android adapter remains compatible with the plugin's MTU return semantics. This checkout still has staged Android icons only, with no runner/Gradle configuration (as previously requested), so no Android platform build was possible.

## iPhone/controller acceptance steps

1. Install **both** builds, cold boot the controller, and confirm the five-second OLED animation still transitions to actual status. Hold B4 to enter diagnostics, navigate to App connection, press B3, connect the iPhone, and complete the system passkey pairing prompt if needed. Do not include first-time manual authorization/pairing in the already-authorized acceptance runs.
2. Capture app `[ConfigTiming]` output using the console command above and firmware timing output through its existing USB diagnostics interface. Confirm `protocol: 2`, `frameBytes` greater than 20 (typically 182 on iOS), and actual MTU/interval logs. If the app uses v1, firmware was not updated or lacks capability bit 7. MTU-23 fallback must be identified separately rather than mistaken for a fast-path pass.
3. On a test controller/configuration, use representative mixed OSC/UDP/MIDI settings, then a valid near-limit configuration with four buttons × four triggers. The automated `nearLimit()` fixture in `test/config_fast_test.dart` constructs 12,286 bytes by sizing UDP text payloads. The editor has no JSON-import feature; prepare equivalent settings through the existing editors. Record existing settings before changing a working controller. Keep the full configuration within the existing 12,288-byte validator.
4. For each configuration, run at least ten Refresh operations and ten ordinary button saves, alternating two valid names/values so changed saves actually persist. Separately run routing saves with valid destinations. Measure from tap until settings are ready / Saved is shown; correlate with the operation's `elapsedMs`, zero routine reads, frame size and firmware persistence time. Record each result and maximum; acceptance requires each healthy authorized run to be ≤5,000 ms. No UI success is allowed before authoritative verification.
5. Save identical values repeatedly: expect no additional NVS write and unchanged revision. Disconnect during upload, around persistence, and during verification. Reconnect, authorize again and perform a full Refresh. The committed state must be old or new atomically, never partial, and the app must not replay the mutation automatically. Test a second editor's revision conflict and explicit draft reconciliation.
6. During transfers, press physical buttons and observe OSC/UDP at a test receiver and USB MIDI at a monitor, including Note Off deadlines. Test Wi-Fi reconnection while USB MIDI remains usable, and network triggers with USB connected only for power. Repeat under typical Wi-Fi/BLE coexistence; record any radio-related outliers.
7. Test valid and deliberately failing Wi-Fi credential changes separately. Association/DHCP may take up to 30 seconds. Verify successful scoped network readback or rollback, no password payload in diagnostics, and retained BLE/USB operation. These are not ordinary ≤5 s saves.
8. Confirm all existing configurations survive reset and that later BLE/Wi-Fi reconnects do not replay the boot animation. If iOS cached the former characteristic properties, disconnect/reconnect and re-discover services before judging notification support; do not erase app/controller data just to run the benchmark.
