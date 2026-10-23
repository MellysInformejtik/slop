/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define NG_GPU_MEMORY_MAGIC 0x4d37474eu /* NG7M */
#define NG_GPU_MEMORY_VERSION 1u
#define NG_GPU_MEMORY_BUFFER 1u
#define NG_GPU_MEMORY_PAGE 4096u

/* UMD input describes a linear buffer, never a CPU/GPU address. */
typedef struct {
    uint32_t Magic, Version, Size, Kind;
    uint64_t Bytes;
    uint32_t Alignment, Flags;
    uint32_t Reserved[4];
} NG_GPU_MEMORY_REQUEST;
typedef char NG_MEMORY_SIZE_CHECK[sizeof(NG_GPU_MEMORY_REQUEST) == 48 ? 1 : -1];

typedef struct {
    uint64_t LogicalBytes, BackingBytes;
    uint32_t Alignment;
} NG_GPU_MEMORY_LAYOUT;

int NgGpuMemoryLayout(const void *Request, size_t RequestBytes,
                      NG_GPU_MEMORY_LAYOUT *Layout);
/* Checks logical bounds, including overflow; padding is never user data. */
int NgGpuMemoryRange(const NG_GPU_MEMORY_LAYOUT *Layout,
                    uint64_t Offset, uint64_t Bytes);
