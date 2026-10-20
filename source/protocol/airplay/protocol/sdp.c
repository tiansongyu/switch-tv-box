#include "sdp.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool copy_value(char *output, size_t capacity, const char *value)
{
    size_t size = strlen(value);
    if (!size || size >= capacity || output[0])
        return false;
    memcpy(output, value, size + 1u);
    return true;
}

static bool number(const char **text, uint32_t *value)
{
    char *end;
    unsigned long parsed;
    while (**text == ' ' || **text == '\t')
        ++*text;
    if (!isdigit((unsigned char)**text))
        return false;
    errno = 0;
    parsed = strtoul(*text, &end, 10);
    if (errno || parsed > UINT32_MAX)
        return false;
    *value = (uint32_t)parsed;
    *text = end;
    return true;
}

static bool complete(const char *text)
{
    while (*text == ' ' || *text == '\t')
        ++text;
    return !*text;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

typedef struct
{
    const uint8_t *data;
    size_t bit_count;
    size_t position;
} AudioConfigBits;

static bool config_bits(AudioConfigBits *bits, unsigned count, uint32_t *value)
{
    if (bits->position > bits->bit_count || count > 24u ||
        count > bits->bit_count - bits->position)
        return false;
    *value = 0u;
    while (count--)
    {
        *value = (*value << 1u) |
            ((bits->data[bits->position / 8u] >>
              (7u - bits->position % 8u)) & 1u);
        bits->position++;
    }
    return true;
}

/* Check the negotiated fields before handing AudioSpecificConfig to FFmpeg.
 * ELD extension blocks are length-delimited and may vary between senders. */
static bool validate_aac_config(const AirPlayMirrorAudioFormat *format)
{
    static const uint32_t rates[] = {
        96000u, 88200u, 64000u, 48000u, 44100u, 32000u, 24000u,
        22050u, 16000u, 12000u, 11025u, 8000u, 7350u};
    AudioConfigBits bits = {
        format->codec_config, format->codec_config_size * 8u, 0u};
    uint32_t object, index, rate, channels, value, frame_short;
    if (!config_bits(&bits, 5u, &object))
        return false;
    if (object == 31u)
    {
        if (!config_bits(&bits, 6u, &value))
            return false;
        object = 32u + value;
    }
    if (!config_bits(&bits, 4u, &index))
        return false;
    /* The FFmpeg AAC decoder requires a standard frequency index. */
    if (index >= sizeof(rates) / sizeof(rates[0]))
        return false;
    rate = rates[index];
    if (!config_bits(&bits, 4u, &channels) ||
        rate != format->sample_rate || channels != format->channels ||
        object != (format->compression_type == AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD
                       ? 39u : 2u) ||
        !config_bits(&bits, 1u, &frame_short))
        return false;
    if (object == 2u)
    {
        if (format->samples_per_frame != (frame_short ? 960u : 1024u) ||
            !config_bits(&bits, 1u, &value) || value || /* Core coder dependency. */
            !config_bits(&bits, 1u, &value))
            return false;
        if (value && (!config_bits(&bits, 1u, &value) || value))
            return false; /* Unknown GASpecificConfig extension. */
    }
    else
    {
        if (format->samples_per_frame != (frame_short ? 480u : 512u) ||
            !config_bits(&bits, 3u, &value) || value || /* Resilience flags. */
            !config_bits(&bits, 1u, &value) || value)  /* Unsupported LD-SBR. */
            return false;
        for (;;)
        {
            uint32_t length;
            if (!config_bits(&bits, 4u, &value))
                return false;
            if (!value)
                break;
            if (!config_bits(&bits, 4u, &length))
                return false;
            if (length == 15u)
            {
                if (!config_bits(&bits, 8u, &value))
                    return false;
                length += value;
                if (length == 270u)
                {
                    if (!config_bits(&bits, 16u, &value))
                        return false;
                    length += value;
                }
            }
            if (length > (bits.bit_count - bits.position) / 8u)
                return false;
            bits.position += length * 8u;
        }
        if (!config_bits(&bits, 2u, &value) || value)
            return false;
    }
    /* Allow zero padding, but do not silently enable a different SBR/PS
     * profile through an unnegotiated sync extension. */
    while (bits.position < bits.bit_count)
        if (!config_bits(&bits, 1u, &value) || value)
            return false;
    return true;
}

static bool parse_aac(const char *codec, char *fmtp, AirPlayMirrorAudioFormat *format)
{
    const char *value = codec + 14u;
    uint32_t rate, channels = 2u, duration = 0u;
    uint8_t compression = AIRPLAY_MIRROR_AUDIO_CT_AAC_LC;
    char *config = NULL, *cursor = fmtp;
    bool have_mode = false, have_duration = false;

    if (!number(&value, &rate))
        return false;
    if (*value == '/')
    {
        ++value;
        if (!number(&value, &channels))
            return false;
    }
    if (!complete(value) || channels != 2u)
        return false;
    while (*cursor)
    {
        char *field = cursor, *end;
        cursor = strchr(cursor, ';');
        if (cursor)
            *cursor++ = '\0';
        else
            cursor = field + strlen(field);
        while (*field == ' ' || *field == '\t')
            ++field;
        end = field + strlen(field);
        while (end > field && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        if (strncasecmp(field, "mode=", 5u) == 0)
        {
            if (have_mode)
                return false;
            have_mode = true;
            if (strcasecmp(field + 5u, "AAC-eld") == 0)
                compression = AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD;
            else if (strcasecmp(field + 5u, "AAC-hbr") != 0)
                return false;
        }
        else if (strncasecmp(field, "constantDuration=", 17u) == 0)
        {
            value = field + 17u;
            if (have_duration || !number(&value, &duration) || !complete(value))
                return false;
            have_duration = true;
        }
        else if (strncasecmp(field, "config=", 7u) == 0)
        {
            if (config)
                return false;
            config = field + 7u;
        }
        else if (*field)
            return false;
    }
    if (duration && duration !=
        (compression == AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD ? 480u : 1024u))
        return false;
    if (!airplay_mirror_audio_format(compression, (uint16_t)duration, rate, format))
        return false;
    if (config)
    {
        size_t length = strlen(config);
        if (!length || length % 2u || length / 2u > sizeof(format->codec_config))
            return false;
        for (size_t index = 0u; index < length; index += 2u)
        {
            int high = hex_digit(config[index]), low = hex_digit(config[index + 1u]);
            if (high < 0 || low < 0)
                return false;
            format->codec_config[index / 2u] = (uint8_t)((high << 4) | low);
        }
        format->codec_config_size = length / 2u;
    }
    return validate_aac_config(format);
}

bool airplay_sdp_parse(const uint8_t *body, size_t size, AirPlaySdp *result)
{
    char line[1024], codec[96] = {0}, fmtp[256] = {0};
    size_t offset = 0u;
    bool audio = false, rtpmap = false, have_fmtp = false;
    uint32_t payload = 0u, rtp_payload = 0u, fmtp_payload = 0u;
    AirPlaySdp parsed = {0};

    if (!body || !size || size > 16384u || !result || memchr(body, 0, size))
        return false;
    while (offset < size)
    {
        size_t start = offset, length;
        const char *value;
        while (offset < size && body[offset] != '\n' && body[offset] != '\r')
            ++offset;
        length = offset - start;
        if (length >= sizeof(line))
            return false;
        memcpy(line, body + start, length);
        line[length] = '\0';
        while (offset < size && (body[offset] == '\n' || body[offset] == '\r'))
            ++offset;
        if (strncmp(line, "m=", 2u) == 0)
        {
            uint32_t port;
            if (audio || strncmp(line, "m=audio ", 8u) != 0)
                return false; /* Do not acknowledge an unsupported video SDP. */
            value = line + 8u;
            if (!number(&value, &port) || port > UINT16_MAX ||
                strncmp(value, " RTP/AVP ", 9u) != 0)
                return false;
            value += 9u;
            if (!number(&value, &payload) || payload < 96u ||
                payload > 127u || !complete(value))
                return false;
            audio = true;
        }
        else if (strncmp(line, "a=rtpmap:", 9u) == 0)
        {
            value = line + 9u;
            if (rtpmap || !number(&value, &rtp_payload) || *value != ' ')
                return false;
            while (*value == ' ')
                ++value;
            if (!copy_value(codec, sizeof(codec), value))
                return false;
            rtpmap = true;
        }
        else if (strncmp(line, "a=fmtp:", 7u) == 0)
        {
            value = line + 7u;
            if (have_fmtp || !number(&value, &fmtp_payload))
                return false;
            while (*value == ' ')
                ++value;
            if (strlen(value) >= sizeof(fmtp))
                return false;
            strcpy(fmtp, value);
            have_fmtp = true;
        }
        else if (strncmp(line, "a=rsaaeskey:", 12u) == 0)
        {
            if (!copy_value(parsed.rsa_key, sizeof(parsed.rsa_key), line + 12u))
                return false;
        }
        else if (strncmp(line, "a=fpaeskey:", 11u) == 0)
        {
            if (!copy_value(parsed.fairplay_key, sizeof(parsed.fairplay_key), line + 11u))
                return false;
        }
        else if (strncmp(line, "a=aesiv:", 8u) == 0)
        {
            if (!copy_value(parsed.iv, sizeof(parsed.iv), line + 8u))
                return false;
        }
    }
    if (!audio || !rtpmap || payload != rtp_payload ||
        (have_fmtp && payload != fmtp_payload) ||
        (parsed.rsa_key[0] && parsed.fairplay_key[0]) ||
        (!!parsed.iv[0] != !!(parsed.rsa_key[0] || parsed.fairplay_key[0])))
        return false;
    if (strcmp(codec, "AppleLossless") == 0)
    {
        uint32_t values[11];
        const char *value = fmtp;
        if (!have_fmtp)
            return false;
        for (size_t index = 0u; index < 11u; ++index)
            if (!number(&value, &values[index]))
                return false;
        if (!complete(value) ||
            !airplay_mirror_audio_format_alac(values, &parsed.format))
            return false;
    }
    else if (strncmp(codec, "L16/", 4u) == 0)
    {
        const char *value = codec + 4u;
        uint32_t rate, channels = 1u;
        if (!number(&value, &rate))
            return false;
        if (*value == '/')
        {
            ++value;
            if (!number(&value, &channels))
                return false;
        }
        if (!complete(value) || (rate != 44100u && rate != 48000u) ||
            channels < 1u || channels > 2u)
            return false;
        parsed.format.codec = AIRPLAY_MIRROR_AUDIO_CODEC_PCM_S16BE;
        parsed.format.compression_type = AIRPLAY_MIRROR_AUDIO_CT_PCM;
        parsed.format.sample_rate = rate;
        parsed.format.channels = (uint16_t)channels;
        parsed.format.bits_per_sample = 16u;
        parsed.format.samples_per_frame = 352u;
    }
    else if (strncasecmp(codec, "mpeg4-generic/", 14u) == 0)
    {
        if (!parse_aac(codec, fmtp, &parsed.format))
            return false;
    }
    else
        return false;
    parsed.format.rtp_payload_type = (uint8_t)payload;
    *result = parsed;
    return true;
}
