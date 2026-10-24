# Four-trigger development configuration and USB MIDI — M4B / 0.4.1

## Settings and exact examples

Edit `include/commands.dev.h`, rebuild, and manually upload. The existing tested
IPv4 **10.128.151.148** is preserved; verify it still belongs to your intended
receiver. No configuration is saved at runtime. Each `Triggers::Button` has a
1–4 printable ASCII character label, an array pointer, and an explicit count
0–4. Array counts must match the actual storage. A fifth trigger rejects that
entire button before any output; it is never truncated.

| Button / slot | Protocol | Exact settings | Destination |
| --- | --- | --- | --- |
| B1 / 1 | OSC | `/composition/columns/1/connect`, int32 `1` | 10.128.151.148:7000 |
| B1 / 2 | UDP | `CUELINK_B1`, exactly 10 bytes | 10.128.151.148:9000 |
| B1 / 3 | USB MIDI pulse | Channel 1, note 60, velocity 100, duration 100 ms | USB host |
| B2 / 1 | OSC | `/composition/columns/2/connect`, int32 `1` | 10.128.151.148:7000 |
| B3 / 1 | UDP | `CUELINK_B3`, exactly 10 bytes | 10.128.151.148:9000 |
| B3 / 2 | USB MIDI CC | Channel 1, controller 1, value 127 | USB host |
| B4 / 1 | UDP | `CUELINK_B4`, exactly 10 bytes | 10.128.151.148:9000 |

All other slots, including every fourth slot, are empty. These are editable
examples for a general-purpose tool. Resolume paths must be checked against
the receiving installation; OSC also works with other OSC applications.

`Triggers::Trigger::osc(&settings)`, `::udp(&settings)`, and `::midi(&message)`
select distinct typed settings; pass `false` as the second argument to disable
a slot. Repeated protocols and arbitrary combinations are supported. Settings,
argument arrays, byte arrays, destination arrays, and labels must outlive the
engine. The static development definitions satisfy that requirement.

OSC uses `Triggers::Osc {address, arguments, argumentCount, destinations}`.
Arguments use `Command::Argument::int32`, `float32`, or `string`; the existing
OSC encoder preserves type tags, big-endian numbers, NUL termination and
four-byte alignment. UDP uses `Triggers::Udp {bytes, length, destinations}`:
length is explicit, including embedded zeros if desired; no newline, NUL, or
padding is appended. Empty raw packets and encoded packets above 1024 bytes
are rejected. Strings/labels use the supported ASCII subset.

A destination list is `{array, count}` with 0–8 entries of `{numericIPv4, port}`.
Ports must be 1–65535; unicast numeric IPv4 is required. Empty lists or unset
addresses disable that network output and report a configuration error.
Invalid individual entries are reported separately; valid entries still run.
A list longer than eight is rejected before sending any part of that trigger.

`useMasterDestinations` defaults to `false`. When enabled, fill `masterDestinations`
and its count: that list replaces the effective destinations of every OSC/UDP
slot, preserving each individual list. An empty/invalid master list prevents
network submissions; MIDI remains independent. Oversized stored individual
lists are rejected even when master routing is enabled. One trigger fans the
same encoded packet to every effective destination; use separate slots for
different payloads.

MIDI factories are `Midi::Message::pulse(channel,note,velocity,durationMs=100)`,
`noteOn(channel,note,velocity)`, `noteOff(channel,note,velocity=0)`,
`controlChange(channel,controller,value)`, and `programChange(channel,program)`.
Channels are user-facing **1–16** and converted to 0–15 only on the wire. Notes,
controllers, CC values, velocities and programs use **0–127**. Program Change
is consistently zero-based (program 0 is the first program), regardless of an
application displaying programs as 1–128. Pulse velocity is 1–127 and duration
1–60000 ms; explicit Note On accepts velocity zero according to MIDI semantics.

## Execution and results

