/* SPDX-License-Identifier: MIT */
#ifndef NEGRONI_GPU_ABI_H
#define NEGRONI_GPU_ABI_H

#include <stddef.h>
#include <stdint.h>

/* Pointer-free, fixed-width wire format shared by native ARM64 UMD and KMD. */
#define NG_GPU_ABI_MAGIC 0x3337474eu /* NG73 */
#define NG_GPU_ABI_VERSION 1u
#define NG_GPU_QUERY_CAPS 1u
#define NG_GPU_FEATURE_QUERY_CAPS 1u
#define NG_GPU_FEATURE_RENDER (1u << 1)
#define NG_GPU_STAGE_NOT_STARTED 0u
#define NG_GPU_STAGE_RESOURCES_VALIDATED 1u

typedef struct _NG_GPU_CAPS_QUERY {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Size;
    uint32_t Operation;
    uint32_t TargetGpuId; /* Build target, not a hardware-detected chip ID. */
    uint32_t Features;
    uint32_t Stage;
    uint32_t Reserved[9];
} NG_GPU_CAPS_QUERY;

typedef char NG_GPU_CAPS_QUERY_SIZE_CHECK[(sizeof(NG_GPU_CAPS_QUERY) == 64) ? 1 : -1];

void NgGpuInitCapsQuery(NG_GPU_CAPS_QUERY *Query);
/* Invalid requests leave the caller's entire buffer unchanged. No MMIO. */
int NgGpuHandleCapsQuery(void *Buffer, size_t Length, uint32_t Stage);
int NgGpuValidateCapsReply(const NG_GPU_CAPS_QUERY *Reply);

#endif
