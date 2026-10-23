#!/bin/bash
# Compile-only WDDM 2.7 DDI-interface / WDDM 1.3 render-only prototype.
# Incomplete DRIVER_INITIALIZATION_DATA; never package or install this binary.
set -euo pipefail
cd "$(dirname "$0")"
K="/mnt/c/Program Files (x86)/Windows Kits/10"; V=10.0.28000.0
I="$K/Include/$V"; L="$K/Lib/$V/km/arm64"
mkdir -p out
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE \
  /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
  /Foout/NegroniGpuAbi.obj NegroniGpuAbi.c 2>out/abi-compile.log
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
  -D_WIN32_WINNT=0x0A00 -DDXGKDDI_INTERFACE_VERSION=0x4002 \
  /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
  /Foout/NegroniGpuWddm.obj NegroniGpuWddm.c 2>out/wddm-compile.log
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
  -D_WIN32_WINNT=0x0A00 /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
  /Foout/NegroniGpuFirmware.obj NegroniGpuFirmware.c 2>>out/wddm-compile.log
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
  -D_WIN32_WINNT=0x0A00 /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
  /Foout/NegroniGpuFirmwareIo.obj NegroniGpuFirmwareIo.c 2>>out/wddm-compile.log
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
  -D_WIN32_WINNT=0x0A00 /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
  /Foout/NegroniGpuPm4.obj NegroniGpuPm4.c 2>>out/wddm-compile.log
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
  -D_WIN32_WINNT=0x0A00 /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
  /Foout/NegroniGpuQueue.obj NegroniGpuQueue.c 2>>out/wddm-compile.log
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
  -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
  -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
  -D_WIN32_WINNT=0x0A00 -DDXGKDDI_INTERFACE_VERSION=0x4002 \
  /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
  /Foout/NegroniGpuObjects.obj NegroniGpuObjects.c 2>>out/wddm-compile.log
for source in NegroniGpuMemory NegroniGpuAllocation NegroniGpuPageTable; do
  clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
    -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic \
    -D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 \
    -D_WIN32_WINNT=0x0A00 -DDXGKDDI_INTERFACE_VERSION=0x4002 \
    /I "$I/km/crt" /I "$I/km" /I "$I/shared" \
    /Foout/$source.obj "$source.c" 2>>out/wddm-compile.log
done
lld-link /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry /MACHINE:ARM64 \
  /NODEFAULTLIB /INTEGRITYCHECK /BASE:0x1C2000000 /TSAWARE:NO /STACK:0x40000 \
  /OUT:out/NegroniGpuWddm.sys out/NegroniGpuWddm.obj out/NegroniGpuFirmware.obj out/NegroniGpuFirmwareIo.obj out/NegroniGpuPm4.obj out/NegroniGpuQueue.obj out/NegroniGpuAbi.obj out/NegroniGpuObjects.obj \
  out/NegroniGpuMemory.obj out/NegroniGpuAllocation.obj out/NegroniGpuPageTable.obj \
  "$L/ntoskrnl.lib" "$L/hal.lib" "$L/displib.lib" \
  "$L/BufferOverflowFastFailK.lib" "$L/libcntpr.lib"
file out/NegroniGpuWddm.sys
sha256sum out/NegroniGpuWddm.sys
