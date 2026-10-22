// Early WDDM render-only miniport bring-up for Negroni SM8450.
// This checkpoint validates/maps the ACPI MMIO resources through Dxgkrnl but
// deliberately performs no register access, power sequencing, or submission.
// The callback table is incomplete for a usable adapter and must not be staged.
#include <ntddk.h>
#include <dispmprt.h>
#include "NegroniGpuFirmware.h"
#include "NegroniGpuFirmwareIo.h"

#define NEGRONI_GPU_TAG 'gNgN'
#define NEGRONI_GPU_RANGE_COUNT 7

typedef struct _NEGRONI_GPU_RANGE {
    ULONGLONG Start;
    ULONG Length;
    PVOID Mapping;
} NEGRONI_GPU_RANGE;

typedef struct _NEGRONI_GPU_ADAPTER {
    PDEVICE_OBJECT Pdo;
    DXGKRNL_INTERFACE Dxgk;
    ULONG RangeCount;
    NEGRONI_GPU_RANGE Ranges[NEGRONI_GPU_RANGE_COUNT];
    PVOID GmuFirmware;
    ULONG GmuFirmwareLength;
    NG_GMU_FW_BLOCK GmuBlocks[NG_GMU_FW_MAX_BLOCKS];
    size_t GmuBlockCount;
    PVOID SqeFirmware;
    ULONG SqeFirmwareLength;
    PVOID ZapManifest;
    ULONG ZapManifestLength;
    NG_ZAP_FW_IMAGE ZapImage;
    PVOID ZapSegments[NG_ZAP_FW_MAX_SEGMENTS];
    ULONG ZapSegmentLengths[NG_ZAP_FW_MAX_SEGMENTS];
    BOOLEAN Started;
} NEGRONI_GPU_ADAPTER;

static const NEGRONI_GPU_RANGE ExpectedRanges[NEGRONI_GPU_RANGE_COUNT] = {
    { 0x00100000, 0x1f4200, NULL },
    { 0x03d00000, 0x40000, NULL }, { 0x03d6a000, 0x35000, NULL },
    { 0x03d50000, 0x10000, NULL }, { 0x03da0000, 0x40000, NULL },
    { 0x0b290000, 0x10000, NULL }, { 0x03d61000, 0x00800, NULL },
};

static VOID
NegroniGpuFreeFirmware(NEGRONI_GPU_ADAPTER *Adapter)
{
    ULONG i;
    if (Adapter->GmuFirmware != NULL)
        ExFreePoolWithTag(Adapter->GmuFirmware, 'fNgN');
    if (Adapter->SqeFirmware != NULL)
        ExFreePoolWithTag(Adapter->SqeFirmware, 'fNgN');
    if (Adapter->ZapManifest != NULL)
        ExFreePoolWithTag(Adapter->ZapManifest, 'fNgN');
    for (i = 0; i < NG_ZAP_FW_MAX_SEGMENTS; ++i) {
        if (Adapter->ZapSegments[i] != NULL)
            ExFreePoolWithTag(Adapter->ZapSegments[i], 'fNgN');
        Adapter->ZapSegments[i] = NULL;
        Adapter->ZapSegmentLengths[i] = 0;
    }
    Adapter->GmuFirmware = NULL;
    Adapter->GmuFirmwareLength = 0;
    Adapter->GmuBlockCount = 0;
    Adapter->SqeFirmware = NULL;
    Adapter->SqeFirmwareLength = 0;
    Adapter->ZapManifest = NULL;
    Adapter->ZapManifestLength = 0;
    RtlZeroMemory(Adapter->GmuBlocks, sizeof(Adapter->GmuBlocks));
    RtlZeroMemory(&Adapter->ZapImage, sizeof(Adapter->ZapImage));
}

