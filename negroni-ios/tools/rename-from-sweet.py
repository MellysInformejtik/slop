#!/usr/bin/env python3
"""One-time import of the ios-sweet XNU patches (xnu/patches) into negroni-ios: sweet -> negroni.

Case-preserving rename in file names and contents (SWEET/Sweet/sweet -> NEGRONI/Negroni/negroni).
Stock xnu never contains "sweet", so only lines our patches add change; hunk line counts stay the same.
usage: rename-from-sweet.py SRC_PATCH_DIR DST_PATCH_DIR
"""
import pathlib
import sys

PAIRS = (('SWEET', 'NEGRONI'), ('Sweet', 'Negroni'), ('sweet', 'negroni'))


def rename(text):
    for a, b in PAIRS:
        text = text.replace(a, b)
    return text


src, dst = map(pathlib.Path, sys.argv[1:])
dst.mkdir(parents=True, exist_ok=True)
n = 0
for f in sorted(src.iterdir()):
    if f.suffix not in ('.patch', '.sh'):
        continue
    out = dst / rename(f.name)
    out.write_bytes(rename(f.read_text(errors='surrogateescape')).encode(errors='surrogateescape'))
    out.chmod(f.stat().st_mode)
    n += 1
print(f'{n} files -> {dst}')
