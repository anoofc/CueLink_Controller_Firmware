#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
scratch=$(mktemp -d "${TMPDIR:-/tmp}/cuelink-config.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -I.pio/libdeps/seeed_xiao_esp32s3/ArduinoJson/src -Itests/commands -Itests/network -Iinclude \
  src/command.cpp src/triggers.cpp src/midi.cpp src/commands.cpp src/configuration.cpp src/config_transfer.cpp src/config_manager.cpp \
  tests/config/config_test.cpp -o "$scratch/config"
"$scratch/config"

python3 tests/config/partitions_test.py
