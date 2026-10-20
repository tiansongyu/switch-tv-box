#include "eden_memory_diagnostics.h"

#ifdef __SWITCH__
#include "app/platform_compat.h"
#include "log/log.h"

#include <stdatomic.h>
#include <stdint.h>

Result __real_svcMapMemory(void *dst_addr, void *src_addr, u64 size);
Result __real_svcCreateThread(Handle *out, void *entry, void *arg,
                             void *stack_top, int prio, int cpuid);
void __real_nvMapClose(NvMap *map);

/* Do not wait or recursively diagnose failures raised by logging itself. */
static atomic_flag g_diagnostic_active = ATOMIC_FLAG_INIT;
/* Accessed only while g_diagnostic_active is held. Clean, complete closes
 * do not consume this budget; after eight reports no further close is scanned. */
static unsigned g_nvmap_diagnostic_reports = 0;

static void nvmap_diagnostic_record(bool *reported)
{
    if (!*reported)
    {
        ++g_nvmap_diagnostic_reports;
        *reported = true;
    }
}

static bool diagnostic_begin(Result result)
{
    return R_FAILED(result) && tvbox_eden_compat_enabled() &&
           !atomic_flag_test_and_set_explicit(&g_diagnostic_active,
                                              memory_order_acquire);
}

static void diagnostic_query_range(const char *label, u64 address, u64 size)
{
    u64 cursor = address;
    u64 remaining = size ? size : 1;

    /* Walk boundaries rather than assuming the first page describes the whole
     * stack. Limit diagnostics even if the original request has a huge size. */
    for (unsigned segment = 0; segment < 8 && remaining; ++segment)
    {
        MemoryInfo info = {0};
        u32 page_info = 0;
        Result result = svcQueryMemory(&info, &page_info, cursor);
        if (R_FAILED(result))
        {
            log_error("[eden-memory] %s query=0x%llX rc=0x%08X\n",
                      label, (unsigned long long)cursor, (unsigned)result);
            return;
        }
        log_error(
            "[eden-memory] %s query=0x%llX region=0x%llX size=0x%llX "
            "type=0x%X attr=0x%X perm=0x%X ipc=%u device=%u page=0x%X\n",
            label, (unsigned long long)cursor, (unsigned long long)info.addr,
            (unsigned long long)info.size, info.type, info.attr, info.perm,
            info.ipc_refcount, info.device_refcount, page_info);

        if (cursor < info.addr || cursor - info.addr >= info.size)
            return;
        u64 available = info.size - (cursor - info.addr);
        if (available >= remaining)
            return;
        if (available > UINT64_MAX - cursor)
            return;
        cursor += available;
        remaining -= available;
    }
    if (remaining)
        log_error("[eden-memory] %s range truncated after 8 regions\n", label);
}

Result __wrap_svcMapMemory(void *dst_addr, void *src_addr, u64 size)
{
    Result result = __real_svcMapMemory(dst_addr, src_addr, size);
    if (diagnostic_begin(result))
    {
        log_error(
            "[eden-memory] stage=svcMapMemory rc=0x%08X "
            "src=0x%llX dst=0x%llX size=0x%llX\n",
            (unsigned)result, (unsigned long long)(uintptr_t)src_addr,
            (unsigned long long)(uintptr_t)dst_addr, (unsigned long long)size);
        diagnostic_query_range("map-source", (u64)(uintptr_t)src_addr, size);
        diagnostic_query_range("map-destination", (u64)(uintptr_t)dst_addr, size);
        atomic_flag_clear_explicit(&g_diagnostic_active, memory_order_release);
    }
    return result;
}

Result __wrap_svcCreateThread(Handle *out, void *entry, void *arg,
                             void *stack_top, int prio, int cpuid)
{
    Result result = __real_svcCreateThread(out, entry, arg, stack_top, prio, cpuid);
    if (diagnostic_begin(result))
    {
        log_error(
            "[eden-memory] stage=svcCreateThread rc=0x%08X "
            "entry=0x%llX arg=0x%llX stack_top=0x%llX priority=%d cpu=%d\n",
            (unsigned)result, (unsigned long long)(uintptr_t)entry,
            (unsigned long long)(uintptr_t)arg,
            (unsigned long long)(uintptr_t)stack_top, prio, cpuid);
        u64 stack = (u64)(uintptr_t)stack_top;
        diagnostic_query_range("thread-stack", stack ? stack - 1 : 0, 1);
        atomic_flag_clear_explicit(&g_diagnostic_active, memory_order_release);
    }
    return result;
}

