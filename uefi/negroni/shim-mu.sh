#!/bin/bash
# Build Mu-Silicium negroni DEBUG, wrap the FD in the ios-sweet shim (ABL runs the shim; it copies the FD to
# 0xA7100000 and jumps), flash boot_a, set slot a active and reboot. Usage: uefi/negroni/shim-mu.sh [noflash]
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
mu=${MU:-$HOME/ios-work/Mu-Silicium}
F=/mnt/c/Users/paw/scoop/shims/fastboot.exe
# shared touch headers (drivers/negroni-touch) into NegroniTouchDxe
if [ "${SYNC_TOUCH_HEADERS:-1}" = 1 ]; then
  mkdir -p "$mu/Platforms/OnePlus/negroniPkg/Drivers/NegroniTouchDxe"
  cp "$repo"/drivers/negroni-touch/*.h "$mu/Platforms/OnePlus/negroniPkg/Drivers/NegroniTouchDxe/"
fi
(cd "$mu" && source ~/ios-work/mu-venv/bin/activate && python3 build_uefi.py -d negroni -r DEBUG > "$mu/../negroni-uefi/build-debug.log" 2>&1) \
	|| { tail -30 "$mu/../negroni-uefi/build-debug.log"; exit 1; }
fd=$mu/Build/negroniPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd
BOARD=negroni DTB=$repo/dtb/negroni/sm8450-oneplus-negroni.dtb "$repo/shim/build.sh" "$fd" > /dev/null
rd=$(mktemp); cp "$fd" "$rd"
if [ -n "${ESP:-}" ]; then
	# UniXNU payload FAT volume right after the FV, at the 1 MiB boundary NegroniBootMemoryDxe expects
	fvlen=$(stat -c %s "$fd"); truncate -s $(( (fvlen + 0xfffff) & ~0xfffff )) "$rd"; cat "$ESP" >> "$rd"
else
	truncate -s 16M "$rd"	# ABL hangs on small ramdisks
fi
out=$repo/shim/out/negroni-uefi/shim-mu-debug-v4.img
mkbootimg --kernel "$repo/shim/out/Image" --ramdisk "$rd" --header_version 4 --pagesize 4096 \
	--os_version 12.0.0 --os_patch_level 2026-03-00 --cmdline bootconfig -o "$out"
rm -f "$rd"
echo "$out"
[ "${1:-}" = noflash ] && exit 0
"$F" flash boot_a "$(wslpath -w "$out")"
"$F" set_active a
"$F" reboot
