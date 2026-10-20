#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool eden;
static uint32_t nifm_address;
static unsigned nifm_queries;
static int fail_option;
static uint32_t joined_address, outbound_address;
static unsigned ttl;
static char sent_packet[4096];
static long test_gethostid(void) { ++nifm_queries; return nifm_address; }
bool tvbox_eden_compat_enabled(void) { return eden; }
static void svcSleepThread(int64_t ns) { (void)ns; }
static void randomGet(void *p, size_t n) { memset(p, 0, n); }
static uint64_t randomGet64(void) { return 1; }
static int test_socket(int domain, int type, int protocol)
{ (void)domain; (void)type; (void)protocol; return 42; }
static int test_close(int fd) { assert(fd == 42); return 0; }
static int test_bind(int fd, const struct sockaddr *address, socklen_t size)
{
    (void)fd;
    assert(size == sizeof(struct sockaddr_in));
    const struct sockaddr_in *local = (const struct sockaddr_in *)address;
    assert(local->sin_addr.s_addr == htonl(INADDR_ANY));
    assert(local->sin_port == htons(1900));
    return 0;
}
static int test_setsockopt(int fd, int level, int option, const void *value, socklen_t size)
{
    (void)fd;
    if (level != IPPROTO_IP)
        return 0;
    if (option == fail_option)
    {
        errno = ENODEV;
        return -1;
    }
    if (option == IP_ADD_MEMBERSHIP)
    {
        assert(size == sizeof(struct ip_mreq));
        const struct ip_mreq *membership = value;
        assert(membership->imr_multiaddr.s_addr == inet_addr("239.255.255.250"));
        joined_address = membership->imr_interface.s_addr;
    }
    if (option == IP_MULTICAST_IF)
    {
        assert(size == sizeof(struct in_addr));
        outbound_address = ((const struct in_addr *)value)->s_addr;
    }
    if (option == IP_MULTICAST_TTL)
        ttl = *(const unsigned char *)value;
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
#define bind test_bind
#define setsockopt test_setsockopt
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
    assert(nifm_queries == 2);
    g_ssdp.local_ip = address;
    assert(create_socket(&g_ssdp));
    assert(joined_address == nifm_address && outbound_address == nifm_address);
    assert(ttl == 4);
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
    g_ssdp.local_ip = NULL;
    ssdp_socket_close_fd(atomic_exchange(&g_ssdp.socket_fd, -1));
    free(base);

    /* Invalid NIFM addresses must not silently fall back to a VPN route. */
    const char *invalid[] = {"0.0.0.0", "127.0.0.1", "224.0.0.1", "255.255.255.255"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        nifm_address = inet_addr(invalid[i]);
        assert(!determine_local_ip(&address) && address == NULL);
        assert(dlna_build_url_base_alloc(49152) == NULL);
    }
    /* The physical NRO must also work on a LAN without an Internet route. */
    eden = g_ssdp.eden_compat = false;
    nifm_address = inet_addr("192.168.50.12");
    assert(determine_local_ip(&address));
    assert(strcmp(address, "192.168.50.12") == 0);
    base = dlna_build_url_base_alloc(49152);
    assert(base && strcmp(base, "http://192.168.50.12:49152/") == 0);
    g_ssdp.local_ip = address;
    assert(create_socket(&g_ssdp));
    assert(joined_address == nifm_address && outbound_address == nifm_address);
    ssdp_socket_close_fd(atomic_exchange(&g_ssdp.socket_fd, -1));
    const int failures[] = {IP_ADD_MEMBERSHIP, IP_MULTICAST_IF, IP_MULTICAST_TTL};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i)
    {
        fail_option = failures[i];
        assert(!create_socket(&g_ssdp));
        assert(atomic_load(&g_ssdp.socket_fd) == -1);
    }
    NetworkDiagnosticSnapshot snapshot;
    assert(network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_SSDP, &snapshot));
    assert(snapshot.open_sockets == 0 && snapshot.socket_close_underflows == 0);
    free(address);
    free(base);
    puts("Native discovery: LAN address, URLBase, multicast membership/output agree; failures close sockets");
}
