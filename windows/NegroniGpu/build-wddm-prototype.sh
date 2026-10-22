#!/bin/bash
# Compile-only WDDM 2.7 render-only resource-map prototype for ARM64.
# Incomplete DRIVER_INITIALIZATION_DATA; never package or install this binary.
set -euo pipefail
cd "$(dirname "$0")"
K="/mnt/c/Program Files (x86)/Windows Kits/10"; V=10.0.28000.0
I="$K/Include/$V"; L="$K/Lib/$V/km/arm64"
mkdir -p out
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
  -D_WIN32_WINNT=0x0A00 -DDXGKDDI_INTERFACE_VERSION=0xC004 \
  /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
  /Foout/NegroniGpuWddm.obj NegroniGpuWddm.c 2>out/wddm-compile.log
lld-link /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry /MACHINE:ARM64 \
  /NODEFAULTLIB /INTEGRITYCHECK /BASE:0x1C2000000 /TSAWARE:NO /STACK:0x40000 \
  /OUT:out/NegroniGpuWddm.sys out/NegroniGpuWddm.obj \
  "$L/ntoskrnl.lib" "$L/hal.lib" "$L/displib.lib" \
  "$L/BufferOverflowFastFailK.lib" "$L/libcntpr.lib"
file out/NegroniGpuWddm.sys
sha256sum out/NegroniGpuWddm.sys
