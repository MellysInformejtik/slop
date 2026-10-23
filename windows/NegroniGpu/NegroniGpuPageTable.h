/* SPDX-License-Identifier: MIT */
#ifndef NEGRONI_GPU_PAGETABLE_H
#define NEGRONI_GPU_PAGETABLE_H

#include <stdint.h>

/*
 * Stage-1 page tables of the Adreno SMMU context the 3D core reads and writes through: 39-bit
 * GPU virtual addresses, three levels (1 GiB / 2 MiB / 4 KiB), 4 KiB pages, the format the XNU
 * port runs on the phone (bsd/dev/negroni_adreno.c space_map/space_on, Linux io-pgtable-arm).
 * CPU-only: it fills tables in memory the caller provides and says what to write to the context
 * bank; it does not touch the SMMU or invalidate its TLB. Caller serializes access.
 */
#define NG_GPU_PT_VA_BITS       39u
#define NG_GPU_PT_PAGE          4096u
#define NG_GPU_PT_READ_ONLY     1u

/* What the context bank is given with these tables (arm-smmu.h layout, as on the phone). */
#define NG_GPU_PT_TCR           (25u | 1u << 8 | 1u << 10 | 3u << 12 | 1u << 23)   /* T0SZ 25, WB WA, inner shareable, EPD1 */
#define NG_GPU_PT_TCR2          (1u | 1u << 4 | 7u << 15)
#define NG_GPU_PT_MAIR0         0xf404ff44u     /* index 0: normal non-cacheable */
#define NG_GPU_PT_SCTLR         (1u << 0 | 1u << 1 | 1u << 2 | 1u << 5 | 1u << 12)

typedef struct _NG_GPU_PT_OPS {
    /* One zeroed, 4 KiB-aligned 4 KiB table; its physical address in *Phys. NULL: none. */
    void *(*AllocTable)(void *Context, uint64_t *Phys);
    void (*FreeTable)(void *Context, void *Table, uint64_t Phys);
    /* The table's bytes are about to be read by the SMMU (clean the CPU cache); may be NULL. */
    void (*Publish)(void *Context, void *Table);
    void *Context;
} NG_GPU_PT_OPS;

typedef struct _NG_GPU_PT_TABLE {
    uint64_t *Virt;
    uint64_t Phys;
} NG_GPU_PT_TABLE;

typedef struct _NG_GPU_PAGETABLE {
    NG_GPU_PT_OPS Ops;
    NG_GPU_PT_TABLE *Tables;            /* Tables[0] is the root */
    uint32_t TableCount, TableCapacity;
    uint64_t MappedPages;
} NG_GPU_PAGETABLE;

/* Storage holds the bookkeeping for up to Capacity tables (root included). 1: success. */
int NgGpuPtInit(NG_GPU_PAGETABLE *Pt, const NG_GPU_PT_OPS *Ops, NG_GPU_PT_TABLE *Storage, uint32_t Capacity);
void NgGpuPtDestroy(NG_GPU_PAGETABLE *Pt);
/* Bytes from GpuVa to physical Phys, 4 KiB aligned. A page already mapped is refused and nothing of
 * this call stays mapped. 1: success. */
int NgGpuPtMap(NG_GPU_PAGETABLE *Pt, uint64_t GpuVa, uint64_t Phys, uint64_t Bytes, uint32_t Flags);
/* Pages not mapped are skipped. Returns the pages that were unmapped. */
uint64_t NgGpuPtUnmap(NG_GPU_PAGETABLE *Pt, uint64_t GpuVa, uint64_t Bytes);
/* 1, with *Phys and *Flags filled in, when the page of GpuVa is mapped. */
int NgGpuPtLookup(const NG_GPU_PAGETABLE *Pt, uint64_t GpuVa, uint64_t *Phys, uint32_t *Flags);
/* TTBR0 for the context bank: the root's address with the ASID above it. */
uint64_t NgGpuPtTtbr0(const NG_GPU_PAGETABLE *Pt, uint16_t Asid);

#endif
