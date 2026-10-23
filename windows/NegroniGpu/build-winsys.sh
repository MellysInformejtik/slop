#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
sdk="/mnt/c/Program Files (x86)/Windows Kits/10"
ver=10.0.28000.0
inc="$sdk/Include/$ver"
lib="$sdk/Lib/$ver/um/arm64"
mkdir -p out
for source in NegroniGpuAbi NegroniGpuWinsys winsys-probe; do
  clang-cl --target=aarch64-pc-windows-msvc /GS- /Zl /O2 /W4 /WX /c \
    -D_ARM64_ -DARM64 -D_WIN64 -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN \
    -DDXGKDDI_INTERFACE_VERSION=0xC004 \
    -Wno-pragma-pack -Wno-ignored-pragma-intrinsic -Wno-unknown-pragmas \
    -imsvc "$inc/um" -imsvc "$inc/shared" -imsvc "$inc/km/crt" \
    "/Foout/$source-user.obj" "$source.c"
done
llvm-lib /OUT:out/NegroniGpuWinsys.lib out/NegroniGpuAbi-user.obj out/NegroniGpuWinsys-user.obj
lld-link /SUBSYSTEM:CONSOLE,6.02 /ENTRY:mainCRTStartup /MACHINE:ARM64 \
  /NODEFAULTLIB /OUT:out/winsys-probe.exe \
  out/NegroniGpuAbi-user.obj out/NegroniGpuWinsys-user.obj out/winsys-probe-user.obj \
  "$lib/kernel32.lib" "$lib/gdi32.lib" "$lib/shell32.lib"
file out/winsys-probe.exe
sha256sum out/winsys-probe.exe
