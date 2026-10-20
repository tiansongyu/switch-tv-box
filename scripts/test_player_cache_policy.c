#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "player/backend/libmpv_airplay.h"
#include "player/cache_policy.h"

static void test_live_hls_uses_player_defaults(void)
{
    PlayerCachePolicy policy =
        player_cache_policy_for_uri("https://example.invalid/live/index.m3u8");
    char options[256];

    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(policy.cache_enabled);
    assert(!policy.configure_cache);
    assert(!policy.disable_http_persistence);
    assert(policy.forward_mib == 0u);
    assert(policy.backward_mib == 0u);
    assert(policy.readahead_secs == 0u);
    assert(strcmp(player_cache_policy_name(policy.kind), "network-default") == 0);
    assert(player_cache_policy_format_options(&policy, false, options, sizeof(options)));
    assert(strcmp(options, "pause=no") == 0);
}

static void test_direct_mp4_uses_small_startup_cache(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(
        "https://example.invalid/video/clip.MP4?token=1");
    char options[256];

    assert(policy.kind == PLAYER_CACHE_POLICY_DIRECT_MP4);
    assert(policy.configure_cache);
    assert(policy.cache_enabled);
    assert(policy.forward_mib == 8u);
    assert(policy.backward_mib == 2u);
    assert(policy.readahead_secs == 2u);
    assert(player_cache_policy_format_options(&policy, false, options,
                                              sizeof(options)));
    assert(strcmp(options,
                  "pause=no,cache=yes,cache-pause-initial=no,"
                  "demuxer-readahead-secs=2,demuxer-max-bytes=8MiB,"
                  "demuxer-max-back-bytes=2MiB") == 0);
}

static void test_airplay_remote_hls_policy(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(
        "http://127.0.0.1:7000/airplay-hls/session/master.m3u8");
    char options[256];

    assert(policy.kind == PLAYER_CACHE_POLICY_AIRPLAY_REMOTE_HLS);
    assert(policy.configure_cache);
    assert(policy.cache_enabled);
    assert(policy.disable_http_persistence);
    assert(strcmp(player_cache_policy_name(policy.kind),
                  "airplay-reverse-hls") == 0);
    assert(player_cache_policy_format_options(&policy, true, options,
                                              sizeof(options)));
    assert(strcmp(options,
                  "pause=yes,cache=yes,cache-pause-initial=no,"
                  "demuxer-readahead-secs=20,demuxer-max-bytes=20MiB,"
                  "demuxer-max-back-bytes=10MiB,"
                  "demuxer-lavf-o=http_persistent=no") == 0);

    policy = player_cache_policy_for_uri(
        "https://example.invalid/airplay-hls/session/master.m3u8");
    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(!policy.configure_cache);
    assert(!policy.disable_http_persistence);
    assert(player_cache_policy_format_options(&policy, false, options,
                                              sizeof(options)));
    assert(strstr(options, "demuxer-lavf-format") == NULL);
    policy = player_cache_policy_for_uri(
        "http://127.0.0.1:7000/channels/live.m3u8");
    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(!policy.configure_cache);
    assert(!policy.disable_http_persistence);
    assert(player_cache_policy_format_options(&policy, false, options,
                                              sizeof(options)));
    assert(strstr(options, "demuxer-lavf-format") == NULL);
}

static void test_mirror_policy(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(PLAYER_LIBMPV_AIRPLAY_URI);
    char options[64];

    assert(policy.kind == PLAYER_CACHE_POLICY_AIRPLAY_MIRROR);
    assert(policy.configure_cache);
    assert(!policy.cache_enabled);
    assert(policy.forward_mib == 0u);
    assert(policy.backward_mib == 0u);
    assert(policy.readahead_secs == 0u);
    assert(strcmp(player_cache_policy_name(policy.kind),
                  "airplay-mirror-low-latency") == 0);
    assert(player_cache_policy_format_options(&policy, true, options, sizeof(options)));
    assert(strcmp(options, "pause=yes,cache=no") == 0);
}

