/* SPDX-License-Identifier: MIT */
#include "NegroniGpuMemory.h"
#include <string.h>

int NgGpuMemoryLayout(const void *Request, size_t RequestBytes,
                      NG_GPU_MEMORY_LAYOUT *Layout)
{
    NG_GPU_MEMORY_REQUEST request;
    NG_GPU_MEMORY_LAYOUT result;
    uint64_t mask;
    unsigned i;
    if (!Request || !Layout || RequestBytes != sizeof(request))
        return 0;
    memcpy(&request, Request, sizeof(request));
    if (request.Magic != NG_GPU_MEMORY_MAGIC ||
        request.Version != NG_GPU_MEMORY_VERSION ||
        request.Size != sizeof(request) || request.Kind != NG_GPU_MEMORY_BUFFER ||
        !request.Bytes || request.Flags)
        return 0;
    for (i = 0; i < 4; ++i)
        if (request.Reserved[i]) return 0;
    if (request.Alignment < NG_GPU_MEMORY_PAGE || request.Alignment > 65536 ||
        (request.Alignment & (request.Alignment - 1)))
        return 0;
    /* VidMm rounds storage to native pages; starting alignment is separate. */
    mask = NG_GPU_MEMORY_PAGE - 1;
    if (request.Bytes > UINT64_MAX - mask)
        return 0;
    memset(&result, 0, sizeof(result));
    result.LogicalBytes = request.Bytes;
    result.BackingBytes = (request.Bytes + mask) & ~mask;
    if (result.BackingBytes > SIZE_MAX)
        return 0;
    result.Alignment = request.Alignment;
    *Layout = result;
    return 1;
}

int NgGpuMemoryRange(const NG_GPU_MEMORY_LAYOUT *Layout,
                    uint64_t Offset, uint64_t Bytes)
{
    return Layout && Bytes && Offset < Layout->LogicalBytes &&
           Bytes <= Layout->LogicalBytes - Offset;
}
