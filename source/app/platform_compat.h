#ifndef SWITCH_TV_BOX_PLATFORM_COMPAT_H
#define SWITCH_TV_BOX_PLATFORM_COMPAT_H

#include <stdbool.h>

/* Explicit opt-in on the emulator SD; never infer an emulator from firmware
 * version or the absence of Atmosphere on a physical console. */
bool tvbox_eden_compat_enabled(void);

#endif
