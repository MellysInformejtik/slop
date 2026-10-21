/* negroni: tracing off */
#pragma once
#include <string>
#define HITRACE_TAG_HDF 0
static inline void StartTrace(unsigned long long, const std::string &, float = -1) {}
static inline void FinishTrace(unsigned long long) {}
#define HITRACE_METER_NAME(tag, name) ((void)0)
#define HITRACE_METER_FMT(tag, fmt, ...) ((void)0)