static void test_unknown_uri_and_invalid_output(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(NULL);
    char small[8];

    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(!player_cache_policy_format_options(&policy, false, small, sizeof(small)));
    assert(small[sizeof(small) - 1] == '\0');
    assert(!player_cache_policy_format_options(NULL, false, small, sizeof(small)));
    assert(!player_cache_policy_format_options(&policy, false, NULL, 0));
}

static void test_iptv_timeout_is_per_media_owner(void)
{
    const char *uri = "https://example.invalid/live/index.m3u8";
    IptvPlaybackSettings settings = {IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_BALANCED};
    PlayerCachePolicy policy =
        player_cache_policy_for_media(uri, PLAYER_MEDIA_OWNER_IPTV, settings, false);
    char options[512];

    assert(player_cache_policy_format_options(&policy, false, options,
                                              sizeof(options)));
    assert(strstr(options, "network-timeout=45"));
    assert(strstr(options, "cache-pause-initial=yes"));
    assert(strstr(options, "cache-pause=yes,cache-pause-wait=30,cache-secs=120"));
    assert(strstr(options, "hls-bitrate=max"));
    /* Reusing the exact URL through another receiver must not inherit the
     * IPTV limit. Both source handoff and file-option reset matter here. */
    const PlayerMediaOwner other_owners[] = {
        PLAYER_MEDIA_OWNER_NONE, PLAYER_MEDIA_OWNER_DLNA,
        PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO, PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR
    };
    for (size_t index = 0; index < sizeof(other_owners) / sizeof(other_owners[0]);
         ++index)
    {
        policy = player_cache_policy_for_media(uri, other_owners[index], settings, false);
        assert(player_cache_policy_format_options(&policy, false, options,
                                                  sizeof(options)));
        assert(strcmp(options, "pause=no") == 0);
    }
    policy = player_cache_policy_for_media(
        "https://example.invalid/clip.mp4", PLAYER_MEDIA_OWNER_IPTV, settings, false);
    assert(player_cache_policy_format_options(&policy, true, options,
                                              sizeof(options)));
    assert(strstr(options, "demuxer-readahead-secs=120"));
    assert(strstr(options, ",network-timeout=45"));
    /* A buffer that fits the original options but not the appended timeout
     * must fail instead of silently losing the limit. */
    char short_options[160];
    policy = player_cache_policy_for_media(uri, PLAYER_MEDIA_OWNER_IPTV, settings, false);
    assert(!player_cache_policy_format_options(&policy, false, short_options,
                                               sizeof(short_options)));
    assert(short_options[sizeof(short_options) - 1u] == '\0');
}

