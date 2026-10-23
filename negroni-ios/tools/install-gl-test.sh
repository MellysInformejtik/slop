#!/bin/bash
# Put Mesa (libOSMesa with freedreno, the gpu-mesa CI artifact) and gl_test into the negroni iOS root image, with a
# launchd job that runs gl_test once, 60 s after boot, its output on the kernel console (so in the persistent log
# next to the kernel's RedwoodGPU lines). The shell is Procursus's (/var/jb, add-sileo-root.sh): iOS has none. Needs the kernel of patch 0118 booted with negroniadreno=22
# negroniadreno_keep=1; without the GPU service Mesa draws with softpipe and says so.
#
#   sudo negroni-ios/tools/install-gl-test.sh DIR [~/ios-work/ios16-root-full.img]   (DIR: the unpacked artifact)
set -euo pipefail
U=${SUDO_USER:-$USER}
H=$(getent passwd "$U" | cut -d: -f6)
art=${1:?artifact directory}
img=${2:-$H/ios-work/ios16-root-full.img}
mnt=/mnt/iosroot
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
for f in libOSMesa.8.dylib gl_test; do [ -f "$art/$f" ] || { echo "missing $art/$f" >&2; exit 1; }; done
fsck.hfsplus -fy "$img" | grep -v 'Illegal name' || true
mkdir -p "$mnt"
mount -t hfsplus -o loop,force,rw "$img" "$mnt"
trap 'mountpoint -q "$mnt" && umount "$mnt"' EXIT
install -d -o 0 -g 0 -m 0755 "$mnt/redwood/lib" "$mnt/redwood/bin"
install -o 0 -g 0 -m 0755 "$art/libOSMesa.8.dylib" "$mnt/redwood/lib/libOSMesa.8.dylib"
install -o 0 -g 0 -m 0755 "$art/gl_test" "$mnt/redwood/bin/gl_test"
cat > "$mnt/redwood/bin/gl-test.sh" <<'SH'
#!/var/jb/bin/sh
export PATH=/var/jb/usr/bin:/var/jb/bin:/usr/bin:/bin
sleep 60
echo "NEGRONI GLTEST: start" > /dev/console
/redwood/bin/gl_test 2 > /dev/console 2>&1
echo "NEGRONI GLTEST: exit $?" > /dev/console
SH
chown 0:0 "$mnt/redwood/bin/gl-test.sh"; chmod 0755 "$mnt/redwood/bin/gl-test.sh"
L=$mnt/System/Library/xpc/launchd.plist
python3 - "$L" <<'PY'
import os, plistlib, sys
p = sys.argv[1]
st = os.stat(p)
d = plistlib.load(open(p, 'rb'))
d['LaunchDaemons']['/System/Library/LaunchDaemons/com.negroni.gltest.plist'] = {
    'Label': 'com.negroni.gltest',
    'ProgramArguments': ['/var/jb/bin/sh', '/redwood/bin/gl-test.sh'],
    'RunAtLoad': True,
    'UserName': 'root',
}
plistlib.dump(d, open(p, 'wb'), fmt=plistlib.FMT_BINARY)
os.chown(p, st.st_uid, st.st_gid); os.chmod(p, st.st_mode & 0o7777)
print('launchd.plist: com.negroni.gltest added')
PY
sync
umount "$mnt"
trap - EXIT
fsck.hfsplus -fy "$img" | grep -v 'Illegal name' || true
echo "done: img2simg $img ${img%.img}.simg, then flash userdata"
