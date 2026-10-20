#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/airplay/mirror/audio.h"

typedef struct
{
    AirPlayMirrorAudioFormat format;
    char rsa_key[345];
    char fairplay_key[129];
    char iv[25];
} AirPlaySdp;

/* Bounded, single-audio-stream AirPlay 1 SDP. Unknown attributes are ignored. */
bool airplay_sdp_parse(const uint8_t *body, size_t size, AirPlaySdp *result);
