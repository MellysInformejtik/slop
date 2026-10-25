#!/usr/bin/env python3
"""UniXNU payload volume (FAT) for an negroni-ios kernel, booted by DarwinBoot.

usage: mkdarwinboot.py KERNEL ADT OUT.img --cmdline "..." [--size-mib 64] [--timeout 3]

DarwinBoot copies the kernel's segments as they are in the file, so the chained
rebases that mkpayload.py applies at payload time are applied here, to the file.
The ADT must carry /chosen/darwinboot-memory-size (mkadt.py --darwinboot) and a
dram-base where DarwinBoot places the window (negroni: the Mu "Kernel RAM" region).
"""
import os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkpayload import parse_macho, apply_chained_fixups

TOML = '''version = 1

[boot]
timeout = {timeout}
default = "ios16"

[ui]
title = "negroni-ios"

[[entry]]
id = "ios16"
title = "iOS 18 (negroni-ios XNU)"
type = "xnu"
# UEFI FAT paths: backslashes (DarwinBoot passes the path through unchanged)
path = '\Payloads\kernel.macho'
args = "{cmdline}"
'''


def main():
    a = sys.argv[1:]
    def opt(name, default=None):
        if name in a:
            i = a.index(name); v = a[i + 1]; del a[i:i + 2]; return v
        return default
    cmdline = opt('--cmdline', '')
    size = int(opt('--size-mib', '64'))
    timeout = int(opt('--timeout', '3'))
    kernel, adt, out = a
    buf = bytearray(open(kernel, 'rb').read())
    n = apply_chained_fixups(buf, parse_macho(buf))
    d = tempfile.mkdtemp()
    try:
        os.makedirs(f'{d}/EFI/UniXNU'); os.makedirs(f'{d}/Payloads')
        open(f'{d}/Payloads/kernel.macho', 'wb').write(buf)
        shutil.copy(adt, f'{d}/Payloads/kernel.macho.adt')
        open(f'{d}/EFI/UniXNU/unixnu.toml', 'w').write(TOML.format(timeout=timeout, cmdline=cmdline.replace('"', '\\"')))
        if os.path.exists(out): os.remove(out)
        with open(out, 'wb') as f:
            f.truncate(size << 20)
        # pyfatfs (pip) instead of mkfs.vfat/mtools, which the build host may lack
        from pyfatfs.PyFat import PyFat
        from pyfatfs.PyFatFS import PyFatFS
        pf = PyFat(); pf.mkfs(out, fat_type=PyFat.FAT_TYPE_FAT32, label='NEGRONIESP', size=size << 20); pf.close()
        fs = PyFatFS(out)
        for root, dirs, files in os.walk(d):
            rel = os.path.relpath(root, d).replace(os.sep, '/')
            rel = '' if rel == '.' else rel
            for dn in dirs:
                fs.makedir(f'/{rel}/{dn}'.replace('//', '/'))
            for fn in files:
                with open(os.path.join(root, fn), 'rb') as src, fs.openbin(f'/{rel}/{fn}'.replace('//', '/'), 'w') as dst:
                    shutil.copyfileobj(src, dst)
        fs.close()
    finally:
        shutil.rmtree(d)
    print(f'{out}: {n} rebases applied, {size} MiB FAT')


if __name__ == '__main__':
    main()
