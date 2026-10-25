#!/bin/bash
# Put UniXNU's NativeMetal plugin (the gpu-metal CI artifact: NativeAdrenoMetal.bundle, with Turnip's
# libvulkan_freedreno.dylib inside) into the negroni iOS 18 root image as
# /System/Library/Extensions/NativeAdrenoMetal.bundle, where Metal's discovery looks for the accelerator's
# MetalPluginName. Needs the kernel of patch 0129 booted with negroniadreno=22 negroniadreno_keep=1
# negronimetal=1. No publish marker: the plugin runs its own GPU test in the process that loads it and the
# render server keeps drawing on the CPU (stage 1). Then img2simg and flash userdata.
#
#   sudo negroni-ios/tools/install-metal-plugin.sh DIR [~/ios-work/ios18-root-full.img]   (DIR: the unpacked artifact)
set -euo pipefail
U=${SUDO_USER:-$USER}
H=$(getent passwd "$U" | cut -d: -f6)
art=${1:?artifact directory}
img=${2:-$H/ios-work/ios18-root-full.img}
mnt=/mnt/iosroot
bundle=$art/adreno-metal-plugin/NativeAdrenoMetal.bundle
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
[ -f "$bundle/NativeAdrenoMetal" ] || { echo "missing $bundle/NativeAdrenoMetal" >&2; exit 1; }
[ -f "$bundle/libvulkan_freedreno.dylib" ] || { echo "missing $bundle/libvulkan_freedreno.dylib" >&2; exit 1; }
lsmod | grep -q hfsplus || insmod "$H/ios-work/tools/wsl-kernel/fs/hfsplus/hfsplus.ko"
fsck.hfsplus -fy "$img" | grep -v 'Illegal name' || true
mkdir -p "$mnt"
mount -t hfsplus -o loop,force,rw "$img" "$mnt"
trap 'mountpoint -q "$mnt" && umount "$mnt"' EXIT
dest=$mnt/System/Library/Extensions/NativeAdrenoMetal.bundle
rm -rf "$dest"
cp -R "$bundle" "$dest"
chown -R 0:0 "$dest"
find "$dest" -type d -exec chmod 0755 {} +
find "$dest" -type f -exec chmod 0644 {} +
chmod 0755 "$dest/NativeAdrenoMetal" "$dest/libvulkan_freedreno.dylib"
# stage 1: no publish marker (the render server keeps its own renderer)
rm -f "$mnt/private/var/ngpu/addons/NativeAdrenoMetal.publish"
ls -la "$dest"
sync
umount "$mnt"
trap - EXIT
fsck.hfsplus -fy "$img" | grep -v 'Illegal name' || true
echo "done: img2simg $img ${img%.img}.simg, then flash userdata"
