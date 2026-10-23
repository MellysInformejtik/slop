/* Host-only stand-ins. ARM64 build separately checks actual WDK declarations. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
typedef void *HANDLE;
typedef size_t SIZE_T;
typedef int BOOLEAN;
typedef int32_t NTSTATUS;
typedef pthread_mutex_t FAST_MUTEX;
#define APIENTRY
#define IN_CONST_HANDLE const HANDLE
#define TRUE 1
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_NOT_SUPPORTED (-2)
#define STATUS_NO_MEMORY (-3)
#define STATUS_INVALID_DEVICE_STATE (-4)
#define POOL_FLAG_NON_PAGED 1
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef struct { unsigned Value; } NG_TEST_FLAGS;
typedef struct {
    HANDLE hDevice;
    NG_TEST_FLAGS Flags;
    unsigned Pasid;
    HANDLE hKmdProcess;
} DXGKARG_CREATEDEVICE, *INOUT_PDXGKARG_CREATEDEVICE;
typedef struct {
    unsigned DmaBufferSize, DmaBufferSegmentSet, DmaBufferPrivateDataSize;
    unsigned AllocationListSize, PatchLocationListSize, Reserved;
    NG_TEST_FLAGS Caps;
    unsigned PagingCompanionNodeId;
} DXGK_CONTEXTINFO;
typedef struct {
    HANDLE hContext;
    unsigned NodeOrdinal, EngineAffinity;
    NG_TEST_FLAGS Flags;
    void *pPrivateDriverData;
    unsigned PrivateDriverDataSize;
    DXGK_CONTEXTINFO ContextInfo;
} DXGKARG_CREATECONTEXT, *INOUT_PDXGKARG_CREATECONTEXT;
void *ExAllocatePool2(unsigned, size_t, unsigned);
void ExFreePoolWithTag(void *, unsigned);
static inline void ExInitializeFastMutex(FAST_MUTEX *m) {
    assert(pthread_mutex_init(m, NULL) == 0);
}
static inline void ExAcquireFastMutex(FAST_MUTEX *m) {
    assert(pthread_mutex_lock(m) == 0);
}
static inline void ExReleaseFastMutex(FAST_MUTEX *m) {
    assert(pthread_mutex_unlock(m) == 0);
}
