#ifndef SWITCH_TV_BOX_PLATFORM_COMPAT_H
#define SWITCH_TV_BOX_PLATFORM_COMPAT_H

#include <stdbool.h>
#include <sys/select.h>

/* Explicit opt-in on the emulator SD; never infer an emulator from firmware
 * version or the absence of Atmosphere on a physical console. */
bool tvbox_eden_compat_enabled(void);

/* Eden has a small shared BSD worker pool. Poll readiness without leaving a
 * worker blocked; wait in the calling application thread between polls.
 * Other platforms retain the native select implementation. */
int tvbox_socket_select(int nfds, fd_set *readfds, fd_set *writefds,
                        fd_set *exceptfds, struct timeval *timeout);

#endif
