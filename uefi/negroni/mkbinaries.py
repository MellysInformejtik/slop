#!/usr/bin/env python3
"""Build Binaries/negroni from the uefiextract dump, mirroring Binaries/r0q INFs matched by FILE_GUID."""
import os, re, shutil, sys
from pathlib import Path
ref, dump, out, patched = map(Path, sys.argv[1:5])
mods = {}
for info in dump.rglob('info.txt'):
    txt = info.read_text(errors='replace')
    g = re.search(r'^File GUID: (\S+)', txt, re.M)
    if g and re.search(r'^Type: File', txt, re.M):
        mods[g.group(1).upper()] = info.parent
def body(d, kind):
    for s in d.iterdir():
        if s.is_dir() and kind in s.name: return (s / 'body.bin').read_bytes()
    for s in d.rglob('*'):
        if s.is_dir() and kind in s.name: return (s / 'body.bin').read_bytes()
missing, raw_missing = [], []
for inf in ref.rglob('*.inf'):
    text = inf.read_text()
    m = re.search(r'FILE_GUID\s*=\s*(\S+)', text)
    if not m: continue
    g = m.group(1).upper()
    rel = inf.relative_to(ref); dst = out / rel; dst.parent.mkdir(parents=True, exist_ok=True)
    if g not in mods: missing.append(str(rel)); continue
    d = mods[g]
    dst.write_text(text)
    for kind, fname in re.findall(r'^\s*(\w+)\|([^|]+)\|', text, re.M):
        name = Path(fname).name
        if kind == 'DXE_DEPEX': data = body(d, 'dependency section')
        elif kind in ('PE32', 'TE'):
            pf = patched / name
            data = pf.read_bytes() if pf.exists() else body(d, 'PE32 image section') or body(d, 'TE image section')
        elif kind == 'UI': data = body(d, 'UI section')
        elif kind == 'RAW': data = body(d, 'Raw section')
        else: data = None
        if data is None: print('no section', kind, rel); continue
        (dst.parent / name).write_bytes(data)
# raw files (configs, panels) by name
rawdir = out / 'RawFiles'; rawdir.mkdir(parents=True, exist_ok=True)
for d in mods.values():
    nm = d.name.split(' ', 1)[-1]
    if '.' in nm:
        b = body(d, 'Raw section') or (d / 'body.bin').read_bytes()
        (rawdir / nm).write_bytes(b)
print('missing (r0q-only):', *missing, sep='\n  ')
