#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
scratch=$(mktemp -d "${TMPDIR:-/tmp}/cuelink-midi.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -Itests/network -Iinclude src/midi.cpp tests/midi/midi_test.cpp -o "$scratch/midi"
"$scratch/midi"
