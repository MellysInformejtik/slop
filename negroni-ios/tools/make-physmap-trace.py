#!/usr/bin/env python3
"""Generate the iOS 18 diagnostic patch without modifying a shared XNU tree."""
import difflib
import pathlib
import sys

source = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2])
before = source.read_text()
after = before.replace('#include <kern/assert.h>\n',
                       '#include <kern/assert.h>\n#include <kern/backtrace.h>\n#include <kern/clock.h>\n', 1)
marker = '\n\nvm_map_address_t\nphystokv(pmap_paddr_t pa)'
assert before.count(marker) == 1
helper = r'''

/* Failure-only diagnostics. Never widen the physmap or touch the rejected PA.
 * The global guard also prevents recursive faults in printf/backtrace from
 * recursing through the diagnostic. Every caller still reaches its panic.
 * Log before panic so framebuffer/console-ring captures contain the frames
 * even if the later panic-buffer-to-UFS flush cannot complete.
 */
static void __attribute__((noinline))
negroni_trace_bad_physmap(const char *operation, uint64_t address,
    uint64_t length, uintptr_t caller)
{
    static unsigned int entered;
    uintptr_t frames[16] = { 0 };
    backtrace_info_t info = BTI_NONE;
    unsigned int count;

    if (!__sync_bool_compare_and_swap(&entered, 0, 1)) {
        return;
    }
    printf("NEGRONI PHYSMAP: %s addr=0x%llx len=0x%llx caller=0x%llx\n",
        operation, (unsigned long long)address, (unsigned long long)length,
        (unsigned long long)caller);
    printf("NEGRONI PHYSMAP: phys=0x%llx real=0x%llx virt=0x%llx size=0x%llx active=%u\n",
        (unsigned long long)gPhysBase, (unsigned long long)real_phys_size,
        (unsigned long long)gVirtBase, (unsigned long long)gPhysSize,
        (unsigned int)kva_active);
    /* Early bootstrap does not yet have a normal thread stack. */
    if (!kva_active) {
        return;
    }
    count = backtrace(frames, 16, NULL, &info);
    for (unsigned int i = 0; i < count && i < 16; ++i) {
        printf("NEGRONI PHYSMAP: frame[%u]=0x%llx\n", i,
            (unsigned long long)frames[i]);
    }
    printf("NEGRONI PHYSMAP: end count=%u flags=0x%x\n", count, (unsigned int)info);
    /* The panic log does not reach the partition; the console ring does, once a second
     * (ufslogsecs=1, flushed by a thread on another core). Give it three. */
    delay(3000000);
    panic("NEGRONI PHYSMAP %s pa 0x%llx caller 0x%llx bt 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx (count %u)", operation, (unsigned long long)address, (unsigned long long)caller, frames[0], frames[1], frames[2], frames[3], frames[4], frames[5], frames[6], frames[7], count);
}
'''
after = after.replace(marker, helper + marker, 1)
physical = '\t\tpanic("%s: illegal PA:'
assert after.count(physical) == 2
after = after.replace(physical,
    '\t\tnegroni_trace_bad_physmap(__func__, pa, 0, (uintptr_t)__builtin_return_address(0));\n' + physical, 1)
start = after.index('phystokv_range(pmap_paddr_t pa')
after = after[:start] + after[start:].replace(physical,
    '\t\tnegroni_trace_bad_physmap(__func__, pa, *max_len, (uintptr_t)__builtin_return_address(0));\n' + physical, 1)
virtual = '\t\tpanic("%s: illegal VA:'
assert after.count(virtual) == 1
after = after.replace(virtual,
    '\t\tnegroni_trace_bad_physmap(__func__, va, 0, (uintptr_t)__builtin_return_address(0));\n' + virtual, 1)
output.write_text(''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
    fromfile='a/osfmk/arm64/arm_vm_init.c', tofile='b/osfmk/arm64/arm_vm_init.c')))
print(output)
