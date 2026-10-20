#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include "player/render/internal.h"
#include "player/types.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "view regression: %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)
static uint64_t now_ms = 1000;
static bool allow_frontend = true;
uint64_t armGetSystemTick(void) { return now_ms * UINT64_C(1000000); }
uint64_t armTicksToNs(uint64_t ticks) { return ticks; }
void consoleInit(void *console) { (void)console; }
void consoleClear(void) {}
bool tvbox_eden_compat_enabled(void) { return true; }
void log_info(const char *format, ...) { (void)format; }
void log_warn(const char *format, ...) { (void)format; }
bool frontend_connect(ViewContext *context) { context->status.render_api_connected = true; return true; }
bool frontend_open_home(ViewContext *context) { context->status.foreground_video_active = true; return true; }
bool frontend_open(ViewContext *context) { context->status.foreground_video_active = allow_frontend; return allow_frontend; }
void frontend_close(ViewContext *context, bool restore) { (void)restore; context->status.foreground_video_active = false; }
void frontend_shutdown(ViewContext *context) { (void)context; }
bool frontend_render(ViewContext *context) { (void)context; return true; }

static void sync_media(const char *uri, const char *metadata, PlayerState state)
{
    PlayerSnapshot snapshot = {.has_media = uri != NULL, .media = {.uri = (char *)uri, .metadata = (char *)metadata}, .state = state};
    player_view_sync(&snapshot);
    player_view_begin_frame();
}

static void expect_mode(PlayerViewMode expected)
{
    PlayerViewStatus status = {0};
    CHECK(player_view_get_status(&status));
    CHECK(status.active_view == expected);
    player_view_status_clear(&status);
}

static void test_same_uri_restart(void)
{
    const char *uri = "http://192.0.2.1/video.mp4";
    CHECK(player_view_init());
    sync_media(uri, NULL, PLAYER_STATE_PLAYING);
    expect_mode(PLAYER_VIEW_VIDEO);
    sync_media(uri, NULL, PLAYER_STATE_STOPPED);
    CHECK(player_view_show_home()); /* DLNA explicit Stop completion in main. */
    player_view_begin_frame();
    expect_mode(PLAYER_VIEW_HOME);
    sync_media(uri, NULL, PLAYER_STATE_LOADING); /* Same URL, new Play. */
    expect_mode(PLAYER_VIEW_VIDEO);
    sync_media(uri, NULL, PLAYER_STATE_PLAYING);
    CHECK(player_view_show_home()); /* Browsing home during the same session. */
    sync_media(uri, NULL, PLAYER_STATE_PAUSED);
    expect_mode(PLAYER_VIEW_HOME);
    sync_media(uri, NULL, PLAYER_STATE_PLAYING);
    expect_mode(PLAYER_VIEW_HOME);
    sync_media("http://192.0.2.1/next.mp4", NULL, PLAYER_STATE_LOADING);
    expect_mode(PLAYER_VIEW_VIDEO);
    player_view_deinit();
}

static void test_audio_video_transitions(void)
{
    CHECK(player_view_init());
    sync_media(PLAYER_SCREEN_MIRROR_URI, "AirPlay Audio", PLAYER_STATE_PLAYING);
    expect_mode(PLAYER_VIEW_HOME);
    CHECK(player_view_show_home());
    sync_media("http://192.0.2.1/video.mp4", NULL, PLAYER_STATE_LOADING);
    expect_mode(PLAYER_VIEW_VIDEO);
    sync_media(PLAYER_SCREEN_MIRROR_URI, "AirPlay Audio", PLAYER_STATE_PLAYING);
    expect_mode(PLAYER_VIEW_HOME);
    CHECK(player_view_show_home());
    sync_media(PLAYER_SCREEN_MIRROR_URI, "AirPlay Mirror", PLAYER_STATE_PLAYING);
    expect_mode(PLAYER_VIEW_VIDEO); /* Audio and mirroring share the URI. */
    sync_media(PLAYER_SCREEN_MIRROR_URI, "AirPlay Audio", PLAYER_STATE_PLAYING);
    expect_mode(PLAYER_VIEW_HOME);
    player_view_deinit();
}

static void test_stop_hold_and_retry(void)
{
    const char *uri = "http://192.0.2.1/video.mp4";
    CHECK(player_view_init());
    now_ms = 1000;
    sync_media(uri, NULL, PLAYER_STATE_PLAYING);
    now_ms = 1100;
    sync_media(uri, NULL, PLAYER_STATE_STOPPED);
    expect_mode(PLAYER_VIEW_VIDEO);
    now_ms = 2600;
    sync_media(uri, NULL, PLAYER_STATE_STOPPED);
    expect_mode(PLAYER_VIEW_HOME);
    sync_media(uri, NULL, PLAYER_STATE_PLAYING);
    expect_mode(PLAYER_VIEW_VIDEO);
    sync_media(uri, NULL, PLAYER_STATE_ERROR);
    expect_mode(PLAYER_VIEW_HOME);
    CHECK(player_view_show_home());
    allow_frontend = false;
    sync_media(uri, NULL, PLAYER_STATE_LOADING);
    expect_mode(PLAYER_VIEW_HOME);
    allow_frontend = true;
    sync_media(uri, NULL, PLAYER_STATE_PLAYING);
    expect_mode(PLAYER_VIEW_VIDEO);
    player_view_deinit();
}

int main(void)
{
    test_same_uri_restart();
    test_audio_video_transitions();
    test_stop_hold_and_retry();
    puts("Player view tests passed: same-URI replay, manual home, audio/video transitions, stop hold and renderer retry.");
    return 0;
}