static void test_iptv_quality_and_buffer_choices(void)
{
    const unsigned int seconds[] = {6u, 30u, 60u};
    const unsigned int prefetch[] = {30u, 120u, 180u};
    const unsigned int memory[] = {16u, 48u, 96u};
    const unsigned int back_memory[] = {2u, 4u, 4u};
    const unsigned int network_timeout[] = {20u, 45u, 60u};
    const char *rates[] = {"max", "1500000", "min"};
    for (int quality = 0; quality < 3; ++quality)
    {
        for (int buffer = 0; buffer < 3; ++buffer)
        {
            IptvPlaybackSettings settings = {
                (IptvQuality)quality, (IptvBufferMode)buffer
            };
            PlayerCachePolicy policy = player_cache_policy_for_media(
                "http://example.invalid/master.m3u8", PLAYER_MEDIA_OWNER_IPTV,
                settings, false);
            char options[512];
            assert(policy.kind == PLAYER_CACHE_POLICY_IPTV);
            assert(policy.pause_wait_secs == seconds[buffer]);
            assert(policy.readahead_secs >= policy.pause_wait_secs);
            assert(policy.cache_secs >= policy.pause_wait_secs);
            assert(policy.readahead_secs == prefetch[buffer]);
            assert(policy.cache_secs == prefetch[buffer]);
            assert(policy.forward_mib == memory[buffer]);
            assert(policy.backward_mib == back_memory[buffer]);
            assert(policy.backward_mib <= policy.forward_mib);
            assert(policy.forward_mib + policy.backward_mib <= 100u);
            assert(policy.network_timeout_secs == network_timeout[buffer]);
            assert(strcmp(policy.hls_bitrate, rates[quality]) == 0);
            assert(player_cache_policy_format_options(
                &policy, false, options, sizeof(options)));
            assert(strstr(options, "cache-pause-initial=yes"));
            assert(strstr(options, "cache-pause=yes"));
            assert(!strstr(options, "demuxer-lavf-format"));
            assert(!strstr(options, "vf=")); /* Quality is variant selection. */
            assert(!strstr(options, "scale="));

            /* The next receiver keeps its existing URI-specific policy. */
            PlayerCachePolicy dlna = player_cache_policy_for_media(
                "http://example.invalid/clip.mp4", PLAYER_MEDIA_OWNER_DLNA,
                settings, false);
            assert(dlna.kind == PLAYER_CACHE_POLICY_DIRECT_MP4);
            assert(!dlna.pause_initial && !dlna.hls_bitrate);
            assert(dlna.network_timeout_secs == 0u);
            assert(dlna.readahead_secs == 2u);
            PlayerCachePolicy mirror = player_cache_policy_for_media(
                PLAYER_LIBMPV_AIRPLAY_URI, PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR,
                settings, false);
            assert(!mirror.cache_enabled && !mirror.pause_initial &&
                   !mirror.hls_bitrate);
        }
    }
    PlayerCachePolicy invalid = player_cache_policy_for_media(
        "http://example.invalid/master.m3u8", PLAYER_MEDIA_OWNER_IPTV,
        (IptvPlaybackSettings){(IptvQuality)99, (IptvBufferMode)-1}, false);
    assert(invalid.pause_wait_secs == 60u);
    assert(invalid.forward_mib == 96u && invalid.backward_mib == 4u);
    assert(invalid.network_timeout_secs == 60u);
    assert(strcmp(invalid.hls_bitrate, "max") == 0);
}

static void test_direct_http_strict_public_ipv4(void)
{
    const char *allowed[] = {
        "http://1.1.1.1/", "http://8.8.8.8:80/live/index.m3u8",
        "http://223.255.255.255:65535/master.m3u8?token=a%2Fb+1",
        "http://100.63.255.255/", "http://100.128.0.1/",
        "http://172.15.255.255/", "http://172.32.0.1/",
        "http://192.0.3.1/", "http://198.17.255.255/", "http://198.20.0.1/",
    };
    const char *rejected[] = {
        NULL, "", "https://1.1.1.1/a", "HTTP://1.1.1.1/a",
        "http://example.com/a", "http://localhost/a", "http://[::1]/a",
        "http://user:pass@1.1.1.1/a", "http://1.1.1.1@8.8.8.8/a",
        "http://1.1.1.1.example.com/a", "http://1.1.1.1%2f.example.com/a",
        "http://01.1.1.1/a", "http://0x1.1.1.1/a",
        "http://16843009/a", "http://1.1.1/a", "http://1.1.1.1.2/a",
        "http://256.1.1.1/a", "http://1.1.1.1:/a",
        "http://1.1.1.1:0/a", "http://1.1.1.1:65536/a",
        "http://1.1.1.1:123456/a", "http://1.1.1.1:+80/a",
        "http://1.1.1.1\\@127.0.0.1/a", "http://1.1.1.1/a#fragment",
        "http://1.1.1.1/a\r\nHost: localhost", "http://1.1.1.1/a b",
        "http://1.1.1.1", "http://1.1.1.1?x=1",
        "http://0.1.2.3/a", "http://10.1.2.3/a",
        "http://100.64.0.1/a", "http://100.127.255.254/a",
        "http://127.0.0.1:18952/a", "http://169.254.169.254/a",
        "http://172.16.0.1/a", "http://172.31.255.254/a",
        "http://192.168.0.1/a", "http://192.0.0.9/a", "http://192.0.2.1/a",
        "http://192.88.99.1/a", "http://198.18.0.1/a",
        "http://198.19.255.254/a", "http://198.51.100.1/a",
        "http://203.0.113.1/a", "http://224.0.0.1/a",
        "http://239.255.255.250/a", "http://240.1.2.3/a",
        "http://255.255.255.255/a",
    };
    for (size_t index = 0; index < sizeof(allowed) / sizeof(allowed[0]); ++index)
        assert(player_iptv_direct_http_uri_allowed(allowed[index]));
    for (size_t index = 0; index < sizeof(rejected) / sizeof(rejected[0]); ++index)
        assert(!player_iptv_direct_http_uri_allowed(rejected[index]));
}

