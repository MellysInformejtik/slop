/* SPDX-License-Identifier: MIT */
/* Native Windows transport tests. D3DKMT calls below are mocks, never a GPU. */
#include "NegroniGpuWinsys.h"

static unsigned opens, escapes, closes, mode;
#define CHECK(x) do { if (!(x)) ExitProcess(__LINE__); } while (0)

NTSTATUS APIENTRY D3DKMTOpenAdapterFromLuid(const D3DKMT_OPENADAPTERFROMLUID *Open)
{
    ++opens;
    CHECK(Open->AdapterLuid.HighPart == 1 && Open->AdapterLuid.LowPart == 2);
    if (mode == 1)
        return (NTSTATUS)0xc0000001L;
    ((D3DKMT_OPENADAPTERFROMLUID *)Open)->hAdapter = 7;
    return 0;
}

NTSTATUS APIENTRY D3DKMTEscape(const D3DKMT_ESCAPE *Escape)
{
    NG_GPU_CAPS_QUERY *q = (NG_GPU_CAPS_QUERY *)Escape->pPrivateDriverData;
    ++escapes;
    CHECK(Escape->hAdapter == 7 && Escape->hDevice == 0 && Escape->hContext == 0);
    CHECK(Escape->Type == D3DKMT_ESCAPE_DRIVERPRIVATE && Escape->Flags.Value == 0);
    CHECK(Escape->PrivateDriverDataSize == sizeof(*q));
    if (mode == 2)
        return (NTSTATUS)0xc00000bbL;
    CHECK(NgGpuHandleCapsQuery(q, sizeof(*q), NG_GPU_STAGE_RESOURCES_VALIDATED));
    if (mode == 3) q->Magic ^= 1;
    if (mode == 4) q->Features |= NG_GPU_FEATURE_RENDER;
    return 0;
}

NTSTATUS APIENTRY D3DKMTCloseAdapter(const D3DKMT_CLOSEADAPTER *Close)
{
    CHECK(Close->hAdapter == 7);
    ++closes;
    return 0;
}

void mainCRTStartup(void)
{
    NG_GPU_WINSYS ws;
    LUID luid = {2,1};
    DWORD written;
    static const char pass[] = "A730 Windows winsys transport tests passed\r\n";
    CHECK(NgGpuWinsysOpen(luid, NULL) < 0 && opens == 0);
    for (mode = 1; mode <= 4; ++mode) {
        unsigned oldCloses = closes, oldEscapes = escapes;
        CHECK(NgGpuWinsysOpen(luid, &ws) < 0);
        CHECK(ws.Adapter == 0 && ws.Caps.Magic == 0);
        CHECK(!NgGpuWinsysCanRender(&ws));
        CHECK(closes == oldCloses + (mode != 1));
        CHECK(escapes == oldEscapes + (mode != 1));
    }
    mode = 0;
    CHECK(NgGpuWinsysOpen(luid, &ws) >= 0 && ws.Adapter == 7);
    CHECK(NgGpuValidateCapsReply(&ws.Caps));
    CHECK(!NgGpuWinsysCanRender(&ws));
    NgGpuWinsysClose(&ws);
    CHECK(ws.Adapter == 0 && ws.Caps.Features == 0 && closes == 4);
    NgGpuWinsysClose(&ws);
    NgGpuWinsysClose(NULL);
    CHECK(closes == 4 && !NgGpuWinsysCanRender(NULL));
    (void)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), pass, sizeof(pass)-1, &written, NULL);
    ExitProcess(0);
}
