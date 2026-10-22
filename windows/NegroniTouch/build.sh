#!/bin/bash
# Build NegroniTouch.sys (ARM64 KMDF 1.15 + VHF) with clang-cl/lld-link against the installed WDK; no MSVC needed.
set -euo pipefail
cd "$(dirname "$0")"
K="/mnt/c/Program Files (x86)/Windows Kits/10"; V=10.0.28000.0
I="$K/Include/$V"; L="$K/Lib/$V/km/arm64"; W="$K/Lib/wdf/kmdf/arm64/1.15"
mkdir -p out
clang-cl --target=aarch64-pc-windows-msvc /kernel /GS- /Zl /O2 /W3 /c \
	-Wno-unknown-pragmas -Wno-ignored-attributes -Wno-ignored-pragma-intrinsic -Wno-microsoft-anon-tag \
	-D_ARM64_ -DARM64 -D_WIN64 -D_KERNEL_MODE -DNTDDI_VERSION=0x0A000010 -D_WIN32_WINNT=0x0A00 \
	-DKMDF_VERSION_MAJOR=1 -DKMDF_VERSION_MINOR=15 -DPOOL_NX_OPTIN=1 \
	'-DWDF_TYPE_DEFAULT_SECTION_NAME=".rdata"' \
	/I "$I/km/crt" /I "$I/km" /I "$I/shared" /I "$K/Include/wdf/kmdf/1.15" /I ../../drivers/negroni-touch \
	/Foout/NegroniTouch.obj NegroniTouch.c 2> out/compile.log
# WDF_TYPE_DEFAULT_SECTION_NAME: clang marks WDF's const context-type record read-only, so in .data it
# became a second, read-only .data section and the kernel refused to load the image (0xC0000018).
# libcntpr: the kernel CRT pieces vhfkm.lib (built with MSVC) expects (_Interlocked*, __security_push/pop_cookie)
lld-link /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:FxDriverEntry /MACHINE:ARM64 /NODEFAULTLIB /INTEGRITYCHECK \
	/OUT:out/NegroniTouch.sys out/NegroniTouch.obj "$W/WdfDriverEntry.lib" "$W/WdfLdr.lib" \
	"$L/ntoskrnl.lib" "$L/hal.lib" "$L/wmilib.lib" "$L/BufferOverflowFastFailK.lib" "$L/vhfkm.lib" "$L/libcntpr.lib"
ls -l out/NegroniTouch.sys