static void test_direct_http_per_file_isolation_and_capacity(void)
{
    const char *uri = "http://1.1.1.1:8080/master.m3u8?token=abc";
    IptvPlaybackSettings settings = {IPTV_QUALITY_BALANCED, IPTV_BUFFER_STABLE};
    char options[512], exact[512];
    PlayerCachePolicy policy =
        player_cache_policy_for_media(uri, PLAYER_MEDIA_OWNER_IPTV, settings, true);
    assert(player_cache_policy_format_options(&policy, true, options, sizeof(options)));
    assert(strstr(options, ",http-proxy=http://127.0.0.1:18957"));
    assert(strstr(options, "network-timeout=60"));
    assert(strstr(options, "demuxer-max-bytes=96MiB"));
    size_t length = strlen(options);
    assert(length + 1u < sizeof(options));
    assert(!player_cache_policy_format_options(&policy, true, exact, length));
    assert(player_cache_policy_format_options(&policy, true, exact, length + 1u));
    assert(strcmp(exact, options) == 0);

    const PlayerMediaOwner owners[] = {
        PLAYER_MEDIA_OWNER_NONE, PLAYER_MEDIA_OWNER_DLNA,
        PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO, PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR
    };
    for (size_t index = 0; index < sizeof(owners) / sizeof(owners[0]); ++index)
    {
        policy = player_cache_policy_for_media(uri, owners[index], settings, true);
        assert(player_cache_policy_format_options(&policy, false, options, sizeof(options)));
        assert(!strstr(options, "http-proxy="));
    }
    policy = player_cache_policy_for_media(uri, PLAYER_MEDIA_OWNER_IPTV, settings, false);
    assert(player_cache_policy_format_options(&policy, false, options, sizeof(options)));
    assert(!strstr(options, "http-proxy="));
    const char *other_uris[] = {
        "http://127.0.0.1:18952/live.m3u8",
        "http://10.30.0.44/live.m3u8", "https://1.1.1.1/live.m3u8",
        "http://example.com/live.m3u8",
    };
    for (size_t index = 0; index < sizeof(other_uris) / sizeof(other_uris[0]); ++index)
    {
        policy = player_cache_policy_for_media(
            other_uris[index], PLAYER_MEDIA_OWNER_IPTV, settings, true);
        assert(player_cache_policy_format_options(&policy, false, options, sizeof(options)));
        assert(!strstr(options, "http-proxy="));
    }
}

int main(void)
{
    test_live_hls_uses_player_defaults();
    test_direct_mp4_uses_small_startup_cache();
    test_airplay_remote_hls_policy();
    test_mirror_policy();
    test_unknown_uri_and_invalid_output();
    test_iptv_timeout_is_per_media_owner();
    test_iptv_quality_and_buffer_choices();
    test_direct_http_strict_public_ipv4();
    test_direct_http_per_file_isolation_and_capacity();
    puts("player cache policy tests passed");
    return 0;
}
