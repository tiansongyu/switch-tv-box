#define _POSIX_C_SOURCE 200809L

#include "app/platform_compat.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static atomic_uint access_calls;
static atomic_uint select_calls;
static bool marker_present;
static bool expect_compat;
static volatile sig_atomic_t signals_seen;

int __real_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

int __wrap_select(int nfds, fd_set *readfds, fd_set *writefds,
                   fd_set *exceptfds, struct timeval *timeout)
{
    atomic_fetch_add(&select_calls, 1);
    if (expect_compat)
        assert(timeout && timeout->tv_sec == 0 && timeout->tv_usec == 0);
    return __real_select(nfds, readfds, writefds, exceptfds, timeout);
}

static void sleep_ms(unsigned milliseconds)
{
    struct timespec duration = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000,
    };
    while (nanosleep(&duration, &duration) < 0)
        assert(errno == EINTR);
}

int __wrap_access(const char *path, int mode)
{
    assert(strcmp(path, "sdmc:/switch/switch-tv-box/eden-compat.flag") == 0);
    assert(mode == F_OK);
    atomic_fetch_add(&access_calls, 1);
    sleep_ms(15); /* Force simultaneous first callers to await initialization. */
    if (marker_present)
        return 0;
    errno = ENOENT;
    return -1;
}

static uint64_t now_ns(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void *check_cached_marker(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 100; ++i)
        assert(tvbox_eden_compat_enabled() == expect_compat);
    return NULL;
}

static void check_marker_initialization(void)
{
    pthread_t callers[12];
    for (unsigned i = 0; i < 12; ++i)
        assert(pthread_create(&callers[i], NULL, check_cached_marker, NULL) == 0);
    for (unsigned i = 0; i < 12; ++i)
        assert(pthread_join(callers[i], NULL) == 0);
    assert(atomic_load(&access_calls) == 1);
    marker_present = !marker_present;
    assert(tvbox_eden_compat_enabled() == expect_compat);
    assert(atomic_load(&access_calls) == 1);
}

typedef struct
{
    int socket_fd;
    struct sockaddr_in target;
    pthread_t thread;
    enum { SEND_PACKET, CLOSE_SOCKET, INTERRUPT_THREAD } action;
} DelayedAction;

static void *delayed_action(void *opaque)
{
    DelayedAction *action = opaque;
    sleep_ms(20);
    if (action->action == SEND_PACKET)
        assert(sendto(action->socket_fd, "hello", 5, 0,
                      (struct sockaddr *)&action->target,
                      sizeof(action->target)) == 5);
    else if (action->action == CLOSE_SOCKET)
        assert(close(action->socket_fd) == 0);
    else
        assert(pthread_kill(action->thread, SIGUSR1) == 0);
    return NULL;
}

static int udp_receiver(struct sockaddr_in *address)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0 && fd < FD_SETSIZE);
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, (struct sockaddr *)address, sizeof(*address)) == 0);
    socklen_t size = sizeof(*address);
    assert(getsockname(fd, (struct sockaddr *)address, &size) == 0);
    return fd;
}

