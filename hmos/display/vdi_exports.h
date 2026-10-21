/** negroni: C entry points of libdisplay_composer_vdi_impl.z.so for HarmonyOS NEXT's composer service. */
#pragma once
#include "display_composer_vdi_impl.h"
#include "common/include/display_vdi_adapter_interface.h"
#include "negroni_klog.h"

namespace OHOS::HDI::Display::Composer {
using namespace OHOS::HDI::Display::Composer::V1_0;
using namespace OHOS::HDI::Display::Composer::V1_1;
using namespace OHOS::HDI::Display::Composer::V1_2;
using namespace OHOS::HDI::Display::Composer::V1_3;

static inline OHOS::HDI::DISPLAY::DisplayComposerVdiImpl *NegroniComposer()
{
    static auto *impl = new OHOS::HDI::DISPLAY::DisplayComposerVdiImpl();
    return impl;
}

static inline void NegroniStub(const char *name)
{
    NegroniKlog("<4>", "%s: not supported (stub)\n", name);
}
} // namespace OHOS::HDI::Display::Composer
