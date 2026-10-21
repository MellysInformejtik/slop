#!/bin/bash
# negroni-ios: write IMAGE to userdata (sda14) from OrangeFox in verified 512 MiB chunks: adb push to
# recovery RAM, dd at the chunk offset, sha256 of the written chunk against the local one. adb exec-in
# from WSL into Windows adb.exe is not byte-exact, and fastboot drops writes beyond ~8 GiB of userdata.
# usage: push-root.sh IMAGE [FIRST_CHUNK]
set -euo pipefail
img=$1 first=${2:-0}
A=/mnt/c/Users/paw/scoop/shims/adb.exe
C=512   # MiB
tmp=$(mktemp -d)
n=$(( ($(stat -c %s "$img") + (C << 20) - 1) / (C << 20) ))
"$A" shell 'mkdir -p /tmp/root-chunk' < /dev/null
for ((i = first; i < n; i++)); do
	dd if="$img" of="$tmp/c" bs=1M skip=$((i * C)) count=$C status=none
	want=$(sha256sum "$tmp/c" | cut -d' ' -f1)
	for try in 1 2 3; do
		"$A" push "$(wslpath -w "$tmp/c")" /tmp/root-chunk/c < /dev/null > /dev/null
		"$A" shell "dd if=/tmp/root-chunk/c of=/dev/block/sda14 bs=1048576 seek=$((i * C)) conv=notrunc,fsync 2>/dev/null; rm /tmp/root-chunk/c" < /dev/null
		got=$("$A" shell "dd if=/dev/block/sda14 bs=1048576 skip=$((i * C)) count=$C 2>/dev/null | sha256sum" < /dev/null | tr -d '\r' | cut -d' ' -f1)
		[ "$got" = "$want" ] && break
		echo "chunk $i try $try mismatch"
	done
	[ "$got" = "$want" ] || { echo "chunk $i FAILED"; exit 1; }
	echo "$(date +%T) chunk $((i + 1))/$n OK"
done
rm -rf "$tmp"
echo "userdata written and verified"
