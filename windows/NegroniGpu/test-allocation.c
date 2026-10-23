#include "test-allocation-shim.h"
#include "NegroniGpuMemory.h"
#include "NegroniGpuAllocation.h"
#include <stdio.h>

static unsigned live, attempts, fail_at;
void *ExAllocatePool2(unsigned flags, size_t size, unsigned tag)
{
    void *p;
    (void)flags; (void)tag;
    if (++attempts == fail_at) return NULL;
    p = calloc(1, size);
    if (p) ++live;
    return p;
}
void ExFreePoolWithTag(void *p, unsigned tag)
{
    (void)tag;
    assert(p && live);
    --live; free(p);
}
static NG_GPU_MEMORY_REQUEST request(void)
{
    NG_GPU_MEMORY_REQUEST r = {0};
    r.Magic = NG_GPU_MEMORY_MAGIC; r.Version = NG_GPU_MEMORY_VERSION;
    r.Size = sizeof(r); r.Kind = NG_GPU_MEMORY_BUFFER;
    r.Bytes = 4097; r.Alignment = 4096;
    return r;
}
int main(void)
{
    NG_GPU_MEMORY_REQUEST r = request(), bad;
    NG_GPU_MEMORY_LAYOUT layout, saved;
    unsigned char unaligned[sizeof(r) + 1];
    DXGK_ALLOCATIONINFO infos[4], before[4];
    DXGKARG_CREATEALLOCATION args = {0};
    DXGKARG_DESTROYALLOCATION destroy = {0};
    HANDLE handles[4], duplicate[2];
    unsigned i, j;
    assert(NgGpuMemoryLayout(&r, sizeof(r), &layout));
    assert(layout.LogicalBytes == 4097 && layout.BackingBytes == 8192);
    assert(NgGpuMemoryRange(&layout, 4096, 1));
    assert(!NgGpuMemoryRange(&layout, 4096, 2));
    assert(!NgGpuMemoryRange(&layout, UINT64_MAX, 2));
    assert(!NgGpuMemoryRange(&layout, 1, UINT64_MAX));
    assert(!NgGpuMemoryRange(&layout, 0, 0));
    saved = layout;
    for (i = 0; i < sizeof(r); ++i) {
        assert(!NgGpuMemoryLayout(&r, i, &layout));
        assert(!memcmp(&layout, &saved, sizeof(layout)));
    }
    memcpy(unaligned + 1, &r, sizeof(r));
    assert(NgGpuMemoryLayout(unaligned + 1, sizeof(r), &layout));
    for (i = 0; i < 4; ++i) {
        bad = r; bad.Reserved[i] = 1;
        assert(!NgGpuMemoryLayout(&bad, sizeof(bad), &layout));
    }
    bad = r; bad.Bytes = UINT64_MAX;
    assert(!NgGpuMemoryLayout(&bad, sizeof(bad), &layout));
    bad.Bytes = 0;
    assert(!NgGpuMemoryLayout(&bad, sizeof(bad), &layout));
    bad = r; bad.Alignment = 6144;
    assert(!NgGpuMemoryLayout(&bad, sizeof(bad), &layout));
    bad.Alignment = 131072;
    assert(!NgGpuMemoryLayout(&bad, sizeof(bad), &layout));
    bad = r; bad.Kind = 2;
    assert(!NgGpuMemoryLayout(&bad, sizeof(bad), &layout));
    bad = r; bad.Flags = 1;
    assert(!NgGpuMemoryLayout(&bad, sizeof(bad), &layout));

    memset(infos, 0, sizeof(infos));
    for (i = 0; i < 4; ++i) {
        infos[i].pPrivateDriverData = &r;
        infos[i].PrivateDriverDataSize = sizeof(r);
        infos[i].Size = 123; /* output canary */
    }
    memcpy(before, infos, sizeof(infos));
    args.NumAllocations = 4; args.pAllocationInfo = infos;
    assert(NgGpuCreateAllocations(&args, 0) == STATUS_INVALID_DEVICE_STATE);
    assert(!live && !attempts && !memcmp(before, infos, sizeof(infos)));
    /* Every failed allocation position must release all earlier objects. */
    for (i = 1; i <= 4; ++i) {
        attempts = 0; fail_at = i;
        assert(NgGpuCreateAllocations(&args, 1) == STATUS_NO_MEMORY);
        assert(!live && !memcmp(before, infos, sizeof(infos)));
    }
    fail_at = 0;
    for (i = 0; i < 4; ++i) {
        infos[i].PrivateDriverDataSize--;
        assert(NgGpuCreateAllocations(&args, 1) == STATUS_INVALID_PARAMETER);
        assert(!live);
        infos[i].PrivateDriverDataSize++;
        assert(!memcmp(before, infos, sizeof(infos)));
    }
    assert(NgGpuCreateAllocations(&args, 1) == 0 && live == 4);
    for (i = 0; i < 4; ++i) {
        handles[i] = infos[i].hAllocation;
        assert(handles[i] && infos[i].Size == 8192);
        assert(infos[i].Alignment == 4096 && infos[i].Flags.CpuVisible);
        assert(infos[i].SupportedReadSegmentSet == 1);
        assert(infos[i].SupportedWriteSegmentSet == 1);
        for (j = 0; j < i; ++j) assert(handles[i] != handles[j]);
    }
    duplicate[0] = handles[0]; duplicate[1] = handles[0];
    destroy.NumAllocations = 2; destroy.pAllocationList = duplicate;
    assert(NgGpuDestroyAllocations(&destroy) == STATUS_INVALID_PARAMETER);
    assert(live == 4);
    duplicate[1] = NULL;
    assert(NgGpuDestroyAllocations(&destroy) == STATUS_INVALID_PARAMETER);
    assert(live == 4);
    destroy.NumAllocations = 4; destroy.pAllocationList = handles;
    assert(NgGpuDestroyAllocations(&destroy) == 0 && !live);
    puts("A730 allocation layout, bounds and transactional ownership tests passed");
    return 0;
}
