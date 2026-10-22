/* SPDX-License-Identifier: MIT */
#include "NegroniGpuQueue.h"
#include <stddef.h>

static int
NgGpuIsPowerOfTwo(uint32_t Value)
{
    return Value >= 2u && (Value & (Value - 1u)) == 0;
}

int
NgGpuRingInit(NG_GPU_RING *Ring, uint32_t *Storage, uint32_t CapacityWords)
{
    if (Ring == NULL || Storage == NULL || !NgGpuIsPowerOfTwo(CapacityWords))
        return 0;
    Ring->Words = Storage;
    Ring->CapacityWords = CapacityWords;
    Ring->Mask = CapacityWords - 1u;
    Ring->WriteSequence = 0;
    Ring->ReadSequence = 0;
    return 1;
}

uint32_t
NgGpuRingUsedWords(const NG_GPU_RING *Ring)
{
    uint64_t used;
    if (Ring == NULL || Ring->Words == NULL ||
        Ring->WriteSequence < Ring->ReadSequence)
        return UINT32_MAX;
    used = Ring->WriteSequence - Ring->ReadSequence;
    if (used >= Ring->CapacityWords)
        return UINT32_MAX;
    return (uint32_t)used;
}

int
NgGpuRingWrite(NG_GPU_RING *Ring, const uint32_t *Words, uint32_t WordCount)
{
    uint32_t used, i;

    if (Ring == NULL || Ring->Words == NULL ||
        (WordCount != 0 && Words == NULL))
        return 0;
    used = NgGpuRingUsedWords(Ring);
    if (used == UINT32_MAX || WordCount > Ring->CapacityWords - 1u - used ||
        Ring->WriteSequence > UINT64_MAX - WordCount)
        return 0;

    for (i = 0; i < WordCount; ++i)
        Ring->Words[(uint32_t)(Ring->WriteSequence + i) & Ring->Mask] = Words[i];
    Ring->WriteSequence += WordCount;
    return 1;
}

int
NgGpuRingUpdateReadPointer(NG_GPU_RING *Ring, uint32_t HardwareReadPointer)
{
    uint64_t candidate, base;

    if (Ring == NULL || Ring->Words == NULL ||
        HardwareReadPointer >= Ring->CapacityWords ||
        NgGpuRingUsedWords(Ring) == UINT32_MAX)
        return 0;

    base = Ring->ReadSequence & ~(uint64_t)Ring->Mask;
    candidate = base | HardwareReadPointer;
    if (candidate < Ring->ReadSequence) {
        if (candidate > UINT64_MAX - Ring->CapacityWords)
            return 0;
        candidate += Ring->CapacityWords;
    }
    if (candidate > Ring->WriteSequence)
        return 0;
    Ring->ReadSequence = candidate;
    return 1;
}

uint32_t
NgGpuRingWritePointer(const NG_GPU_RING *Ring)
{
    if (Ring == NULL || Ring->Words == NULL)
        return UINT32_MAX;
    return (uint32_t)Ring->WriteSequence & Ring->Mask;
}

void
NgGpuFencesInit(NG_GPU_FENCES *Fences)
{
    if (Fences == NULL)
        return;
    Fences->LastSubmitted = 0;
    Fences->Completed = 0;
}

uint64_t
NgGpuFenceIssue(NG_GPU_FENCES *Fences)
{
    if (Fences == NULL || Fences->LastSubmitted == UINT64_MAX)
        return 0;
    return ++Fences->LastSubmitted;
}

int
NgGpuFenceComplete(NG_GPU_FENCES *Fences, uint64_t CompletedValue)
{
    if (Fences == NULL || CompletedValue < Fences->Completed ||
        CompletedValue > Fences->LastSubmitted)
        return 0;
    Fences->Completed = CompletedValue;
    return 1;
}

int
NgGpuFenceIsComplete(const NG_GPU_FENCES *Fences, uint64_t FenceValue)
{
    return Fences != NULL && FenceValue != 0 &&
        FenceValue <= Fences->Completed;
}
