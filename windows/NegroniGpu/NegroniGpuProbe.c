// Stage-0 ARM64 KMDF probe for the Negroni SM8450 Adreno 730 ACPI node.
// It validates translated MMIO resources only. It never maps/registers GPU
// memory, requests clocks, loads firmware, or changes display ownership.
#include <ntddk.h>
#include <wdf.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD NegroniGpuEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE NegroniGpuEvtPrepareHardware;

typedef struct _NEGRONI_GPU_RANGE {
    ULONGLONG Start;
    ULONGLONG Length;
} NEGRONI_GPU_RANGE;

static const NEGRONI_GPU_RANGE ExpectedRanges[] = {
    { 0x03d00000, 0x40000 }, { 0x03d6a000, 0x35000 },
    { 0x03d50000, 0x10000 }, { 0x03da0000, 0x40000 },
    { 0x0b290000, 0x10000 }, { 0x03d61000, 0x00800 },
};

static BOOLEAN
RangeWasSeen(const NEGRONI_GPU_RANGE *Seen, ULONG SeenCount,
             const NEGRONI_GPU_RANGE *Expected)
{
    ULONG i;
    for (i = 0; i < SeenCount; ++i) {
        if (Seen[i].Start == Expected->Start && Seen[i].Length == Expected->Length)
            return TRUE;
    }
    return FALSE;
}

NTSTATUS
NegroniGpuEvtPrepareHardware(WDFDEVICE Device,
                             WDFCMRESLIST ResourcesRaw,
                             WDFCMRESLIST ResourcesTranslated)
{
    NEGRONI_GPU_RANGE seen[RTL_NUMBER_OF(ExpectedRanges)];
    ULONG i, count = 0;
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesRaw);

    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d =
            WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (d == NULL)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (d->Type != CmResourceTypeMemory)
            continue;
        if (count == RTL_NUMBER_OF(seen))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        seen[count].Start = d->u.Memory.Start.QuadPart;
        seen[count].Length = d->u.Memory.Length;
        ++count;
    }

    for (i = 0; i < RTL_NUMBER_OF(ExpectedRanges); ++i) {
        if (!RangeWasSeen(seen, count, &ExpectedRanges[i])) {
            DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                       "NegroniGpuProbe: expected MMIO range %lu missing; refusing start\n", i);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "NegroniGpuProbe: ACPI resources validated (%lu MMIO ranges); GPU left untouched\n",
               count);
    return STATUS_SUCCESS;
}

NTSTATUS
NegroniGpuEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDFDEVICE device;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(Driver);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = NegroniGpuEvtPrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);
    status = WdfDeviceCreate(&DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
    if (NT_SUCCESS(status))
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "NegroniGpuProbe: bound to ACPI\\QCOM0A73; probe only, no acceleration\n");
    return status;
}

NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, NegroniGpuEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                           &config, WDF_NO_HANDLE);
}
