#pragma once
/* Included after ntddk.h and dispmprt.h. CPU ownership only; no GPU state. */
NTSTATUS NgGpuCreateDeviceObject(INOUT_PDXGKARG_CREATEDEVICE Args);
NTSTATUS APIENTRY NgGpuDestroyDeviceObject(IN_CONST_HANDLE Device);
NTSTATUS APIENTRY NgGpuCreateContextObject(IN_CONST_HANDLE Device,
    INOUT_PDXGKARG_CREATECONTEXT Args);
NTSTATUS APIENTRY NgGpuDestroyContextObject(IN_CONST_HANDLE Context);
