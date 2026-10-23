/* SPDX-License-Identifier: MIT */
/* Native ARM64 Windows diagnostic client; no CRT startup dependency. */
#include "NegroniGpuWinsys.h"
#include <shellapi.h>

static void output(const char *Text)
{
    DWORD length = 0, written;
    while (Text[length] != 0)
        ++length;
    (void)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), Text, length, &written, NULL);
}

static void hex(uint32_t Value)
{
    static const char digits[] = "0123456789abcdef";
    char text[9];
    unsigned i;
    for (i = 0; i < 8; ++i)
        text[i] = digits[(Value >> (28 - 4 * i)) & 15];
    text[8] = 0;
    output(text);
}

static int parsehex(const WCHAR *Text, uint32_t *Value)
{
    uint32_t value = 0;
    unsigned count = 0;
    if (Text[0] == L'0' && (Text[1] == L'x' || Text[1] == L'X'))
        Text += 2;
    while (*Text != 0) {
        unsigned digit;
        WCHAR c = *Text++;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        else return 0;
        if (++count > 8) return 0;
        value = (value << 4) | digit;
    }
    *Value = value;
    return count != 0;
}

void mainCRTStartup(void)
{
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    NTSTATUS status;
    DWORD exitCode = 1;
    if (argv == NULL)
        ExitProcess(1);
    if (argc == 1) {
        D3DKMT_ADAPTERINFO adapters[64];
        D3DKMT_ENUMADAPTERS2 list = {0};
        unsigned i;
        list.NumAdapters = 64;
        list.pAdapters = adapters;
        status = D3DKMTEnumAdapters2(&list);
        if (status < 0 || list.NumAdapters > 64) {
            output("Adapter enumeration failed: "); hex((uint32_t)status); output("\r\n");
        } else {
            for (i = 0; i < list.NumAdapters; ++i) {
                D3DKMT_CLOSEADAPTER close = {0};
                output("LUID "); hex((uint32_t)adapters[i].AdapterLuid.HighPart);
                output(" "); hex(adapters[i].AdapterLuid.LowPart);
                output(" sources="); hex(adapters[i].NumOfSources); output("\r\n");
                close.hAdapter = adapters[i].hAdapter;
                (void)D3DKMTCloseAdapter(&close);
            }
            output("Query only the Negroni prototype: winsys-probe.exe HIGH_HEX LOW_HEX\r\n");
            exitCode = 0;
        }
    } else if (argc == 3) {
        NG_GPU_WINSYS winsys;
        uint32_t high, low;
        if (parsehex(argv[1], &high) && parsehex(argv[2], &low)) {
            LUID luid;
            luid.HighPart = (LONG)high;
            luid.LowPart = low;
            status = NgGpuWinsysOpen(luid, &winsys);
            if (status < 0) {
                output("Negroni handshake failed: "); hex((uint32_t)status); output("\r\n");
            } else {
                output("Negroni ABI v1; target=A730; stage="); hex(winsys.Caps.Stage);
                output(" features="); hex(winsys.Caps.Features);
                output("\r\nMetadata query only; allocation and rendering are not implemented.\r\n");
                NgGpuWinsysClose(&winsys);
                exitCode = 0;
            }
        }
    } else {
        output("Usage: winsys-probe.exe [HIGH_HEX LOW_HEX]\r\n");
    }
    LocalFree(argv);
    ExitProcess(exitCode);
}
