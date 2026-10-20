#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/ioctl.h>

static unsigned ioctl_calls;
static int mock_ioctl_unsupported;

static int test_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    va_start(args, request);
    void *value = va_arg(args, void *);
    va_end(args);
    ++ioctl_calls;
    if (mock_ioctl_unsupported)
    {
        errno = ENOTTY;
        return -1;
    }
    return ioctl(fd, request, value);
}

/* Include the private receive helper so this exercises the production path.
 * Unused Switch service functions are discarded by the host test linker. */
static void svcSleepThread(int64_t ns) { (void)ns; }
#define ioctl test_ioctl
#include "../source/protocol/dlna/discovery/ssdp.c"
#undef ioctl

static bool readable(int fd)
{
    fd_set set;
    struct timeval timeout = {0};
    FD_ZERO(&set);
    FD_SET(fd, &set);
    int result = select(fd + 1, &set, NULL, NULL, &timeout);
    assert(result >= 0);
    return result > 0;
}

static void send_packet(int sender, const struct sockaddr_in *receiver,
                        const void *data, size_t size)
{
    assert(sendto(sender, data, size, 0, (const struct sockaddr *)receiver,
                  sizeof(*receiver)) == (ssize_t)size);
}

static void expect_packet(int fd, const void *data, size_t size)
{
    struct sockaddr_in from;
    ssize_t length = -1;
    assert(readable(fd));
    char *received = ssdp_recv_packet_alloc(fd, &from, &length);
    assert(received);
    assert(length == (ssize_t)size);
    assert(memcmp(received, data, size) == 0);
    assert(received[size] == '\0');
    assert(from.sin_family == AF_INET);
    free(received);
}

int main(void)
{
    int receiver = socket(AF_INET, SOCK_DGRAM, 0);
    int sender = socket(AF_INET, SOCK_DGRAM, 0);
    assert(receiver >= 0 && sender >= 0);
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    assert(bind(receiver, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t address_size = sizeof(address);
    assert(getsockname(receiver, (struct sockaddr *)&address, &address_size) == 0);
    struct timeval timeout = {.tv_sec = 2};
    assert(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                      sizeof(timeout)) == 0);
    const char search[] = "M-SEARCH * HTTP/1.1\r\nST: ssdp:all\r\n\r\n";

    /* Real-console branch still uses the exact pending packet length. */
    g_ssdp.eden_compat = false;
    send_packet(sender, &address, search, sizeof(search) - 1);
    expect_packet(receiver, search, sizeof(search) - 1);
    assert(ioctl_calls == 1);
    assert(!readable(receiver));
    send_packet(sender, &address, "", 0);
    assert(readable(receiver));
    struct sockaddr_in native_from;
    ssize_t native_length = 123;
    assert(!ssdp_recv_packet_alloc(receiver, &native_from, &native_length));
    assert(native_length == -1);
    assert(ioctl_calls == 2);
    assert(!readable(receiver));

    /* An emulator that rejects every ioctl must still drain ordinary and
     * maximum-size IPv4 datagrams without leaving select permanently ready. */
    g_ssdp.eden_compat = true;
    mock_ioctl_unsupported = 1;
    ioctl_calls = 0;
    send_packet(sender, &address, search, sizeof(search) - 1);
    expect_packet(receiver, search, sizeof(search) - 1);
    assert(!readable(receiver));

    unsigned char *large = malloc(SSDP_MAX_DATAGRAM_SIZE);
    assert(large);
    for (size_t i = 0; i < SSDP_MAX_DATAGRAM_SIZE; ++i)
        large[i] = (unsigned char)(i * 29u + 7u);
    send_packet(sender, &address, large, SSDP_MAX_DATAGRAM_SIZE);
    expect_packet(receiver, large, SSDP_MAX_DATAGRAM_SIZE);
    free(large);
    assert(!readable(receiver));

    /* Empty UDP payloads are ignored, but must be consumed as well. */
    send_packet(sender, &address, "", 0);
    send_packet(sender, &address, search, sizeof(search) - 1);
    assert(readable(receiver));
    struct sockaddr_in from;
    ssize_t length = 123;
    assert(!ssdp_recv_packet_alloc(receiver, &from, &length));
    assert(length == -1);
    expect_packet(receiver, search, sizeof(search) - 1);
    assert(!readable(receiver));
    assert(ioctl_calls == 0);

    close(sender);
    close(receiver);
    puts("SSDP receive: native FIONREAD and ioctl-free Eden UDP draining passed");
    return 0;
}
