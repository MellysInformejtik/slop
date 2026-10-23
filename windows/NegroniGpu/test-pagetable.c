/* SPDX-License-Identifier: MIT */
#include "NegroniGpuPageTable.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tables from the C heap; their "physical" address is their own (4 KiB aligned, below 2^48). */
static unsigned allocated, freed, published, refuse_after = ~0u;

static void *
alloc_table(void *context, uint64_t *phys)
{
    (void)context;
    if (allocated >= refuse_after)
        return NULL;
    void *table = aligned_alloc(NG_GPU_PT_PAGE, NG_GPU_PT_PAGE);
    assert(table);
    memset(table, 0xa5, NG_GPU_PT_PAGE);    /* the module must zero it */
    allocated++;
    *phys = (uint64_t)(uintptr_t)table;
    return table;
}

static void
free_table(void *context, void *table, uint64_t phys)
{
    (void)context;
    assert((uint64_t)(uintptr_t)table == phys);
    free(table);
    freed++;
}

static void
publish_table(void *context, void *table)
{
    (void)context;
    assert(table);
    published++;
}

static const NG_GPU_PT_OPS ops = { alloc_table, free_table, publish_table, NULL };

static void
fresh(NG_GPU_PAGETABLE *pt, NG_GPU_PT_TABLE *storage, uint32_t capacity)
{
    allocated = freed = published = 0;
    refuse_after = ~0u;
    assert(NgGpuPtInit(pt, &ops, storage, capacity));
    assert(pt->TableCount == 1 && allocated == 1);
}

