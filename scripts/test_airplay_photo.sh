#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/switch-tv-box-photo.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
cd "$ROOT"
"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Isource -pthread \
    source/protocol/airplay/protocol/plist.c source/protocol/airplay/protocol/rtsp.c \
    source/protocol/airplay/media/remote_hls.c source/protocol/airplay/media/remote_video.c \
    scripts/test_airplay_photo.c -lm -o "$TMP/photo"
"$TMP/photo"
