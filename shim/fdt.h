#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);
/*
 * The idx-th <base size> pair of the top-level /memory* nodes' reg (two
 * address and two size cells, as on every Qualcomm tree). Returns 0 past the end.
 */
int fdt_memory(uintptr_t fdt, int idx, uint64_t *base, uint64_t *size);
