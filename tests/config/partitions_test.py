"""Verify M5 configuration storage fits without moving existing NVS/apps."""
import csv
from pathlib import Path
rows = {}
for row in csv.reader(line for line in Path('partitions_cuelink.csv').read_text().splitlines() if not line.startswith('#')):
    name, kind, subtype, offset, size, *_ = (s.strip() for s in row)
    rows[name] = (int(offset, 0), int(size, 0))
assert rows['nvs'] == (0x9000, 0x5000)
assert rows['app0'] == (0x10000, 0x330000)
assert rows['app1'] == (0x340000, 0x330000)
assert rows['cuelinkcfg'] == (0x7e0000, 0x10000)
assert rows['spiffs'][0] == 0x670000
previous_end = 0
for offset, size in sorted(rows.values()):
    assert offset >= previous_end
    previous_end = offset + size
assert previous_end == 8 * 1024 * 1024
# Three blob generations during NVS replacement, page/entry overhead, spare GC.
assert 3 * (12288 + 63 + 32 + 1024) + 8192 < rows['cuelinkcfg'][1]
print('Dedicated NVS capacity and preserved application/default-NVS offsets passed.')
