#ifndef SWITCH_TV_BOX_EDEN_SOCKET_COMPAT_H
#define SWITCH_TV_BOX_EDEN_SOCKET_COMPAT_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Linker wrappers. Physical consoles always receive the original arguments. */
int __wrap_socket(int domain, int type, int protocol);
int __wrap_setsockopt(int socket_fd, int level, int option_name,
                      const void *value, socklen_t value_size);

#ifdef __cplusplus
}
#endif

#endif
