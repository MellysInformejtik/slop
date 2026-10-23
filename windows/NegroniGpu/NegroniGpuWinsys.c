/* SPDX-License-Identifier: MIT */
#include "NegroniGpuWinsys.h"

#define NG_STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000dL)
#define NG_STATUS_DRIVER_MISMATCH ((NTSTATUS)0xc01e0009L)

NTSTATUS NgGpuWinsysOpen(LUID AdapterLuid, NG_GPU_WINSYS *Winsys)
{
    D3DKMT_OPENADAPTERFROMLUID open = {0};
    D3DKMT_ESCAPE escape = {0};
    NTSTATUS status;
    if (Winsys == NULL)
        return NG_STATUS_INVALID_PARAMETER;
    ZeroMemory(Winsys, sizeof(*Winsys));
    open.AdapterLuid = AdapterLuid;
    status = D3DKMTOpenAdapterFromLuid(&open);
    if (status < 0)
        return status;
    Winsys->Adapter = open.hAdapter;
    NgGpuInitCapsQuery(&Winsys->Caps);
    escape.hAdapter = Winsys->Adapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &Winsys->Caps;
    escape.PrivateDriverDataSize = sizeof(Winsys->Caps);
    /* HardwareAccess stays clear: the KMD query returns metadata only. */
    status = D3DKMTEscape(&escape);
    if (status >= 0 && !NgGpuValidateCapsReply(&Winsys->Caps))
        status = NG_STATUS_DRIVER_MISMATCH;
    if (status < 0)
        NgGpuWinsysClose(Winsys);
    return status;
}

void NgGpuWinsysClose(NG_GPU_WINSYS *Winsys)
{
    if (Winsys == NULL)
        return;
    if (Winsys->Adapter != 0) {
        D3DKMT_CLOSEADAPTER close = {0};
        close.hAdapter = Winsys->Adapter;
        (void)D3DKMTCloseAdapter(&close);
    }
    ZeroMemory(Winsys, sizeof(*Winsys));
}

int NgGpuWinsysCanRender(const NG_GPU_WINSYS *Winsys)
{
    return Winsys != NULL && Winsys->Adapter != 0 &&
        NgGpuValidateCapsReply(&Winsys->Caps) &&
        (Winsys->Caps.Features & NG_GPU_FEATURE_RENDER) != 0;
}
