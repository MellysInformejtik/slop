#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

firmware_dir="${1:-/home/paw/ios-work/negroni-firmware/gpu}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -Wno-multichar -DNG_ALLOCATION_TEST \
  -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
  NegroniGpuMemory.c NegroniGpuAllocation.c test-allocation.c -o "$tmp/test-allocation"
"$tmp/test-allocation"

cc -std=c11 -Wall -Wextra -Werror -Wno-multichar -DNG_OBJECT_TEST \
  -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
  NegroniGpuObjects.c test-objects.c -o "$tmp/test-objects"
"$tmp/test-objects"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer NegroniGpuAbi.c test-abi.c -o "$tmp/test-abi"
"$tmp/test-abi"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer NegroniGpuPm4.c test-pm4.c -o "$tmp/test-pm4"
"$tmp/test-pm4"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer NegroniGpuQueue.c test-queue.c -o "$tmp/test-queue"
"$tmp/test-queue"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer NegroniGpuPageTable.c test-pagetable.c -o "$tmp/test-pagetable"
"$tmp/test-pagetable"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer NegroniGpuFirmware.c test-firmware-parser.c \
  -o "$tmp/test-firmware"
"$tmp/test-firmware" "$firmware_dir"
