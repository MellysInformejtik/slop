/* SPDX-License-Identifier: MIT */
#ifndef NEGRONI_GPU_FIRMWARE_H
#define NEGRONI_GPU_FIRMWARE_H

#include <stddef.h>
#include <stdint.h>

#define NG_GMU_FW_MAX_BLOCKS 256u
#define NG_ZAP_FW_MAX_SEGMENTS 16u
#define NG_ZAP_FW_MAX_SPAN 0x00002000u

typedef enum _NG_GMU_REGION {
    NgGmuRegionInvalid = 0,
    NgGmuRegionItcm,
    NgGmuRegionDtcm,
    NgGmuRegionIcache
} NG_GMU_REGION;

typedef struct _NG_GMU_FW_BLOCK {
    uint32_t Address;
    uint32_t Size;
    uint32_t Type;
    uint32_t Value;
    const uint8_t *Data;
    NG_GMU_REGION Region;
} NG_GMU_FW_BLOCK;

typedef struct _NG_ZAP_FW_SEGMENT {
    uint32_t Index;
    uint32_t PhysicalAddress;
    uint32_t FileSize;
    uint32_t MemorySize;
} NG_ZAP_FW_SEGMENT;

typedef struct _NG_ZAP_FW_IMAGE {
    uint32_t SegmentCount;
    uint32_t LowestAddress;
    uint32_t SpanSize;
    NG_ZAP_FW_SEGMENT Segments[NG_ZAP_FW_MAX_SEGMENTS];
} NG_ZAP_FW_IMAGE;

/* No allocation, MMIO, file access, or TrustZone calls. */
int NgParseGmuFirmware(const uint8_t *Buffer, size_t Length,
                       NG_GMU_FW_BLOCK *Blocks, size_t Capacity,
                       size_t *BlockCount);
int NgParseZapManifest(const uint8_t *Buffer, size_t Length,
                       NG_ZAP_FW_IMAGE *Image);

#endif