static NTSTATUS
NegroniGpuLoadFirmwarePackage(NEGRONI_GPU_ADAPTER *Adapter)
{
    static const PCWSTR zapNames[NG_ZAP_FW_MAX_SEGMENTS] = {
        L"a730_zap.b00", L"a730_zap.b01", L"a730_zap.b02", L"a730_zap.b03",
        L"a730_zap.b04", L"a730_zap.b05", L"a730_zap.b06", L"a730_zap.b07",
        L"a730_zap.b08", L"a730_zap.b09", L"a730_zap.b10", L"a730_zap.b11",
        L"a730_zap.b12", L"a730_zap.b13", L"a730_zap.b14", L"a730_zap.b15"
    };
    ULONG i;
    NTSTATUS status;

    NegroniGpuFreeFirmware(Adapter);
    status = NgReadPackageFirmware(Adapter->Pdo, L"gmu_gen70000.bin",
        0x00100000, &Adapter->GmuFirmware, &Adapter->GmuFirmwareLength);
    if (!NT_SUCCESS(status))
        goto Fail;
    if (!NgParseGmuFirmware(Adapter->GmuFirmware, Adapter->GmuFirmwareLength,
        Adapter->GmuBlocks, RTL_NUMBER_OF(Adapter->GmuBlocks),
        &Adapter->GmuBlockCount)) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Fail;
    }

    status = NgReadPackageFirmware(Adapter->Pdo, L"a730_sqe.fw",
        0x00100000, &Adapter->SqeFirmware, &Adapter->SqeFirmwareLength);
    if (!NT_SUCCESS(status))
        goto Fail;
    status = NgReadPackageFirmware(Adapter->Pdo, L"a730_zap.mdt",
        PAGE_SIZE, &Adapter->ZapManifest, &Adapter->ZapManifestLength);
    if (!NT_SUCCESS(status))
        goto Fail;
    if (!NgParseZapManifest(Adapter->ZapManifest, Adapter->ZapManifestLength,
        &Adapter->ZapImage)) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Fail;
    }

    for (i = 0; i < Adapter->ZapImage.SegmentCount; ++i) {
        ULONG index = Adapter->ZapImage.Segments[i].Index;
        if (index >= RTL_NUMBER_OF(zapNames)) {
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Fail;
        }
        status = NgReadPackageFirmware(Adapter->Pdo, zapNames[index],
            NG_ZAP_FW_MAX_SPAN, &Adapter->ZapSegments[index],
            &Adapter->ZapSegmentLengths[index]);
        if (!NT_SUCCESS(status))
            goto Fail;
        if (Adapter->ZapSegmentLengths[index] <
            Adapter->ZapImage.Segments[i].FileSize) {
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto Fail;
        }
    }

    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "NegroniGpuWddm: firmware package validated (%Iu GMU blocks, %lu ZAP segments, span %#x); GPU untouched\n",
        Adapter->GmuBlockCount, Adapter->ZapImage.SegmentCount,
        Adapter->ZapImage.SpanSize);
    return STATUS_SUCCESS;

Fail:
    NegroniGpuFreeFirmware(Adapter);
    return status;
}

static VOID
NegroniGpuUnmapRanges(NEGRONI_GPU_ADAPTER *Adapter)
{
    ULONG i;
    for (i = 0; i < NEGRONI_GPU_RANGE_COUNT; ++i) {
        if (Adapter->Ranges[i].Mapping != NULL) {
            Adapter->Dxgk.DxgkCbUnmapMemory(Adapter->Dxgk.DeviceHandle,
                                           Adapter->Ranges[i].Mapping);
            Adapter->Ranges[i].Mapping = NULL;
        }
    }
    NegroniGpuFreeFirmware(Adapter);
    Adapter->RangeCount = 0;
    Adapter->Started = FALSE;
}

