/* negroni: replaces drivers_peripheral display/utils/include/display_log.h (hilog) */
#pragma once
#include "negroni_klog.h"
#define DISPLAY_LOGD(fmt, ...) ((void)0)
#define DISPLAY_LOGI(fmt, ...) NegroniKlog("<6>", "%s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define DISPLAY_LOGW(fmt, ...) NegroniKlog("<4>", "%s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define DISPLAY_LOGE(fmt, ...) NegroniKlog("<3>", "%s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define DISPLAY_CHK_RETURN(val, ret, ...) do { if (val) { __VA_ARGS__; return (ret); } } while (0)
#define DISPLAY_CHK_RETURN_NOT_VALUE(val, ...) do { if (val) { __VA_ARGS__; return; } } while (0)
#define CHECK_NULLPOINTER_RETURN_VALUE(pointer, ret) do { if ((pointer) == NULL) { DISPLAY_LOGE("pointer is null"); return (ret); } } while (0)
#define CHECK_NULLPOINTER_RETURN(pointer) do { if ((pointer) == NULL) { DISPLAY_LOGE("pointer is null"); return; } } while (0)
#define DISPLAY_DEBUGLOG(...) ((void)0)
#define DISPLAY_UNUSED(x) (void)(x)