void __wrap_nvMapClose(NvMap *map)
{
    /* Keep the original release behavior, including its handling of remote
     * maps. Snapshot only locally backed, initialized Eden mappings. */
    if (!tvbox_eden_compat_enabled() || !map || !map->has_init ||
        !map->cpu_addr || !map->size)
    {
        __real_nvMapClose(map);
        return;
    }
    const NvMap before = *map;
    __real_nvMapClose(map);

    if (atomic_flag_test_and_set_explicit(&g_diagnostic_active,
                                          memory_order_acquire))
        return;
    if (g_nvmap_diagnostic_reports >= 8)
    {
        atomic_flag_clear_explicit(&g_diagnostic_active, memory_order_release);
        return;
    }

    u64 cursor = (u64)(uintptr_t)before.cpu_addr;
    u64 remaining = before.size;
    bool reported = false;
    unsigned segment = 0;
    for (; segment < 8 && remaining; ++segment)
    {
        MemoryInfo info = {0};
        u32 page_info = 0;
        Result result = svcQueryMemory(&info, &page_info, cursor);
        if (R_FAILED(result))
        {
            nvmap_diagnostic_record(&reported);
            log_error(
                "[eden-memory] stage=nvMapClose query_failed rc=0x%08X "
                "handle=0x%X id=0x%X base=0x%llX size=0x%X query=0x%llX\n",
                (unsigned)result, before.handle, before.id,
                (unsigned long long)(uintptr_t)before.cpu_addr, before.size,
                (unsigned long long)cursor);
            break;
        }
        if (info.attr != 0 || info.perm != Perm_Rw ||
            info.ipc_refcount != 0 || info.device_refcount != 0)
        {
            nvmap_diagnostic_record(&reported);
            log_error(
                "[eden-memory] stage=nvMapClose residual_state "
                "handle=0x%X id=0x%X base=0x%llX size=0x%X kind=0x%X cached=%d "
                "query=0x%llX region=0x%llX region_size=0x%llX "
                "type=0x%X attr=0x%X perm=0x%X ipc=%u device=%u page=0x%X\n",
                before.handle, before.id,
                (unsigned long long)(uintptr_t)before.cpu_addr, before.size,
                (unsigned)before.kind, before.is_cpu_cacheable ? 1 : 0,
                (unsigned long long)cursor, (unsigned long long)info.addr,
                (unsigned long long)info.size, info.type, info.attr, info.perm,
                info.ipc_refcount, info.device_refcount, page_info);
        }
        if (cursor < info.addr || cursor - info.addr >= info.size)
        {
            nvmap_diagnostic_record(&reported);
            log_error("[eden-memory] stage=nvMapClose invalid_query_range "
                      "query=0x%llX region=0x%llX size=0x%llX\n",
                      (unsigned long long)cursor, (unsigned long long)info.addr,
                      (unsigned long long)info.size);
            break;
        }
        u64 available = info.size - (cursor - info.addr);
        if (available >= remaining)
        {
            remaining = 0;
            break;
        }
        if (available > UINT64_MAX - cursor)
        {
            nvmap_diagnostic_record(&reported);
            log_error("[eden-memory] stage=nvMapClose query_range_overflow\n");
            break;
        }
        cursor += available;
        remaining -= available;
    }
    if (remaining && segment == 8)
    {
        nvmap_diagnostic_record(&reported);
        log_error("[eden-memory] stage=nvMapClose range truncated after 8 regions "
                  "query=0x%llX remaining=0x%llX\n",
                  (unsigned long long)cursor, (unsigned long long)remaining);
    }
    atomic_flag_clear_explicit(&g_diagnostic_active, memory_order_release);
}
#endif
