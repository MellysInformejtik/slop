#!/usr/bin/env python3
"""Build the negroni firmware area (kernel bsd/dev/negroni_ufs.c: negroni_fw_load).

  mkfwarea.py OUT.bin FILE [FILE ...]

8 MiB: block 0 = "NEGRONFW" | u32 1 | u32 count | 16 x {name[48], u32 offset, u32 size, sha256[32]},
files from 4 KiB on, each 4 KiB aligned, named by their basename. It is written with dd to the 2048
blocks right before the persistent log area at the end of userdata (block = log block - 2048).
Vendor firmware: keep OUT.bin out of git.
"""
import hashlib, os, struct, sys

SIZE, BLOCK, MAX = 8 << 20, 4096, 16
out, files = sys.argv[1], sys.argv[2:]
if not files or len(files) > MAX:
    sys.exit(__doc__)
area = bytearray(SIZE)
entries, at = [], BLOCK
for path in files:
    data = open(path, 'rb').read()
    name = os.path.basename(path).encode()
    if len(name) > 47 or at + len(data) > SIZE:
        sys.exit(f'{path}: name too long or area full')
    area[at:at + len(data)] = data
    entries.append(struct.pack('<48sII32s', name, at, len(data), hashlib.sha256(data).digest()))
    print(f'{name.decode():24} {len(data):8} bytes at {at:#x} {hashlib.sha256(data).hexdigest()[:16]}')
    at = (at + len(data) + BLOCK - 1) & ~(BLOCK - 1)
header = b'NEGRONFW' + struct.pack('<II', 1, len(entries)) + b''.join(entries)
area[:len(header)] = header
open(out, 'wb').write(area)
print(f'{out}: {len(entries)} files, {at} of {SIZE} bytes used')
