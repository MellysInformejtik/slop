#!/bin/bash
# Build native HarmonyOS NEXT boot images + userdata for OnePlus SM8450 "negroni".
# Output in $OUT: boot-native.img, logview.img, vendor_boot.img, vbmeta.img, userdata.img
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
TOOLS=$HERE/../tools
KSRC=${KSRC:-$HOME/hmos-work/src/linux}
KOUT=$KSRC/output
FW=${FW:-$HOME/hmos-work/fw/pura70-6.1.0.135/img}
OUT=${OUT:-$HOME/hmos-work/out/native}
USERDATA_SIZE=${USERDATA_SIZE:-24G}
DTB=$KOUT/arch/arm64/boot/dts/qcom/sm8450-oneplus-negroni.dtb
BASE_CMDLINE="console=tty0 printk.devkmsg=on bootconfig"
BOOT_CMDLINE="$BASE_CMDLINE loglevel=7 enforcing=0 ohos.boot.hardware=Kirin9010"
LOG_CMDLINE="$BASE_CMDLINE loglevel=1 hmos.mode=log"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

mkdir -p "$OUT/ramdisk"

echo "== kernel"
make -s -C "$KSRC" O="$KOUT" defconfig sm8450.config ohos.config
make -s -C "$KSRC" O="$KOUT" -j"$(nproc)" Image.gz qcom/sm8450-oneplus-negroni.dtb
make -s -C "$KSRC" O="$KOUT" headers_install INSTALL_HDR_PATH="$OUT/uapi"

echo "== first stage"
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Wno-unused-result \
	-I"$OUT/uapi/include" -o "$OUT/ramdisk/init" "$HERE/firststage.c"

echo "== ramdisk"
gcc -O2 -o "$OUT/gen_init_cpio" "$KSRC/usr/gen_init_cpio.c"
cat > "$OUT/initramfs.list" <<EOF
dir /dev 0755 0 0
dir /proc 0755 0 0
dir /sys 0755 0 0
dir /ud 0755 0 0
dir /usr 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/null 0666 0 0 c 1 3
file /init $OUT/ramdisk/init 0755 0 0
EOF
"$OUT/gen_init_cpio" "$OUT/initramfs.list" | gzip -9 > "$OUT/ramdisk.cpio.gz"

echo "== boot images"
python3 -I "$TOOLS/mkvbmeta.py" "$OUT/vbmeta.img" 2
mkbootimg --header_version 4 --pagesize 4096 \
	--vendor_boot "$OUT/vendor_boot.img" --dtb "$DTB"
for v in "boot-native:$BOOT_CMDLINE" "logview:$LOG_CMDLINE"; do
	mkbootimg --header_version 4 --pagesize 4096 \
		--kernel "$KOUT/arch/arm64/boot/Image.gz" --ramdisk "$OUT/ramdisk.cpio.gz" \
		--os_version 12.0.0 --os_patch_level 2026-03-00 \
		--cmdline "${v#*:}" -o "$OUT/${v%%:*}.img"
done

echo "== userdata"
UD=$OUT/userdata-root
rm -rf "$UD"
mkdir -p "$UD/hmos/log" "$UD/hmos/override/vendor/etc" "$UD/hmos/override/system/etc/selinux"
# hard links: no 8 GB copy
ln "$FW/system.full.img" "$UD/hmos/system.img"
for p in vendor sys_prod chip_prod cust patch; do
	[ -f "$FW/$p.img" ] && ln "$FW/$p.img" "$UD/hmos/$p.img"
done
# second stage mounts /vendor/etc/fstab.${ohos.boot.hardware}; NEXT's own /data is hmfs
# no /data entry: the first stage bind-mounts /hmos/data there (userdata holds the images)
: > "$UD/hmos/override/vendor/etc/fstab.Kirin9010"
mkdir -p "$UD/hmos/data"
sed 's/^SELINUX=.*/SELINUX=permissive/' \
	"$HOME/hmos-work/fw/pura70-6.1.0.135/root/system/system/etc/selinux/config" \
	> "$UD/hmos/override/system/etc/selinux/config"
rm -f "$OUT/userdata.raw"
mke2fs -q -t ext4 -L userdata -O encrypt -E root_owner=0:0 -d "$UD" "$OUT/userdata.raw" "$USERDATA_SIZE"
img2simg "$OUT/userdata.raw" "$OUT/userdata.img"
rm -f "$OUT/userdata.raw"

ls -la "$OUT"/*.img
