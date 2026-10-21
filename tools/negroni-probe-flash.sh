#!/bin/sh
# negroni UniXNU boot probe: payload-small.img with kernel.macho replaced by a PSCI-reset probe at SYMBOL,
# then shim-mu.sh flashes boot_a. Reboot = SYMBOL reached; hang = not reached.
# usage: tools/negroni-probe-flash.sh SYMBOL
set -e
repo=$(cd "$(dirname "$0")/.." && pwd)
o=$HOME/ios-work/unixnu-out
python3 "$repo/tools/probe-reset.py" "$o/kernel/kernel.macho" "$1" "$o/kernel-probe.macho"
cp "$o/payload-small.img" "$o/payload-probe.img"
"$HOME/ios-work/mu-venv/bin/python" - "$o/payload-probe.img" "$o/kernel-probe.macho" <<'PY' 2>&1 | grep -v -i 'warning\|pkg_resources'
import sys, hashlib
from pyfatfs.PyFatFS import PyFatFS
img, k = sys.argv[1:]
data = open(k, 'rb').read()
fs = PyFatFS(img)
with fs.openbin('/EFI/UniXNU/Payloads/kernel.macho', 'r+') as f:   # same size: overwrite in place, no reallocation
    f.write(data)
fs.close()
got = PyFatFS(img).readbytes('/EFI/UniXNU/Payloads/kernel.macho')
assert got == data, 'payload kernel readback differs'
print('probe kernel in payload', hashlib.sha256(got).hexdigest()[:16])
PY
cd "$repo" && ESP="$o/payload-probe.img" uefi/negroni/shim-mu.sh
