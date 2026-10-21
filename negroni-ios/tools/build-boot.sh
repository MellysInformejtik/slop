#!/bin/bash
# negroni-ios boot image: kernel -> payload.bin (ADT + cmdline), FAT volume with the Mu-started loader
# (\EFI\BOOT\BOOTAA64.EFI = NegroniXnuBoot) and \negroni\payload.bin, then Mu + shim -> boot_a image.
#
# usage: negroni-ios/tools/build-boot.sh KERNEL [OUT_DIR]     (env CMDLINE overrides the default)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
kernel=$1
out=${2:-$HOME/ios-work/negroni-ios-out}
mu=${MU:-$HOME/ios-work/Mu-Silicium}
loader=$mu/Build/negroniPkg/DEBUG_CLANGPDB/AARCH64/NegroniXnuBoot.efi
py=${PY:-$HOME/ios-work/mu-venv/bin/python}
# Kernel window: bottom 3 GiB of the 0x840000000 bank (Mu "Kernel RAM"); the loader checks it is free RAM.
dram=${DRAM:-840000000:C0000000}
cmdline=${CMDLINE:-debug=0xc serial=3 cpus=1 keepsyms=1 -noprogress rd=ufsroot0 wdt=-1 negronikeys=1 negronitouch=0 negroniusb=0}

mkdir -p "$out"
python3 "$here/mkadt.py" "$out/negroni.adt" --board negroni --ufs --dram "$dram"
python3 "$here/mkpayload.py" "$kernel" "$out/negroni.adt" "$out/payload.bin" --cmdline "$cmdline"
[ -f "$loader" ] || { echo "no $loader: build Mu first (uefi/negroni/shim-mu.sh noflash)"; exit 1; }

rm -f "$out/boot-volume.img"
"$py" - "$out/boot-volume.img" "$loader" "$out/payload.bin" <<'PY'
import os, shutil, sys
from pyfatfs.PyFatFS import PyFatFS
from pyfatfs.PyFat import PyFat
img, loader, payload = sys.argv[1:]
size = ((os.path.getsize(payload) + os.path.getsize(loader)) // (1 << 20) + 16) << 20
open(img, 'wb').truncate(size)
pf = PyFat(); pf.mkfs(img, fat_type=PyFat.FAT_TYPE_FAT32, label='NEGRONIIOS', size=size); pf.close()
fs = PyFatFS(img)
fs.makedirs('/EFI/BOOT'); fs.makedirs('/negroni')
for src, dst in ((loader, '/EFI/BOOT/BOOTAA64.EFI'), (payload, '/negroni/payload.bin')):
    with open(src, 'rb') as a, fs.openbin(dst, 'w') as b:
        shutil.copyfileobj(a, b, 1 << 20)
fs.close()
chk = PyFatFS(img)
for src, dst in ((loader, '/EFI/BOOT/BOOTAA64.EFI'), (payload, '/negroni/payload.bin')):
    assert chk.readbytes(dst) == open(src, 'rb').read(), dst
print(f'{img}: {size >> 20} MiB, loader + payload verified')
PY
ESP="$out/boot-volume.img" "$repo/uefi/negroni/shim-mu.sh" noflash
cp "$repo/shim/out/negroni-uefi/shim-mu-debug-v4.img" "$out/boot_a.img"
echo "boot_a image: $out/boot_a.img"
