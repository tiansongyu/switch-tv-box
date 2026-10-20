#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol/airplay/media/remote_video.h"

static const uint8_t png[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x04,0x00,0x00,0x00,0xb5,0x1c,0x0c,0x02,
    0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x64,0xf8,0x0f,0x00,0x01,0x05,
    0x01,0x01,0x27,0x18,0xe3,0x66,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};
/* Marker sniffing is only transport validation; libmpv performs image decoding. */
static const uint8_t jpeg_markers[] = {0xff,0xd8,0xff,0xe0,0xff,0xd9};

typedef struct
{
    AirPlayRemoteVideo *remote;
    unsigned claims, releases, loads, plays, stops;
    unsigned effective_stops, effective_releases;
    uint32_t owner_generation, first_generation;
    bool fail_load, cancel_claim, pressure_claim, replace_claim, replace_stop;
    uint16_t port;
    char url[AIRPLAY_REMOTE_VIDEO_URL_MAX + 1u];
} Recorder;

static int request(Recorder *r, uint64_t session, const char *method, const char *uri,
                   const char *key, const char *action, const void *body, size_t size);

static bool claim(uint64_t session, uint32_t generation, void *data)
{
    Recorder *r = data;
    assert(generation);
    r->claims++;
    r->owner_generation = generation;
    if (!r->first_generation)
        r->first_generation = generation;
    if (r->cancel_claim)
        assert(request(r, session, "POST", "/stop", NULL, NULL, NULL, 0) == 200);
    if (r->pressure_claim)
        for (unsigned i = 0; i < 8; ++i)
        {
            char key[32];
            snprintf(key, sizeof(key), "pressure-%u", i);
            assert(request(r, session, "PUT", "/photo", key, "cacheOnly", png, sizeof(png)) == 200);
        }
    if (r->replace_claim)
    {
        r->replace_claim = false;
        assert(request(r, session, "PUT", "/photo", "replacement", NULL,
                       png, sizeof(png)) == 200);
    }
    return true;
}
static void release(uint64_t session, uint32_t generation, void *data)
{
    Recorder *r = data;
    (void)session;
    assert(generation);
    r->releases++;
    if (r->owner_generation == generation)
    {
        r->owner_generation = 0u;
        r->effective_releases++;
    }
}
static bool load(const char *url, const char *metadata, void *data)
{
    Recorder *r = data;
    assert(metadata && strcmp(metadata, "AirPlay Photo") == 0);
    snprintf(r->url, sizeof(r->url), "%s", url);
    r->loads++;
    return !r->fail_load;
}
static bool play(void *data) { ((Recorder *)data)->plays++; return true; }
static bool pause_player(void *data) { (void)data; return true; }
static bool stop(void *data) { ((Recorder *)data)->stops++; return true; }
static bool stop_owner(uint64_t session, uint32_t generation, void *data)
{
    Recorder *r = data;
    r->stops++;
    if (r->replace_stop)
    {
        r->replace_stop = false;
        assert(request(r, session, "PUT", "/photo", "replacement", NULL,
                       png, sizeof(png)) == 200);
    }
    if (generation && r->owner_generation == generation)
        r->effective_stops++;
    return true;
}
static bool seek(int ms, void *data) { (void)ms; (void)data; return true; }
static bool snapshot(AirPlayRemoteVideoSnapshot *out, void *data)
{
    (void)data;
    memset(out, 0, sizeof(*out));
    out->has_media = true;
    out->state = AIRPLAY_REMOTE_VIDEO_PLAYING;
    return true;
}
static uint16_t port(void *data) { return ((Recorder *)data)->port; }
static void create(Recorder *r)
{
    memset(r, 0, sizeof(*r));
    r->port = 7000;
    AirPlayRemoteVideoOps ops = {
        .claim_owner = claim, .release_owner = release, .load = load, .play = play,
        .stop_owner = stop_owner,
        .pause = pause_player, .stop = stop, .seek_ms = seek, .snapshot = snapshot,
        .control_port = port, .user_data = r
    };
    assert(airplay_remote_video_create(&ops, &r->remote));
}
static void header(AirPlayRtspRequest *req, const char *name, const char *value)
{
    if (!value) return;
    snprintf(req->headers[req->header_count].name, sizeof(req->headers[0].name), "%s", name);
    snprintf(req->headers[req->header_count++].value, sizeof(req->headers[0].value), "%s", value);
}
static int request(Recorder *r, uint64_t session, const char *method, const char *uri,
                   const char *key, const char *action, const void *body, size_t size)
{
    AirPlayRtspRequest *req = calloc(1, sizeof(*req));
    AirPlayRtspResponse *res = calloc(1, sizeof(*res));
    bool handled = false;
    assert(req && res);
    snprintf(req->method, sizeof(req->method), "%s", method);
    snprintf(req->uri, sizeof(req->uri), "%s", uri);
    header(req, "X-Apple-AssetKey", key);
    header(req, "X-Apple-AssetAction", action);
    req->body = (uint8_t *)body;
    req->body_length = size;
    assert(airplay_rtsp_response_init(res, "HTTP/1.1", 200));
    assert(airplay_remote_video_route(r->remote, session, req, res, &handled));
    assert(handled);
    int status = res->status_code;
    airplay_rtsp_response_clear(res);
    free(res);
    free(req);
    return status;
}
static int read_image(Recorder *r, const char *url, const void *expected, size_t size)
{
    AirPlayRtspRequest *req = calloc(1, sizeof(*req));
    AirPlayRtspResponse *res = calloc(1, sizeof(*res));
    bool handled = false;
    assert(req && res);
    const char *path = strstr(url, AIRPLAY_REMOTE_PHOTO_LOCAL_PREFIX);
    assert(path && airplay_remote_video_is_local_uri(path));
    snprintf(req->method, sizeof(req->method), "GET");
    snprintf(req->uri, sizeof(req->uri), "%s", path);
    assert(airplay_rtsp_response_init(res, "HTTP/1.1", 200));
    assert(airplay_remote_video_route_local(r->remote, req, res, &handled));
    assert(handled && res->close_connection);
    int status = res->status_code;
    if (expected)
    {
        assert(status == 200);
        assert(res->body_length == size && memcmp(res->body, expected, size) == 0);
        bool image_type = false;
        for (size_t i = 0; i < res->header_count; ++i)
            if (!strcmp(res->headers[i].name, "Content-Type"))
                image_type = !strcmp(res->headers[i].value, "image/png") ||
                             !strcmp(res->headers[i].value, "image/jpeg");
        assert(image_type);
    }
    airplay_rtsp_response_clear(res);
    free(res);
    free(req);
    return status;
}

