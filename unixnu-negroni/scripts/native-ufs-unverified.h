/*
 * negroni: NativeUfs built with NATIVE_VERIFIED_BLOCK=0. Upstream defines these two only inside its
 * NATIVE_VERIFIED_BLOCK section, but the read path uses them in both configurations. Force-included
 * by build-arm-driver.py for NativeUfs (diagnostic counters only, same definitions as upstream).
 */
#pragma once
#include <stdint.h>
volatile uint64_t native_ufs_read_ticks = 0;
static inline uint64_t timerTicks (void) {uint64_t v; __asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (v)); return v;}