static NTSTATUS
NTAPI
NegroniGpuAddDevice(PDEVICE_OBJECT PhysicalDeviceObject,
                    PVOID *MiniportDeviceContext)
{
    NEGRONI_GPU_ADAPTER *adapter;
    if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL)
        return STATUS_INVALID_PARAMETER;
    *MiniportDeviceContext = NULL;
    adapter = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*adapter), NEGRONI_GPU_TAG);
    if (adapter == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(adapter, sizeof(*adapter));
    adapter->Pdo = PhysicalDeviceObject;
    *MiniportDeviceContext = adapter;
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
NegroniGpuStartDevice(IN_CONST_PVOID MiniportDeviceContext,
                      IN_PDXGK_START_INFO StartInfo,
                      IN_PDXGKRNL_INTERFACE DxgkInterface,
                      OUT_PULONG NumberOfVideoPresentSources,
                      OUT_PULONG NumberOfChildren)
{
    NEGRONI_GPU_ADAPTER *adapter = (NEGRONI_GPU_ADAPTER *)MiniportDeviceContext;
    DXGK_DEVICE_INFO deviceInfo;
    PCM_RESOURCE_LIST translated;
    ULONG i, j, memoryCount = 0;
    NTSTATUS status;

    if (adapter == NULL || StartInfo == NULL || DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL ||
        DxgkInterface->DxgkCbGetDeviceInformation == NULL ||
        DxgkInterface->DxgkCbMapMemory == NULL ||
        DxgkInterface->DxgkCbUnmapMemory == NULL)
        return STATUS_INVALID_PARAMETER;
    NegroniGpuUnmapRanges(adapter);
    *NumberOfVideoPresentSources = 0; // render-only: no display ownership
    *NumberOfChildren = 0;
    adapter->Dxgk = *DxgkInterface;

    RtlZeroMemory(&deviceInfo, sizeof(deviceInfo));
    status = adapter->Dxgk.DxgkCbGetDeviceInformation(
        adapter->Dxgk.DeviceHandle, &deviceInfo);
    if (!NT_SUCCESS(status) || deviceInfo.TranslatedResourceList == NULL)
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;

    translated = deviceInfo.TranslatedResourceList;
    for (i = 0; i < translated->Count; ++i) {
        PCM_FULL_RESOURCE_DESCRIPTOR full = &translated->List[i];
        ULONG k;
        for (k = 0; k < full->PartialResourceList.Count; ++k) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &full->PartialResourceList.PartialDescriptors[k];
            BOOLEAN matched = FALSE;
            if (d->Type != CmResourceTypeMemory)
                continue;
            ++memoryCount;
            for (j = 0; j < NEGRONI_GPU_RANGE_COUNT; ++j) {
                if ((ULONGLONG)d->u.Memory.Start.QuadPart == ExpectedRanges[j].Start &&
                    d->u.Memory.Length == ExpectedRanges[j].Length &&
                    adapter->Ranges[j].Length == 0) {
                    PHYSICAL_ADDRESS pa = d->u.Memory.Start;
                    PVOID va = NULL;
                    status = adapter->Dxgk.DxgkCbMapMemory(
                        adapter->Dxgk.DeviceHandle, pa, d->u.Memory.Length,
                        TRUE, FALSE, MmNonCached, &va);
                    if (!NT_SUCCESS(status) || va == NULL) {
                        NegroniGpuUnmapRanges(adapter);
                        return NT_SUCCESS(status) ? STATUS_INSUFFICIENT_RESOURCES : status;
                    }
                    adapter->Ranges[j].Start = ExpectedRanges[j].Start;
                    adapter->Ranges[j].Length = ExpectedRanges[j].Length;
                    adapter->Ranges[j].Mapping = va;
                    ++adapter->RangeCount;
                    matched = TRUE;
                    break;
                }
            }
            if (!matched) {
                NegroniGpuUnmapRanges(adapter);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
        }
    }

    if (memoryCount != NEGRONI_GPU_RANGE_COUNT || adapter->RangeCount != NEGRONI_GPU_RANGE_COUNT) {
        NegroniGpuUnmapRanges(adapter);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    status = NegroniGpuLoadFirmwarePackage(adapter);
    if (!NT_SUCCESS(status)) {
        NegroniGpuUnmapRanges(adapter);
        return status;
    }
    adapter->Started = TRUE;
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "NegroniGpuWddm: WDDM 2.7 resource and firmware package ready; GPU registers untouched\n");
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
NegroniGpuStopDevice(IN_CONST_PVOID MiniportDeviceContext)
{
    NEGRONI_GPU_ADAPTER *adapter = (NEGRONI_GPU_ADAPTER *)MiniportDeviceContext;
    if (adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    NegroniGpuUnmapRanges(adapter);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
NegroniGpuRemoveDevice(IN_CONST_PVOID MiniportDeviceContext)
{
    NEGRONI_GPU_ADAPTER *adapter = (NEGRONI_GPU_ADAPTER *)MiniportDeviceContext;
    if (adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    NegroniGpuUnmapRanges(adapter);
    ExFreePoolWithTag(adapter, NEGRONI_GPU_TAG);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
NegroniGpuQueryAdapterInfo(IN_CONST_HANDLE AdapterHandle,
                           IN_CONST_PDXGKARG_QUERYADAPTERINFO Query)
{
    DXGK_WDDMDEVICECAPS *caps;

    // This pre-start query reports only the DDI level this prototype was
    // compiled for. It intentionally does not advertise DRIVERCAPS/rendering.
    if (AdapterHandle == NULL || Query == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Query->Type != DXGKQAITYPE_WDDMDEVICECAPS)
        return STATUS_NOT_SUPPORTED;
    if (Query->pOutputData == NULL ||
        Query->OutputDataSize < sizeof(DXGK_WDDMDEVICECAPS))
        return STATUS_BUFFER_TOO_SMALL;

    caps = (DXGK_WDDMDEVICECAPS *)Query->pOutputData;
    caps->WDDMVersion = DXGKDDI_WDDMv2_7;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    DRIVER_INITIALIZATION_DATA init;
    RtlZeroMemory(&init, sizeof(init));
    init.Version = DXGKDDI_INTERFACE_VERSION_WDDM2_7;
    init.DxgkDdiAddDevice = NegroniGpuAddDevice;
    init.DxgkDdiStartDevice = NegroniGpuStartDevice;
    init.DxgkDdiStopDevice = NegroniGpuStopDevice;
    init.DxgkDdiRemoveDevice = NegroniGpuRemoveDevice;
    init.DxgkDdiQueryAdapterInfo = NegroniGpuQueryAdapterInfo;
    return DxgkInitialize(DriverObject, RegistryPath, &init);
}
