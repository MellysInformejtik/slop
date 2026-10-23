/* SPDX-License-Identifier: MIT */
#include "NegroniGpuAbi.h"
#include <string.h>

void NgGpuInitCapsQuery(NG_GPU_CAPS_QUERY *Query)
{
    if (Query == NULL)
        return;
    memset(Query, 0, sizeof(*Query));
    Query->Magic = NG_GPU_ABI_MAGIC;
    Query->Version = NG_GPU_ABI_VERSION;
    Query->Size = sizeof(*Query);
    Query->Operation = NG_GPU_QUERY_CAPS;
}

static int NgGpuValidCapsHeader(const NG_GPU_CAPS_QUERY *Query)
{
    size_t i;
    if (Query->Magic != NG_GPU_ABI_MAGIC ||
        Query->Version != NG_GPU_ABI_VERSION ||
        Query->Size != sizeof(*Query) || Query->Operation != NG_GPU_QUERY_CAPS)
        return 0;
    for (i = 0; i < sizeof(Query->Reserved) / sizeof(Query->Reserved[0]); ++i)
        if (Query->Reserved[i] != 0)
            return 0;
    return 1;
}

int NgGpuHandleCapsQuery(void *Buffer, size_t Length, uint32_t Stage)
{
    NG_GPU_CAPS_QUERY query;
    if (Buffer == NULL || Length != sizeof(query) ||
        Stage > NG_GPU_STAGE_RESOURCES_VALIDATED)
        return 0;
    /* Copy once: unaligned buffers work, and all validation uses this snapshot. */
    memcpy(&query, Buffer, sizeof(query));
    if (!NgGpuValidCapsHeader(&query) || query.TargetGpuId != 0 ||
        query.Features != 0 || query.Stage != 0)
        return 0;
    query.TargetGpuId = 730;
    query.Features = NG_GPU_FEATURE_QUERY_CAPS;
    query.Stage = Stage;
    memcpy(Buffer, &query, sizeof(query));
    return 1;
}

int NgGpuValidateCapsReply(const NG_GPU_CAPS_QUERY *Reply)
{
    return Reply != NULL && NgGpuValidCapsHeader(Reply) &&
        Reply->TargetGpuId == 730 && Reply->Features == NG_GPU_FEATURE_QUERY_CAPS &&
        Reply->Stage <= NG_GPU_STAGE_RESOURCES_VALIDATED;
}
