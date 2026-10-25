# Interface refinement verification

Implemented 25 October 2026 in the firmware and sibling Flutter project. No controller was flashed or used for physical verification. The initial attachment contained the written requirements. Ten reference screenshots (IMG_1238.PNG–IMG_1247.PNG) were subsequently supplied and reviewed against the implemented UI and generated Flutter captures.

## Changed files

Flutter (`../../flutter/cuelink_config/`):

- `lib/ui.dart`: shared spacing, typography, card/button themes, expandable help, responsive switches, and app bars that accommodate larger text.
- `lib/main.dart`: compact discovery, conditional Stop, connection help, consistent overview typography and padding.
- `lib/config/screens.dart`: controller summaries; button, trigger, routing, and network editor layouts; responsive destinations; optional previews/help/revision details; concise status; transfer cancellation; explicit draft conflict handling. Stale drafts cannot save until explicitly reloaded/discarded. Reloading settings retains the draft for review; discarding an edited draft requires confirmation.
- `lib/config/client.dart`: shorter presentation messages; transfer, authorization, revision checks, and readback behavior retained.
- `test/widget_test.dart`, `test/config_widget_test.dart`: updated wording expectations.
- `test/ui_review_test.dart`: responsive layout and draft regression coverage, with optional PNG captures.

Firmware:

- `src/display.cpp`, `include/display.h`: compact home layout, larger network text, bottom button indicators, transient feedback in the header, no diagnostics hint footer, unchanged B4 diagnostics handling, and skipped identical framebuffer transfers.
- `include/display_home.h`: complete OLED home rendering and measured text bounds shared with the native raster test.
- `tests/display/layout_test.cpp`, `tests/display/run.sh`: real U8g2 font rasterization and pixel bounds checks.
- `docs/ui_refinement.md`: this verification record.

## Reference screenshot comparison

All ten supplied screenshots show the earlier interface, identifiable by “Slot”, “Load / query device”, and the promotional discovery heading. Their visible issues are covered by the implemented refinements:

- Discovery: compact connection heading, scan as the primary action, Stop only during scanning, connection steps in expandable help, and no status footer.
- Controller settings before/during/after loading: conditional authorization instructions, compact transfer status with cancellation, Load settings/Refresh, singular trigger wording, explicit disabled protocols, and a 12-pixel gap between Routing and Network.
- Button editors, including the scrolled three-trigger example: Button name, optional label help, compact trigger headers with integrated switches, accessible reorder/delete actions, and separated Add trigger and save controls.
- Trigger settings, including both scroll positions: compact arguments/destinations, separated add actions, short routing status, expandable command preview, and Done/Cancel with draft-save help.
- Routing: Use master destinations, short explanation, optional technical behavior, and no empty-master warning while individual routing is active.
- Network: status card, Scan networks, Network name (SSID), Password options, concise rollback explanation, and optional details.

The references confirm the original spacing and copy problems; this comparison did not identify a further implementation change. No OLED photograph was included. Reviewing these screenshots does not verify the updated app on a physical device.

## Automated checks and rendered review

- Flutter analysis: no issues.
- Flutter test suite, including the responsive review matrix.
- PlatformIO release build for `seeed_xiao_esp32s3`.
- Existing firmware configuration, commands/UDP, MIDI scheduling, network, BLE, and button state-machine host tests.
- Native U8g2 full-home raster tests for Connected, Connecting, Reconnecting, and Offline, including the longest IPv4, No IP, pressed/unpressed buttons, transient feedback, hold progress, abbreviation, and glyph centering. Guard bytes around the framebuffer remain intact. The button state-machine host tests also pass after the OLED refinement.

The Flutter review uses 390×844 at normal text size and 320×700 at 160% and 200%. Captures cover discovery, authorization, loaded settings, transfer progress, four triggers, disabled triggers, reorder boundaries, a 12-character name, dirty/conflicting drafts and discard confirmation, OSC/UDP/MIDI, empty and populated destinations, routing, network credentials, static IP controls, and keyboard insets in each editor. This is software-rendered widget review with simulated keyboard/screen insets, rather than a physical phone or live BLE session.

Generated screenshots are under the Flutter project's ignored `build/ui_review/` directory. Native OLED network-row rasters are under `.pio/display-review/`. They are reproducible and are not committed binary assets.

Reproduce from the Flutter project:

```sh
flutter analyze
flutter test
# FLUTTER_ROOT points to the SDK so review captures can load Roboto and icons.
FLUTTER_ROOT=/path/to/flutter flutter test test/ui_review_test.dart --dart-define=CUE_UI_REVIEW=true
```

Reproduce from firmware:

