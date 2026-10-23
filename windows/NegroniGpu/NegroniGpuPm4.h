/* SPDX-License-Identifier: MIT */
#ifndef NEGRONI_GPU_PM4_H
#define NEGRONI_GPU_PM4_H

#include <stddef.h>
#include <stdint.h>

/* Encode the A7xx PM4 packet forms exercised by the Negroni XNU bring-up. */
int NgGpuEncodeType7(uint32_t Opcode, const uint32_t *Payload,
                    size_t PayloadCount, uint32_t *Output,
                    size_t OutputCapacity, size_t *WordsWritten);
int NgGpuEncodeType4Write(uint32_t Register, uint32_t Value,
                          uint32_t *Output, size_t OutputCapacity,
                          size_t *WordsWritten);

/*
 * Validate only PM4 framing, packet bounds, and header parity. This is not a
 * security/semantic validator: callers must still whitelist opcodes, registers,
 * memory references, and engine state before any stream can reach hardware.
 */
int NgGpuValidatePacketStream(const uint32_t *Words, size_t WordCount,
                              size_t MaxPackets, size_t *PacketsValidated);

#endif
