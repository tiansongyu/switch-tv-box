#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool eden;
static uint32_t nifm_address;
static unsigned nifm_queries, route_queries;
static char sent_packet[4096];
static long test_gethostid(void) { ++nifm_queries; return nifm_address; }
bool tvbox_eden_compat_enabled(void) { return eden; }
static void svcSleepThread(int64_t ns) { (void)ns; }
static void randomGet(void *p, size_t n) { memset(p, 0, n); }
static uint64_t randomGet64(void) { return 1; }
static int test_socket(int domain, int type, int protocol)
{ (void)domain; (void)type; (void)protocol; return 42; }
static int test_close(int fd) { assert(fd == 42); return 0; }
static int test_connect(int fd, const struct sockaddr *address, socklen_t size)
{ (void)fd; (void)address; (void)size; ++route_queries; return 0; }
static int test_getsockname(int fd, struct sockaddr *address, socklen_t *size)
{
    (void)fd;
    assert(*size >= sizeof(struct sockaddr_in));
    struct sockaddr_in route = {.sin_family = AF_INET};
    route.sin_addr.s_addr = inet_addr("198.18.0.1");
    memcpy(address, &route, sizeof(route));
    *size = sizeof(route);
    return 0;
}
static ssize_t test_sendto(int fd, const void *data, size_t size, int flags,
                           const struct sockaddr *address, socklen_t length)
{
    (void)fd; (void)flags; (void)address; (void)length;
    assert(size < sizeof(sent_packet));
    memcpy(sent_packet, data, size);
    sent_packet[size] = 0;
    return (ssize_t)size;
}
void log_error(const char *format, ...) { (void)format; }
void log_warn(const char *format, ...) { (void)format; }
void log_info(const char *format, ...) { (void)format; }
const char *dlna_server_info_get(void) { return "Test/1 UPnP/1.0"; }

#define gethostid test_gethostid
#define socket test_socket
#define close test_close
#define connect test_connect
#define getsockname test_getsockname
#define sendto test_sendto
#include "../source/protocol/dlna/discovery/ssdp.c"
#include "../source/protocol/dlna_control.c"

int main(void)
{
    char *address = NULL;
    char *base;
    nifm_address = inet_addr("10.30.0.44");
    eden = g_ssdp.eden_compat = true;
    assert(determine_local_ip(&address));
    assert(strcmp(address, "10.30.0.44") == 0);
    base = dlna_build_url_base_alloc(49152);
    assert(base && strcmp(base, "http://10.30.0.44:49152/") == 0);
    assert(nifm_queries == 2 && route_queries == 0);
    g_ssdp.location = ssdp_strdup_printf("%sdescription.xml", base);
    atomic_store(&g_ssdp.socket_fd, 42);
    send_notify_packet("upnp:rootdevice", "uuid:test");
    assert(strstr(sent_packet, "LOCATION: http://10.30.0.44:49152/description.xml\r\n"));
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(1900)};
    send_msearch_response("upnp:rootdevice", "uuid:test", &peer);
    assert(strstr(sent_packet, "LOCATION: http://10.30.0.44:49152/description.xml\r\n"));
    free(g_ssdp.location);
    g_ssdp.location = NULL;
    free(address);
    free(base);

    /* Invalid NIFM addresses must not silently fall back to a VPN route. */
    const char *invalid[] = {"0.0.0.0", "127.0.0.1", "224.0.0.1", "255.255.255.255"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        nifm_address = inet_addr(invalid[i]);
        assert(!determine_local_ip(&address) && address == NULL);
        assert(dlna_build_url_base_alloc(49152) == NULL);
    }
    assert(route_queries == 0);

    /* Physical-console mode retains the original route-based result and
     * never asks the emulator-only NIFM selector. */
    eden = g_ssdp.eden_compat = false;
    unsigned previous_nifm_queries = nifm_queries;
    assert(determine_local_ip(&address));
    assert(strcmp(address, "198.18.0.1") == 0);
    base = dlna_build_url_base_alloc(49152);
    assert(base && strcmp(base, "http://198.18.0.1:49152/") == 0);
    assert(route_queries == 2 && nifm_queries == previous_nifm_queries);
    free(address);
    free(base);
    puts("DLNA address: Eden LAN URLBase/SSDP agree; native routing unchanged");
}