Existing debounced run events are the only cue input: B1–B3 on stable press,
B4 on short release. Holding, boot, reconnect, diagnostic navigation and mode
changes emit no cues. Enabled slots run in ascending order; destinations run in
list order. Transports are sequential best effort, with no synchronization claim.
One failed slot or destination does not stop subsequent ones. Wi-Fi and USB
availability are independently checked, including their state at the input event.
There is no offline cue queue, automatic failed-cue retry, or reconnect replay.

UDP reuses one nonblocking socket bound to an automatically selected local port.
All configured valid remote port numbers are excluded from source-port selection;
setup is bounded to eight bind attempts. Wi-Fi disconnect, IP/loss changes or
local errors invalidate it. Only a fresh cue can reopen it. Each trigger encodes
into the existing fixed 1024-byte buffer; it is cleared after the attempt.
Maximum fanout is bounded at four buttons × four slots × eight destinations per
input batch, with one nonblocking send attempt per destination. Driver, I2C and
stack calls still have their normal execution cost; this is not a hard real-time
or synchronized transport.

A pulse reserves one of 16 channel/note entries before Note On. Capacity exhaustion
rejects the new pulse before any On. A successfully submitted repeated pulse on
the same channel/note reuses its entry and refreshes the deadline; the old timer
cannot cut it off. No intervening Off is emitted. A failed retrigger retains the
original deadline. Successful explicit Note On/Off on that key cancels the older
pulse timer. Other channels/notes are independent. Scheduled Note Off uses zero
velocity and runs in every UI mode, with unsigned rollover-safe timing.

USB disconnect/suspend drops pending releases; reconnect emits nothing. An Off
is attempted once when due. A local release failure is recorded separately from
the initial trigger result and never retried. Host applications differ in their
handling of repeated Note On or disconnect; use the receiving application's
Stop/Panic function if it retains a note after disconnect or failed release.
Firmware emits no automatic panic or other unsolicited MIDI.

Results distinguish local submission, invalid configuration/destination/command,
Wi-Fi unavailable, USB unavailable, local transport failure and scheduler full.
A network trigger is fully successful only when **all** its effective destinations
accept local submission. OLED feedback counts fully successful triggers:
`B1: 3 sent`, `B1: Partial 2/3`, or `B2: No triggers`, restored after 1.5 seconds.
A single partly submitted fanout may therefore show `Partial 0/1`; destination
successes remain visible separately. All-failed attempts show zero sent; diagnostics
and logs identify why. Local submission never proves receipt or application execution.

Per-button diagnostic pages show configured count and four slot rows. The USB MIDI
page shows mounted/ready, pending releases, release failures and dropped releases.
The Trigger details page uses B3 to cycle button/slot and destination halves;
it displays four destination outcomes at a time. MIDI details show message values,
initial result and the latest separately identified release channel/note/result.
B1/B2 and B4 long exit retain their original behavior. Result codes: OK accepted,
PT partial, WF Wi-Fi unavailable, UF USB unavailable, LF local failure, SF scheduler
full, CF configuration error, `--` no attempt. Counters reset on reboot.

Serial logs one `[TRIGGER]` per enabled slot and one `[DEST]` per network destination.
`[MIDI OFF]` reports later releases. No credentials or payload contents are logged.
A bounded 4096-byte best-effort CDC log buffer drops logs when detached/full;
one bounded chunk is drained each loop. This avoids the installed USBCDC driver's
potential indefinite write loop under backpressure. Cue processing never waits
for a serial reader. Log delivery is not guaranteed under sustained overload.

## Exact USB implementation and changes

The installed PlatformIO Espressif32 **6.12.0**, Arduino-ESP32 **2.0.17**
(`framework-arduinoespressif32@3.20017.241212`) are now explicitly pinned.
The installed SDK already enables TinyUSB CDC and MIDI; no framework upgrade
or new dependency was needed. U8g2 2.36.17 and NimBLE-Arduino 2.3.9 are retained.
Native USB MIDI 1.0 uses that core's `tinyusb_enable_interface`,
`TUD_MIDI_DESCRIPTOR` and `tud_midi_packet_write`, with standard four-byte USB
MIDI event packets. There is one MIDI cable; incoming host MIDI is not used.

