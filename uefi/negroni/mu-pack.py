#!/usr/bin/env python3
"""Put an OS boot image behind Mu in the same slot, like lk2nd.

  uefi/negroni/mu-pack.py MU_BOOT.img OS_boot.img OS_vendor_boot.img OUTDIR

OUTDIR/boot_a.img        = Mu (shim + FD) padded to 32 MiB, then the OS boot.img
OUTDIR/vendor_boot_a.img = our shim vendor_boot padded to 1 MiB, then the OS vendor_boot.img
ABL only reads what the first header describes; Mu's "Android / Linux" entry finds the OS
images at those offsets (NegroniSlotBBoot.c BOOT_OS_OFFSET / VENDOR_OS_OFFSET). Flash both to
slot A with the bootloader's fastboot (vbmeta_a must be the disabled one, dtbo_a erased).
"""
import os
import struct
import sys

BOOT_OS_OFFSET = 0x2000000
VENDOR_OS_OFFSET = 0x100000
PART_SIZE = 0xC000000            # boot_a and vendor_boot_a: 192 MiB
SHIM_VENDOR_BOOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../shim/out/negroni-a/vendor_boot.img")


def align(n, a):
    return (n + a - 1) // a * a


def boot_len(img):
    """Used length of a boot.img v3/v4 (the files are padded to the partition size)."""
    if img[:8] != b"ANDROID!":
        sys.exit("OS boot image: no ANDROID! magic")
    ksize, rsize = struct.unpack_from("<II", img, 8)
    version = struct.unpack_from("<I", img, 40)[0]
    if version < 3:
        sys.exit("OS boot image: header v%d, need v3/v4" % version)
    n = 4096 + align(ksize, 4096) + align(rsize, 4096)
    if version >= 4:
        n += align(struct.unpack_from("<I", img, 1580)[0], 4096)   # signature_size
    return n


def vendor_boot_len(img):
    if img[:8] != b"VNDRBOOT":
        sys.exit("OS vendor_boot image: no VNDRBOOT magic")
    version, page = struct.unpack_from("<II", img, 8)
    vrsize = struct.unpack_from("<I", img, 24)[0]
    hsize, dtbsize = struct.unpack_from("<II", img, 2096)
    n = align(hsize, page) + align(vrsize, page) + align(dtbsize, page)
    if version >= 4:
        table, _, _, bootconfig = struct.unpack_from("<IIII", img, 2112)
        n += align(table, page) + align(bootconfig, page)
    return n


def pack(first, offset, second, out):
    if len(first) > offset:
        sys.exit("%s: Mu part is %d bytes, more than the %#x offset" % (out, len(first), offset))
    if offset + len(second) > PART_SIZE:
        sys.exit("%s: %d bytes do not fit the 192 MiB partition" % (out, offset + len(second)))
    with open(out, "wb") as f:
        f.write(first.ljust(offset, b"\0"))
        f.write(second)
    print("%s: %d bytes (OS image at %#x, %d bytes)" % (out, offset + len(second), offset, len(second)))


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    mu, osboot, osvb, outdir = sys.argv[1:]
    mu_img = open(mu, "rb").read()
    mu_img = mu_img[:boot_len(mu_img)]
    os_boot = open(osboot, "rb").read()
    os_vb = open(osvb, "rb").read()
    os_boot = os_boot[:boot_len(os_boot)]
    os_vb = os_vb[:vendor_boot_len(os_vb)]
    if os_boot[4096:4098] == b"\x1f\x8b":
        sys.exit("OS boot image has a gzip kernel: Mu boots uncompressed arm64 Images only")
    os.makedirs(outdir, exist_ok=True)
    pack(mu_img, BOOT_OS_OFFSET, os_boot, os.path.join(outdir, "boot_a.img"))
    shim_vb = open(SHIM_VENDOR_BOOT, "rb").read()
    pack(shim_vb, VENDOR_OS_OFFSET, os_vb, os.path.join(outdir, "vendor_boot_a.img"))


if __name__ == "__main__":
    main()
