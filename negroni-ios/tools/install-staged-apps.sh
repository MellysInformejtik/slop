#!/bin/bash
# iOS 16 ships Calendar, Notes, Weather, Maps, Mail, Health ... in /private/var/staged_system_apps; a real phone
# installs them during first-boot migration, which our root skips. Move them into /Applications (system apps)
# and drop their /System/Library/AppPlaceholders stubs.
#
#   sudo negroni-ios/tools/install-staged-apps.sh [~/ios-work/ios16-root-full.img]
set -euo pipefail
U=${SUDO_USER:-$USER}
H=$(getent passwd "$U" | cut -d: -f6)
img=${1:-$H/ios-work/ios16-root-full.img}
mnt=/mnt/iosroot
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
fsck.hfsplus -fy "$img" | grep -v 'Illegal name' || true
mkdir -p "$mnt"
mount -t hfsplus -o loop,force,rw "$img" "$mnt"
trap 'mountpoint -q "$mnt" && umount "$mnt"' EXIT
n=0
for app in "$mnt"/private/var/staged_system_apps/*.app; do
	[ -d "$app" ] || continue
	name=$(basename "$app")
	rm -rf "$mnt/Applications/$name" "$mnt/System/Library/AppPlaceholders/$name"
	mv "$app" "$mnt/Applications/$name"
	n=$((n + 1))
done
echo "moved $n apps; /Applications has $(ls "$mnt/Applications" | grep -c '\.app$')"
sync
umount "$mnt"
trap - EXIT
fsck.hfsplus -fy "$img" | grep -v 'Illegal name' || true