static void on_signal(int signal_number)
{
    assert(signal_number == SIGUSR1);
    ++signals_seen;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    expect_compat = strcmp(argv[1], "eden") == 0;
    marker_present = expect_compat;
    check_marker_initialization();

    struct sockaddr_in first_address, second_address;
    int first = udp_receiver(&first_address);
    int second = udp_receiver(&second_address);
    int sender = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sender >= 0);
    int nfds = (first > second ? first : second) + 1;
    fd_set readfds, exceptfds;
    FD_ZERO(&readfds);
    FD_SET(first, &readfds);
    FD_SET(second, &readfds);
    FD_ZERO(&exceptfds);
    FD_SET(first, &exceptfds);
    DelayedAction action = {
        .socket_fd = sender, .target = second_address, .action = SEND_PACKET,
    };
    pthread_t worker;
    assert(pthread_create(&worker, NULL, delayed_action, &action) == 0);
    struct timeval timeout = {.tv_usec = 300000};
    assert(tvbox_socket_select(nfds, &readfds, NULL, &exceptfds, &timeout) == 1);
    assert(!FD_ISSET(first, &readfds) && FD_ISSET(second, &readfds));
    assert(!FD_ISSET(first, &exceptfds));
    assert(timeout.tv_sec == 0 && timeout.tv_usec > 0 &&
           timeout.tv_usec < 300000);
    char packet[5];
    assert(recv(second, packet, sizeof(packet), 0) == 5);
    assert(memcmp(packet, "hello", 5) == 0);
    assert(pthread_join(worker, NULL) == 0);
    if (!expect_compat)
    {
        /* Non-Eden uses exactly one original blocking select call. */
        assert(atomic_load(&select_calls) == 1);
    }
    else
    {
        assert(atomic_load(&select_calls) > 1);

        FD_ZERO(&readfds);
        FD_SET(first, &readfds);
        timeout = (struct timeval){.tv_usec = 25000};
        uint64_t started = now_ns();
        assert(tvbox_socket_select(first + 1, &readfds, NULL, NULL, &timeout) == 0);
        uint64_t elapsed = now_ns() - started;
        assert(elapsed >= 25000000 && elapsed < 1000000000);
        assert(!FD_ISSET(first, &readfds));
        assert(timeout.tv_sec == 0 && timeout.tv_usec == 0);

        FD_ZERO(&readfds);
        FD_SET(first, &readfds);
        timeout = (struct timeval){0};
        unsigned before = atomic_load(&select_calls);
        assert(tvbox_socket_select(first + 1, &readfds, NULL, NULL, &timeout) == 0);
        assert(atomic_load(&select_calls) == before + 1);
        assert(!FD_ISSET(first, &readfds));

        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(sender, &writefds);
        timeout = (struct timeval){.tv_sec = 1};
        assert(tvbox_socket_select(sender + 1, NULL, &writefds, NULL, &timeout) == 1);
        assert(FD_ISSET(sender, &writefds));

        /* NULL timeout remains an infinite wait, released by real UDP input. */
        FD_ZERO(&readfds);
        FD_SET(second, &readfds);
        assert(pthread_create(&worker, NULL, delayed_action, &action) == 0);
        assert(tvbox_socket_select(second + 1, &readfds, NULL, NULL, NULL) == 1);
        assert(FD_ISSET(second, &readfds));
        assert(recv(second, packet, sizeof(packet), 0) == 5);
        assert(pthread_join(worker, NULL) == 0);

        /* A descriptor closed while waiting terminates promptly with EBADF. */
        action = (DelayedAction){.socket_fd = first, .action = CLOSE_SOCKET};
        FD_ZERO(&readfds);
        FD_SET(first, &readfds);
        assert(pthread_create(&worker, NULL, delayed_action, &action) == 0);
        assert(tvbox_socket_select(first + 1, &readfds, NULL, NULL, NULL) == -1);
        assert(errno == EBADF);
        assert(pthread_join(worker, NULL) == 0);
        first = -1;

        struct sigaction signal_action = {.sa_handler = on_signal};
        struct sigaction old_action;
        sigemptyset(&signal_action.sa_mask);
        assert(sigaction(SIGUSR1, &signal_action, &old_action) == 0);
        action = (DelayedAction){
            .thread = pthread_self(), .action = INTERRUPT_THREAD,
        };
        FD_ZERO(&readfds);
        FD_SET(second, &readfds);
        timeout = (struct timeval){.tv_usec = 300000};
        assert(pthread_create(&worker, NULL, delayed_action, &action) == 0);
        assert(tvbox_socket_select(second + 1, &readfds, NULL, NULL, &timeout) == -1);
        assert(errno == EINTR);
        assert(timeout.tv_sec == 0 && timeout.tv_usec > 0 &&
               timeout.tv_usec < 300000);
        assert(pthread_join(worker, NULL) == 0);
        assert(signals_seen == 1);
        assert(sigaction(SIGUSR1, &old_action, NULL) == 0);

        timeout = (struct timeval){.tv_usec = -1};
        assert(tvbox_socket_select(0, NULL, NULL, NULL, &timeout) == -1);
        assert(errno == EINVAL);
    }
    if (first >= 0)
        close(first);
    close(second);
    close(sender);
    printf("platform compat (%s): cached marker, readiness and wait semantics passed\n",
           expect_compat ? "eden" : "native");
    return 0;
}
