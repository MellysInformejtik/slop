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

