// Early WDDM 2.7 DDI-interface miniport with WDDM 1.3 render-only caps.
// This checkpoint validates/maps the ACPI MMIO resources through Dxgkrnl but
// performs only the gated read-only register preflight; no power writes/submission.
// The callback table is incomplete for a usable adapter and must not be staged.
#include <ntddk.h>
#include <dispmprt.h>
#include "NegroniGpuFirmware.h"
#include "NegroniGpuFirmwareIo.h"
#include "NegroniGpuAbi.h"
#include "NegroniGpuObjects.h"
#include "NegroniGpuAllocation.h"

#define NEGRONI_GPU_TAG 'gNgN'
#define NEGRONI_GPU_RANGE_COUNT 7
#define NG_RANGE_GCC 0
#define NG_RANGE_GMU 2

/* SM8450 GCC/GPUCC offsets verified by the Negroni XNU bring-up. */
#define NG_GCC_GPU_CFG_AHB_CBCR 0x81004
#define NG_GPUCC_IN_GMU         0x26000
#define NG_GPUCC_CX_GDSCR       0x09108
#define NG_GPUCC_GX_GDSCR       0x0905c
#define NG_GPUCC_AHB_CBCR       0x0911c
#define NG_GPUCC_CX_GMU_CBCR    0x0913c
#define NG_GMU_AO_COUNTER_L     0x14220
#define NG_GMU_CM3_FW_INIT      0x14070
#define NG_CBCR_OFF             (1u << 31)
#define NG_GDSC_PWR_ON           (1u << 31)

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

/*
 * Stage, Status, Detail (and a few named values) under
 * HKLM\SYSTEM\CurrentControlSet\Services\NegroniGpuWddm: there is no debugger on the phone, and
 * windows\diag\Negroni diagnostics.cmd reads them back. PASSIVE_LEVEL only.
 */
enum {
    NG_TRACE_DRIVER_ENTRY = 1, NG_TRACE_ADD_DEVICE, NG_TRACE_START, NG_TRACE_DEVICE_INFO,
    NG_TRACE_RANGES_MAPPED, NG_TRACE_FIRMWARE, NG_TRACE_PREFLIGHT, NG_TRACE_STARTED
};

static VOID
NgTraceValue(PCWSTR Name, ULONG Value)
{
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        (VOID)RtlWriteRegistryValue(RTL_REGISTRY_SERVICES, L"NegroniGpuWddm", Name, REG_DWORD,
                                    &Value, sizeof(Value));
}

static VOID
NgTrace(ULONG Stage, NTSTATUS Status, ULONG Detail)
{
    NgTraceValue(L"Stage", Stage);
    NgTraceValue(L"Status", (ULONG)Status);
    NgTraceValue(L"Detail", Detail);
}

static BOOLEAN
NegroniGpuReadMappedRegister(NEGRONI_GPU_ADAPTER *Adapter, ULONG RangeIndex,
                             ULONG Offset, PULONG Value)
{
    NEGRONI_GPU_RANGE *range;

    if (Adapter == NULL || Value == NULL || RangeIndex >= NEGRONI_GPU_RANGE_COUNT ||
        (Offset & (sizeof(ULONG) - 1)) != 0)
        return FALSE;
    range = &Adapter->Ranges[RangeIndex];
    if (range->Mapping == NULL || range->Length < sizeof(ULONG) ||
        Offset > range->Length - sizeof(ULONG))
        return FALSE;

    *Value = READ_REGISTER_ULONG((PULONG)((PUCHAR)range->Mapping + Offset));
    return TRUE;
}

