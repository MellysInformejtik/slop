/**
 * negroni: libdisplay_buffer_vdi_impl.z.so for HarmonyOS NEXT 6.1's allocator_host.
 *
 * OpenHarmony's DRM allocator (buffer/vdi_base DisplayBufferVdiImpl) plus the one virtual NEXT appends
 * after ReAllocMem: CloneDmaBufferHandle (vtable order taken from Huawei's gralloc::DisplayBufferVdiImpl).
 */
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "display_buffer_vdi_impl.h"
#include "display_log.h"

namespace OHOS::HDI::DISPLAY {
class NegroniBufferVdi : public DisplayBufferVdiImpl {
public:
    // NEXT: virtual int32_t CloneDmaBufferHandle(const BufferHandle& handle, BufferHandle*& out) const
    virtual int32_t CloneDmaBufferHandle(const BufferHandle& handle, BufferHandle*& out) const
    {
        size_t size = sizeof(BufferHandle) + (handle.reserveFds + handle.reserveInts) * sizeof(int32_t);
        BufferHandle *copy = static_cast<BufferHandle *>(malloc(size));
        if (copy == nullptr) {
            return -6;   // DISPLAY_NOMEM
        }
        memcpy(copy, &handle, size);
        copy->fd = handle.fd >= 0 ? dup(handle.fd) : -1;
        for (uint32_t i = 0; i < handle.reserveFds; i++) {
            copy->reserve[i] = handle.reserve[i] >= 0 ? dup(handle.reserve[i]) : -1;
        }
        if (handle.fd >= 0 && copy->fd < 0) {
            DISPLAY_LOGE("clone: dup fd %d failed errno %d", handle.fd, errno);
            free(copy);
            return -1;  // DISPLAY_FAILURE
        }
        out = copy;
        return 0;   // DISPLAY_SUCCESS
    }
};
} // namespace OHOS::HDI::DISPLAY

extern "C" __attribute__((visibility("default"))) OHOS::HDI::Display::Buffer::V1_0::IDisplayBufferVdi *CreateDisplayBufferVdi()
{
    return new OHOS::HDI::DISPLAY::NegroniBufferVdi();
}

extern "C" __attribute__((visibility("default"))) void DestroyDisplayBufferVdi(OHOS::HDI::Display::Buffer::V1_0::IDisplayBufferVdi *vdi)
{
    delete vdi;
}
