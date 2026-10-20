#include "player/cache_policy.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "player/backend/libmpv_airplay.h"

#define AIRPLAY_REMOTE_HLS_LOCAL_PREFIX "/airplay-hls/"
#define IPTV_DIRECT_HTTP_PROXY "http://127.0.0.1:18957"

bool player_iptv_direct_http_uri_allowed(const char *uri)
{
    if (!uri || strncmp(uri, "http://", 7u) != 0)
        return false;
    for (const unsigned char *byte = (const unsigned char *)uri; *byte; ++byte)
        if (*byte <= 0x20u || *byte == 0x7fu || *byte == '#')
            return false;
    const char *cursor = uri + 7u;
    unsigned int octets[4];
    for (size_t index = 0; index < 4u; ++index)
    {
        const char *start = cursor;
        unsigned int value = 0u;
        while (*cursor >= '0' && *cursor <= '9')
        {
            if (cursor - start >= 3)
                return false;
            value = value * 10u + (unsigned int)(*cursor++ - '0');
        }
        if (cursor == start || value > 255u ||
            (cursor - start > 1 && *start == '0'))
            return false;
        octets[index] = value;
        if (index != 3u && *cursor++ != '.')
            return false;
    }
    if (*cursor == ':')
    {
        const char *start = ++cursor;
        unsigned int port = 0u;
        while (*cursor >= '0' && *cursor <= '9')
        {
            if (cursor - start >= 5)
                return false;
            port = port * 10u + (unsigned int)(*cursor++ - '0');
        }
        if (cursor == start || port == 0u || port > 65535u)
            return false;
    }
    if (*cursor != '/')
        return false;

    /* Match the helper's public unicast policy. Do not send local fixtures,
     * receiver callbacks, special-use or ambiguous addresses to this proxy. */
    unsigned int a = octets[0], b = octets[1], c = octets[2];
    return !(a == 0u || a == 10u || a == 127u || a >= 224u ||
             (a == 100u && b >= 64u && b <= 127u) ||
             (a == 169u && b == 254u) ||
             (a == 172u && b >= 16u && b <= 31u) ||
             (a == 192u && b == 168u) ||
             (a == 192u && b == 0u && (c == 0u || c == 2u)) ||
             (a == 192u && b == 88u && c == 99u) ||
             (a == 198u && (b == 18u || b == 19u)) ||
             (a == 198u && b == 51u && c == 100u) ||
             (a == 203u && b == 0u && c == 113u));
}

static bool is_local_airplay_hls_uri(const char *uri)
{
    const char *path;

    if (!uri || strncmp(uri, "http://127.0.0.1:", 17u) != 0)
        return false;
    path = strchr(uri + 17u, '/');
    return path && strncmp(path, AIRPLAY_REMOTE_HLS_LOCAL_PREFIX,
                           sizeof(AIRPLAY_REMOTE_HLS_LOCAL_PREFIX) - 1u) == 0;
}

static bool is_direct_mp4_uri(const char *uri)
{
    const char *end;

    if (!uri || uri[0] == '\0')
        return false;
    end = strpbrk(uri, "?#");
    if (!end)
        end = uri + strlen(uri);
    return end - uri >= 4 && strncasecmp(end - 4, ".mp4", 4u) == 0;
}

static bool is_http_hls_uri(const char *uri)
{
    const char *authority;
    const char *end;
    const char *path;

    if (!uri)
        return false;
    if (strncasecmp(uri, "http://", 7u) == 0)
        authority = uri + 7u;
    else if (strncasecmp(uri, "https://", 8u) == 0)
        authority = uri + 8u;
    else
        return false;
    end = strpbrk(authority, "?#");
    if (!end)
        end = authority + strlen(authority);
    path = memchr(authority, '/', (size_t)(end - authority));
    return path && end - path >= 6 &&
           strncasecmp(end - 5, ".m3u8", 5u) == 0;
}

PlayerCachePolicy player_cache_policy_for_uri(const char *uri)
{
    PlayerCachePolicy policy = {
        .kind = PLAYER_CACHE_POLICY_NETWORK,
        .cache_enabled = true,
    };

    if (uri && strcmp(uri, PLAYER_LIBMPV_AIRPLAY_URI) == 0)
    {
        policy.kind = PLAYER_CACHE_POLICY_AIRPLAY_MIRROR;
        policy.configure_cache = true;
        policy.cache_enabled = false;
        policy.forward_mib = 0;
        policy.backward_mib = 0;
        policy.readahead_secs = 0;
    }
    else if (is_local_airplay_hls_uri(uri))
    {
        policy.kind = PLAYER_CACHE_POLICY_AIRPLAY_REMOTE_HLS;
        policy.configure_cache = true;
        policy.forward_mib = NXCAST_MEDIA_CACHE_FORWARD_MIB;
        policy.backward_mib = NXCAST_MEDIA_CACHE_BACKWARD_MIB;
        policy.readahead_secs = NXCAST_MEDIA_CACHE_READAHEAD_SECS;
        policy.disable_http_persistence = true;
    }
    else if (is_direct_mp4_uri(uri))
    {
        policy.kind = PLAYER_CACHE_POLICY_DIRECT_MP4;
        policy.configure_cache = true;
        policy.forward_mib = 8u;
        policy.backward_mib = 2u;
        policy.readahead_secs = 2u;
    }
    return policy;
}

