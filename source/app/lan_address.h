#ifndef SWITCH_TV_BOX_LAN_ADDRESS_H
#define SWITCH_TV_BOX_LAN_ADDRESS_H

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

/* libnx gethostid() reads NIFM's current interface address. Discovery must
 * advertise that address, not the route to an arbitrary Internet destination.
 * This also works on a LAN without an Internet/default route. */
static inline bool tvbox_lan_address(uint32_t *out)
{
    uint32_t address;
#ifdef __SWITCH__
    /* Unlike gethostid's fallback value, a failed NIFM query must never be
     * published as a usable receiver address. */
    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc))
        return false;
    rc = nifmGetCurrentIpAddress(&address);
    nifmExit();
    if (R_FAILED(rc))
        return false;
#else
    address = (uint32_t)gethostid();
#endif
    unsigned first_octet = ntohl(address) >> 24;
    if (!out || first_octet == 0 || first_octet == 127 || first_octet >= 224)
        return false;
    *out = address;
    return true;
}

static inline char *tvbox_lan_address_alloc(void)
{
    struct in_addr address;
    if (!tvbox_lan_address(&address.s_addr))
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
