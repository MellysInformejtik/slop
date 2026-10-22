#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
K='/mnt/c/Program Files (x86)/Windows Kits/10'
V=10.0.28000.0
mkdir -p out/v4-catalog
clang-cl --target=aarch64-pc-windows-msvc /GS- /Zl /O2 /W3 /c \
    -Wno-unknown-pragmas -Wno-ignored-pragma-intrinsic -Wno-microsoft-anon-tag -Wno-pragma-pack \
    -D_ARM64_ -D_WIN64 -D_WIN32_WINNT=0x0A00 \
    /I "$K/Include/$V/km/crt" /I "$K/Include/$V/um" /I "$K/Include/$V/shared" \
    /Foout/v4-catalog/TrustTouch.obj TrustTouch.c
lld-link /SUBSYSTEM:CONSOLE /ENTRY:entry /MACHINE:ARM64 /NODEFAULTLIB \
    /OUT:out/v4-catalog/TrustTouch.exe out/v4-catalog/TrustTouch.obj \
    "$K/Lib/$V/um/arm64/kernel32.lib" "$K/Lib/$V/um/arm64/Crypt32.Lib"