PlayerCachePolicy player_cache_policy_for_media(const char *uri,
                                                PlayerMediaOwner owner,
                                                IptvPlaybackSettings settings,
                                                bool iptv_direct_http_enabled)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(uri);

    /* Keep this a per-file option: a later AirPlay or DLNA request, even for
     * the same URL, must retain its own network wait behavior. */
    if (owner == PLAYER_MEDIA_OWNER_IPTV)
    {
        static const unsigned int buffer_secs[] = {6u, 30u, 60u};
        static const unsigned int readahead_secs[] = {30u, 120u, 180u};
        static const unsigned int forward_mib[] = {16u, 48u, 96u};
        static const unsigned int backward_mib[] = {2u, 4u, 4u};
        static const unsigned int network_timeout_secs[] = {20u, 45u, 60u};
        static const char *const bitrate[] = {"max", "1500000", "min"};

        if (settings.quality < IPTV_QUALITY_ORIGINAL ||
            settings.quality > IPTV_QUALITY_SMOOTH)
            settings.quality = IPTV_QUALITY_ORIGINAL;
        if (settings.buffer < IPTV_BUFFER_FAST ||
            settings.buffer > IPTV_BUFFER_STABLE)
            settings.buffer = IPTV_BUFFER_STABLE;
        policy.kind = PLAYER_CACHE_POLICY_IPTV;
        policy.configure_cache = true;
        policy.cache_enabled = true;
        policy.forward_mib = forward_mib[settings.buffer];
        policy.backward_mib = backward_mib[settings.buffer];
        policy.readahead_secs = readahead_secs[settings.buffer];
        policy.pause_initial = true;
        policy.pause_wait_secs = buffer_secs[settings.buffer];
        policy.cache_secs = readahead_secs[settings.buffer];
        policy.hls_bitrate = bitrate[settings.quality];
        /* Begin at the newest complete segment before filling the startup
         * cache. FFmpeg's -3 default can start at the edge of a short live
         * window; a slow first download then expires the following segments.
         * FFmpeg ignores this live-only option for finished/VOD playlists. */
        policy.latest_hls_segment = is_http_hls_uri(uri);
        policy.network_timeout_secs = network_timeout_secs[settings.buffer];
        if (iptv_direct_http_enabled && player_iptv_direct_http_uri_allowed(uri))
            policy.http_proxy = IPTV_DIRECT_HTTP_PROXY;
    }
    return policy;
}

const char *player_cache_policy_name(PlayerCachePolicyKind kind)
{
    switch (kind)
    {
    case PLAYER_CACHE_POLICY_NETWORK:
        return "network-default";
    case PLAYER_CACHE_POLICY_DIRECT_MP4:
        return "direct-mp4-fast";
    case PLAYER_CACHE_POLICY_AIRPLAY_REMOTE_HLS:
        return "airplay-reverse-hls";
    case PLAYER_CACHE_POLICY_AIRPLAY_MIRROR:
        return "airplay-mirror-low-latency";
    case PLAYER_CACHE_POLICY_IPTV:
        return "iptv-buffered";
    default:
        return "unknown";
    }
}

bool player_cache_policy_format_options(const PlayerCachePolicy *policy,
                                        bool paused,
                                        char *output,
                                        size_t output_size)
{
    int written;

    if (!policy || !output || output_size == 0)
        return false;
    if (!policy->configure_cache)
    {
        written = snprintf(output, output_size, "pause=%s",
                           paused ? "yes" : "no");
    }
    else if (!policy->cache_enabled)
    {
        written = snprintf(output, output_size, "pause=%s,cache=no",
                           paused ? "yes" : "no");
    }
    else
    {
        written = snprintf(
            output, output_size,
            "pause=%s,cache=yes,cache-pause-initial=%s,"
            "demuxer-readahead-secs=%u,demuxer-max-bytes=%uMiB,"
            "demuxer-max-back-bytes=%uMiB%s",
            paused ? "yes" : "no", policy->pause_initial ? "yes" : "no",
            policy->readahead_secs,
            policy->forward_mib, policy->backward_mib,
            policy->disable_http_persistence
                ? ",demuxer-lavf-o=http_persistent=no"
                : "");
    }
    if (written < 0 || (size_t)written >= output_size)
        return false;
    size_t used = (size_t)written;
    if (policy->pause_wait_secs > 0u)
    {
        written = snprintf(output + used, output_size - used,
                           ",cache-pause=yes,cache-pause-wait=%u,cache-secs=%u",
                           policy->pause_wait_secs, policy->cache_secs);
        if (written < 0 || (size_t)written >= output_size - used)
            return false;
        used += (size_t)written;
    }
    if (policy->hls_bitrate)
    {
        /* Select an existing HLS variant; this never transcodes a single-rate
         * stream or promises a particular output resolution. */
        written = snprintf(output + used, output_size - used,
                           ",hls-bitrate=%s", policy->hls_bitrate);
        if (written < 0 || (size_t)written >= output_size - used)
            return false;
        used += (size_t)written;
    }
    if (policy->network_timeout_secs > 0u)
    {
        written = snprintf(output + used, output_size - used,
                           ",network-timeout=%u",
                           policy->network_timeout_secs);
        if (written < 0 || (size_t)written >= output_size - used)
            return false;
        used += (size_t)written;
    }
    if (policy->latest_hls_segment)
    {
        /* The append action keeps the existing AirPlay relay option if an
         * IPTV entry happens to reference that local HLS endpoint. mpv 0.36
         * supports append on key/value list options without nested commas. */
        written = snprintf(
            output + used, output_size - used,
            policy->disable_http_persistence
                ? ",demuxer-lavf-o-append=live_start_index=-1"
                : ",demuxer-lavf-o=live_start_index=-1");
        if (written < 0 || (size_t)written >= output_size - used)
            return false;
        used += (size_t)written;
    }
    if (policy->http_proxy)
    {
        written = snprintf(output + used, output_size - used,
                           ",http-proxy=%s", policy->http_proxy);
        return written >= 0 && (size_t)written < output_size - used;
    }
    return true;
}
