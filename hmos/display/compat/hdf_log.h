/* negroni: replaces HDF hdf_log.h (hilog) */
#pragma once
#include "negroni_klog.h"
#define HDF_LOGD(fmt, ...) ((void)0)
#define HDF_LOGV(fmt, ...) ((void)0)
#define HDF_LOGI(fmt, ...) NegroniKlog("<6>", fmt "\n", ##__VA_ARGS__)
#define HDF_LOGW(fmt, ...) NegroniKlog("<4>", fmt "\n", ##__VA_ARGS__)
#define HDF_LOGE(fmt, ...) NegroniKlog("<3>", fmt "\n", ##__VA_ARGS__)
