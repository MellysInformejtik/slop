#!/usr/bin/env python3
"""Boot-progress probe for a kernel that hangs before any output.

Overwrites the first instructions of SYMBOL in a Mach-O kernel with a PSCI SYSTEM_RESET call
(SMC conduit, as sm8450.dtsi says). If the phone reboots instead of hanging, the kernel reached
SYMBOL. Works with the MMU on or off. One probe per image; move it to bisect.

usage: probe-reset.py KERNEL.macho SYMBOL OUT.macho
"""
import struct
import subprocess
import sys

kernel, symbol, out = sys.argv[1:]
data = bytearray(open(kernel, 'rb').read())

nm = subprocess.run(['llvm-nm', kernel], capture_output=True, text=True, check=True).stdout
addr = next((int(l.split()[0], 16) for l in nm.splitlines() if l.split()[-1] == '_' + symbol), None)
if addr is None:
    raise SystemExit(f'{symbol}: not in the symbol table')

magic, _, _, _, ncmds, _, _, _ = struct.unpack_from('<IiiIIIII', data, 0)
assert magic == 0xfeedfacf, 'not a 64-bit Mach-O'
off, fileoff = 32, None


def segments(base):
    """LC_SEGMENT_64 (vmaddr, vmsize, fileoff) of the Mach-O at base, and of fileset entries."""
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from('<IiiIIIII', data, base)
    p = base + 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, p)
        if cmd == 0x19:
            vmaddr, vmsize, foff, fsize = struct.unpack_from('<QQQQ', data, p + 24)
            yield vmaddr, fsize, foff
        elif cmd == 0x80000035:                 # LC_FILESET_ENTRY
            _, entry_off = struct.unpack_from('<QQ', data, p + 8)
            yield from segments(entry_off)
        p += size


for vmaddr, fsize, foff in segments(0):
    if vmaddr <= addr < vmaddr + fsize:
        fileoff = foff + addr - vmaddr
        break
if fileoff is None:
    raise SystemExit(f'{symbol} {addr:#x}: no file-backed segment holds it')

code = [
    0x52800120,             # mov  w0, #9
    0x72b08000,             # movk w0, #0x8400, lsl #16   -> 0x84000009 PSCI SYSTEM_RESET
    0xd4000003,             # smc  #0
    0x14000000,             # b    .
]
struct.pack_into('<4I', data, fileoff, *code)
open(out, 'wb').write(data)
print(f'{symbol} va {addr:#x} file {fileoff:#x}: PSCI SYSTEM_RESET probe')
