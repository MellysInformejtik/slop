/* SPDX-License-Identifier: MIT */
#include "NegroniGpuPageTable.h"

#include <stddef.h>

/* Descriptor bits (ARMv8 VMSA long descriptors; values as bsd/dev/negroni_adreno.c uses them). */
#define PTE_VALID_TABLE         3ull            /* levels 1-2: next-level table */
#define PTE_VALID_PAGE          3ull            /* level 3: page */
#define PTE_UNPRIVILEGED        (1ull << 6)
#define PTE_READ_ONLY           (1ull << 7)
#define PTE_SHARED              (2ull << 8)
#define PTE_ACCESSED            (1ull << 10)
#define PTE_NOT_GLOBAL          (1ull << 11)
#define PTE_ADDRESS             0x0000fffffffff000ull
#define PTE_CORE                (PTE_SHARED | PTE_ACCESSED | PTE_NOT_GLOBAL | PTE_UNPRIVILEGED)        /* MAIR index 0 */

static uint32_t
index_at(uint64_t va, unsigned level)
{
    return (uint32_t)(va >> (12u + 9u * (3u - level))) & 0x1ffu;
}

static uint64_t *
table_of(const NG_GPU_PAGETABLE *Pt, uint64_t entry)
{
    uint64_t phys = entry & PTE_ADDRESS;

    for (uint32_t i = 0; i < Pt->TableCount; ++i)
        if (Pt->Tables[i].Phys == phys)
            return Pt->Tables[i].Virt;
    return NULL;
}

static void
publish(const NG_GPU_PAGETABLE *Pt, uint64_t *table)
{
    if (Pt->Ops.Publish)
        Pt->Ops.Publish(Pt->Ops.Context, table);
}

static int
add_table(NG_GPU_PAGETABLE *Pt, uint64_t **virt, uint64_t *phys)
{
    if (Pt->TableCount == Pt->TableCapacity)
        return 0;
    *virt = (uint64_t *)Pt->Ops.AllocTable(Pt->Ops.Context, phys);
    if (!*virt || (*phys & (NG_GPU_PT_PAGE - 1)) || (*phys & ~PTE_ADDRESS)) {
        if (*virt)
            Pt->Ops.FreeTable(Pt->Ops.Context, *virt, *phys);
        return 0;
    }
    for (uint32_t i = 0; i < NG_GPU_PT_PAGE / 8; ++i)
        (*virt)[i] = 0;
    Pt->Tables[Pt->TableCount].Virt = *virt;
    Pt->Tables[Pt->TableCount++].Phys = *phys;
    return 1;
}

int
NgGpuPtInit(NG_GPU_PAGETABLE *Pt, const NG_GPU_PT_OPS *Ops, NG_GPU_PT_TABLE *Storage, uint32_t Capacity)
{
    uint64_t *root, phys;

    if (!Pt || !Ops || !Ops->AllocTable || !Ops->FreeTable || !Storage || Capacity < 3)
        return 0;
    Pt->Ops = *Ops;
    Pt->Tables = Storage;
    Pt->TableCount = 0;
    Pt->TableCapacity = Capacity;
    Pt->MappedPages = 0;
    if (!add_table(Pt, &root, &phys))
        return 0;
    publish(Pt, root);
    return 1;
}

void
NgGpuPtDestroy(NG_GPU_PAGETABLE *Pt)
{
    if (!Pt || !Pt->Tables)
        return;
    for (uint32_t i = 0; i < Pt->TableCount; ++i)
        Pt->Ops.FreeTable(Pt->Ops.Context, Pt->Tables[i].Virt, Pt->Tables[i].Phys);
    Pt->TableCount = 0;
    Pt->Tables = NULL;
    Pt->MappedPages = 0;
}

/* The level-3 table for va; with Create, missing levels are made. NULL: none (or no room). */
static uint64_t *
leaf_table(NG_GPU_PAGETABLE *Pt, uint64_t va, int Create)
{
    uint64_t *table = Pt->Tables[0].Virt;

    for (unsigned level = 1; level < 3; ++level) {
        uint64_t *entry = &table[index_at(va, level)];
        uint64_t *next;
        uint64_t phys;

        if ((*entry & 3u) == PTE_VALID_TABLE) {
            next = table_of(Pt, *entry);
        } else if (*entry != 0) {
            return NULL;            /* a block mapping: never made here */
        } else if (!Create || !add_table(Pt, &next, &phys)) {
            return NULL;
        } else {
            publish(Pt, next);
            *entry = phys | PTE_VALID_TABLE;
            publish(Pt, table);
        }
        if (!next)
            return NULL;
        table = next;
    }
    return table;
}

