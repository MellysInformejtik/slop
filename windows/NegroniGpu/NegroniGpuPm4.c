/* SPDX-License-Identifier: MIT */
#include "NegroniGpuPm4.h"

static uint32_t
NgGpuOddParity(uint32_t Value)
{
    return (0x9669u >> (0x0fu & (Value ^ (Value >> 4) ^ (Value >> 8) ^
        (Value >> 12) ^ (Value >> 16) ^ (Value >> 20) ^ (Value >> 24) ^
        (Value >> 28)))) & 1u;
}

int
NgGpuEncodeType7(uint32_t Opcode, const uint32_t *Payload,
                 size_t PayloadCount, uint32_t *Output,
                 size_t OutputCapacity, size_t *WordsWritten)
{
    size_t i;
    uint32_t header;

    if (WordsWritten != NULL)
        *WordsWritten = 0;
    if (Output == NULL || WordsWritten == NULL || Opcode > 0x7fu ||
        PayloadCount > 0x7fffu ||
        (PayloadCount != 0 && Payload == NULL) ||
        OutputCapacity < PayloadCount + 1)
        return 0;

    header = (7u << 28) | (uint32_t)PayloadCount |
        (NgGpuOddParity((uint32_t)PayloadCount) << 15) |
        (Opcode << 16) | (NgGpuOddParity(Opcode) << 23);
    Output[0] = header;
    for (i = 0; i < PayloadCount; ++i)
        Output[i + 1] = Payload[i];
    *WordsWritten = PayloadCount + 1;
    return 1;
}

int
NgGpuEncodeType4Write(uint32_t Register, uint32_t Value,
                      uint32_t *Output, size_t OutputCapacity,
                      size_t *WordsWritten)
{
    if (WordsWritten != NULL)
        *WordsWritten = 0;
    if (Output == NULL || WordsWritten == NULL || Register > 0x7fffu ||
        OutputCapacity < 2)
        return 0;

    Output[0] = (4u << 28) | 1u | (NgGpuOddParity(1u) << 7) |
        (Register << 8) | (NgGpuOddParity(Register) << 27);
    Output[1] = Value;
    *WordsWritten = 2;
    return 1;
}
