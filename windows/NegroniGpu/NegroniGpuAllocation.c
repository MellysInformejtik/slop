/* VidMm allocation metadata. Does NOT allocate/map physical GPU memory.
 * Caller supplies an implemented aperture segment; 0 means unavailable.
 * Allocation objects are adapter scoped, never owned by a device/context.
 */
#ifdef NG_ALLOCATION_TEST
#include "test-allocation-shim.h"
#else
#include <ntddk.h>
#include <dispmprt.h>
#endif
#include "NegroniGpuMemory.h"
#include "NegroniGpuAllocation.h"

#define NG_ALLOCATION_TAG 'aNgN'
typedef struct _NG_ALLOCATION {
    struct _NG_ALLOCATION *Next;
    NG_GPU_MEMORY_LAYOUT Layout;
} NG_ALLOCATION;

NTSTATUS NgGpuCreateAllocations(INOUT_PDXGKARG_CREATEALLOCATION Args,
                               unsigned ApertureSegmentId)
{
    NG_ALLOCATION *head = NULL, **tail = &head, *allocation, *next;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    unsigned i;
    if (!Args || !Args->NumAllocations || !Args->pAllocationInfo)
        return STATUS_INVALID_PARAMETER;
    if (Args->Flags.Value || Args->hResource || Args->PrivateDriverDataSize)
        return STATUS_NOT_SUPPORTED;
    if (ApertureSegmentId != 1)
        return STATUS_INVALID_DEVICE_STATE;
    /* Stage all objects first: rejected batch must expose no partial handles. */
    for (i = 0; i < Args->NumAllocations; ++i) {
        DXGK_ALLOCATIONINFO *info = &Args->pAllocationInfo[i];
        NG_GPU_MEMORY_LAYOUT layout;
        if (info->Flags.Value ||
            !NgGpuMemoryLayout(info->pPrivateDriverData,
                               info->PrivateDriverDataSize, &layout))
            goto rollback;
        allocation = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                     sizeof(*allocation), NG_ALLOCATION_TAG);
        if (!allocation) {
            status = STATUS_NO_MEMORY;
            goto rollback;
        }
        allocation->Layout = layout;
        *tail = allocation;
        tail = &allocation->Next;
    }
    allocation = head;
    for (i = 0; i < Args->NumAllocations; ++i) {
        DXGK_ALLOCATIONINFO *info = &Args->pAllocationInfo[i];
        next = allocation->Next;
        allocation->Next = NULL;
        info->Alignment = allocation->Layout.Alignment;
        info->Size = (SIZE_T)allocation->Layout.BackingBytes;
        info->PitchAlignedSize = 0;
        info->HintedBank.Value = 0;
        info->PreferredSegment.Value = 0;
        info->PreferredSegment.SegmentId0 = ApertureSegmentId;
        info->SupportedReadSegmentSet = 1u << (ApertureSegmentId - 1);
        info->SupportedWriteSegmentSet = info->SupportedReadSegmentSet;
        info->EvictionSegmentSet = 0;
        info->MaximumRenamingListLength = 0;
        info->Flags.Value = 0;
        info->Flags.CpuVisible = 1;
        info->pAllocationUsageHint = NULL;
        info->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
        info->hAllocation = allocation;
        allocation = next;
    }
    return STATUS_SUCCESS;
rollback:
    while (head) {
        next = head->Next;
        ExFreePoolWithTag(head, NG_ALLOCATION_TAG);
        head = next;
    }
    return status;
}

NTSTATUS NgGpuDestroyAllocations(IN_CONST_PDXGKARG_DESTROYALLOCATION Args)
{
    unsigned i, j;
    if (!Args || !Args->NumAllocations || !Args->pAllocationList)
        return STATUS_INVALID_PARAMETER;
    if (Args->Flags.Value || Args->hResource)
        return STATUS_NOT_SUPPORTED;
    /* Dxgkrnl supplies valid driver handles. Reject malformed lists atomically. */
    for (i = 0; i < Args->NumAllocations; ++i) {
        if (!Args->pAllocationList[i]) return STATUS_INVALID_PARAMETER;
        for (j = 0; j < i; ++j)
            if (Args->pAllocationList[i] == Args->pAllocationList[j])
                return STATUS_INVALID_PARAMETER;
    }
    for (i = 0; i < Args->NumAllocations; ++i)
        ExFreePoolWithTag(Args->pAllocationList[i], NG_ALLOCATION_TAG);
    return STATUS_SUCCESS;
}