static void test_cache_and_ownership(void)
{
    Recorder r;
    create(&r);
    assert(request(&r, 10, "PUT", "/photo", "first", NULL, png, sizeof(png)) == 200);
    assert(r.claims == 1 && r.loads == 1 && r.plays == 1);
    assert(read_image(&r, r.url, png, sizeof(png)) == 200);
    char first_url[256];
    snprintf(first_url, sizeof(first_url), "%.*s", (int)sizeof(first_url)-1, r.url);
    assert(request(&r, 10, "PUT", "/photo", "second", "cacheOnly", png, sizeof(png)) == 200);
    assert(r.claims == 1 && r.loads == 1);
    assert(request(&r, 11, "PUT", "/photo", "second", "displayCached", NULL, 0) == 412);
    assert(request(&r, 10, "PUT", "/photo", "second", "displayCached", NULL, 0) == 200);
    assert(r.claims == 1 && r.loads == 2);
    char second_url[256];
    snprintf(second_url, sizeof(second_url), "%.*s", (int)sizeof(second_url)-1, r.url);
    for (int i = 0; i < 12; ++i)
    {
        char key[16];
        snprintf(key, sizeof(key), "new-%d", i);
        assert(request(&r, 11, "PUT", "/photo", key, "cacheOnly", jpeg_markers, sizeof(jpeg_markers)) == 200);
    }
    assert(read_image(&r, second_url, png, sizeof(png)) == 200); /* Displayed image is pinned. */
    assert(read_image(&r, first_url, NULL, 0) == 404);
    assert(request(&r, 10, "PUT", "/photo", "first", "displayCached", NULL, 0) == 412);
    assert(request(&r, 10, "PUT", "/photo", "second", "cacheOnly", jpeg_markers, sizeof(jpeg_markers)) == 200);
    assert(read_image(&r, second_url, png, sizeof(png)) == 200); /* Replacement cannot mutate an in-flight URL. */
    assert(request(&r, 10, "PUT", "/photo", "second", "displayCached", NULL, 0) == 200);
    assert(strcmp(second_url, r.url));
    assert(read_image(&r, r.url, jpeg_markers, sizeof(jpeg_markers)) == 200);
    assert(request(&r, 99, "POST", "/stop", NULL, NULL, NULL, 0) == 409);
    assert(request(&r, 11, "POST", "/stop", NULL, NULL, NULL, 0) == 200); /* Cache-only session. */
    assert(r.stops == 0 && r.releases == 0);
    assert(request(&r, 10, "POST", "/stop", NULL, NULL, NULL, 0) == 200);
    assert(r.stops == 1 && r.releases == 1);
    assert(read_image(&r, r.url, NULL, 0) == 404);
    airplay_remote_video_destroy(r.remote);
}

