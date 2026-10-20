#!/usr/bin/env python3
"""Port r0q byte patches onto another build: match aarch64 code words around the patch with branch/adr/ldr-literal
immediates and page offsets masked. Patched words that are PC-relative branches are re-encoded for the new site."""
import struct, sys
o, p, t = [open(x, 'rb').read() for x in sys.argv[1:4]]
out = sys.argv[4]
W = lambda b, i: struct.unpack_from('<I', b, i)[0]

def mask(w):
    if (w & 0x7c000000) == 0x14000000: return w & 0xfc000000          # b / bl
    if (w & 0xff000010) == 0x54000000: return w & 0xff00001f          # b.cond
    if (w & 0x7e000000) == 0x34000000: return w & 0xff00001f          # cbz/cbnz
    if (w & 0x7e000000) == 0x36000000: return w & 0xfff8001f          # tbz/tbnz
    if (w & 0x1f000000) == 0x10000000: return w & 0x9f00001f          # adr/adrp
    if (w & 0x3b000000) == 0x18000000: return w & 0xff00001f          # ldr literal
    if (w & 0xffc00000) == 0x91000000: return w & 0xffc003ff          # add imm (adrp low12)
    if (w & 0x3b400000) == 0x39400000: return w & 0xffc003ff          # ldr/str unsigned imm (struct offsets)
    return w

def words(b, a, n): return [mask(W(b, a + 4*k)) for k in range(n)]

diff = [i for i in range(0, len(o) - 3, 4) if o[i:i+4] != p[i:i+4]]
runs = []
for i in diff:
    if runs and i == runs[-1][1]: runs[-1][1] = i + 4
    else: runs.append([i, i + 4])
t = bytearray(t); ok = True
tw = [mask(W(t, k)) for k in range(0, len(t) - 3, 4)]
for a, b in runs:
    found = None
    for ctx in (12, 10, 8, 6, 5, 4):
        pat = words(o, a - 4*ctx, ctx + (b - a)//4 + ctx)
        L = len(pat)
        hits = [k for k in range(len(tw) - L) if tw[k] == pat[0] and tw[k:k+L] == pat]
        if len(hits) == 1: found = (hits[0] + ctx) * 4; break
    if found is None:
        print(f"  run {a:#x}+{b-a}: NO MATCH"); ok = False; continue
    for k in range(0, b - a, 4):
        nw, ow = W(p, a + k), W(o, a + k)
        if (nw & 0x7c000000) == 0x14000000 and (ow & 0x7c000000) != 0x14000000 or ((nw ^ ow) & 0xfc000000 == 0 and (nw & 0x7c000000) == 0x14000000):
            off = ((nw & 0x3ffffff) ^ 0x2000000) - 0x2000000
            dest = a + k + off*4                     # destination in r0q
            # destination moves by the same delta as the site if it is near (same function)
            nd = found + k + off*4
            nw = (nw & 0xfc000000) | (((nd - (found + k)) // 4) & 0x3ffffff)
        struct.pack_into('<I', t, found + k, nw)
    print(f"  run {a:#x}+{b-a} -> {found:#x} ctx {ctx}  orig_here={bytes(W(t,found).to_bytes(4,'little')).hex()}")
open(out, 'wb').write(t)
sys.exit(0 if ok else 1)
