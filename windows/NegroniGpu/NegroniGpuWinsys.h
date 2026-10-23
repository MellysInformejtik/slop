/* SPDX-License-Identifier: MIT */
#ifndef NEGRONI_GPU_WINSYS_H
#define NEGRONI_GPU_WINSYS_H

#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include "NegroniGpuAbi.h"

typedef struct _NG_GPU_WINSYS {
    D3DKMT_HANDLE Adapter;
    NG_GPU_CAPS_QUERY Caps;
} NG_GPU_WINSYS;

/* Initializes output; close an existing connection before reusing it. */
/* Open a caller-selected LUID. No generic GPU probing or hardware writes. */
NTSTATUS NgGpuWinsysOpen(LUID AdapterLuid, NG_GPU_WINSYS *Winsys);
void NgGpuWinsysClose(NG_GPU_WINSYS *Winsys);
/* False until allocation/submission/synchronization exist on both sides. */
int NgGpuWinsysCanRender(const NG_GPU_WINSYS *Winsys);

#endif
