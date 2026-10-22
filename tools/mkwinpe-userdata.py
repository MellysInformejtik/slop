#!/usr/bin/env python3
"""Pack a WinPE tree (efi/, boot/, sources/boot.wim, ...) into a FAT32 image for the userdata partition.

  mkwinpe-userdata.py SRC_DIR OUT.img [--size-mib 1024]

Mu boots \\efi\\boot\\bootaa64.efi from it. Rollback copies (*-before-*) and _touch_mount are skipped.
Every file is read back from the image and compared by SHA-256. Then: img2simg OUT.img OUT.simg
"""
import hashlib, os, sys
from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS

a = sys.argv[1:]
size = 1024
if '--size-mib' in a:
    i = a.index('--size-mib'); size = int(a[i + 1]); del a[i:i + 2]
src, out = a

def skip(rel):
    return '-before-' in rel or rel.split('/')[0] == '_touch_mount'

files = []
for root, dirs, names in os.walk(src):
    for n in names:
        p = os.path.join(root, n)
        rel = os.path.relpath(p, src).replace(os.sep, '/')
        if not skip(rel):
            files.append(rel)
total = sum(os.path.getsize(os.path.join(src, r)) for r in files)
if total > (size - 64) << 20:
    sys.exit(f'{total >> 20} MiB does not fit in {size} MiB')

if os.path.exists(out):
    os.remove(out)
with open(out, 'wb') as f:
    f.truncate(size << 20)
pf = PyFat(); pf.mkfs(out, fat_type=PyFat.FAT_TYPE_FAT32, label='WINPE', size=size << 20); pf.close()
fs = PyFatFS(out)
sums = {}
for rel in sorted(files):
    d = os.path.dirname(rel)
    if d:
        fs.makedirs('/' + d, recreate=True)
    h = hashlib.sha256()
    with open(os.path.join(src, rel), 'rb') as f, fs.openbin('/' + rel, 'w') as o:
        while chunk := f.read(1 << 20):
            h.update(chunk); o.write(chunk)
    sums[rel] = h.hexdigest()
fs.close()

fs = PyFatFS(out)
for rel, want in sums.items():
    h = hashlib.sha256()
    with fs.openbin('/' + rel, 'r') as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    if h.hexdigest() != want:
        sys.exit('read-back mismatch: ' + rel)
fs.close()
print(f'{out}: {len(files)} files, {total >> 20} MiB, all read back OK')
for rel in ('efi/boot/bootaa64.efi', 'sources/boot.wim', 'efi/microsoft/boot/bcd'):
    if rel in sums:
        print(f'  {rel} {sums[rel][:16]}')
