#!/bin/bash
# A complete iOS 18.7.8 (22H352) root for the negroni userdata partition, from the iPhone XR (iPhone11,8)
# IPSW, booted with the iPhone X (D22) identity of tools/mkadt.py as Bergamota does (ry-diffusion/Bergamota,
# branch ios18). Every app and daemon Apple ships, Procursus + Sileo, setup skipped, the bring-up changes
# (tools/sweet-root-tweaks.py with the iOS 18 build). arm64e binaries are left as Apple signed them: the
# SM8450 cores have pointer authentication and our kernel leaves the keys off, so PAC instructions pass
# through (Bergamota's pacstrip rewrite is for the Poco F1's CPU, which has none). Needs the kernel patch
# negroni-ios/xnu/patches-ios18 and boot-args negroniarm64e=1. Output: OUT.img and OUT.simg.
#
#   negroni-ios/tools/fetch-ios18.sh   (once: ipsw download + AEA decryption into ~/ios-work/ios18/ipsw)
#   sudo negroni-ios/tools/build-ios18-root.sh [OUT]     (default ~/ios-work/ios18-root-full.img)
#
# Flash: fastboot -S 512M flash userdata OUT.simg   (replaces everything on userdata)
set -euo pipefail
U=${SUDO_USER:-$USER}
H=$(getent passwd "$U" | cut -d: -f6)
repo=$(cd "$(dirname "$0")/../.." && pwd)
dmgs=$H/ios-work/ios18/ipsw/22H352__iPhone11,8
out=${1:-$H/ios-work/ios18-root-full.img}
mnt=/mnt/iosroot
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
export PATH=$H/ios-work/tools/apfs-fuse/build:$PATH

# 1. the OS, SystemOS and AppOS images (BuildManifest 22H352 iPhone11,8: OS 094-31934-020,
#    Cryptex1,SystemOS 094-32062-020, Cryptex1,AppOS 094-32850-020), decrypted by fetch-ios18.sh
for d in 094-31934-020.dmg 094-32062-020.dmg 094-32850-020.dmg; do
	[ -s "$dmgs/$d" ] || { echo "missing $dmgs/$d (run negroni-ios/tools/fetch-ios18.sh)" >&2; exit 1; }
done

# 2. the whole system and its cryptexes into a 22 GiB HFS+ image (userdata is 24 GiB); an image left by an earlier run is reused
#    (its last read-only fsck may report a harmless "volume header needs minor repair")
if [ ! -e "$out" ]; then
	bash "$repo/tools/mkiosroot.sh" "$dmgs/094-31934-020.dmg" "$dmgs/094-32062-020.dmg" "$dmgs/094-32850-020.dmg" "$out" 22G || true
else
	echo "reusing $out"
fi
[ -f "$out" ] || { echo "no $out" >&2; exit 1; }

# 3. data-volume directories, the system keybag, and the boot changes (setup skipped, activation,
#    CommCenter jobs, migration marker, MediaToolbox, SpringBoard watchdogs)
fsck.hfsplus -fy "$out" | grep -v 'Illegal name' || true
mkdir -p "$mnt"
mount -t hfsplus -o loop,force,rw "$out" "$mnt"
trap 'mountpoint -q "$mnt" && umount "$mnt"' EXIT
db="$mnt/private/var/db"
mkdir -p "$db/diagnostics/timesync" "$db/uuidtext"
for d in diagnostics uuidtext diagnostics/timesync; do
	touch "$db/$d/.migrated"
	chown 272:272 "$db/$d" "$db/$d/.migrated"
	chmod 0644 "$db/$d/.migrated"
done
chmod 0751 "$db/diagnostics"
chmod 0755 "$db/uuidtext" "$db/diagnostics/timesync"
kb="$mnt/private/var/keybags"
mkdir -p "$kb"; chown 0:0 "$kb"; chmod 0700 "$kb"
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' > "$kb/systembag.kb"
chown 0:0 "$kb/systembag.kb"; chmod 0600 "$kb/systembag.kb"
NEGRONI_IOS_BUILD=22H352 NEGRONI_IOS_VERSION=18.7.8 python3 "$repo/tools/sweet-root-tweaks.py" "$mnt"
ls "$mnt/Applications" | grep -c '\.app$' | sed 's/^/apps in \/Applications: /'
df -h "$mnt" | tail -1
sync
umount "$mnt"
trap - EXIT
fsck.hfsplus -fy "$out" | grep -v 'Illegal name' || true

# 3b. the removable Apple apps (Calendar, Notes, Weather, Maps ...) from staged_system_apps into /Applications
bash "$repo/negroni-ios/tools/install-staged-apps.sh" "$out"

# 4. Procursus + Sileo (add-sileo-root.sh; Sileo is registered only once
#    /var/jb/var/negroni-no-uicache is deleted)
[ -n "${SKIP_SILEO:-}" ] || bash "$repo/negroni-ios/tools/add-sileo-root.sh" "$out"

# 5. sparse image for fastboot
img2simg "$out" "${out%.img}.simg"
chown "$U:$U" "$out" "${out%.img}.simg"
ls -la "$out" "${out%.img}.simg"
echo "done: fastboot -S 512M flash userdata ${out%.img}.simg"
