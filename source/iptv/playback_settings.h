#pragma once

#include <stdbool.h>

typedef enum
{
    IPTV_QUALITY_ORIGINAL = 0,
    IPTV_QUALITY_BALANCED = 1,
    IPTV_QUALITY_SMOOTH = 2
} IptvQuality;

typedef enum
{
    IPTV_BUFFER_FAST = 0,
    IPTV_BUFFER_BALANCED = 1,
    IPTV_BUFFER_STABLE = 2
} IptvBufferMode;

typedef struct
{
    IptvQuality quality;
    IptvBufferMode buffer;
} IptvPlaybackSettings;

/* Missing or invalid files use ORIGINAL + STABLE. Loading is explicit;
 * get() is a coherent, nonblocking snapshot and never performs file I/O. */
void iptv_playback_settings_init(void);
IptvPlaybackSettings iptv_playback_settings_get(void);
/* Persist first, then publish. A failure preserves the previous settings. */
bool iptv_playback_settings_set(IptvPlaybackSettings settings);
