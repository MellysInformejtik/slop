#!/bin/bash
# Build an ARM64 KMDF resource-validation probe against the installed WDK.
# This is deliberately not the WDDM/acceleration driver and is never installed here.
set -euo pipefail
cd "$(dirname "$0")"
K="/mnt/c/Program Files (x86)/Windows Kits/10"; V=10.0.28000.0
I="$K/Include/$V"; L="$K/Lib/$V/km/arm64"; W="$K/Lib/wdf/kmdf/arm64/1.15"
mkdir -p out
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
  -D_WIN32_WINNT=0x0A00 -DKMDF_VERSION_MAJOR=1 -DKMDF_VERSION_MINOR=15 \
  -DPOOL_NX_OPTIN=1 '-DWDF_TYPE_DEFAULT_SECTION_NAME=".rdata"' \
  /I "$I/km/crt" /I "$I/km" /I "$I/shared" /I "$K/Include/wdf/kmdf/1.15" \
  /Foout/NegroniGpuProbe.obj NegroniGpuProbe.c 2>out/compile.log
lld-link /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:FxDriverEntry /MACHINE:ARM64 \
  /NODEFAULTLIB /INTEGRITYCHECK /BASE:0x1C1000000 /TSAWARE:NO /STACK:0x40000 \
  /OUT:out/NegroniGpuProbe.sys out/NegroniGpuProbe.obj \
  "$W/WdfDriverEntry.lib" "$W/WdfLdr.lib" "$L/ntoskrnl.lib" "$L/hal.lib" \
  "$L/BufferOverflowFastFailK.lib" "$L/libcntpr.lib"
file out/NegroniGpuProbe.sys
sha256sum out/NegroniGpuProbe.sys
