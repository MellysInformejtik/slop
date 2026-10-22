#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

firmware_dir="${1:-/home/paw/ios-work/negroni-firmware/gpu}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer NegroniGpuPm4.c test-pm4.c -o "$tmp/test-pm4"
"$tmp/test-pm4"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer NegroniGpuQueue.c test-queue.c -o "$tmp/test-queue"
"$tmp/test-queue"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer NegroniGpuFirmware.c test-firmware-parser.c \
  -o "$tmp/test-firmware"
"$tmp/test-firmware" "$firmware_dir"
