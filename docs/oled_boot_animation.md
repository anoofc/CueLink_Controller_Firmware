# CueLink OLED boot animation

Implemented for the existing SSD1306 128×64/U8g2 display. No controller was flashed during verification.

## Sequence and layout

- 0–1 s: four rounded buttons reveal sequentially, clockwise (top-left, top-right, bottom-right, bottom-left), at 250 ms intervals.
- 1–4 s: CueLink appears below the mark in native `u8g2_font_helvB10_tr`. One filled button cycles clockwise against three outlined buttons.
- 4–5 s: all four buttons settle filled; the four-dot activity indicator finishes its final one-second loop.
- At 5 s: render the current operational screen, including actual Wi-Fi state and current controller mode. No connection-ready condition is required.

The approved mark is recreated with native 12×12 rounded boxes, radius 4, and 2-pixel gaps. The mark spans x=51–76 and y=7–32. Wordmark placement uses measured glyph ink bounds; it is centered in a 12-pixel row starting at y=36. The activity indicator sits at y=55. The complete composition occupies y=7–56, symmetric within the 64-pixel display, and stays within safe horizontal margins. There is no bitmap, diagnostic text, percentage, or initialization progress bar.

## State and display ownership

`include/display_boot.h` contains the pure timed state machine and U8g2 renderer. Successful OLED initialization arms it once. Missing/failed I2C discovery leaves the existing display-unavailable path in place, so controller operation continues.

The first frame is transferred using the existing one-tile-row-per-loop mechanism. The five-second clock starts from `millis()` only after all eight rows of that first boot frame have been submitted. Setup/first-frame latency is not counted as part of the animation. Elapsed-time subtraction handles `millis()` rollover. Completion is latched; reconnects and repeated arm/first-frame callbacks cannot replay it. A software reset creates a fresh instance and plays it again.

The display owns rendering while waiting for its first frame and while playing. Normal status rendering cannot overwrite the animation. Frame drawing is capped at 25 FPS; identical-frame transfer suppression remains enabled. At completion, any unfinished boot transfer is replaced, the frame cache is invalidated, and the current operational frame is cleared/redrawn and transferred in full. There is no blank interstitial frame and no residual boot pixels after the operational frame finishes transferring.

`src/main.cpp` is unchanged. Buttons, network, trigger dispatch, MIDI scheduling, configuration, and BLE continue to be serviced before the display update. No `delay()`, sleep, blocking animation loop, readiness wait, or connection callback starts the animation. Existing operational layouts are unchanged.

## Files

- `include/display_boot.h`: one-shot timer/state and native boot artwork.
- `include/display.h`: animation state and frame-ownership flag.
- `src/display.cpp`: successful-init arming, first-frame timestamp, boot/live rendering selection, and clean handover.
- `tests/display/boot_test.cpp`: timer/stage/raster/handover and real button/MIDI scheduling tests.
- `tests/display/run.sh`: builds/runs the existing home-layout tests and new boot tests against the pinned U8g2 library.
- `docs/oled_boot_animation.md`: this record.

## Completed checks

- PlatformIO release build for `seeed_xiao_esp32s3`: passed.
- Native display tests: passed. Tested delayed first-frame start, five-second boundary, one-shot behavior, fresh-instance/reset behavior, timer rollover, clockwise stage masks, 125 frames at 40 ms intervals, clear margins/row spacing, and framebuffer guard bytes.
- Home-layout regression tests: passed. Operational screen pixels remain identical when rendered after the final boot frame, including connected/connecting/reconnecting/offline states.
- Real button debounce and MIDI Note Off scheduler in a simulated five-second boot loop: passed. A B1 event executes at its debounce deadline and its scheduled Note Off executes while the animation still owns the OLED.
- Existing configuration, commands/UDP, MIDI, network, BLE, and physical-button state-machine host suites: passed.
- Visual inspection of native raster snapshots: completed. `.pio/display-review/boot-sequence.png` shows the stages and an offline handover example.

`.pio/display-review/boot-animation.gif` contains the five-second native raster sequence followed by a 1.5-second operational-screen example for preview; the firmware animation itself is five seconds. Generated review artifacts remain ignored.

Reproduce from the firmware directory:

```sh
sh tests/display/run.sh
/Users/anoofc/.platformio/penv/bin/pio run
```

## Physical controller checks remaining

Verify wall-clock duration and visual smoothness over the real I2C bus, cold boot and software reset, live BLE/Wi-Fi responsiveness and trigger output during the animation, physical button/USB MIDI response, and behavior with a disconnected OLED. Confirm real-time transition under the device's workload and that reconnecting later does not replay the animation. Host tests and source-level review do not constitute physical-device verification.
