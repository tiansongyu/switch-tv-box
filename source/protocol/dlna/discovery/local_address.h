#ifndef SWITCH_TV_BOX_DLNA_LOCAL_ADDRESS_H
#define SWITCH_TV_BOX_DLNA_LOCAL_ADDRESS_H

#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

/* Eden's NIFM address follows its selected network_interface. BSD routing may
 * instead follow a host VPN, so route probing can advertise an unreachable
 * proxy address. Use the same NIFM address as the application status display.
 * Call only for the explicitly enabled Eden compatibility mode. */
static inline char *dlna_eden_local_address_alloc(void)
{
    struct in_addr address = {.s_addr = (uint32_t)gethostid()};
    uint32_t host_address = ntohl(address.s_addr);
    unsigned first_octet = host_address >> 24;
    if (first_octet == 0 || first_octet == 127 || first_octet >= 224)
        return NULL;
    char *text = malloc(INET_ADDRSTRLEN);
    if (text && !inet_ntop(AF_INET, &address, text, INET_ADDRSTRLEN))
    {
        free(text);
        text = NULL;
    }
    return text;
}

#endif
