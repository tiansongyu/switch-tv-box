#ifdef __SWITCH__
#include <switch.h>

#include <stdatomic.h>
#include <stdint.h>

#include "app/platform_compat.h"
#include "log/log.h"

Result __real_audrenInitialize(const AudioRendererConfig *config);
Result __real_audrvCreate(AudioDriver *driver,
                         const AudioRendererConfig *config,
                         int num_final_mix_channels);
Result __real_audrenStartAudioRenderer(void);
Result __real_audrvUpdate(AudioDriver *driver);
Result __real_svcCreateTransferMemory(Handle *out, void *address,
                                      size_t size, u32 permission);

/* Logging may allocate memory itself. Never recursively diagnose its failures.
 * The result check is deliberately first: successful calls must not read or
 * cache the SD marker before filesystem initialization has completed. */
static atomic_flag g_audio_diagnostic_active = ATOMIC_FLAG_INIT;
static atomic_bool g_audio_update_failure_reported = false;

static bool audio_diagnostic_begin(Result result)
{
    return R_FAILED(result) && tvbox_eden_compat_enabled() &&
           !atomic_flag_test_and_set_explicit(&g_audio_diagnostic_active,
                                              memory_order_acquire);
}

static void audio_diagnostic_end(void)
{
    atomic_flag_clear_explicit(&g_audio_diagnostic_active, memory_order_release);
}

static void audio_diagnostic_config(const AudioRendererConfig *config)
{
    if (!config)
        return;
    log_error(
        "[eden-audio] config output_rate=%u voices=%d effects=%d sinks=%d "
        "mixes=%d mix_buffers=%d\n",
        (unsigned)config->output_rate, config->num_voices,
        config->num_effects, config->num_sinks, config->num_mix_objs,
        config->num_mix_buffers);
}

static void audio_diagnostic_transfer_range(void *address, size_t size)
{
    u64 cursor = (u64)(uintptr_t)address;
    u64 remaining = size ? (u64)size : 1u;
    unsigned segment = 0u;

    /* Inspect multiple regions, including clean regions before a DeviceShared
     * boundary, while keeping failure diagnostics bounded on fragmented heaps. */
    for (; segment < 8 && remaining; ++segment)
    {
        MemoryInfo info = {0};
        u32 page_info = 0u;
        Result result = svcQueryMemory(&info, &page_info, cursor);
        if (R_FAILED(result))
        {
            log_error(
                "[eden-audio] transfer-source segment=%u query=0x%llX "
                "remaining=0x%llX query_rc=0x%08X\n",
                segment, (unsigned long long)cursor,
                (unsigned long long)remaining, (unsigned)result);
            return;
        }
        log_error(
            "[eden-audio] transfer-source segment=%u query=0x%llX "
            "region=0x%llX size=0x%llX type=0x%X attr=0x%X perm=0x%X "
            "ipc=%u device=%u page=0x%X\n",
            segment, (unsigned long long)cursor, (unsigned long long)info.addr,
            (unsigned long long)info.size, info.type, info.attr, info.perm,
            info.ipc_refcount, info.device_refcount, page_info);

        if (cursor < info.addr || cursor - info.addr >= info.size)
        {
            log_error("[eden-audio] transfer-source invalid_query_range\n");
            return;
        }
        u64 available = info.size - (cursor - info.addr);
        if (available >= remaining)
            return;
        if (available > UINT64_MAX - cursor)
        {
            log_error("[eden-audio] transfer-source query_range_overflow\n");
            return;
        }
        cursor += available;
        remaining -= available;
    }
    if (remaining)
        log_error("[eden-audio] transfer-source range truncated after 8 regions "
                  "query=0x%llX remaining=0x%llX\n",
                  (unsigned long long)cursor, (unsigned long long)remaining);
}

Result __wrap_audrenInitialize(const AudioRendererConfig *config)
{
    Result result = __real_audrenInitialize(config);
    if (audio_diagnostic_begin(result))
    {
        log_error("[eden-audio] stage=audrenInitialize rc=0x%08X\n",
                  (unsigned)result);
        audio_diagnostic_config(config);
        audio_diagnostic_end();
    }
    return result;
}

Result __wrap_audrvCreate(AudioDriver *driver,
                         const AudioRendererConfig *config,
                         int num_final_mix_channels)
{
    Result result = __real_audrvCreate(driver, config, num_final_mix_channels);
    if (audio_diagnostic_begin(result))
    {
        log_error(
            "[eden-audio] stage=audrvCreate rc=0x%08X final_mix_channels=%d\n",
            (unsigned)result, num_final_mix_channels);
        audio_diagnostic_config(config);
        audio_diagnostic_end();
    }
    return result;
}

Result __wrap_audrenStartAudioRenderer(void)
{
    Result result = __real_audrenStartAudioRenderer();
    if (audio_diagnostic_begin(result))
    {
        log_error("[eden-audio] stage=audrenStartAudioRenderer rc=0x%08X\n",
                  (unsigned)result);
        audio_diagnostic_end();
    }
    return result;
}

Result __wrap_audrvUpdate(AudioDriver *driver)
{
    Result result = __real_audrvUpdate(driver);
    if (R_FAILED(result) &&
        !atomic_load_explicit(&g_audio_update_failure_reported,
                              memory_order_relaxed) &&
        audio_diagnostic_begin(result))
    {
        if (!atomic_exchange_explicit(&g_audio_update_failure_reported, true,
                                      memory_order_relaxed))
        {
            log_error(
                "[eden-audio] stage=audrvUpdate rc=0x%08X first_failure=1\n",
                (unsigned)result);
            if (driver)
                audio_diagnostic_config(&driver->config);
        }
        audio_diagnostic_end();
    }
    return result;
}

Result __wrap_svcCreateTransferMemory(Handle *out, void *address,
                                      size_t size, u32 permission)
{
    Result result = __real_svcCreateTransferMemory(out, address, size, permission);
    if (audio_diagnostic_begin(result))
    {
        log_error(
            "[eden-audio] stage=svcCreateTransferMemory rc=0x%08X "
            "src=0x%llX size=0x%llX requested_perm=0x%X\n",
            (unsigned)result, (unsigned long long)(uintptr_t)address,
            (unsigned long long)size, (unsigned)permission);
        audio_diagnostic_transfer_range(address, size);
        audio_diagnostic_end();
    }
    return result;
}
#endif
