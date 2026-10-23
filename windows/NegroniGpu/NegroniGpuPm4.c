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
NgGpuValidatePacketStream(const uint32_t *Words, size_t WordCount,
                          size_t MaxPackets, size_t *PacketsValidated)
{
    size_t offset = 0;
    size_t packets = 0;

    if (PacketsValidated != NULL)
        *PacketsValidated = 0;
    if (Words == NULL || WordCount == 0 || MaxPackets == 0 ||
        PacketsValidated == NULL)
        return 0;

    while (offset < WordCount) {
        uint32_t header = Words[offset];
        uint32_t packetType = header >> 28;
        size_t payloadCount;

        if (packets >= MaxPackets)
            return 0;
        if (packetType == 7u) {
            uint32_t opcode = (header >> 16) & 0x7fu;
            uint32_t count = header & 0x7fffu;
            uint32_t opcodeParity = (header >> 23) & 1u;
            uint32_t countParity = (header >> 15) & 1u;

            /* Bits 24..27 are reserved in the type-7 header form we emit. */
            if ((header & 0x0f000000u) != 0 ||
                NgGpuOddParity(opcode) != opcodeParity ||
                NgGpuOddParity(count) != countParity)
                return 0;
            payloadCount = count;
        } else if (packetType == 4u) {
            uint32_t count = header & 0x7fu;
            uint32_t reg = (header >> 8) & 0x7fffu;
            uint32_t countParity = (header >> 7) & 1u;
            uint32_t regParity = (header >> 27) & 1u;

            /* This implementation accepts only the single-register write form. */
            if (count != 1u || (header & 0x07800000u) != 0 ||
                NgGpuOddParity(count) != countParity ||
                NgGpuOddParity(reg) != regParity)
                return 0;
            payloadCount = count;
        } else {
            /* Type 0/2/other headers are not emitted by the current encoder. */
            return 0;
        }

        if (payloadCount > WordCount - offset - 1u)
            return 0;
        offset += payloadCount + 1u;
        ++packets;
    }

    *PacketsValidated = packets;
    return 1;
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
