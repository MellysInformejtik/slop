/* SPDX-License-Identifier: MIT */
#include "NegroniGpuFirmware.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
Put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void
MakeGmuBlock(uint8_t *p, uint32_t address, uint32_t size)
{
    Put32(p, address);
    Put32(p + 4, size);
    Put32(p + 8, 0x12345678);
    Put32(p + 12, 0x87654321);
    if (size != 0)
        memset(p + 16, 0xa5, size);
}

static void
MakeElfSegment(uint8_t *ph, uint32_t paddr, uint32_t filesz,
               uint32_t memsz, uint32_t flags)
{
    Put32(ph, 1);              /* PT_LOAD */
    Put32(ph + 4, 0);          /* p_offset: data is in a separate .bNN file */
    Put32(ph + 8, paddr);      /* p_vaddr */
    Put32(ph + 12, paddr);     /* p_paddr */
    Put32(ph + 16, filesz);
    Put32(ph + 20, memsz);
    Put32(ph + 24, flags);
    Put32(ph + 28, 0x1000);
}

static void
TestGmu(void)
{
    uint8_t blob[40] = {0};
    NG_GMU_FW_BLOCK blocks[4];
    size_t count = 99;

    MakeGmuBlock(blob, 0x4000, 4);
    MakeGmuBlock(blob + 20, 0x10004000, 4);
    assert(NgParseGmuFirmware(blob, sizeof(blob), blocks, 4, &count));
    assert(count == 2);
    assert(blocks[0].Region == NgGmuRegionIcache && blocks[0].Data[0] == 0xa5);
    assert(blocks[1].Region == NgGmuRegionDtcm);

    count = 99;
    assert(!NgParseGmuFirmware(blob, sizeof(blob) - 1, blocks, 4, &count));
    assert(count == 0); /* truncated payload */
    MakeGmuBlock(blob, 0x00ffffff, 2);
    assert(!NgParseGmuFirmware(blob, 18, blocks, 4, &count)); /* leaves icache */
    MakeGmuBlock(blob, 0x4000, 8);
    assert(!NgParseGmuFirmware(blob, 20, blocks, 4, &count)); /* truncated data */
    MakeGmuBlock(blob, 0x4000, 4);
    assert(!NgParseGmuFirmware(blob, 20, blocks, 0, &count)); /* no output capacity */
}

static void
TestZap(void)
{
    uint8_t elf[52 + 2 * 32] = {0};
    NG_ZAP_FW_IMAGE image;

    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 1; elf[5] = 1;
    Put32(elf + 28, 52);
    elf[42] = 32;
    elf[44] = 2;
    MakeElfSegment(elf + 52, 0x1000, 0x500, 0x800, 0);
    MakeElfSegment(elf + 84, 0x2000, 0x100, 0x100, 0x02000000); /* hash segment */

    assert(NgParseZapManifest(elf, sizeof(elf), &image));
    assert(image.SegmentCount == 1);
    assert(image.Segments[0].Index == 0);
    assert(image.Segments[0].FileSize == 0x500);
    assert(image.SpanSize == 0x1000);

    elf[4] = 2;
    assert(!NgParseZapManifest(elf, sizeof(elf), &image)); /* ELF64 unsupported */
    elf[4] = 1;
    Put32(elf + 52 + 16, 0x900);
    Put32(elf + 52 + 20, 0x800);
    assert(!NgParseZapManifest(elf, sizeof(elf), &image)); /* filesz > memsz */
    Put32(elf + 52 + 16, 0x500);
    Put32(elf + 52 + 20, 0x800);
    Put32(elf + 52 + 12, 0xfffffff0);
    assert(!NgParseZapManifest(elf, sizeof(elf), &image)); /* address overflow */
    assert(image.SegmentCount == 0); /* failed parse clears output */
}

int
main(void)
{
    TestGmu();
    TestZap();
    puts("A730 firmware parser tests passed");
    return 0;
}