static VOID
NegroniGpuReadOnlyPreflight(NEGRONI_GPU_ADAPTER *Adapter)
{
    ULONG gccCfg, cx, gx, ahb, gmuClock, fwInit, counter0, counter1;
    LARGE_INTEGER interval;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;

    if (!NegroniGpuReadMappedRegister(Adapter, NG_RANGE_GCC,
        NG_GCC_GPU_CFG_AHB_CBCR, &gccCfg)) {
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "NegroniGpuWddm: GCC register range check failed; no further MMIO\n");
        return;
    }
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "NegroniGpuWddm: read-only GCC GPU_CFG_AHB_CBCR=%#x\n", gccCfg);
    NgTraceValue(L"GccGpuCfgAhb", gccCfg);
    if (gccCfg & NG_CBCR_OFF) {
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "NegroniGpuWddm: GCC AHB clock is off; skip GPUCC/GMU reads\n");
        return;
    }

    if (!NegroniGpuReadMappedRegister(Adapter, NG_RANGE_GMU,
            NG_GPUCC_IN_GMU + NG_GPUCC_CX_GDSCR, &cx) ||
        !NegroniGpuReadMappedRegister(Adapter, NG_RANGE_GMU,
            NG_GPUCC_IN_GMU + NG_GPUCC_GX_GDSCR, &gx) ||
        !NegroniGpuReadMappedRegister(Adapter, NG_RANGE_GMU,
            NG_GPUCC_IN_GMU + NG_GPUCC_AHB_CBCR, &ahb) ||
        !NegroniGpuReadMappedRegister(Adapter, NG_RANGE_GMU,
            NG_GPUCC_IN_GMU + NG_GPUCC_CX_GMU_CBCR, &gmuClock)) {
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "NegroniGpuWddm: GPUCC register range check failed; no GMU reads\n");
        return;
    }
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "NegroniGpuWddm: read-only GPUCC CX=%#x GX=%#x AHB=%#x GMUCLK=%#x\n",
        cx, gx, ahb, gmuClock);
    NgTraceValue(L"GpuccCxGdsc", cx);
    NgTraceValue(L"GpuccGxGdsc", gx);
    NgTraceValue(L"GpuccAhb", ahb);
    NgTraceValue(L"GpuccGmuClock", gmuClock);
    if (!(cx & NG_GDSC_PWR_ON) || (gmuClock & NG_CBCR_OFF)) {
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "NegroniGpuWddm: CX/GMU clock not ready; skip GMU CX reads\n");
        return;
    }

    if (!NegroniGpuReadMappedRegister(Adapter, NG_RANGE_GMU,
            NG_GMU_AO_COUNTER_L, &counter0) ||
        !NegroniGpuReadMappedRegister(Adapter, NG_RANGE_GMU,
            NG_GMU_CM3_FW_INIT, &fwInit)) {
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "NegroniGpuWddm: GMU register range check failed\n");
        return;
    }
    interval.QuadPart = -10000; /* 1 ms, StartDevice is PASSIVE_LEVEL. */
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &interval);
    if (!NegroniGpuReadMappedRegister(Adapter, NG_RANGE_GMU,
            NG_GMU_AO_COUNTER_L, &counter1))
        return;
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "NegroniGpuWddm: read-only GMU AO counter %#x -> %#x; FW_INIT=%#x\n",
        counter0, counter1, fwInit);
    NgTraceValue(L"GmuAoCounterDelta", counter1 - counter0);
    NgTraceValue(L"GmuFwInit", fwInit);
}

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
        /* StartDevice matches only empty slots, including after stop/failure. */
        Adapter->Ranges[i].Start = 0;
        Adapter->Ranges[i].Length = 0;
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
    NgTrace(NG_TRACE_ADD_DEVICE, STATUS_SUCCESS, 0);
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
        DxgkInterface->DxgkCbUnmapMemory == NULL) {
        NgTrace(NG_TRACE_START, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    NgTrace(NG_TRACE_START, STATUS_PENDING, 0);
    NegroniGpuUnmapRanges(adapter);
    *NumberOfVideoPresentSources = 0; // render-only: no display ownership
    *NumberOfChildren = 0;
    adapter->Dxgk = *DxgkInterface;

    RtlZeroMemory(&deviceInfo, sizeof(deviceInfo));
    status = adapter->Dxgk.DxgkCbGetDeviceInformation(
        adapter->Dxgk.DeviceHandle, &deviceInfo);
    if (!NT_SUCCESS(status) || deviceInfo.TranslatedResourceList == NULL) {
        status = NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
        NgTrace(NG_TRACE_DEVICE_INFO, status, 0);
        return status;
    }

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
                        status = NT_SUCCESS(status) ? STATUS_INSUFFICIENT_RESOURCES : status;
                        NgTrace(NG_TRACE_RANGES_MAPPED, status, j);
                        return status;
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
                NgTraceValue(L"UnexpectedRangeStart", (ULONG)d->u.Memory.Start.QuadPart);
                NgTraceValue(L"UnexpectedRangeLength", d->u.Memory.Length);
                NgTrace(NG_TRACE_RANGES_MAPPED, STATUS_DEVICE_CONFIGURATION_ERROR, memoryCount);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
        }
    }

    if (memoryCount != NEGRONI_GPU_RANGE_COUNT || adapter->RangeCount != NEGRONI_GPU_RANGE_COUNT) {
        NegroniGpuUnmapRanges(adapter);
        NgTrace(NG_TRACE_RANGES_MAPPED, STATUS_DEVICE_CONFIGURATION_ERROR, memoryCount << 8 | adapter->RangeCount);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    NgTrace(NG_TRACE_RANGES_MAPPED, STATUS_SUCCESS, adapter->RangeCount);
    status = NegroniGpuLoadFirmwarePackage(adapter);
    if (!NT_SUCCESS(status)) {
        NegroniGpuUnmapRanges(adapter);
        NgTrace(NG_TRACE_FIRMWARE, status, 0);
        return status;
    }
    NgTrace(NG_TRACE_FIRMWARE, STATUS_SUCCESS, (ULONG)adapter->GmuBlockCount << 16 | adapter->ZapImage.SegmentCount);
    NegroniGpuReadOnlyPreflight(adapter);
    NgTrace(NG_TRACE_PREFLIGHT, STATUS_SUCCESS, 0);
    adapter->Started = TRUE;
    NgTrace(NG_TRACE_STARTED, STATUS_SUCCESS, adapter->RangeCount);
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "NegroniGpuWddm: WDDM 2.7 resources and firmware ready; no GPU register writes issued\n");
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
NegroniGpuEscape(IN_CONST_HANDLE AdapterHandle,
                 IN_CONST_PDXGKARG_ESCAPE Escape)
{
    NEGRONI_GPU_ADAPTER *adapter = (NEGRONI_GPU_ADAPTER *)AdapterHandle;
    if (adapter == NULL || Escape == NULL || Escape->Flags.Value != 0 ||
        Escape->hDevice != NULL || Escape->hContext != NULL)
        return STATUS_INVALID_PARAMETER;
    /* Adapter-only metadata query: no register access or process-owned handles. */
    if (!NgGpuHandleCapsQuery(Escape->pPrivateDriverData,
            Escape->PrivateDriverDataSize, adapter->Started ?
            NG_GPU_STAGE_RESOURCES_VALIDATED : NG_GPU_STAGE_NOT_STARTED))
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
NegroniGpuQueryAdapterInfo(IN_CONST_HANDLE AdapterHandle,
                           IN_CONST_PDXGKARG_QUERYADAPTERINFO Query)
{
    DXGK_DRIVERCAPS *caps;

    // WDDM 1.3 (as Microsoft's render-only sample): Dxgkrnl learns the driver model from
    // DRIVERCAPS. Only the version is reported; no rendering, scheduling or display features.
    if (AdapterHandle == NULL || Query == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Query->Type != DXGKQAITYPE_DRIVERCAPS)
        return STATUS_NOT_SUPPORTED;
    if (Query->pOutputData == NULL ||
        Query->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
        return STATUS_BUFFER_TOO_SMALL;

    caps = (DXGK_DRIVERCAPS *)Query->pOutputData;
    RtlZeroMemory(caps, sizeof(*caps));
    caps->WDDMVersion = DXGKDDI_WDDMv1_3;
    NgTraceValue(L"QueriedDriverCaps", 1);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
NegroniGpuCreateAllocation(IN_CONST_HANDLE AdapterHandle,
                          INOUT_PDXGKARG_CREATEALLOCATION Args)
{
    NEGRONI_GPU_ADAPTER *adapter = (NEGRONI_GPU_ADAPTER *)AdapterHandle;
    if (!adapter || !adapter->Started)
        return STATUS_INVALID_DEVICE_STATE;
    /* No aperture is declared or backed by SMMU paging yet. Keep fail-closed. */
    return NgGpuCreateAllocations(Args, 0);
}

static NTSTATUS APIENTRY
NegroniGpuDestroyAllocation(IN_CONST_HANDLE AdapterHandle,
                           IN_CONST_PDXGKARG_DESTROYALLOCATION Args)
{
    if (!AdapterHandle)
        return STATUS_INVALID_PARAMETER;
    return NgGpuDestroyAllocations(Args);
}

static NTSTATUS APIENTRY
NegroniGpuCreateDevice(IN_CONST_HANDLE AdapterHandle,
                       INOUT_PDXGKARG_CREATEDEVICE Args)
{
    NEGRONI_GPU_ADAPTER *adapter = (NEGRONI_GPU_ADAPTER *)AdapterHandle;
    if (!adapter || !adapter->Started)
        return STATUS_INVALID_DEVICE_STATE;
    return NgGpuCreateDeviceObject(Args);
}

NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    DRIVER_INITIALIZATION_DATA init;
    RtlZeroMemory(&init, sizeof(init));
    init.Version = DXGKDDI_INTERFACE_VERSION_WDDM1_3;
    init.DxgkDdiAddDevice = NegroniGpuAddDevice;
    init.DxgkDdiStartDevice = NegroniGpuStartDevice;
    init.DxgkDdiStopDevice = NegroniGpuStopDevice;
    init.DxgkDdiRemoveDevice = NegroniGpuRemoveDevice;
    init.DxgkDdiQueryAdapterInfo = NegroniGpuQueryAdapterInfo;
    init.DxgkDdiEscape = NegroniGpuEscape;
    init.DxgkDdiCreateDevice = NegroniGpuCreateDevice;
    init.DxgkDdiDestroyDevice = NgGpuDestroyDeviceObject;
    init.DxgkDdiCreateContext = NgGpuCreateContextObject;
    init.DxgkDdiDestroyContext = NgGpuDestroyContextObject;
    init.DxgkDdiCreateAllocation = NegroniGpuCreateAllocation;
    init.DxgkDdiDestroyAllocation = NegroniGpuDestroyAllocation;
    {
        NTSTATUS status = DxgkInitialize(DriverObject, RegistryPath, &init);
        NgTrace(NG_TRACE_DRIVER_ENTRY, status, 0);
        return status;
    }
}
