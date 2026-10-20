#if !defined(__SWITCH__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform_compat.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

/* 0 = unread, 1 = initialization in progress, 2 = disabled, 3 = enabled.
 * The launcher writes the marker before starting the application. Never poll
 * the SD card from network loops, including when the marker is absent. */
static atomic_int g_eden_compat_state = 0;

static int compat_sleep_ns(uint64_t duration_ns)
{
#ifdef __SWITCH__
    svcSleepThread((int64_t)duration_ns);
    return 0;
#else
    struct timespec duration = {
        .tv_sec = (time_t)(duration_ns / UINT64_C(1000000000)),
        .tv_nsec = (long)(duration_ns % UINT64_C(1000000000)),
    };
    return nanosleep(&duration, NULL);
#endif
}

bool tvbox_eden_compat_enabled(void)
{
    int state = atomic_load_explicit(&g_eden_compat_state, memory_order_acquire);
    if (state == 0)
    {
        int expected = 0;
        if (atomic_compare_exchange_strong_explicit(
                &g_eden_compat_state, &expected, 1,
                memory_order_acq_rel, memory_order_acquire))
        {
            state = access("sdmc:/switch/switch-tv-box/eden-compat.flag", F_OK)
                        == 0 ? 3 : 2;
            atomic_store_explicit(&g_eden_compat_state, state,
                                  memory_order_release);
        }
        else
            state = expected;
    }
    while (state == 1)
    {
        (void)compat_sleep_ns(UINT64_C(1000000));
        state = atomic_load_explicit(&g_eden_compat_state, memory_order_acquire);
    }
    return state == 3;
}

static int compat_now_ns(uint64_t *now)
{
#ifdef __SWITCH__
    *now = armTicksToNs(armGetSystemTick());
#else
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0)
        return -1;
    *now = (uint64_t)timestamp.tv_sec * UINT64_C(1000000000) +
           (uint64_t)timestamp.tv_nsec;
#endif
    return 0;
}

static void compat_remaining_time(struct timeval *timeout, uint64_t remaining)
{
    if (!timeout)
        return;
    timeout->tv_sec = (time_t)(remaining / UINT64_C(1000000000));
    timeout->tv_usec = (suseconds_t)((remaining % UINT64_C(1000000000)) / 1000u);
}

int tvbox_socket_select(int nfds, fd_set *readfds, fd_set *writefds,
                        fd_set *exceptfds, struct timeval *timeout)
{
    if (!tvbox_eden_compat_enabled())
        return select(nfds, readfds, writefds, exceptfds, timeout);

    if (nfds < 0 || (timeout && (timeout->tv_sec < 0 ||
        timeout->tv_usec < 0 || timeout->tv_usec >= 1000000)))
    {
        errno = EINVAL;
        return -1;
    }

    fd_set original_read, original_write, original_except;
    if (readfds)
        original_read = *readfds;
    if (writefds)
        original_write = *writefds;
    if (exceptfds)
        original_except = *exceptfds;

    uint64_t started;
    if (compat_now_ns(&started) != 0)
        return -1;
    uint64_t duration = UINT64_MAX;
    if (timeout)
    {
        uint64_t fraction = (uint64_t)timeout->tv_usec * 1000u;
        uint64_t seconds = (uint64_t)timeout->tv_sec;
        duration = seconds > (UINT64_MAX - fraction) / UINT64_C(1000000000)
                       ? UINT64_MAX
                       : seconds * UINT64_C(1000000000) + fraction;
    }

    for (;;)
    {
        fd_set ready_read, ready_write, ready_except;
        struct timeval immediate = {0};
        if (readfds)
            ready_read = original_read;
        if (writefds)
            ready_write = original_write;
        if (exceptfds)
            ready_except = original_except;

        int result = select(nfds, readfds ? &ready_read : NULL,
                            writefds ? &ready_write : NULL,
                            exceptfds ? &ready_except : NULL, &immediate);
        int saved_errno = errno;
        uint64_t now;
        if (compat_now_ns(&now) != 0)
            return -1;
        uint64_t elapsed = now >= started ? now - started : 0;
        uint64_t remaining = elapsed < duration ? duration - elapsed : 0;
        compat_remaining_time(timeout, remaining);

        if (result != 0 || (timeout && remaining == 0))
        {
            if (result >= 0)
            {
                if (readfds)
                    *readfds = ready_read;
                if (writefds)
                    *writefds = ready_write;
                if (exceptfds)
                    *exceptfds = ready_except;
            }
            errno = saved_errno;
            return result;
        }

        uint64_t sleep_ns = UINT64_C(1000000);
        if (timeout && remaining < sleep_ns)
            sleep_ns = remaining;
        if (compat_sleep_ns(sleep_ns) != 0)
        {
            saved_errno = errno;
            if (compat_now_ns(&now) == 0)
            {
                elapsed = now >= started ? now - started : 0;
                compat_remaining_time(timeout,
                    elapsed < duration ? duration - elapsed : 0);
            }
            errno = saved_errno;
            return -1;
        }
    }
}
