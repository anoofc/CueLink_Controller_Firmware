#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
scratch=$(mktemp -d "${TMPDIR:-/tmp}/cuelink-network.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
# Isolate the actual production source from local credentials. The second build
# has no secrets header or credential macros, exercising the missing-file path.
cp src/network.cpp include/network.h include/serial_log.h "$scratch/"
c++ -std=c++11 -Wall -Wextra -Werror -Itests/network -I"$scratch" \
  -DCUELINK_WIFI_SSID='"test-network"' \
  "$scratch/network.cpp" tests/network/network_test.cpp -o "$scratch/configured"
"$scratch/configured"
c++ -std=c++11 -Wall -Wextra -Werror -Itests/network -I"$scratch" \
  -DTEST_UNCONFIGURED \
  "$scratch/network.cpp" tests/network/network_test.cpp -o "$scratch/unconfigured"
"$scratch/unconfigured"