Board flags for hardware USB serial/JTAG mode (`ARDUINO_USB_MODE=1`) and automatic
CDC are removed. Explicit `ARDUINO_USB_MODE=0` and `ARDUINO_USB_CDC_ON_BOOT=0`
select one native TinyUSB device. CDC and MIDI descriptors register before one
`USB.begin()`. The product name is `CueLink Controller`. Explicit CDC is the only
application serial implementation. Monitor DTR and RTS are now enabled because
this core's USBCDC connection state requires both. Startup never waits for USB.
Board, flash layout, baud rate and upload mechanism are retained.

BLE UUIDs, protocol version 1 and both 12-byte formats remain unchanged. Firmware
version becomes 0.4.1; optional capability bit 4 advertises USB MIDI (`0x001f`).
Existing readers ignore unknown bits. Flutter UI and production code are unchanged;
only its shared-vector test expectation changes.

## Build, manual upload and recovery

From `firmware/` (use `~/.platformio/penv/bin/pio` if needed):

```sh
pio run -e seeed_xiao_esp32s3
pio device list
# Close the serial monitor first; replace PORT with the current physical port.
pio run -e seeed_xiao_esp32s3 -t upload --upload-port PORT
pio device list
pio device monitor -p PORT -b 115200
```

Native composite CDC can enumerate at a different `/dev/cu.usbmodem...` path.
Re-list ports after upload/reset and select the new one. Use a USB data cable.
If automatic bootloader entry fails, hold the board's physical **BOOT** button,
press and release **RESET**, then release BOOT. This is the board BOOT button,
not CueLink B4. List ports again, select the ROM bootloader port, upload manually,
then press RESET if needed. Re-list the application CDC port afterward. The first
upload from the older hardware-CDC firmware may require this recovery procedure.
No upload has been performed by the coding agent.

## Physical macOS verification checklist

1. Manually upload, attach via USB data cable and open **Audio MIDI Setup →
   Window → Show MIDI Studio**. Confirm CueLink Controller enumerates as a MIDI
   device. Open Protokol's MIDI tab (or another MIDI monitor), select/enable the
   CueLink input. Its OSC tab is separate and uses UDP port 7000 for these examples.
2. In run mode press B1 once. Expect Note On channel 1 / note 60 / velocity 100,
   then Note Off about 100 ms later. Press B3 once: expect CC channel 1 / controller
   1 / value 127. No MIDI is expected from B2/B4. Held B1/B3 must not repeat.
3. Press B1 rapidly again within 100 ms: expect two On submissions and one Off
   100 ms after the latest successful On. For explicit On/Off and Program Change,
   edit a development slot and manually rebuild/upload. Verify program 0 and 127
   as raw zero-based values even if the monitor uses different displayed numbering.
4. Use a longer test pulse duration to enter diagnostics before its deadline;
   confirm the Off still arrives. Diagnostic navigation and B4 long holds must
   generate no new cues. Inspect USB state and separate release results.
5. Disable the access point and confirm B1/B3 MIDI still works. With USB unavailable,
   check network sends using an independent power source where hardware allows it.
   Disconnect/reconnect USB during a long pulse: no old Off or cue should replay;
   inspect dropped release count. Confirm boot/reconnection is silent.
6. For raw UDP, on the destination computer run
   `python3 tools/udp_receiver.py --port 9000`, then press B1/B3/B4. Expect exact
   ten-byte text payloads from the table. Stop with Ctrl+C. Use the destination
   computer's actual reachable IPv4; hotspot client isolation can prevent peers
   talking despite local submission. For OSC, enable a receiver on port 7000 and
   verify both addresses with int32 1. Do not share a listening port between tools.
7. Recheck OLED/network pages, DHCP recovery, all button gates and BLE app discovery,
   read-only connection and live status while MIDI/OSC/UDP are active. Open/close
   the serial monitor and stress its output without changing cue behavior.

Hardware USB enumeration, host MIDI behavior, RF coexistence, and physical OLED
layout require these manual checks; compilation and host tests cannot establish them.
