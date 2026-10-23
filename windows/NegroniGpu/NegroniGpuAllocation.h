#pragma once
/* WDK declarations must precede this header. */
NTSTATUS NgGpuCreateAllocations(INOUT_PDXGKARG_CREATEALLOCATION Args,
                               unsigned ApertureSegmentId);
NTSTATUS NgGpuDestroyAllocations(IN_CONST_PDXGKARG_DESTROYALLOCATION Args);
