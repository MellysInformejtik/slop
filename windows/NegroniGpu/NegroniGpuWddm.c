// Early WDDM render-only miniport bring-up for Negroni SM8450.
// This checkpoint validates/maps the ACPI MMIO resources through Dxgkrnl but
// deliberately performs no register access, power sequencing, or submission.
// The callback table is incomplete for a usable adapter and must not be staged.
#include <ntddk.h>
#include <dispmprt.h>

#define NEGRONI_GPU_TAG 'gNgN'
#define NEGRONI_GPU_RANGE_COUNT 6

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
    BOOLEAN Started;
} NEGRONI_GPU_ADAPTER;

static const NEGRONI_GPU_RANGE ExpectedRanges[NEGRONI_GPU_RANGE_COUNT] = {
    { 0x03d00000, 0x40000, NULL }, { 0x03d6a000, 0x35000, NULL },
    { 0x03d50000, 0x10000, NULL }, { 0x03da0000, 0x40000, NULL },
    { 0x0b290000, 0x10000, NULL }, { 0x03d61000, 0x00800, NULL },
};

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
    adapter->Started = TRUE;
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "NegroniGpuWddm: WDDM 2.7 render-only resource map ready; GPU registers untouched\n");
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
