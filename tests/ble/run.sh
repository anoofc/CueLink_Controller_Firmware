#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
scratch=$(mktemp -d "${TMPDIR:-/tmp}/cuelink-ble.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -Iinclude tests/ble/protocol_test.cpp -o "$scratch/protocol"
"$scratch/protocol" > "$scratch/vectors.json"
python3 - "$scratch/vectors.json" <<'PY'
import json, sys
from pathlib import Path
assert json.loads(Path(sys.argv[1]).read_text()) == json.loads(Path('docs/ble_protocol_vectors.json').read_text())
print('BLE encoder matches shared vectors; discovery timeout and rollover checks passed.')
PY
