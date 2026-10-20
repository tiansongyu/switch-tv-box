#pragma once

#include <stdbool.h>

typedef enum {
    HOME_FOCUS_TV = 0,
    HOME_FOCUS_AIRPLAY,
    HOME_FOCUS_DLNA,
    HOME_FOCUS_LANGUAGE,
    HOME_FOCUS_PLAYER,
    HOME_FOCUS_BACK
} HomeFocus;

typedef enum {
    HOME_PAGE_MAIN = 0,
    HOME_PAGE_AIRPLAY,
    HOME_PAGE_DLNA
} HomePage;

/* Logical 1280x720 coordinates shared by rendering and touch input. */
enum {
    HOME_TV_LEFT = 48, HOME_TV_TOP = 218,
    HOME_TV_RIGHT = 424, HOME_TV_BOTTOM = 558,
    HOME_AIRPLAY_LEFT = 452, HOME_AIRPLAY_TOP = 218,
    HOME_AIRPLAY_RIGHT = 828, HOME_AIRPLAY_BOTTOM = 558,
    HOME_DLNA_LEFT = 856, HOME_DLNA_TOP = 218,
    HOME_DLNA_RIGHT = 1232, HOME_DLNA_BOTTOM = 558,
    HOME_PLAYER_LEFT = 88, HOME_PLAYER_TOP = 522,
    HOME_PLAYER_RIGHT = 440, HOME_PLAYER_BOTTOM = 580,
    HOME_LANGUAGE_LEFT = 1104, HOME_LANGUAGE_TOP = 36,
    HOME_LANGUAGE_RIGHT = 1232, HOME_LANGUAGE_BOTTOM = 84,
    HOME_BACK_LEFT = 48, HOME_BACK_TOP = 122,
    HOME_BACK_RIGHT = 196, HOME_BACK_BOTTOM = 172
};

void home_ui_init(const char *preference_path, bool system_chinese);
bool home_ui_is_chinese(void);
bool home_ui_toggle_language(void);
const char *home_ui_text(const char *english, const char *chinese);
/* Exact built-in UI labels only, never metadata/source/channel names.
 * Unknown strings and NULL pass through unchanged; no allocation is performed.
 * Translate at display time, after any English state/prefix comparisons. */
const char *home_ui_translate(const char *english);
bool home_ui_hit(HomeFocus target, int x, int y);
/* Main-page navigation stays on the three cards; up reaches language. */
HomeFocus home_ui_navigate(HomeFocus focus, int horizontal, int vertical);