static int
range_ok(uint64_t GpuVa, uint64_t Bytes)
{
    uint64_t limit = 1ull << NG_GPU_PT_VA_BITS;

    return Bytes && !(GpuVa & (NG_GPU_PT_PAGE - 1)) && !(Bytes & (NG_GPU_PT_PAGE - 1)) &&
           GpuVa < limit && Bytes <= limit - GpuVa;
}

int
NgGpuPtMap(NG_GPU_PAGETABLE *Pt, uint64_t GpuVa, uint64_t Phys, uint64_t Bytes, uint32_t Flags)
{
    uint64_t attributes = PTE_CORE | ((Flags & NG_GPU_PT_READ_ONLY) ? PTE_READ_ONLY : 0);
    uint64_t done = 0;

    if (!Pt || !Pt->Tables || !range_ok(GpuVa, Bytes) || (Phys & (NG_GPU_PT_PAGE - 1)) ||
        (Flags & ~NG_GPU_PT_READ_ONLY) || Phys + Bytes < Phys || ((Phys + Bytes - 1) & ~PTE_ADDRESS & ~0xfffull))
        return 0;
    for (; done < Bytes; done += NG_GPU_PT_PAGE) {
        uint64_t va = GpuVa + done;
        uint64_t *leaf = leaf_table(Pt, va, 1);
        uint64_t *entry;

        if (!leaf)
            break;
        entry = &leaf[index_at(va, 3)];
        if (*entry != 0)
            break;
        *entry = (Phys + done) | attributes | PTE_VALID_PAGE;
        Pt->MappedPages++;
        if (((va + NG_GPU_PT_PAGE) & 0x1fffffull) == 0 || done + NG_GPU_PT_PAGE == Bytes)
            publish(Pt, leaf);
    }
    if (done == Bytes)
        return 1;
    if (done)
        NgGpuPtUnmap(Pt, GpuVa, done);  /* nothing of a refused call stays mapped */
    return 0;
}

uint64_t
NgGpuPtUnmap(NG_GPU_PAGETABLE *Pt, uint64_t GpuVa, uint64_t Bytes)
{
    uint64_t unmapped = 0;

    if (!Pt || !Pt->Tables || !range_ok(GpuVa, Bytes))
        return 0;
    for (uint64_t off = 0; off < Bytes; off += NG_GPU_PT_PAGE) {
        uint64_t va = GpuVa + off;
        uint64_t *leaf = leaf_table(Pt, va, 0);

        if (leaf && leaf[index_at(va, 3)] != 0) {
            leaf[index_at(va, 3)] = 0;
            Pt->MappedPages--;
            unmapped++;
            if (((va + NG_GPU_PT_PAGE) & 0x1fffffull) == 0 || off + NG_GPU_PT_PAGE == Bytes)
                publish(Pt, leaf);
        }
    }
    return unmapped;
}

int
NgGpuPtLookup(const NG_GPU_PAGETABLE *Pt, uint64_t GpuVa, uint64_t *Phys, uint32_t *Flags)
{
    uint64_t entry;

    if (!Pt || !Pt->Tables || GpuVa >> NG_GPU_PT_VA_BITS)
        return 0;
    uint64_t *leaf = leaf_table((NG_GPU_PAGETABLE *)Pt, GpuVa & ~(uint64_t)(NG_GPU_PT_PAGE - 1), 0);
    if (!leaf || ((entry = leaf[index_at(GpuVa, 3)]) & 3u) != PTE_VALID_PAGE)
        return 0;
    if (Phys)
        *Phys = (entry & PTE_ADDRESS) | (GpuVa & (NG_GPU_PT_PAGE - 1));
    if (Flags)
        *Flags = (entry & PTE_READ_ONLY) ? NG_GPU_PT_READ_ONLY : 0;
    return 1;
}

uint64_t
NgGpuPtTtbr0(const NG_GPU_PAGETABLE *Pt, uint16_t Asid)
{
    if (!Pt || !Pt->Tables || !Pt->TableCount)
        return 0;
    return Pt->Tables[0].Phys | (uint64_t)Asid << 48;
}
