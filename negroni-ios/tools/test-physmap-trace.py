#!/usr/bin/env python3
"""Exercise the exact generated helper with mocked XNU backtrace/console APIs."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
patch = root / 'xnu/patches-ios18/0128-ios18-physmap-failure-trace.patch'
added = '\n'.join(line[1:] for line in patch.read_text().splitlines()
                  if line.startswith('+') and not line.startswith('+++'))
helper = added[added.index('static void'):added.index('\n\t\tnegroni_trace_bad_physmap')]
preamble = r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef unsigned int backtrace_info_t;
#define BTI_NONE 0
static uint64_t gPhysBase=0x100000000ull, real_phys_size=0xc0000000ull;
static uint64_t gVirtBase=0xffff000000000000ull, gPhysSize=0xc0000000ull;
static unsigned int kva_active, calls, lines, panics;
static unsigned int backtrace(uintptr_t *frames, unsigned int capacity,
    void *control, backtrace_info_t *flags) {
    assert(capacity == 16 && control == NULL);
    ++calls;
    for (unsigned int i=0;i<capacity;++i) frames[i]=0x12340000+i;
    *flags=2;
    return 16;
}
static int trace_printf(const char *format, ...) { assert(format); ++lines; return 0; }
#define printf trace_printf
static void trace_panic(const char *format, ...) { assert(format); ++panics; }
#define panic trace_panic
static unsigned int delays;
static void delay(int usec) { assert(usec == 3000000); ++delays; }
'''
main = r'''
int main(int argc, char **argv) {
    (void)argv;
    kva_active = argc > 1;
    negroni_trace_bad_physmap("phystokv_range", 0xc7a24000, 4096, 0x12345678);
    assert(calls == (kva_active ? 1u : 0u));
    assert(lines == (kva_active ? 19u : 2u));
    assert(panics == (kva_active ? 1u : 0u));
    negroni_trace_bad_physmap("nested", 0, 0, 0);
    assert(calls == (kva_active ? 1u : 0u));
    assert(lines == (kva_active ? 19u : 2u));
    assert(panics == (kva_active ? 1u : 0u));
    assert(delays == panics);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'trace.c').write_text(preamble + helper + main)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'trace.c'),
                    '-o', str(path / 'trace')], check=True)
    subprocess.run([str(path / 'trace')], check=True)
    subprocess.run([str(path / 'trace'), 'active'], check=True)
print('Failure trace: early boot, 16-frame bound, frames in the panic string and repeated-entry guard passed')