static void test_limits_and_cleanup(void)
{
    Recorder r;
    create(&r);
    assert(request(&r, 10, "GET", "/photo", NULL, NULL, png, sizeof(png)) == 405);
    assert(request(&r, 10, "PUT", "/photo", NULL, "cacheOnly", png, sizeof(png)) == 400);
    assert(request(&r, 10, "PUT", "/photo", "key", "invalid", png, sizeof(png)) == 400);
    assert(request(&r, 10, "PUT", "/photo", "key", "displayCached", png, sizeof(png)) == 400);
    assert(request(&r, 10, "PUT", "/photo", "key", NULL, "not an image", 12) == 415);
    assert(request(&r, 10, "PUT", "/photo", "key", NULL, png, AIRPLAY_REMOTE_PHOTO_MAX_BYTES + 1u) == 413);
    assert(r.claims == 0 && r.loads == 0);
    uint8_t *large = malloc(AIRPLAY_REMOTE_PHOTO_MAX_BYTES);
    assert(large);
    memset(large, 0, AIRPLAY_REMOTE_PHOTO_MAX_BYTES);
    memcpy(large, png, sizeof(png));
    for (unsigned i = 0; i < AIRPLAY_REMOTE_PHOTO_CACHE_SLOTS + 3u; ++i)
    {
        char key[16];
        snprintf(key, sizeof(key), "large-%u", i);
        assert(request(&r, 10, "PUT", "/photo", key, "cacheOnly", large, AIRPLAY_REMOTE_PHOTO_MAX_BYTES) == 200);
    }
    assert(request(&r, 10, "PUT", "/photo", "large-0", "displayCached", NULL, 0) == 412);
    assert(request(&r, 10, "PUT", "/photo", "large-6", "displayCached", NULL, 0) == 200);
    assert(read_image(&r, r.url, large, AIRPLAY_REMOTE_PHOTO_MAX_BYTES) == 200);
    airplay_remote_video_session_closed(r.remote, 10);
    assert(r.stops == 0 && r.releases == 0);
    assert(read_image(&r, r.url, large, AIRPLAY_REMOTE_PHOTO_MAX_BYTES) == 200);
    assert(request(&r, 10, "PUT", "/photo", "large-6", "displayCached", NULL, 0) == 200);
    free(large);
    assert(request(&r, 10, "POST", "/stop", NULL, NULL, NULL, 0) == 200);
    assert(r.stops == 1 && r.releases == 1);
    assert(request(&r, 10, "PUT", "/photo", "large-6", "displayCached", NULL, 0) == 412);
    assert(read_image(&r, r.url, NULL, 0) == 404);
    assert(request(&r, 11, "PUT", "/photo", "cached", "cacheOnly", png, sizeof(png)) == 200);
    airplay_remote_video_session_closed(r.remote, 11);
    assert(request(&r, 11, "PUT", "/photo", "cached", "displayCached", NULL, 0) == 200);
    r.port = 0;
    assert(request(&r, 11, "PUT", "/photo", "cached", NULL, png, sizeof(png)) == 503);
    assert(request(&r, 11, "PUT", "/photo", "cached", "cacheOnly", png, sizeof(png)) == 200);
    airplay_remote_video_destroy(r.remote);
}

