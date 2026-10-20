#include "platform_compat.h"

#include <unistd.h>

bool tvbox_eden_compat_enabled(void)
{
    return access("sdmc:/switch/switch-tv-box/eden-compat.flag", F_OK) == 0;
}
