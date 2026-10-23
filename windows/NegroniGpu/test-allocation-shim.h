#pragma once
#include "test-objects-shim.h"
typedef union {
    unsigned Value;
    struct { unsigned CpuVisible : 1; unsigned Reserved : 31; };
} NG_TEST_ALLOCATION_FLAGS;
typedef union {
    unsigned Value;
    struct { unsigned SegmentId0 : 5; unsigned Reserved : 27; };
} NG_TEST_SEGMENT;
typedef struct {
    void *pPrivateDriverData;
    unsigned PrivateDriverDataSize, Alignment;
    SIZE_T Size, PitchAlignedSize;
    NG_TEST_FLAGS HintedBank;
    NG_TEST_SEGMENT PreferredSegment;
    unsigned SupportedReadSegmentSet, SupportedWriteSegmentSet, EvictionSegmentSet;
    unsigned MaximumRenamingListLength;
    HANDLE hAllocation;
    NG_TEST_ALLOCATION_FLAGS Flags;
    void *pAllocationUsageHint;
    unsigned AllocationPriority;
} DXGK_ALLOCATIONINFO;
typedef struct {
    const void *pPrivateDriverData;
    unsigned PrivateDriverDataSize, NumAllocations;
    DXGK_ALLOCATIONINFO *pAllocationInfo;
    HANDLE hResource;
    NG_TEST_FLAGS Flags;
} DXGKARG_CREATEALLOCATION, *INOUT_PDXGKARG_CREATEALLOCATION;
typedef struct {
    unsigned NumAllocations;
    const HANDLE *pAllocationList;
    HANDLE hResource;
    NG_TEST_FLAGS Flags;
} DXGKARG_DESTROYALLOCATION;
typedef const DXGKARG_DESTROYALLOCATION *IN_CONST_PDXGKARG_DESTROYALLOCATION;
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
