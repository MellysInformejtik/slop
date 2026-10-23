#include "test-objects-shim.h"
#include "NegroniGpuObjects.h"
#include <stdio.h>
#include <stdatomic.h>

static _Atomic size_t live;
static int fail_next;
void *ExAllocatePool2(unsigned flags, size_t size, unsigned tag)
{
    void *p;
    (void)flags; (void)tag;
    if (fail_next) { fail_next = 0; return NULL; }
    p = calloc(1, size);
    if (p) ++live;
    return p;
}
void ExFreePoolWithTag(void *p, unsigned tag)
{
    (void)tag;
    assert(p && live);
    --live;
    free(p);
}
static DXGKARG_CREATEDEVICE device_args(void)
{
    DXGKARG_CREATEDEVICE a = {0};
    a.hDevice = (HANDLE)(uintptr_t)123;
    return a;
}
static DXGKARG_CREATECONTEXT context_args(void)
{
    DXGKARG_CREATECONTEXT a = {0};
    a.hContext = (HANDLE)(uintptr_t)456;
    a.EngineAffinity = 1;
    memset(&a.ContextInfo, 0xa5, sizeof(a.ContextInfo));
    return a;
}
static void *destroy_batch(void *arg)
{
    DXGKARG_CREATECONTEXT *contexts = arg;
    unsigned i;
    for (i = 0; i < 64; ++i)
        assert(NgGpuDestroyContextObject(contexts[i].hContext) == 0);
    return NULL;
}
int main(void)
{
    DXGKARG_CREATEDEVICE d, before_d;
    DXGKARG_CREATECONTEXT c, before_c, children[512];
    pthread_t threads[8];
    unsigned i;
    assert(NgGpuCreateDeviceObject(NULL) == STATUS_INVALID_PARAMETER);
    for (i = 1; i < 32; ++i) {
        d = device_args(); d.Flags.Value = 1u << i; before_d = d;
        assert(NgGpuCreateDeviceObject(&d) == STATUS_NOT_SUPPORTED);
        assert(!memcmp(&d, &before_d, sizeof(d)) && live == 0);
    }
    d = device_args(); before_d = d; fail_next = 1;
    assert(NgGpuCreateDeviceObject(&d) == STATUS_NO_MEMORY);
    assert(!memcmp(&d, &before_d, sizeof(d)) && live == 0);
    d = device_args(); d.Pasid = 1;
    assert(NgGpuCreateDeviceObject(&d) == STATUS_NOT_SUPPORTED);
    d = device_args(); d.hKmdProcess = (HANDLE)(uintptr_t)1;
    assert(NgGpuCreateDeviceObject(&d) == STATUS_NOT_SUPPORTED);
    d = device_args(); d.Flags.Value = 1;
    assert(NgGpuCreateDeviceObject(&d) == 0 && live == 1);
    for (i = 1; i < 32; ++i) {
        c = context_args(); c.Flags.Value = 1u << i; before_c = c;
        assert(NgGpuCreateContextObject(d.hDevice, &c) == STATUS_NOT_SUPPORTED);
        assert(!memcmp(&c, &before_c, sizeof(c)) && live == 1);
    }
    c = context_args(); c.NodeOrdinal = 1;
    assert(NgGpuCreateContextObject(d.hDevice, &c) == STATUS_NOT_SUPPORTED);
    c = context_args(); c.EngineAffinity = 0;
    assert(NgGpuCreateContextObject(d.hDevice, &c) == STATUS_NOT_SUPPORTED);
    c.EngineAffinity = 3;
    assert(NgGpuCreateContextObject(d.hDevice, &c) == STATUS_NOT_SUPPORTED);
    c = context_args(); c.PrivateDriverDataSize = 1;
    assert(NgGpuCreateContextObject(d.hDevice, &c) == STATUS_NOT_SUPPORTED);
    c = context_args(); before_c = c; fail_next = 1;
    assert(NgGpuCreateContextObject(d.hDevice, &c) == STATUS_NO_MEMORY);
    assert(!memcmp(&c, &before_c, sizeof(c)) && live == 1);
    /* Multiple contexts, and no artificial device/context count cap. */
    for (i = 0; i < 512; ++i) {
        children[i] = context_args(); children[i].Flags.Value = i & 1;
        assert(NgGpuCreateContextObject(d.hDevice, &children[i]) == 0);
        assert(children[i].ContextInfo.DmaBufferSize == 65536);
        assert(children[i].ContextInfo.Reserved == 0);
        assert(children[i].ContextInfo.Caps.Value == 0);
    }
    assert(live == 513);
    assert(NgGpuDestroyDeviceObject(d.hDevice) == 0 && live == 513);
    c = context_args(); before_c = c;
    assert(NgGpuCreateContextObject(d.hDevice, &c) == STATUS_INVALID_DEVICE_STATE);
    assert(!memcmp(&c, &before_c, sizeof(c)) && live == 513);
    for (i = 0; i < 8; ++i)
        assert(pthread_create(&threads[i], NULL, destroy_batch, &children[64*i]) == 0);
    for (i = 0; i < 8; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(live == 0);
    /* Normal child-first close and an independent device. */
    d = device_args(); assert(NgGpuCreateDeviceObject(&d) == 0);
    c = context_args(); assert(NgGpuCreateContextObject(d.hDevice, &c) == 0);
    assert(NgGpuDestroyContextObject(c.hContext) == 0 && live == 1);
    assert(NgGpuDestroyDeviceObject(d.hDevice) == 0 && live == 0);
    assert(NgGpuDestroyDeviceObject(NULL) == STATUS_INVALID_PARAMETER);
    assert(NgGpuDestroyContextObject(NULL) == STATUS_INVALID_PARAMETER);
    puts("WDDM software object ownership tests passed (mock allocator/locks)");
    return 0;
}
