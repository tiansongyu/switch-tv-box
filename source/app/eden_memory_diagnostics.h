#ifndef SWITCH_TV_BOX_EDEN_MEMORY_DIAGNOSTICS_H
#define SWITCH_TV_BOX_EDEN_MEMORY_DIAGNOSTICS_H

#ifdef __SWITCH__
#include <switch.h>

/* Linker wrappers: always forward the original arguments and result.
 * Only failures in the explicit Eden compatibility mode are diagnosed. */
Result __wrap_svcMapMemory(void *dst_addr, void *src_addr, u64 size);
Result __wrap_svcCreateThread(Handle *out, void *entry, void *arg,
                             void *stack_top, int prio, int cpuid);
void __wrap_nvMapClose(NvMap *map);
#endif

#endif
