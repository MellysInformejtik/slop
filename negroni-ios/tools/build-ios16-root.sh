#!/bin/bash
# A fresh, complete iOS 16.7.16 root for the negroni userdata partition, from the iPhone X IPSW:
# every app and daemon Apple ships (no debloat), Procursus + Sileo, the setup assistant skipped and the
# bring-up changes that let it boot (tools/sweet-root-tweaks.py). Output: OUT.img and OUT.simg.
#
#   sudo negroni-ios/tools/build-ios16-root.sh [OUT]     (default ~/ios-work/ios16-root-full.img)
#
# Flash: fastboot -S 512M flash userdata OUT.simg   (replaces everything on userdata)
set -euo pipefail
U=${SUDO_USER:-$USER}
H=$(getent passwd "$U" | cut -d: -f6)
repo=$(cd "$(dirname "$0")/../.." && pwd)
ipsw=$H/ios-work/iPhone10,3_16.7.16_20H392_Restore.ipsw
dmgs=$H/ios-work/ipsw/20H392
out=${1:-$H/ios-work/ios16-root-full.img}
mnt=/mnt/iosroot
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
[ -f "$ipsw" ] || { echo "missing $ipsw" >&2; exit 1; }
[ ! -e "$out" ] || { echo "refusing to overwrite $out" >&2; exit 1; }
export PATH=$H/ios-work/tools/apfs-fuse/build:$PATH

# 1. the OS, SystemOS and AppOS images (BuildManifest: OS 087-86698-044, Cryptex1,SystemOS
#    087-86593-047, Cryptex1,AppOS 087-86715-047)
mkdir -p "$dmgs"
for d in 087-86698-044.dmg 087-86593-047.dmg 087-86715-047.dmg; do
	[ -s "$dmgs/$d" ] || unzip -o -q "$ipsw" "$d" -d "$dmgs"
done

# 2. the whole system and its cryptexes into a 16 GiB HFS+ image
bash "$repo/tools/mkiosroot.sh" "$dmgs/087-86698-044.dmg" "$dmgs/087-86593-047.dmg" "$dmgs/087-86715-047.dmg" "$out" 16G

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
python3 "$repo/tools/sweet-root-tweaks.py" "$mnt"
ls "$mnt/Applications" | grep -c '\.app$' | sed 's/^/apps in \/Applications: /'
df -h "$mnt" | tail -1
sync
umount "$mnt"
trap - EXIT
fsck.hfsplus -fy "$out" | grep -v 'Illegal name' || true

# 4. Procursus + Sileo (add-sileo-root.sh; Sileo is registered only once
#    /var/jb/var/negroni-no-uicache is deleted)
bash "$repo/negroni-ios/tools/add-sileo-root.sh" "$out"

# 5. sparse image for fastboot
img2simg "$out" "${out%.img}.simg"
chown "$U:$U" "$out" "${out%.img}.simg"
ls -la "$out" "${out%.img}.simg"
echo "done: fastboot -S 512M flash userdata ${out%.img}.simg"
