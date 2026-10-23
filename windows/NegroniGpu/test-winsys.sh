#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
sdk="/mnt/c/Program Files (x86)/Windows Kits/10"
inc="$sdk/Include/10.0.28000.0"
mkdir -p out
for source in NegroniGpuAbi NegroniGpuWinsys test-winsys; do
  clang-cl --target=x86_64-pc-windows-msvc /GS- /Zl /O2 /W4 /WX /c \
    -D_AMD64_ -DAMD64 -D_WIN64 -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN \
    -DDXGKDDI_INTERFACE_VERSION=0xC004 \
    -Wno-pragma-pack -Wno-ignored-pragma-intrinsic -Wno-unknown-pragmas \
    -imsvc "$inc/um" -imsvc "$inc/shared" -imsvc "$inc/km/crt" \
    "/Foout/$source-host.obj" "$source.c"
done
lld-link /SUBSYSTEM:CONSOLE,6.02 /ENTRY:mainCRTStartup /MACHINE:X64 \
  /NODEFAULTLIB /OUT:out/test-winsys.exe \
  out/NegroniGpuAbi-host.obj out/NegroniGpuWinsys-host.obj out/test-winsys-host.obj \
  "$sdk/Lib/10.0.28000.0/um/x64/kernel32.lib"
./out/test-winsys.exe