```sh
pio run
sh tests/display/run.sh
sh tests/config/run.sh
sh tests/commands/run.sh
sh tests/midi/run.sh
sh tests/network/run.sh
sh tests/ble/run.sh
c++ -std=c++11 -Wall -Wextra -Werror -Itests/host -Iinclude src/buttons.cpp tests/host/buttons_test.cpp -o /tmp/cuelink-buttons-test
/tmp/cuelink-buttons-test
```

## OLED home layout adjustment — 25 October 2026

The follow-up requested smaller network text, larger header text, and centered labels. No new OLED photograph was present in that message; the current rendering and written requirements were used as the reference.

Measured with the pinned U8g2 rasterizer, using visible glyph pixels:

| Text | Previous font / height | Selected font / height | Change |
| --- | --- | --- | --- |
| CueLink and RUN | `u8g2_font_5x7_tr` / 6 px | `u8g2_font_7x13_tr` / 9 px | +50% |
| Connected | `u8g2_font_helvR10_tr` / 11 px | `u8g2_font_helvR08_tr` / 8 px | −27% |
| IPv4 digits | `u8g2_font_helvR10_tr` / 11 px | `u8g2_font_helvR08_tr` / 8 px | −27% |

The selected network font is the closest measured native size to the requested 30% reduction. Descenders in Connecting/Reconnecting extend the visible string height to 10 pixels. `255.255.255.255` is 80 pixels wide and stays complete.

| Region | Pixel layout |
| --- | --- |
| Header | Ink at y=4–12, shared baseline 13; CueLink begins x=4, RUN ends x=123 |
| Divider | x=4–123, y=14 |
| Wi-Fi state | Horizontally centered in y=16–25 |
| IP / No IP | Horizontally centered in y=27–34 |
| Transient feedback | Centered in y=36–44, `u8g2_font_6x10_tr`; does not replace RUN or the Wi-Fi state |
| Active B4 hold progress | y=61 below the button boxes, width clamped to 120 pixels |
| Buttons | Four 27×14 boxes at x=4,35,66,97 and y=46; 4-pixel gaps; bottom edge y=59 |

Button labels retain the existing `6x12` (one/two characters) and `5x7` (three/four characters) fonts and first-three-characters-plus-dot abbreviation for longer names. Actual glyph ink bounds are measured in a bounded 256-byte scratch framebuffer, using U8g2 string widths and font ascent/descent as the starting metrics. This accounts for side bearings and descenders, keeping AB, lowercase descenders, and punctuation centered to within one pixel. Measurement uses no I2C, waits, or heap allocation. The live framebuffer is never modified by measurement.

The frame-rate limit, one tile-row I2C transfer per loop iteration, and identical-frame suppression remain in place. Physical display timing and flicker still require the OLED. No firmware upload was performed.

Verification completed: `pio run`, `sh tests/display/run.sh`, the existing button state-machine test, and visual inspection of all four complete rasterized home screens. The generated review image is `.pio/display-review/home-layout.png`; individual 128×64 PBMs are beside it. These are software renderings, not hardware photographs.

## Sent-details readability follow-up

The transient send-summary line was too small in the previous home layout. It now uses `u8g2_font_6x10_tr` in a nine-pixel row. Actual `B1: 4 sent` ink grows from 5 to 8 pixels tall (+60%) and from 39 to 59 pixels wide. The IP moves up one pixel to leave a clear row above feedback; the active B4 hold strip moves below the button boxes. Wi-Fi state, full IP, RUN, button labels, and communication behavior are retained.

Native raster checks cover sent counts, invalid configuration, no triggers, partial sends, and release prompts across all Wi-Fi states and pressed/unpressed boxes. They assert centered feedback, readable glyph heights, clear rows between IP/feedback/buttons, equal margins, and framebuffer bounds. The refreshed `home-layout.png` shows the larger send-summary text. Physical readability and I2C timing still require the OLED.

## Physical checks remaining

- BLE discovery, iOS pairing/passkey prompts, B3 authorization, cancellation/reconnection and real configuration transfers.
- Wi-Fi credential changes, connection testing, failure rollback, and reconnect/lost-IP behavior on a real access point.
- OSC/UDP destinations, USB MIDI including scheduled Note Off, and persistence across reboot.
- OLED readability/contrast, transitions and flicker over the actual I²C bus, physical button feedback, and B4 long-press diagnostics.
- Native phone keyboard, safe-area/home-indicator insets and accessibility settings on the target iPhone.

The subsequent five-second native OLED boot sequence and its verification are documented in [OLED boot animation](oled_boot_animation.md). The operational-screen layout described above is retained after boot.
