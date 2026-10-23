/* SPDX-License-Identifier: MIT */
#include "NegroniGpuPm4.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t
OddParity(uint32_t value)
{
    unsigned ones = 0;
    while (value != 0) {
        ones += value & 1u;
        value >>= 1;
    }
    return (ones & 1u) == 0;
}

static void
TestType7(void)
{
    uint32_t output[8] = {0};
    const uint32_t payload[] = { 1u, 0x00000003u, 0x20000000u,
        2u, 0u, 0u, 0u };
    size_t written = 99;

    assert(NgGpuEncodeType7(0x48u, payload, 7, output,
        sizeof(output) / sizeof(output[0]), &written));
    assert(written == 8);
    assert(output[0] == 0x70c80007u); /* exact XNU CP_ME_INIT header */
    assert((output[0] >> 28) == 7u);
    assert(((output[0] >> 16) & 0x7fu) == 0x48u);
    assert((output[0] & 0x7fffu) == 7u);
    assert(OddParity(output[0] & 0x7fffu) == ((output[0] >> 15) & 1u));
    assert(OddParity(0x48u) == ((output[0] >> 23) & 1u));
    for (size_t i = 0; i < 7; ++i)
        assert(output[i + 1] == payload[i]);

    written = 99;
    assert(!NgGpuEncodeType7(0x48u, payload, 7, output, 7, &written));
    assert(written == 0);
    assert(!NgGpuEncodeType7(0x80u, payload, 7, output, 8, &written));
    assert(written == 0);
    assert(!NgGpuEncodeType7(0x48u, NULL, 1, output, 8, &written));
    assert(written == 0);
}

static void
TestType4(void)
{
    uint32_t output[2] = {0};
    size_t written = 0;
    const uint32_t reg = 0x0800u;

    assert(NgGpuEncodeType4Write(reg, 0x12345678u, output, 2, &written));
    assert(written == 2);
    assert(output[0] == 0x40080001u); /* exact parity-checked type-4 header */
    assert((output[0] >> 28) == 4u);
    assert((output[0] & 0x7fu) == 1u);
    assert(((output[0] >> 8) & 0x7fffu) == reg);
    assert(OddParity(1u) == ((output[0] >> 7) & 1u));
    assert(OddParity(reg) == ((output[0] >> 27) & 1u));
    assert(output[1] == 0x12345678u);

    written = 99;
    assert(!NgGpuEncodeType4Write(reg, 0, output, 1, &written));
    assert(written == 0);
    assert(!NgGpuEncodeType4Write(0x8000u, 0, output, 2, &written));
    assert(written == 0);
}

static void
TestPacketStream(void)
{
    uint32_t stream[10] = {0};
    uint32_t meInitPayload[] = { 1u, 0x00000003u, 0x20000000u,
        2u, 0u, 0u, 0u };
    size_t firstWords = 0, secondWords = 0, packets = 99;

    assert(NgGpuEncodeType7(0x48u, meInitPayload, 7, stream,
        sizeof(stream) / sizeof(stream[0]), &firstWords));
    assert(NgGpuEncodeType4Write(0x0800u, 0x12345678u, stream + firstWords,
        sizeof(stream) / sizeof(stream[0]) - firstWords, &secondWords));
    assert(firstWords + secondWords == 10);
    assert(NgGpuValidatePacketStream(stream, 10, 2, &packets));
    assert(packets == 2);

    packets = 99;
    assert(!NgGpuValidatePacketStream(stream, 9, 2, &packets));
    assert(packets == 0);
    assert(!NgGpuValidatePacketStream(stream, 10, 1, &packets));
    assert(packets == 0);

    stream[0] ^= 1u << 15; /* corrupt type-7 count parity */
    assert(!NgGpuValidatePacketStream(stream, 10, 2, &packets));
    stream[0] ^= 1u << 15;
    stream[8] = 0; /* type 0 is outside the admitted framing subset */
    assert(!NgGpuValidatePacketStream(stream, 10, 2, &packets));
    assert(!NgGpuValidatePacketStream(NULL, 1, 1, &packets));
}

int
main(void)
{
    TestType7();
    TestType4();
    TestPacketStream();
    puts("A730 PM4 packet encoder tests passed");
    return 0;
}
