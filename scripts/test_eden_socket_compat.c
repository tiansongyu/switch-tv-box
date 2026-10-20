#define _GNU_SOURCE

#include "app/eden_socket_compat.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static bool eden;
static bool mock_calls;
static unsigned calls;
static int last_domain, last_type, last_protocol;
static int last_socket, last_level, last_option;
static const void *last_pointer;
static socklen_t last_size;
static uint32_t last_milliseconds;

bool tvbox_eden_compat_enabled(void) { return eden; }

int __real_socket(int domain, int type, int protocol)
{
    ++calls;
    last_domain = domain;
    last_type = type;
    last_protocol = protocol;
    if (mock_calls)
    {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    return socket(domain, type, protocol);
}

int __real_setsockopt(int fd, int level, int option, const void *value,
                      socklen_t size)
{
    ++calls;
    last_socket = fd;
    last_level = level;
    last_option = option;
    last_pointer = value;
    last_size = size;
    if (value && size == sizeof(uint32_t))
        memcpy(&last_milliseconds, value, sizeof(last_milliseconds));
    if (mock_calls)
    {
        errno = EBUSY;
        return -1;
    }
    return setsockopt(fd, level, option, value, size);
}

static void expect_milliseconds(int option, time_t seconds, suseconds_t micros,
                                uint32_t milliseconds)
{
    struct timeval timeout = {.tv_sec = seconds, .tv_usec = micros};
    unsigned before = calls;
    assert(__wrap_setsockopt(37, SOL_SOCKET, option,
                             &timeout, sizeof(timeout)) == -1);
    assert(errno == EBUSY && calls == before + 1);
    assert(last_socket == 37 && last_level == SOL_SOCKET && last_option == option);
    assert(last_size == sizeof(uint32_t));
    assert(last_milliseconds == milliseconds);
    assert(timeout.tv_sec == seconds && timeout.tv_usec == micros);
}

static void expect_invalid(time_t seconds, suseconds_t micros, int error)
{
    struct timeval timeout = {.tv_sec = seconds, .tv_usec = micros};
    unsigned before = calls;
    assert(__wrap_setsockopt(37, SOL_SOCKET, SO_RCVTIMEO,
                             &timeout, sizeof(timeout)) == -1);
    assert(errno == error && calls == before);
}

int main(void)
{
    /* Actual native syscalls: CLOEXEC, NONBLOCK and timeval remain functional. */
    int fd = __wrap_socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    assert(fd >= 0);
    assert(fcntl(fd, F_GETFD) & FD_CLOEXEC);
    assert(fcntl(fd, F_GETFL) & O_NONBLOCK);
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 250000};
    assert(__wrap_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                             &timeout, sizeof(timeout)) == 0);
    assert(last_pointer == &timeout && last_size == sizeof(timeout));
    struct timeval observed = {0};
    socklen_t observed_size = sizeof(observed);
    assert(getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &observed, &observed_size) == 0);
    assert(observed.tv_sec == timeout.tv_sec && observed.tv_usec == timeout.tv_usec);
    close(fd);

    mock_calls = true;
    eden = true;
    int original_type = SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK;
    assert(__wrap_socket(AF_INET6, original_type, 6) == -1);
    assert(errno == EPROTONOSUPPORT);
    assert(last_domain == AF_INET6 && last_protocol == 6);
    assert(last_type == (original_type & ~SOCK_CLOEXEC));
    assert(last_type & SOCK_NONBLOCK);

    expect_milliseconds(SO_RCVTIMEO, 0, 0, 0);
    expect_milliseconds(SO_RCVTIMEO, 0, 1, 1);
    expect_milliseconds(SO_RCVTIMEO, 0, 999, 1);
    expect_milliseconds(SO_RCVTIMEO, 0, 1000, 1);
    expect_milliseconds(SO_SNDTIMEO, 0, 1001, 2);
    expect_milliseconds(SO_SNDTIMEO, 1, 999999, 2000);
    expect_milliseconds(SO_SNDTIMEO, 4294967, 295000, UINT32_MAX);
    expect_invalid(-1, 0, EINVAL);
    expect_invalid(0, -1, EINVAL);
    expect_invalid(0, 1000000, EINVAL);
    expect_invalid(4294967, 295001, EOVERFLOW);
    expect_invalid((time_t)INT64_MAX, 0, EOVERFLOW);

    /* Other levels/options, lengths and null pointers pass through unchanged. */
    assert(__wrap_setsockopt(9, SOL_SOCKET, SO_KEEPALIVE,
                             &timeout, sizeof(timeout)) == -1);
    assert(errno == EBUSY && last_pointer == &timeout && last_size == sizeof(timeout));
    assert(__wrap_setsockopt(9, 0, SO_RCVTIMEO,
                             &timeout, sizeof(timeout)) == -1);
    assert(last_level == 0 && last_pointer == &timeout);
    uint32_t milliseconds = 1234;
    assert(__wrap_setsockopt(9, SOL_SOCKET, SO_RCVTIMEO,
                             &milliseconds, sizeof(milliseconds)) == -1);
    assert(last_pointer == &milliseconds && last_size == sizeof(milliseconds));
    assert(__wrap_setsockopt(9, SOL_SOCKET, SO_RCVTIMEO, NULL, sizeof(timeout)) == -1);
    assert(last_pointer == NULL && last_size == sizeof(timeout));

    unsigned char unaligned[sizeof(timeout) + 1];
    memcpy(unaligned + 1, &timeout, sizeof(timeout));
    assert(__wrap_setsockopt(9, SOL_SOCKET, SO_RCVTIMEO,
                             unaligned + 1, sizeof(timeout)) == -1);
    assert(last_size == sizeof(uint32_t) && last_milliseconds == 1250);

    eden = false;
    assert(__wrap_socket(AF_INET6, original_type, 6) == -1);
    assert(last_type == original_type);
    timeout.tv_usec = -1;
    unsigned before = calls;
    assert(__wrap_setsockopt(9, SOL_SOCKET, SO_RCVTIMEO,
                             &timeout, sizeof(timeout)) == -1);
    assert(errno == EBUSY && calls == before + 1 && last_pointer == &timeout);
    puts("Eden socket ABI: native flags/timeval and bounded Eden conversion passed");
    return 0;
}
