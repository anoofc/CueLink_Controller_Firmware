#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
lib=.pio/libdeps/seeed_xiao_esp32s3/U8g2/src/clib
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
# Link the pinned library's real rasterizer and fonts, without Arduino or I2C.
for source in u8g2_buffer u8g2_setup u8g2_font u8g2_fonts u8g2_hvline u8g2_ll_hvline u8g2_intersection u8g2_line u8g2_kerning u8g2_box u8g2_circle u8x8_8x8; do
  cc -w -ffunction-sections -fdata-sections -I"$lib" -c "$lib/$source.c" -o "$out/$source.o"
done
case "$(uname -s)" in
  Darwin) strip_flag=-Wl,-dead_strip ;;
  *) strip_flag=-Wl,--gc-sections ;;
esac
c++ -std=c++11 -Wall -Wextra -Werror -Itests/host -Iinclude -c src/buttons.cpp -o "$out/buttons.o"
c++ -std=c++11 -Wall -Wextra -Werror -Itests/network -Iinclude -c src/midi.cpp -o "$out/midi.o"
mkdir -p .pio/display-review
for test in layout boot; do
  c++ -std=c++11 -Wall -Wextra -Werror -Iinclude -I"$lib/.." -c "tests/display/${test}_test.cpp" -o "$out/test.o"
  c++ "$strip_flag" "$out/test.o" "$out/buttons.o" "$out/midi.o" "$out/"u8*.o -o "$out/$test-test"
  "$out/$test-test" .pio/display-review
done
