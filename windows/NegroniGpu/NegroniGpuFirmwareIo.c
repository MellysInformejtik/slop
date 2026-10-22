/* SPDX-License-Identifier: MIT */
#include "NegroniGpuFirmwareIo.h"

#define NG_FIRMWARE_TAG 'fNgN'
#define NG_MAX_PACKAGE_PATH_CHARS 512

static BOOLEAN
NgValidFirmwareName(PCWSTR Name, USHORT *NameBytes)
{
    USHORT count = 0;
    if (Name == NULL || Name[0] == L'\0')
        return FALSE;
    while (Name[count] != L'\0') {
        if (Name[count] == L'\\' || Name[count] == L'/' ||
            Name[count] == L':' || count >= 63)
            return FALSE;
        ++count;
    }
    *NameBytes = (USHORT)(count * sizeof(WCHAR));
    return TRUE;
}

static NTSTATUS
NgOpenFirmwareDirectory(PDEVICE_OBJECT Pdo, PHANDLE DirectoryHandle)
{
    static const WCHAR valueNameText[] = L"FirmwareDirectory";
    static const WCHAR objectPrefixText[] = L"\\??\\";
    UNICODE_STRING valueName, objectPrefix, packagePath;
    HANDLE keyHandle = NULL;
    PKEY_VALUE_PARTIAL_INFORMATION value = NULL;
    ULONG needed = 0;
    NTSTATUS status;
    WCHAR objectPathBuffer[NG_MAX_PACKAGE_PATH_CHARS];
    UNICODE_STRING objectPath;
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;

    *DirectoryHandle = NULL;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    status = IoOpenDeviceRegistryKey(Pdo, PLUGPLAY_REGKEY_DEVICE,
                                     KEY_QUERY_VALUE, &keyHandle);
    if (!NT_SUCCESS(status))
        return status;

    RtlInitUnicodeString(&valueName, valueNameText);
    status = ZwQueryValueKey(keyHandle, &valueName, KeyValuePartialInformation,
                             NULL, 0, &needed);
    if (status != STATUS_BUFFER_TOO_SMALL && status != STATUS_BUFFER_OVERFLOW)
        goto Exit;
    value = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool2(
        POOL_FLAG_PAGED, needed, NG_FIRMWARE_TAG);
    if (value == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    status = ZwQueryValueKey(keyHandle, &valueName, KeyValuePartialInformation,
                             value, needed, &needed);
    if (!NT_SUCCESS(status))
        goto Exit;
    if (value->Type != REG_SZ || value->DataLength < sizeof(WCHAR) ||
        (value->DataLength % sizeof(WCHAR)) != 0 ||
        value->DataLength > sizeof(objectPathBuffer) - sizeof(objectPrefixText)) {
        status = STATUS_OBJECT_TYPE_MISMATCH;
        goto Exit;
    }

    packagePath.Buffer = (PWSTR)value->Data;
    packagePath.Length = (USHORT)(value->DataLength - sizeof(WCHAR));
    packagePath.MaximumLength = (USHORT)value->DataLength;
    if (packagePath.Length == 0 ||
        packagePath.Buffer[packagePath.Length / sizeof(WCHAR)] != L'\0') {
        status = STATUS_OBJECT_NAME_INVALID;
        goto Exit;
    }

    RtlInitUnicodeString(&objectPrefix, objectPrefixText);
    objectPath.Buffer = objectPathBuffer;
    objectPath.Length = 0;
    objectPath.MaximumLength = sizeof(objectPathBuffer);
    status = RtlAppendUnicodeStringToString(&objectPath, &objectPrefix);
    if (NT_SUCCESS(status))
        status = RtlAppendUnicodeStringToString(&objectPath, &packagePath);
    if (!NT_SUCCESS(status))
        goto Exit;

    InitializeObjectAttributes(&attributes, &objectPath,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = ZwOpenFile(DirectoryHandle,
        FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attributes, &ioStatus,
        FILE_SHARE_READ, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
        FILE_OPEN_FOR_BACKUP_INTENT);

Exit:
    if (value != NULL)
        ExFreePoolWithTag(value, NG_FIRMWARE_TAG);
    if (keyHandle != NULL)
        ZwClose(keyHandle);
    return status;
}

NTSTATUS
NgReadPackageFirmware(PDEVICE_OBJECT Pdo, PCWSTR FileName,
                      ULONG MaximumLength, PVOID *Buffer, PULONG Length)
{
    HANDLE directory = NULL, file = NULL;
    USHORT nameBytes;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    FILE_STANDARD_INFORMATION standard;
    PVOID data = NULL;
    NTSTATUS status;

    if (Buffer != NULL)
        *Buffer = NULL;
    if (Length != NULL)
        *Length = 0;
    if (Pdo == NULL || Buffer == NULL || Length == NULL || MaximumLength == 0 ||
        !NgValidFirmwareName(FileName, &nameBytes))
        return STATUS_INVALID_PARAMETER;

    status = NgOpenFirmwareDirectory(Pdo, &directory);
    if (!NT_SUCCESS(status))
        return status;

    name.Buffer = (PWSTR)FileName;
    name.Length = nameBytes;
    name.MaximumLength = (USHORT)(nameBytes + sizeof(WCHAR));
    InitializeObjectAttributes(&attributes, &name,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, directory, NULL);
    status = ZwOpenFile(&file, FILE_READ_DATA | SYNCHRONIZE, &attributes,
        &ioStatus, FILE_SHARE_READ, FILE_NON_DIRECTORY_FILE |
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    if (!NT_SUCCESS(status))
        goto Exit;

    status = ZwQueryInformationFile(file, &ioStatus, &standard,
        sizeof(standard), FileStandardInformation);
    if (!NT_SUCCESS(status))
        goto Exit;
    if (standard.EndOfFile.QuadPart <= 0 ||
        (ULONGLONG)standard.EndOfFile.QuadPart > MaximumLength ||
        (ULONGLONG)standard.EndOfFile.QuadPart > MAXULONG) {
        status = STATUS_FILE_TOO_LARGE;
        goto Exit;
    }

    *Length = (ULONG)standard.EndOfFile.QuadPart;
    data = ExAllocatePool2(POOL_FLAG_NON_PAGED, *Length, NG_FIRMWARE_TAG);
    if (data == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    status = ZwReadFile(file, NULL, NULL, NULL, &ioStatus, data, *Length,
                        NULL, NULL);
    if (!NT_SUCCESS(status) || ioStatus.Information != *Length) {
        if (NT_SUCCESS(status))
            status = STATUS_END_OF_FILE;
        goto Exit;
    }

    *Buffer = data;
    data = NULL;
    status = STATUS_SUCCESS;

Exit:
    if (data != NULL)
        ExFreePoolWithTag(data, NG_FIRMWARE_TAG);
    if (file != NULL)
        ZwClose(file);
    if (directory != NULL)
        ZwClose(directory);
    if (!NT_SUCCESS(status))
        *Length = 0;
    return status;
}
