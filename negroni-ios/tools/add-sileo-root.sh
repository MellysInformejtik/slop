#!/bin/bash
# Put Procursus (rootless 1800) + Sileo into the negroni iOS 16 root IMAGE before it is pushed to userdata
# (OrangeFox has no hfsplus, so the root cannot be edited on the phone). Layout as a rootless jailbreak:
# /private/preboot/procursus with /private/var/jb -> it (LaunchServices registers apps from there, not from
# /private/var). A launchd job com.negroni.jbprep runs the bootstrap's
# preparation once and registers Sileo.app with uicache at every boot. Also drops the bluetoothd and
# fairplayd jobs, which crash-loop here (no Bluetooth, no SEP).
#
#   sudo negroni-ios/tools/add-sileo-root.sh [~/ios-work/ios16-root.img]
#
# Inputs, not in git: ~/ios-work/jb/bootstrap.tar, ~/ios-work/jb/sileo/{data,control}.tar
set -euo pipefail
U=${SUDO_USER:-$USER}
H=$(getent passwd "$U" | cut -d: -f6)
img=${1:-$H/ios-work/ios16-root.img}
J=$H/ios-work/jb
mnt=/mnt/iosroot
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
for f in "$img" "$J/bootstrap.tar" "$J/sileo/data.tar" "$J/sileo/control.tar"; do
	[ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

fsck.hfsplus -fy "$img" | grep -v 'Illegal name' || true
mkdir -p "$mnt"
mount -t hfsplus -o loop,force,rw "$img" "$mnt"
trap 'mountpoint -q "$mnt" && umount "$mnt"' EXIT
test -d "$mnt/System/Library/xpc"
df -h "$mnt" | tail -1

P=$mnt/private/preboot/procursus
rm -rf "$P.new"; mkdir -p "$P.new"
tar -C "$P.new" -xpf "$J/bootstrap.tar" ./var/jb
tar -C "$P.new" -xpf "$J/sileo/data.tar" ./var/jb
[ -x "$P.new/var/jb/Applications/Sileo.app/Sileo" ] || { echo "no Sileo.app" >&2; exit 1; }
chown 0:0 "$P.new/var/jb/Applications/Sileo.app/giveMeRoot"
chmod 4755 "$P.new/var/jb/Applications/Sileo.app/giveMeRoot"
rm -rf "$P"; mv "$P.new/var/jb" "$P"; rm -rf "$P.new"
rm -f "$mnt/private/var/jb"; ln -s /private/preboot/procursus "$mnt/private/var/jb"
for d in var/mobile var/mobile/Library var/mobile/Library/Caches var/mobile/Library/Preferences \
	var/mobile/Documents var/lib/apt/sileolists var/lib/apt/sileolists/partial var/log; do
	mkdir -p "$P/$d"
done
chown -R 501:501 "$P/var/mobile" "$P/var/lib/apt/sileolists"

# dpkg knows Sileo is installed (else Sileo offers to install itself)
{ tar -xOf "$J/sileo/control.tar" ./control | sed '/^$/d'; printf 'Status: install ok installed\n\n'; } \
	>> "$P/Library/dpkg/status"

cat > "$P/negroni-jb.sh" <<'EOF'
#!/var/jb/bin/sh
# com.negroni.jbprep: bootstrap preparation once, then register the prefix's applications.
export PATH=/var/jb/usr/sbin:/var/jb/usr/bin:/var/jb/sbin:/var/jb/bin:/usr/sbin:/usr/bin:/sbin:/bin
if [ -e /var/jb/prep_bootstrap.sh ]; then
	NO_PASSWORD_PROMPT=1 /var/jb/bin/sh /var/jb/prep_bootstrap.sh
fi
/var/jb/usr/sbin/pwd_mkdb -p /var/jb/etc/master.passwd >/dev/null 2>&1
sleep 40                                    # LaunchServices is not ready right after SpringBoard
/var/jb/usr/bin/uicache -p /var/jb/Applications/Sileo.app
EOF
chown 0:0 "$P/negroni-jb.sh"; chmod 0755 "$P/negroni-jb.sh"

L=$mnt/System/Library/xpc/launchd.plist
[ -f "$L.before-sileo" ] || cp -p "$L" "$L.before-sileo"
python3 - "$L" <<'EOF'
import os, plistlib, sys
p = sys.argv[1]
st = os.stat(p)
d = plistlib.load(open(p, 'rb'))
jobs = d['LaunchDaemons']
jobs['/System/Library/LaunchDaemons/com.negroni.jbprep.plist'] = {
    'Label': 'com.negroni.jbprep',
    'ProgramArguments': ['/var/jb/bin/sh', '/var/jb/negroni-jb.sh'],
    'RunAtLoad': True,
    'UserName': 'root',
    'StandardOutPath': '/var/jb/var/log/negroni-jb.log',
    'StandardErrorPath': '/var/jb/var/log/negroni-jb.log',
}
gone = [k for k, j in jobs.items() if j.get('Label') in ('com.apple.bluetoothd', 'com.apple.fairplayd.H2')]
for k in gone:
    del jobs[k]
plistlib.dump(d, open(p, 'wb'), fmt=plistlib.FMT_BINARY)
os.chown(p, st.st_uid, st.st_gid); os.chmod(p, st.st_mode & 0o7777)
print('launchd.plist: com.negroni.jbprep added;', len(gone), 'crash-looping jobs removed;', len(jobs), 'jobs')
EOF
df -h "$mnt" | tail -1
sync
umount "$mnt"
trap - EXIT
fsck.hfsplus -fy "$img" | grep -v 'Illegal name' || true
echo "done: $img has Procursus + Sileo; push it with negroni-ios/tools/push-root.sh"
