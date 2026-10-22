/* SPDX-License-Identifier: MIT */
#ifndef NEGRONI_GPU_QUEUE_H
#define NEGRONI_GPU_QUEUE_H

#include <stdint.h>

typedef struct _NG_GPU_RING {
    uint32_t *Words;
    uint32_t CapacityWords;
    uint32_t Mask;
    uint64_t WriteSequence;
    uint64_t ReadSequence;
} NG_GPU_RING;

typedef struct _NG_GPU_FENCES {
    uint64_t LastSubmitted;
    uint64_t Completed;
} NG_GPU_FENCES;

/* Caller serializes access (for example with a WDDM/KMD spin lock). */
int NgGpuRingInit(NG_GPU_RING *Ring, uint32_t *Storage, uint32_t CapacityWords);
int NgGpuRingWrite(NG_GPU_RING *Ring, const uint32_t *Words, uint32_t WordCount);
int NgGpuRingUpdateReadPointer(NG_GPU_RING *Ring, uint32_t HardwareReadPointer);
uint32_t NgGpuRingWritePointer(const NG_GPU_RING *Ring);
uint32_t NgGpuRingUsedWords(const NG_GPU_RING *Ring);

void NgGpuFencesInit(NG_GPU_FENCES *Fences);
uint64_t NgGpuFenceIssue(NG_GPU_FENCES *Fences);
int NgGpuFenceComplete(NG_GPU_FENCES *Fences, uint64_t CompletedValue);
int NgGpuFenceIsComplete(const NG_GPU_FENCES *Fences, uint64_t FenceValue);

#endif