int
main(void)
{
    NG_GPU_PAGETABLE pt;
    NG_GPU_PT_TABLE storage[64];
    uint64_t phys;
    uint32_t flags;
    const uint64_t va = 0x100000000ull, pa = 0x80000000ull;

    /* map, look up, attributes */
    fresh(&pt, storage, 64);
    assert(NgGpuPtMap(&pt, va, pa, 3 * NG_GPU_PT_PAGE, 0));
    assert(pt.MappedPages == 3 && pt.TableCount == 3);          /* root, L2, L3 */
    for (unsigned i = 0; i < 3; ++i) {
        assert(NgGpuPtLookup(&pt, va + i * NG_GPU_PT_PAGE + 0x123, &phys, &flags));
        assert(phys == pa + i * NG_GPU_PT_PAGE + 0x123 && flags == 0);
    }
    assert(!NgGpuPtLookup(&pt, va + 3 * NG_GPU_PT_PAGE, &phys, &flags));
    assert(NgGpuPtMap(&pt, va + 0x10000, pa + 0x10000, NG_GPU_PT_PAGE, NG_GPU_PT_READ_ONLY));
    assert(NgGpuPtLookup(&pt, va + 0x10000, &phys, &flags) && flags == NG_GPU_PT_READ_ONLY);
    {   /* the descriptor itself: page, AF, inner shareable, nG, AP[1] unprivileged, AP[2] read-only */
        uint64_t *l2 = (uint64_t *)(uintptr_t)(storage[0].Virt[(va >> 30) & 0x1ff] & 0x0000fffffffff000ull);
        uint64_t *l3 = (uint64_t *)(uintptr_t)(l2[(va >> 21) & 0x1ff] & 0x0000fffffffff000ull);
        assert(l3[0] == (pa | 3u | 1u << 6 | 2u << 8 | 1u << 10 | 1u << 11));
        assert(l3[16] == ((pa + 0x10000) | 3u | 1u << 6 | 1u << 7 | 2u << 8 | 1u << 10 | 1u << 11));
    }
    assert(published >= 3);

    /* an overlapping map is refused and leaves nothing of itself behind */
    assert(!NgGpuPtMap(&pt, va - 2 * NG_GPU_PT_PAGE, 0x90000000ull, 4 * NG_GPU_PT_PAGE, 0));
    assert(!NgGpuPtLookup(&pt, va - 2 * NG_GPU_PT_PAGE, NULL, NULL));
    assert(!NgGpuPtLookup(&pt, va - NG_GPU_PT_PAGE, NULL, NULL));
    assert(NgGpuPtLookup(&pt, va, &phys, NULL) && phys == pa);
    assert(pt.MappedPages == 4);

    /* across 2 MiB and 1 GiB boundaries: the tables needed and no more */
    uint32_t before = pt.TableCount;
    assert(NgGpuPtMap(&pt, va + 0x200000 - NG_GPU_PT_PAGE, pa, 2 * NG_GPU_PT_PAGE, 0));
    assert(pt.TableCount == before + 1);
    assert(NgGpuPtMap(&pt, 0x140000000ull - NG_GPU_PT_PAGE, pa, 2 * NG_GPU_PT_PAGE, 0));
    assert(pt.TableCount == before + 1 + 3);                   /* L3 under the old L2; new L2 and L3 */

    /* refused arguments */
    assert(!NgGpuPtMap(&pt, va + 1, pa, NG_GPU_PT_PAGE, 0));
    assert(!NgGpuPtMap(&pt, va, pa + 1, NG_GPU_PT_PAGE, 0));
    assert(!NgGpuPtMap(&pt, va, pa, 100, 0));
    assert(!NgGpuPtMap(&pt, va, pa, 0, 0));
    assert(!NgGpuPtMap(&pt, 1ull << 39, pa, NG_GPU_PT_PAGE, 0));
    assert(!NgGpuPtMap(&pt, (1ull << 39) - NG_GPU_PT_PAGE, pa, 2 * NG_GPU_PT_PAGE, 0));
    assert(!NgGpuPtMap(&pt, 0x200000000ull, 1ull << 48, NG_GPU_PT_PAGE, 0));
    assert(!NgGpuPtMap(&pt, 0x200000000ull, pa, NG_GPU_PT_PAGE, 2));
    assert(!NgGpuPtLookup(&pt, 1ull << 39, NULL, NULL));

    /* unmap: counts what was there, skips holes */
    uint64_t mapped = pt.MappedPages;
    assert(NgGpuPtUnmap(&pt, va, 8 * NG_GPU_PT_PAGE) == 3);
    assert(pt.MappedPages == mapped - 3);
    assert(!NgGpuPtLookup(&pt, va, NULL, NULL));
    assert(NgGpuPtMap(&pt, va, pa + 0x5000, NG_GPU_PT_PAGE, 0));          /* reusable */
    assert(NgGpuPtLookup(&pt, va, &phys, NULL) && phys == pa + 0x5000);

    /* TTBR0 */
    assert(NgGpuPtTtbr0(&pt, 2) == (storage[0].Phys | 2ull << 48));

    NgGpuPtDestroy(&pt);
    assert(freed == allocated);

    /* out of table bookkeeping: refused, nothing mapped, nothing leaked */
    fresh(&pt, storage, 3);
    assert(NgGpuPtMap(&pt, va, pa, NG_GPU_PT_PAGE, 0));
    assert(!NgGpuPtMap(&pt, va + 0x200000, pa, NG_GPU_PT_PAGE, 0));
    assert(!NgGpuPtLookup(&pt, va + 0x200000, NULL, NULL) && pt.MappedPages == 1);
    NgGpuPtDestroy(&pt);
    assert(freed == allocated);

    /* the allocator refuses: same */
    fresh(&pt, storage, 64);
    refuse_after = 2;
    assert(!NgGpuPtMap(&pt, va, pa, NG_GPU_PT_PAGE, 0));
    assert(pt.MappedPages == 0);
    NgGpuPtDestroy(&pt);
    assert(freed == allocated);

    assert(!NgGpuPtInit(&pt, &ops, storage, 2));
    puts("A730 SMMU page table tests passed");
    return 0;
}
