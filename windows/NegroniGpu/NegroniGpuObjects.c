/* Software context ownership for the incomplete render-only miniport.
 * Dxgkrnl owns callback handle validity. The mutex serializes child creation,
 * device close and child removal; children keep a closed device alive.
 * No adapter/MMIO pointer is retained, so teardown cannot access unmapped MMIO.
 * Hardware contexts, address spaces and submission are still unimplemented.
 */
#ifdef NG_OBJECT_TEST
#include "test-objects-shim.h"
#else
#include <ntddk.h>
#include <dispmprt.h>
#endif
#include "NegroniGpuObjects.h"

#define NG_OBJECT_TAG 'oNgN'
typedef struct {
    FAST_MUTEX Lock;
    HANDLE RuntimeHandle;
    SIZE_T Children;
    BOOLEAN Closing;
} NG_DEVICE;
typedef struct {
    NG_DEVICE *Device;
    HANDLE RuntimeHandle;
} NG_CONTEXT;

NTSTATUS NgGpuCreateDeviceObject(INOUT_PDXGKARG_CREATEDEVICE Args)
{
    NG_DEVICE *device;
    if (!Args || !Args->hDevice)
        return STATUS_INVALID_PARAMETER;
    /* System devices are supported; GDI, reserved flags and GPUVA are not. */
    if (Args->Flags.Value & ~1u)
        return STATUS_NOT_SUPPORTED;
#if defined(NG_OBJECT_TEST) || DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0
    if (Args->Pasid || Args->hKmdProcess)       /* WDDM 2.x only */
        return STATUS_NOT_SUPPORTED;
#endif
    device = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*device), NG_OBJECT_TAG);
    if (!device)
        return STATUS_NO_MEMORY;
    ExInitializeFastMutex(&device->Lock);
    device->RuntimeHandle = Args->hDevice;
    Args->hDevice = device;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY NgGpuDestroyDeviceObject(IN_CONST_HANDLE Device)
{
    NG_DEVICE *device = (NG_DEVICE *)Device;
    BOOLEAN release;
    if (!device)
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&device->Lock);
    device->Closing = TRUE;
    release = device->Children == 0;
    ExReleaseFastMutex(&device->Lock);
    if (release)
        ExFreePoolWithTag(device, NG_OBJECT_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY NgGpuCreateContextObject(IN_CONST_HANDLE Device,
    INOUT_PDXGKARG_CREATECONTEXT Args)
{
    NG_DEVICE *device = (NG_DEVICE *)Device;
    NG_CONTEXT *context;
    if (!device || !Args || !Args->hContext)
        return STATUS_INVALID_PARAMETER;
    if (Args->NodeOrdinal || Args->EngineAffinity != 1 ||
        (Args->Flags.Value & ~1u) || Args->PrivateDriverDataSize)
        return STATUS_NOT_SUPPORTED;
    context = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*context), NG_OBJECT_TAG);
    if (!context)
        return STATUS_NO_MEMORY;
    ExAcquireFastMutex(&device->Lock);
    if (device->Closing || device->Children == (SIZE_T)-1) {
        ExReleaseFastMutex(&device->Lock);
        ExFreePoolWithTag(context, NG_OBJECT_TAG);
        return STATUS_INVALID_DEVICE_STATE;
    }
    ++device->Children;
    ExReleaseFastMutex(&device->Lock);
    context->Device = device;
    context->RuntimeHandle = Args->hContext;
    RtlZeroMemory(&Args->ContextInfo, sizeof(Args->ContextInfo));
    /* Physical-address WDDM 1.x staging contract; no executable packets yet. */
    Args->ContextInfo.DmaBufferSize = 64 * 1024;
    Args->ContextInfo.AllocationListSize = 256;
    Args->ContextInfo.PatchLocationListSize = 1024;
    Args->hContext = context;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY NgGpuDestroyContextObject(IN_CONST_HANDLE Context)
{
    NG_CONTEXT *context = (NG_CONTEXT *)Context;
    NG_DEVICE *device;
    BOOLEAN release;
    if (!context)
        return STATUS_INVALID_PARAMETER;
    device = context->Device;
    ExAcquireFastMutex(&device->Lock);
    --device->Children;
    release = device->Closing && device->Children == 0;
    ExReleaseFastMutex(&device->Lock);
    ExFreePoolWithTag(context, NG_OBJECT_TAG);
    if (release)
        ExFreePoolWithTag(device, NG_OBJECT_TAG);
    return STATUS_SUCCESS;
}
