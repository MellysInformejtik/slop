#!/usr/bin/env python3
"""Stage 1 only: an empty native_legacy_display_probe() for the patched bsd_init, which calls it whether or not the
legacy display is built. The real NativeLegacyDisplay needs a prelinked candidate that stage 1 itself produces."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
out = ROOT / 'build/legacy-display-stub'
out.mkdir(parents=True, exist_ok=True)
(out / 'stub.c').write_text('void native_legacy_display_probe(void) {}\n')
subprocess.run(['xcrun', '--sdk', 'iphoneos', 'clang', '-target', 'arm64-apple-ios17.0', '-mkernel', '-fno-builtin',
                '-O2', '-c', str(out / 'stub.c'), '-o', str(out / 'stub.o')], check=True)
subprocess.run(['xcrun', 'libtool', '-static', '-o', str(out / 'libLegacyDisplayStub.a'), str(out / 'stub.o')], check=True)
print(out / 'libLegacyDisplayStub.a')
