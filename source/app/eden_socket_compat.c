#include "eden_socket_compat.h"

#include "app/platform_compat.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

int __real_socket(int domain, int type, int protocol);
int __real_setsockopt(int socket_fd, int level, int option_name,
                      const void *value, socklen_t value_size);

int __wrap_socket(int domain, int type, int protocol)
{
    /* Eden 0.2.1 treats the full type as an enum and rejects CLOEXEC. The
     * application does not exec another process. Retain all other flags,
     * especially NONBLOCK, instead of silently changing blocking behavior. */
#ifdef SOCK_CLOEXEC
    if (tvbox_eden_compat_enabled())
        type &= ~SOCK_CLOEXEC;
#endif
    return __real_socket(domain, type, protocol);
}

int __wrap_setsockopt(int socket_fd, int level, int option_name,
                      const void *value, socklen_t value_size)
{
    if (tvbox_eden_compat_enabled() && level == SOL_SOCKET &&
        (option_name == SO_RCVTIMEO || option_name == SO_SNDTIMEO) &&
        value && value_size == sizeof(struct timeval))
    {
        /* libnx accepts timeval, while this Eden BSD implementation expects
         * a u32 number of milliseconds. memcpy also accepts unaligned input. */
        struct timeval timeout;
        memcpy(&timeout, value, sizeof(timeout));
        if (timeout.tv_sec < 0 || timeout.tv_usec < 0 ||
            timeout.tv_usec >= 1000000)
        {
            errno = EINVAL;
            return -1;
        }
        uint64_t seconds = (uint64_t)timeout.tv_sec;
        uint64_t fraction_ms = ((uint64_t)timeout.tv_usec + 999u) / 1000u;
        if (seconds > UINT32_MAX / UINT64_C(1000) ||
            seconds * UINT64_C(1000) + fraction_ms > UINT32_MAX)
        {
            errno = EOVERFLOW;
            return -1;
        }
        /* Zero preserves an unlimited wait; a positive sub-millisecond
         * timeout must never round down to that unlimited-wait value. */
        uint32_t milliseconds = (uint32_t)(seconds * 1000u + fraction_ms);
        return __real_setsockopt(socket_fd, level, option_name,
                                 &milliseconds, sizeof(milliseconds));
    }
    return __real_setsockopt(socket_fd, level, option_name, value, value_size);
}
