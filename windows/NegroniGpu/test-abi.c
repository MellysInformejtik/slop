/* SPDX-License-Identifier: MIT */
#include "NegroniGpuAbi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void reject(NG_GPU_CAPS_QUERY q)
{
    NG_GPU_CAPS_QUERY before = q;
    assert(!NgGpuHandleCapsQuery(&q, sizeof(q), 0));
    assert(!memcmp(&q, &before, sizeof(q)));
}

int main(void)
{
    NG_GPU_CAPS_QUERY q, before;
    unsigned char unaligned[sizeof(q) + 2];
    unsigned i;
    NgGpuInitCapsQuery(&q);
    assert(NgGpuHandleCapsQuery(&q, sizeof(q), NG_GPU_STAGE_RESOURCES_VALIDATED));
    assert(NgGpuValidateCapsReply(&q));
    assert(q.TargetGpuId == 730 && !(q.Features & NG_GPU_FEATURE_RENDER));
    assert(q.Stage == NG_GPU_STAGE_RESOURCES_VALIDATED);
    q.Features |= NG_GPU_FEATURE_RENDER;
    assert(!NgGpuValidateCapsReply(&q));
    for (i = 0; i < sizeof(q) / sizeof(uint32_t); ++i) {
        uint32_t words[sizeof(q) / sizeof(uint32_t)];
        NgGpuInitCapsQuery(&q);
        memcpy(words, &q, sizeof(words));
        words[i] ^= 0x80000000u;
        memcpy(&q, words, sizeof(q));
        reject(q);
    }
    NgGpuInitCapsQuery(&q);
    before = q;
    assert(!NgGpuHandleCapsQuery(&q, sizeof(q) - 1, 0));
    assert(!NgGpuHandleCapsQuery(&q, sizeof(q) + 1, 0));
    assert(!NgGpuHandleCapsQuery(&q, sizeof(q), 2));
    assert(!memcmp(&q, &before, sizeof(q)));
    assert(!NgGpuHandleCapsQuery(NULL, sizeof(q), 0));
    assert(!NgGpuValidateCapsReply(NULL));
    memset(unaligned, 0xa5, sizeof(unaligned));
    memcpy(unaligned + 1, &q, sizeof(q));
    assert(NgGpuHandleCapsQuery(unaligned + 1, sizeof(q), 0));
    assert(unaligned[0] == 0xa5 && unaligned[sizeof(unaligned)-1] == 0xa5);
    memcpy(&q, unaligned + 1, sizeof(q));
    assert(NgGpuValidateCapsReply(&q) && q.Stage == 0);
    puts("A730 UMD/KMD ABI tests passed");
    return 0;
}
