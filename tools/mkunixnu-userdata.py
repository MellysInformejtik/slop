#!/usr/bin/env python3
"""UniXNU userdata image for negroni, built on Linux.

Same image as UniXNU-sheng scripts/build-userdata-root-image.py (which needs macOS hdiutil):
    0x000000  "SHNGROOT" manifest (4096 bytes), zero padding to 1 MiB
    0x100000  window "OS"        raw contents of the OS disk image, 1 MiB aligned
    ....      window "SystemOS"  raw contents of the SystemOS disk image, 1 MiB aligned
The raw contents come from the UDIF images directly (koly trailer + blkx tables; zero, raw,
zlib, bzip2 and LZFSE chunks) instead of an hdiutil attach. Zero runs are not written, so the
output file is sparse and img2simg turns it into a small sparse image.

usage: mkunixnu-userdata.py OS.dmg SystemOS.dmg OUT.img [--lzfse /path/liblzfse.so]
"""
import bz2
import ctypes
import hashlib
import plistlib
import struct
import sys
import zlib

MIB = 1 << 20
SECTOR = 512
# negroni LUN 0 GPT "userdata" (sda14), 4096-byte LBAs, read 2026-10-20
USERDATA_BYTES = (0x1cde4fa - 0x2b52ec + 1) * 4096

ZERO, RAW, IGNORE, ADC, ZLIB, BZIP2, LZFSE, LZMA, COMMENT, END = (
    0x00000000, 0x00000001, 0x00000002, 0x80000004, 0x80000005, 0x80000006, 0x80000007, 0x80000008,
    0x7ffffffe, 0xffffffff)


class Lzfse:
    def __init__(self, path):
        self.lib = ctypes.CDLL(path)
        self.lib.lzfse_decode_buffer.restype = ctypes.c_size_t
        self.lib.lzfse_decode_buffer.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]

    def decode(self, data, size):
        out = ctypes.create_string_buffer(size)
        got = self.lib.lzfse_decode_buffer(out, size, data, len(data), None)
        if got != size:
            raise ValueError(f'lzfse: {got} of {size} bytes')
        return out.raw


def udif_chunks(f):
    """(total_bytes, sorted [(out_offset, length, type, src_offset, src_length)])."""
    size = f.seek(0, 2)
    f.seek(-512, 2)
    koly = f.read(512)
    if koly[:4] != b'koly':
        # Not UDIF: a bare image (the SystemOS cryptex is an APFS container, NXSB at 32), which hdiutil
        # attaches as is. Its raw contents are the file itself.
        if size % SECTOR:
            raise SystemExit('neither UDIF nor a whole number of sectors')
        return size, [(0, size, RAW, 0, size)]
    data_fork = struct.unpack_from('>Q', koly, 0x18)[0]
    xml_off, xml_len = struct.unpack_from('>QQ', koly, 0xD8)
    sectors = struct.unpack_from('>Q', koly, 0x1EC)[0]
    f.seek(xml_off)
    plist = plistlib.loads(f.read(xml_len))
    chunks = []
    for blk in plist['resource-fork']['blkx']:
        mish = blk['Data']
        if mish[:4] != b'mish':
            continue
        first, _, data_off = struct.unpack_from('>QQQ', mish, 8)
        count = struct.unpack_from('>I', mish, 0xC8)[0]
        for i in range(count):
            kind, _, sec, nsec, coff, clen = struct.unpack_from('>IIQQQQ', mish, 0xCC + 40 * i)
            if kind in (COMMENT, END) or nsec == 0:
                continue
            chunks.append(((first + sec) * SECTOR, nsec * SECTOR, kind, data_fork + data_off + coff, clen))
    chunks.sort()
    return sectors * SECTOR, chunks


def write_window(src, out, base, lzfse):
    """Decode one UDIF image into out at base. Returns (size, sha256 digest of the raw contents)."""
    total, chunks = udif_chunks(src)
    digest = hashlib.sha256()
    cursor = 0
    zero = bytes(MIB)

    def zeros(n):
        while n:
            k = min(n, MIB)
            digest.update(zero[:k])
            n -= k

    for pos, length, kind, soff, slen in chunks:
        if pos < cursor:
            raise SystemExit('overlapping UDIF chunks')
        zeros(pos - cursor)
        if kind in (ZERO, IGNORE):
            zeros(length)
        else:
            src.seek(soff)
            blob = src.read(slen)
            if kind == RAW:
                data = blob
            elif kind == ZLIB:
                data = zlib.decompress(blob)
            elif kind == BZIP2:
                data = bz2.decompress(blob)
            elif kind == LZFSE:
                data = lzfse.decode(blob, length)
            else:
                raise SystemExit(f'unsupported UDIF chunk type {kind:#x}')
            if len(data) != length:
                raise SystemExit(f'chunk at {pos:#x}: {len(data)} of {length} bytes')
            digest.update(data)
            if data.count(0) != len(data):	# keep the output sparse
                out.seek(base + pos)
                out.write(data)
        cursor = pos + length
    zeros(total - cursor)
    return total, digest.digest()


def main():
    args = sys.argv[1:]
    lzpath = '/home/paw/ios-work/tools/liblzfse.so'
    if '--lzfse' in args:
        i = args.index('--lzfse'); lzpath = args[i + 1]; del args[i:i + 2]
    os_dmg, sys_dmg, output = args
    lzfse = Lzfse(lzpath)
    windows = (('OS', os_dmg), ('SystemOS', sys_dmg))
    layout, cursor = [], MIB
    for name, path in windows:
        with open(path, 'rb') as f:
            size, _ = udif_chunks(f)
        layout.append((name, path, cursor, size))
        cursor += (size + MIB - 1) // MIB * MIB
    image_bytes = cursor
    print(f'image {image_bytes} bytes ({image_bytes / 2**30:.2f} GiB); userdata holds {USERDATA_BYTES}')
    if image_bytes > USERDATA_BYTES:
        raise SystemExit('Refusing: the image is larger than the userdata partition')
    entries = []
    with open(output, 'wb') as out:
        out.truncate(image_bytes)
        for name, path, offset, size in layout:
            with open(path, 'rb') as src:
                got, digest = write_window(src, out, offset, lzfse)
            assert got == size
            entries.append((name, offset, size, digest))
            print(f'{name}: {size} bytes at {offset:#x} sha256 {digest.hex()}', flush=True)
        header = bytearray(4096)
        header[0:8] = b'SHNGROOT'
        struct.pack_into('<IIQ', header, 8, 1, len(entries), image_bytes)
        for index, (name, offset, size, digest) in enumerate(entries):
            struct.pack_into('<32sQQII32s40s', header, 24 + 128 * index, name.encode(), offset, size, 512, 1, digest, bytes(40))
        header[4064:4096] = hashlib.sha256(bytes(header[:4064])).digest()
        out.seek(0)
        out.write(header)
    print(f'manifest sha256 {hashlib.sha256(bytes(header)).hexdigest()}')
    print(f'next: img2simg {output} {output.rsplit(".", 1)[0]}.simg; fastboot -S 512M flash userdata <simg>')


if __name__ == '__main__':
    main()
