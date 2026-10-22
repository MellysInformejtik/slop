/* SPDX-License-Identifier: MIT */
#include "NegroniGpuFirmware.h"

#define NG_GMU_ITCM_BASE   0x00000000u
#define NG_GMU_DTCM_BASE   0x10004000u
#define NG_GMU_TCM_SIZE    0x00004000u
#define NG_GMU_ICACHE_BASE 0x00004000u
#define NG_GMU_ICACHE_SIZE 0x00ffc000u

static uint16_t
NgReadU16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t
NgReadU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int
NgRangeFits(uint32_t Address, uint32_t Size, uint32_t Base, uint32_t Span)
{
    return Address >= Base && Address - Base < Span &&
           Size <= Span - (Address - Base);
}

static NG_GMU_REGION
NgClassifyGmuBlock(uint32_t Address, uint32_t Size)
{
    if (Size == 0)
        return NgGmuRegionInvalid;
    if (NgRangeFits(Address, Size, NG_GMU_ITCM_BASE, NG_GMU_TCM_SIZE))
        return NgGmuRegionItcm;
    if (NgRangeFits(Address, Size, NG_GMU_DTCM_BASE, NG_GMU_TCM_SIZE))
        return NgGmuRegionDtcm;
    if (NgRangeFits(Address, Size, NG_GMU_ICACHE_BASE, NG_GMU_ICACHE_SIZE))
        return NgGmuRegionIcache;
    return NgGmuRegionInvalid;
}

int
NgParseGmuFirmware(const uint8_t *Buffer, size_t Length,
                   NG_GMU_FW_BLOCK *Blocks, size_t Capacity,
                   size_t *BlockCount)
{
    size_t at = 0, count = 0;

    if (BlockCount != NULL)
        *BlockCount = 0;
    if (Buffer == NULL || Blocks == NULL || BlockCount == NULL || Capacity == 0)
        return 0;

    while (at < Length) {
        uint32_t address, size;
        NG_GMU_REGION region;

        if (Length - at < 16 || count == Capacity || count == NG_GMU_FW_MAX_BLOCKS)
            return 0;

        address = NgReadU32(Buffer + at);
        size = NgReadU32(Buffer + at + 4);
        if (size > Length - at - 16)
            return 0;
        at += 16;

        if (size == 0) {
            at += size;
            continue;
        }
        region = NgClassifyGmuBlock(address, size);
        if (region == NgGmuRegionInvalid)
            return 0;

        Blocks[count].Address = address;
        Blocks[count].Size = size;
        Blocks[count].Type = NgReadU32(Buffer + at - 8);
        Blocks[count].Value = NgReadU32(Buffer + at - 4);
        Blocks[count].Data = Buffer + at;
        Blocks[count].Region = region;
        ++count;
        at += size;
    }

    if (count == 0)
        return 0;
    *BlockCount = count;
    return 1;
}

int
NgParseZapManifest(const uint8_t *Buffer, size_t Length,
                   NG_ZAP_FW_IMAGE *Image)
{
    uint32_t phoff, phentsize, phnum, lowest = UINT32_MAX, highest = 0;
    size_t i;

    if (Image != NULL)
        for (i = 0; i < sizeof(*Image); ++i)
            ((uint8_t *)Image)[i] = 0;
    if (Buffer == NULL || Image == NULL || Length < 52 ||
        Buffer[0] != 0x7f || Buffer[1] != 'E' || Buffer[2] != 'L' ||
        Buffer[3] != 'F' || Buffer[4] != 1 || Buffer[5] != 1)
        return 0;

    phoff = NgReadU32(Buffer + 28);
    phentsize = NgReadU16(Buffer + 42);
    phnum = NgReadU16(Buffer + 44);
    if (phentsize != 32 || phnum == 0 || phnum > NG_ZAP_FW_MAX_SEGMENTS ||
        phoff > Length || (size_t)phnum > (Length - phoff) / phentsize)
        return 0;

    for (i = 0; i < phnum; ++i) {
        const uint8_t *ph = Buffer + phoff + i * phentsize;
        uint32_t type = NgReadU32(ph);
        uint32_t paddr = NgReadU32(ph + 12);
        uint32_t filesz = NgReadU32(ph + 16);
        uint32_t memsz = NgReadU32(ph + 20);
        uint32_t flags = NgReadU32(ph + 24);
        uint32_t end;
        NG_ZAP_FW_SEGMENT *segment;

        /* Qualcomm's hash segment uses the vendor flag encoding (value 2). */
        if (type != 1 || ((flags >> 24) & 7u) == 2u || filesz == 0)
            continue;
        if (filesz > memsz || paddr > UINT32_MAX - memsz ||
            Image->SegmentCount == NG_ZAP_FW_MAX_SEGMENTS)
            goto fail;

        end = paddr + memsz;
        if (paddr < lowest)
            lowest = paddr;
        if (end > highest)
            highest = end;

        segment = &Image->Segments[Image->SegmentCount++];
        segment->Index = (uint32_t)i;
        segment->PhysicalAddress = paddr;
        segment->FileSize = filesz;
        segment->MemorySize = memsz;
    }

    if (Image->SegmentCount == 0 || highest <= lowest ||
        highest > UINT32_MAX - 0xfffu)
        goto fail;

    highest = (highest + 0xfffu) & ~0xfffu;
    Image->LowestAddress = lowest;
    Image->SpanSize = highest - lowest;
    if (Image->SpanSize == 0 || Image->SpanSize > NG_ZAP_FW_MAX_SPAN)
        goto fail;
    return 1;

fail:
    for (i = 0; i < sizeof(*Image); ++i)
        ((uint8_t *)Image)[i] = 0;
    return 0;
}