static void test_async_claim_lifetime(void)
{
    Recorder r;
    create(&r);
    r.pressure_claim = true;
    assert(request(&r, 10, "PUT", "/photo", "pending", NULL, png, sizeof(png)) == 200);
    assert(read_image(&r, r.url, png, sizeof(png)) == 200);
    assert(airplay_remote_video_relinquish_session(r.remote, 10, 0u));
    assert(read_image(&r, r.url, NULL, 0) == 404);
    r.pressure_claim = false;
    r.cancel_claim = true;
    unsigned loads = r.loads;
    assert(request(&r, 10, "PUT", "/photo", "cancel", NULL, png, sizeof(png)) == 409);
    assert(r.loads == loads);
    assert(request(&r, 10, "PUT", "/photo", "cancel", "displayCached", NULL, 0) == 412);
    r.cancel_claim = false;
    r.fail_load = true;
    assert(request(&r, 10, "PUT", "/photo", "failed", NULL, png, sizeof(png)) == 503);
    assert(r.stops == 2 && r.releases == 2);
    airplay_remote_video_destroy(r.remote);
}

static void test_reentrant_same_session_claim(void)
{
    Recorder r;
    create(&r);
    r.replace_claim = true;
    assert(request(&r, 10, "PUT", "/photo", "old", NULL, png, sizeof(png)) == 409);
    assert(r.claims == 2 && r.loads == 1 && r.plays == 1);
    assert(r.releases == 1 && r.effective_releases == 0);
    assert(r.owner_generation && r.owner_generation != r.first_generation);
    assert(!airplay_remote_video_relinquish_session(r.remote, 10,
                                                     r.first_generation));
    assert(read_image(&r, r.url, png, sizeof(png)) == 200);
    uint32_t surviving_claim = r.owner_generation;
    assert(request(&r, 10, "PUT", "/photo", "next", NULL, png, sizeof(png)) == 200);
    assert(r.claims == 2 && r.owner_generation == surviving_claim);
    assert(request(&r, 10, "POST", "/stop", NULL, NULL, NULL, 0) == 200);
    assert(r.owner_generation == 0 && r.effective_releases == 1);
    airplay_remote_video_destroy(r.remote);
}

static void test_reentrant_same_session_stop(void)
{
    Recorder r;
    create(&r);
    assert(request(&r, 10, "PUT", "/photo", "old", NULL, png, sizeof(png)) == 200);
    r.replace_stop = true;
    assert(request(&r, 10, "POST", "/stop", NULL, NULL, NULL, 0) == 200);
    assert(r.claims == 2 && r.loads == 2 && r.plays == 2);
    assert(r.effective_stops == 0 && r.effective_releases == 0);
    assert(r.owner_generation && r.owner_generation != r.first_generation);
    assert(read_image(&r, r.url, png, sizeof(png)) == 200);
    assert(request(&r, 10, "POST", "/stop", NULL, NULL, NULL, 0) == 200);
    assert(r.effective_stops == 1 && r.effective_releases == 1);
    airplay_remote_video_destroy(r.remote);
}

static void test_cache_session_snapshots(void)
{
    Recorder r;
    uint64_t ids[AIRPLAY_REMOTE_PHOTO_CACHE_SLOTS] = {0};
    uint64_t first_version = 0u, version = 0u;
    create(&r);
    assert(airplay_remote_video_cached_sessions(r.remote, ids, &first_version) == 0u);
    assert(first_version != 0u);
    assert(request(&r, 10, "PUT", "/photo", "one", "cacheOnly", png, sizeof(png)) == 200);
    assert(request(&r, 10, "PUT", "/photo", "two", "cacheOnly", png, sizeof(png)) == 200);
    airplay_remote_video_session_closed(r.remote, 10);
    assert(airplay_remote_video_cached_sessions(r.remote, ids, &version) == 1u);
    assert(ids[0] == 10u && version > first_version);
    assert(request(&r, 11, "PUT", "/photo", "other", "cacheOnly", png, sizeof(png)) == 200);
    assert(airplay_remote_video_cached_sessions(r.remote, ids, &version) == 2u);
    assert(request(&r, 10, "POST", "/stop", NULL, NULL, NULL, 0) == 200);
    assert(airplay_remote_video_cached_sessions(r.remote, ids, &version) == 1u);
    assert(ids[0] == 11u);
    assert(r.claims == 0 && r.releases == 0 && r.stops == 0);
    airplay_remote_video_destroy(r.remote);
}

int main(void)
{
    test_cache_and_ownership();
    test_limits_and_cleanup();
    test_async_claim_lifetime();
    test_reentrant_same_session_claim();
    test_reentrant_same_session_stop();
    test_cache_session_snapshots();
    puts("AirPlay photo delivery, session-isolated bounded cache, pinning and cleanup passed");
    return 0;
}
