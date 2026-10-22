/* SPDX-License-Identifier: MIT */
#ifndef NEGRONI_GPU_FIRMWARE_IO_H
#define NEGRONI_GPU_FIRMWARE_IO_H

#include <ntddk.h>

NTSTATUS NgReadPackageFirmware(PDEVICE_OBJECT Pdo, PCWSTR FileName,
                               ULONG MaximumLength, PVOID *Buffer,
                               PULONG Length);

#endif
