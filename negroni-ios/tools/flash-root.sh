#!/bin/bash
# negroni-ios: write the iOS root image to userdata from OrangeFox (adb exec-in dd), not by fastboot:
# the ABL's sparse flash drops writes beyond ~8 GiB into userdata. Then restore the negroni-ios boot setup.
#   1 fastboot: stock boot/vendor_boot/dtbo/vbmeta on slot a (OrangeFox needs them), reboot recovery
#   2 recovery: stream IMAGE to /dev/block/sda14, verify sha256 of the written range
#   3 fastboot: negroni vendor_boot, dtbo erased, vbmeta verification off, BOOT_IMG on boot_a, reboot
# usage: negroni-ios/tools/flash-root.sh IMAGE BOOT_IMG
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
img=$1 boot=$2
F=/mnt/c/Users/paw/scoop/shims/fastboot.exe A=/mnt/c/Users/paw/scoop/shims/adb.exe
B=$repo/dtb/negroni/backup
w() { wslpath -w "$1"; }
size=$(stat -c %s "$img")
want=$(sha256sum "$img" | cut -d' ' -f1)
echo "== $(date +%T) image $img $size bytes sha256 $want"

echo "== $(date +%T) waiting for fastboot"
for p in boot_a vendor_boot_a dtbo_a vbmeta_a; do "$F" flash $p "$(w $B/$p.img)" 2>&1 | tr -d '\r' | grep -E "Writing|FAIL"; done
"$F" set_active a; "$F" reboot recovery

echo "== $(date +%T) waiting for recovery"
until "$A" devices 2>/dev/null | tr -d '\r' | grep -q "recovery$"; do sleep 3; done
"$A" shell 'umount /dev/block/sda14 2>/dev/null; true' < /dev/null
echo "== $(date +%T) writing userdata ($((size >> 20)) MiB)"
"$A" exec-in "dd of=/dev/block/sda14 bs=4194304" < "$img"
"$A" shell sync < /dev/null
echo "== $(date +%T) verifying"
got=$("$A" shell "head -c $size /dev/block/sda14 | sha256sum" < /dev/null | tr -d '\r' | cut -d' ' -f1)
echo "phone sha256 $got"
[ "$got" = "$want" ] || { echo "== MISMATCH: userdata not written correctly; slot a is still stock (OrangeFox)"; exit 1; }
echo "== $(date +%T) userdata OK"

"$A" reboot bootloader < /dev/null
echo "== $(date +%T) waiting for fastboot"
"$F" flash vendor_boot_a "$(w $repo/shim/out/negroni-a/vendor_boot.img)" 2>&1 | tr -d '\r' | grep -E "Writing|FAIL"
"$F" erase dtbo_a 2>&1 | tr -d '\r' | grep -E "Erasing|FAIL"
"$F" --disable-verity --disable-verification flash vbmeta_a "$(w $B/vbmeta_a.img)" 2>&1 | tr -d '\r' | grep -E "Writing|FAIL"
"$F" flash boot_a "$(w "$boot")" 2>&1 | tr -d '\r' | grep -E "Writing|FAIL"
"$F" set_active a; "$F" reboot
echo "== $(date +%T) done"
