/* SPDX-License-Identifier: MIT */
#include "NegroniGpuQueue.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void
TestRingWrapAndCapacity(void)
{
    uint32_t words[8] = {0};
    uint32_t first[] = { 10, 11, 12, 13, 14 };
    uint32_t second[] = { 20, 21, 22, 23, 24 };
    NG_GPU_RING ring;

    assert(NgGpuRingInit(&ring, words, 8));
    assert(!NgGpuRingInit(&ring, words, 7));
    assert(NgGpuRingWrite(&ring, first, 5));
    assert(NgGpuRingWritePointer(&ring) == 5);
    assert(NgGpuRingUsedWords(&ring) == 5);
    assert(!NgGpuRingWrite(&ring, second, 3)); /* reserve one empty word */
    assert(!NgGpuRingUpdateReadPointer(&ring, 8));
    assert(!NgGpuRingUpdateReadPointer(&ring, 6)); /* GPU cannot pass WPTR */
    assert(NgGpuRingUpdateReadPointer(&ring, 3));
    assert(NgGpuRingUsedWords(&ring) == 2);
    assert(NgGpuRingWrite(&ring, second, 5)); /* wraps over words 0 and 1 */
    assert(NgGpuRingWritePointer(&ring) == 2);
    assert(words[5] == 20 && words[6] == 21 && words[7] == 22);
    assert(words[0] == 23 && words[1] == 24);
    assert(NgGpuRingUpdateReadPointer(&ring, 3)); /* unchanged pointer frees nothing */
    assert(NgGpuRingUsedWords(&ring) == 7);
    assert(NgGpuRingUpdateReadPointer(&ring, 2));
    assert(NgGpuRingUsedWords(&ring) == 0);
}

static void
TestFences(void)
{
    NG_GPU_FENCES fences;
    uint64_t f1, f2;

    NgGpuFencesInit(&fences);
    f1 = NgGpuFenceIssue(&fences);
    f2 = NgGpuFenceIssue(&fences);
    assert(f1 == 1 && f2 == 2);
    assert(!NgGpuFenceIsComplete(&fences, f1));
    assert(!NgGpuFenceComplete(&fences, 3)); /* not submitted */
    assert(NgGpuFenceComplete(&fences, f1));
    assert(NgGpuFenceIsComplete(&fences, f1));
    assert(!NgGpuFenceIsComplete(&fences, f2));
    assert(!NgGpuFenceComplete(&fences, 0)); /* completion cannot go backward */
    assert(NgGpuFenceComplete(&fences, f2));
    assert(NgGpuFenceIsComplete(&fences, f2));
}

int
main(void)
{
    TestRingWrapAndCapacity();
    TestFences();
    puts("A730 ring and fence tests passed");
    return 0;
}
