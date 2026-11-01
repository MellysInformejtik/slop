#!/usr/bin/env python3
"""Patch the negroni ABL LinuxLoader.efi (extracted from abl_a) to run from RAM under Mu
(`fastboot boot LinuxLoader-paw.efi` in Mu fastboot). It is never flashed: abl is signed.

  tools/abl-patch.py LinuxLoader_a.efi LinuxLoader-paw.efi [--orange "Orange State @paw"]

Patches (data only, in place; code is not changed):
  - the unlocked-boot warning title "Orange State\\n" (a 256-byte MENU_MSG_INFO text buffer).
"""
import argparse
import hashlib
import sys

MSG_SIZE = 256


def patch_msg(d, old, new, what):
    i = d.find(old + b"\0")
    if i < 0:
        sys.exit("%s: %r not found" % (what, old))
    if d.find(old + b"\0", i + 1) >= 0:
        sys.exit("%s: %r found more than once" % (what, old))
    if len(new) >= MSG_SIZE:
        sys.exit("%s: new text longer than %d bytes" % (what, MSG_SIZE - 1))
    end = d.index(b"\0", i)
    if any(d[end:i + MSG_SIZE]):     # the rest of the 256-byte buffer must be padding
        sys.exit("%s: %r is not in a %d-byte buffer" % (what, old, MSG_SIZE))
    d[i:i + MSG_SIZE] = new.ljust(MSG_SIZE, b"\0")
    print("%s: %#x %r -> %r" % (what, i, old, new))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--orange", default="Orange State @paw")
    a = ap.parse_args()
    d = bytearray(open(a.src, "rb").read())
    if d[:2] != b"MZ":
        sys.exit("not a PE/EFI file")
    patch_msg(d, b"Orange State\n", a.orange.encode() + b"\n", "orange title")
    open(a.dst, "wb").write(d)
    print("%s sha256 %s" % (a.dst, hashlib.sha256(d).hexdigest()))


if __name__ == "__main__":
    main()
