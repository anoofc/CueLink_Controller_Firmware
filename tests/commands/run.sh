#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
scratch=$(mktemp -d "${TMPDIR:-/tmp}/cuelink-commands.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -Iinclude \
  src/command.cpp tests/commands/encoding_test.cpp -o "$scratch/encoding"
"$scratch/encoding"
c++ -std=c++11 -Wall -Wextra -Werror -Iinclude \
  src/command.cpp src/udp_transport.cpp tests/commands/transport_test.cpp -o "$scratch/transport"
"$scratch/transport"
c++ -std=c++11 -Wall -Wextra -Werror -Itests/commands -Itests/network -Iinclude \
  src/buttons.cpp src/command.cpp src/triggers.cpp src/midi.cpp src/commands.cpp tests/commands/runtime_test.cpp -o "$scratch/runtime"
"$scratch/runtime"
python3 tests/commands/receiver_test.py
