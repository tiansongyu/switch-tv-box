#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/version.h>

#include "protocol/airplay/protocol/handlers.h"
#include "protocol/airplay/protocol/sdp.h"
#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/security/legacy_rsa.h"
#include "protocol/airplay/security/legacy_rsa_key.h"

static unsigned failures;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; \
} } while (0)

static const char alac_sdp[] =
    "v=0\r\no=iTunes 1 0 IN IP4 192.168.1.2\r\ns=AirTunes\r\n"
    "c=IN IP4 192.168.1.3\r\nt=0 0\r\nm=audio 0 RTP/AVP 96\r\n"
    "a=rtpmap:96 AppleLossless\r\na=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n";

typedef struct
{
    unsigned prepare_count, open_count, record_count, flush_count, stop_count;
    unsigned volume_count, metadata_count;
    float volume;
    bool encrypted;
    bool fail_open;
    uint8_t key[16];
} Recorder;

static bool prepare(const AirPlayTransportSetup *setup, uint16_t *port, void *data)
{
    Recorder *recorder = data;
    CHECK(setup->session_id == 42u && setup->uses_ntp_timing);
    CHECK(setup->peer_timing_port == 6002u);
    CHECK(setup->peer_ipv4_address == inet_addr("192.168.1.2"));
    recorder->prepare_count++;
    *port = 7002u;
    return true;
}

static bool audio_open(uint64_t session_id, const uint8_t key[16],
                       const uint8_t iv[16], const AirPlayMirrorAudioFormat *format,
                       uint32_t peer, uint16_t peer_control, uint16_t *data_port,
                       uint16_t *control_port, void *data)
{
    Recorder *recorder = data;
    CHECK(session_id == 42u && peer == inet_addr("192.168.1.2"));
    CHECK(peer_control == 6001u);
    CHECK(format->codec == AIRPLAY_MIRROR_AUDIO_CODEC_ALAC);
    CHECK(format->sample_rate == 44100u && format->channels == 2u);
    CHECK(format->samples_per_frame == 352u && format->codec_config_size == 36u);
    CHECK(format->rtp_payload_type == 96u);
    recorder->encrypted = key != NULL && iv != NULL;
    if (key)
        memcpy(recorder->key, key, sizeof(recorder->key));
    recorder->open_count++;
    *data_port = 7000u;
    *control_port = 7001u;
    return !recorder->fail_open;
}

static bool record(uint64_t session_id, void *data)
{
    Recorder *recorder = data;
    CHECK(session_id == 42u);
    recorder->record_count++;
    return true;
}

static void stop(uint64_t session_id, void *data)
{
    Recorder *recorder = data;
    CHECK(session_id == 42u);
    recorder->stop_count++;
}

static bool flush(uint64_t session_id, bool has_sequence, uint16_t sequence,
                   bool has_timestamp, uint32_t timestamp, void *data)
{
    Recorder *recorder = data;
    CHECK(session_id == 42u && has_sequence && sequence == 65535u);
    CHECK(has_timestamp && timestamp == 4294967295u);
    recorder->flush_count++;
    return true;
}

static bool volume(uint64_t session_id, float db, void *data)
{
    Recorder *recorder = data;
    CHECK(session_id == 42u);
    recorder->volume = db;
    recorder->volume_count++;
    return true;
}

static void metadata(uint64_t session_id, const char *type, const uint8_t *body,
                      size_t size, void *data)
{
    Recorder *recorder = data;
    CHECK(session_id == 42u && type && body && size);
    recorder->metadata_count++;
}

static const char *header(const AirPlayRtspResponse *response, const char *name)
{
    for (size_t index = 0u; index < response->header_count; ++index)
        if (strcmp(response->headers[index].name, name) == 0)
            return response->headers[index].value;
    return NULL;
}

static void dispatch(AirPlayHandlers *handlers, AirPlayRtspSession *session,
                      const char *method, const char *type, const void *body,
                      size_t body_size, const char *extra_name, const char *extra,
                      AirPlayRtspResponse *response, int expected)
{
    AirPlayRtspRequest *request = calloc(1, sizeof(*request));
    CHECK(request != NULL);
    if (!request)
        return;
    snprintf(request->method, sizeof(request->method), "%s", method);
    strcpy(request->protocol, "RTSP/1.0");
    strcpy(request->uri, "rtsp://192.168.1.3/42");
    request->has_cseq = true;
    request->cseq = session->request_count + 1u;
    request->body = (uint8_t *)body;
    request->body_length = body_size;
    if (type)
    {
        strcpy(request->headers[0].name, "Content-Type");
        snprintf(request->headers[0].value, sizeof(request->headers[0].value), "%s", type);
        request->header_count++;
    }
    if (extra_name)
    {
        size_t index = request->header_count++;
        snprintf(request->headers[index].name, sizeof(request->headers[index].name), "%s", extra_name);
        snprintf(request->headers[index].value, sizeof(request->headers[index].value), "%s", extra);
    }
    CHECK(airplay_rtsp_dispatch(session, request, airplay_handlers_route, handlers, response));
    CHECK(response->status_code == expected);
    free(request);
}

static int random_bytes(void *context, unsigned char *output, size_t size)
{
    return airplay_crypto_random(context, output, size) ? 0 : -1;
}

static void rsa_tests(char wrapped_text[345])
{
    mbedtls_pk_context pk;
    AirPlayCryptoRng rng = {0};
    uint8_t wrapped[256], decrypted[16], key[16], signature[256], signed_block[256];
    size_t size;
    char response[345];
    uint32_t local_ip = inet_addr("192.168.1.3");

    for (size_t index = 0u; index < sizeof(key); ++index)
        key[index] = (uint8_t)index;
    mbedtls_pk_init(&pk);
    CHECK(airplay_crypto_rng_init(&rng, "legacy test"));
#if MBEDTLS_VERSION_MAJOR >= 3
    CHECK(mbedtls_pk_parse_key(&pk, (const unsigned char *)legacy_private_key,
                               sizeof(legacy_private_key), NULL, 0u,
                               random_bytes, &rng) == 0);
#else
    CHECK(mbedtls_pk_parse_key(&pk, (const unsigned char *)legacy_private_key,
                               sizeof(legacy_private_key), NULL, 0u) == 0);
#endif
    mbedtls_rsa_set_padding(mbedtls_pk_rsa(pk), MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
#if MBEDTLS_VERSION_MAJOR >= 3
    CHECK(mbedtls_rsa_rsaes_oaep_encrypt(mbedtls_pk_rsa(pk), random_bytes, &rng,
                                        NULL, 0u, sizeof(key), key, wrapped) == 0);
#else
    CHECK(mbedtls_rsa_rsaes_oaep_encrypt(mbedtls_pk_rsa(pk), random_bytes, &rng,
                                        MBEDTLS_RSA_PUBLIC, NULL, 0u,
                                        sizeof(key), key, wrapped) == 0);
#endif
    CHECK(mbedtls_base64_encode((unsigned char *)wrapped_text, 345u, &size,
                                wrapped, sizeof(wrapped)) == 0);
    CHECK(airplay_legacy_rsa_unwrap(wrapped_text, decrypted));
    CHECK(memcmp(key, decrypted, sizeof(key)) == 0);
    CHECK(!airplay_legacy_rsa_unwrap("aW52YWxpZA==", decrypted));
    CHECK(!airplay_legacy_base64_decode("AA=A", wrapped, sizeof(wrapped), &size));
    CHECK(!airplay_legacy_base64_decode("A", wrapped, sizeof(wrapped), &size));
    CHECK(airplay_legacy_rsa_challenge("AAECAwQFBgcICQoLDA0ODw",
                                      local_ip, "02:11:22:33:44:55", response));
    CHECK(strlen(response) == 342u);
    CHECK(airplay_legacy_base64_decode(response, signature, sizeof(signature), &size));
    CHECK(size == sizeof(signature));
    CHECK(mbedtls_rsa_public(mbedtls_pk_rsa(pk), signature, signed_block) == 0);
    CHECK(signed_block[0] == 0 && signed_block[1] == 1 && signed_block[223] == 0);
    CHECK(memcmp(signed_block + 224u, key, 16u) == 0);
    CHECK(memcmp(signed_block + 240u, &local_ip, 4u) == 0);
    CHECK(memcmp(signed_block + 244u, "\x02\x11\x22\x33\x44\x55", 6u) == 0);
    CHECK(!airplay_legacy_rsa_challenge("AAECAwQFBgcICQoLDA0ODw",
                                       0u, "02:11:22:33:44:55", response));
    airplay_crypto_rng_deinit(&rng);
    mbedtls_pk_free(&pk);
}

static void sdp_tests(void)
{
    AirPlaySdp sdp;
    const char pcm[] = "m=audio 0 RTP/AVP 96\n"
                       "a=rtpmap:96 L16/48000/2\n";
    const char invalid[] = "m=audio 0 RTP/AVP 96\n"
                           "a=rtpmap:97 AppleLossless\n"
                           "a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\n";
    const char mismatched_key[] = "m=audio 0 RTP/AVP 96\n"
                                  "a=rtpmap:96 L16/44100/2\n"
                                  "a=aesiv:AAECAwQFBgcICQoLDA0ODw\n";
    const char aac[] = "m=audio 0 RTP/AVP 96\n"
                       "a=rtpmap:96 mpeg4-generic/44100/2\n"
                       "a=fmtp:96\n";
    const char eld[] = "m=audio 0 RTP/AVP 96\n"
                       "a=rtpmap:96 mpeg4-generic/44100/2\n"
                       "a=fmtp:96 mode=AAC-eld; constantDuration=480\n";
    CHECK(airplay_sdp_parse((const uint8_t *)alac_sdp, strlen(alac_sdp), &sdp));
    CHECK(sdp.format.codec_config_size == 36u && sdp.format.samples_per_frame == 352u);
    CHECK(airplay_sdp_parse((const uint8_t *)pcm, strlen(pcm), &sdp));
    CHECK(sdp.format.codec == AIRPLAY_MIRROR_AUDIO_CODEC_PCM_S16BE &&
          sdp.format.bits_per_sample == 16u && sdp.format.sample_rate == 48000u);
    CHECK(!airplay_sdp_parse((const uint8_t *)invalid, strlen(invalid), &sdp));
    CHECK(!airplay_sdp_parse((const uint8_t *)mismatched_key, strlen(mismatched_key), &sdp));
    CHECK(!airplay_sdp_parse((const uint8_t *)alac_sdp, sizeof(alac_sdp), &sdp));
    CHECK(airplay_sdp_parse((const uint8_t *)aac, strlen(aac), &sdp));
    CHECK(sdp.format.compression_type == AIRPLAY_MIRROR_AUDIO_CT_AAC_LC &&
          sdp.format.samples_per_frame == 1024u && sdp.format.codec_config_size == 2u);
    CHECK(airplay_sdp_parse((const uint8_t *)eld, strlen(eld), &sdp));
    CHECK(sdp.format.compression_type == AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD &&
          sdp.format.samples_per_frame == 480u && sdp.format.codec_config_size == 4u);
    for (unsigned payload = 95u; payload <= 128u; ++payload)
    {
        char body[128];
        snprintf(body, sizeof(body), "m=audio 0 RTP/AVP %u\n"
                 "a=rtpmap:%u L16/44100/2\n", payload, payload);
        bool accepted = airplay_sdp_parse((const uint8_t *)body, strlen(body), &sdp);
        CHECK(accepted == (payload >= 96u && payload <= 127u));
        if (accepted)
            CHECK(sdp.format.rtp_payload_type == payload);
    }
    const struct { const char *mode, *config; bool valid; } configs[] = {
        {"AAC-hbr", "1210", true},
        {"AAC-hbr", "12100000", true},
        {"AAC-hbr", "1190", false}, /* 48 kHz conflicts with rtpmap. */
        {"AAC-hbr", "1208", false}, /* Mono conflicts with stereo rtpmap. */
        {"AAC-hbr", "0a10", false}, /* AAC Main conflicts with AAC-LC. */
        {"AAC-hbr", "1214", false}, /* 960 samples conflicts with duration. */
        {"AAC-hbr", "12", false},   /* Truncated config. */
        {"AAC-hbr", "f8e85000", false},
        {"AAC-eld", "f8e85000", true},
        {"AAC-eld", "f8e8500000", true},
        {"AAC-eld", "f8e85011ab00", true}, /* Bounded ELD extension. */
        {"AAC-eld", "f8e8501f", false},    /* Truncated ELD extension. */
        {"AAC-eld", "f8e85100", false},    /* LD-SBR unsupported by decoder. */
        {"AAC-eld", "f8e84000", false},    /* 512 samples conflicts with duration. */
        {"AAC-eld", "1210", false}
    };
    for (size_t i = 0; i < sizeof(configs) / sizeof(configs[0]); ++i)
    {
        char body[256];
        snprintf(body, sizeof(body), "m=audio 0 RTP/AVP 97\n"
                 "a=rtpmap:97 mpeg4-generic/44100/2\n"
                 "a=fmtp:97 mode=%s;config=%s\n", configs[i].mode, configs[i].config);
        CHECK(airplay_sdp_parse((const uint8_t *)body, strlen(body), &sdp) ==
              configs[i].valid);
    }
}

static void transcript(const char *wrapped)
{
    Recorder recorder = {0};
    uint8_t public_key[32] = {0};
    AirPlayHandlers *handlers = NULL;
    AirPlayRtspSession session;
    AirPlayRtspResponse response = {0};
    AirPlayHandlersConfig config = {
        .friendly_name = "Switch TV Box", .device_id = "02:11:22:33:44:55",
        .pairing_id = "00000000-0000-0000-0000-000000000042",
        .public_key = public_key, .features = 512u,
        .transport_prepare_callback = prepare,
        .legacy_audio_open_callback = audio_open,
        .audio_record_callback = record, .mirror_stop_callback = stop,
        .audio_flush_callback = flush, .audio_volume_callback = volume,
        .audio_metadata_callback = metadata, .callback_user_data = &recorder
    };
    const char *transport = "RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;"
                            "control_port=6001;timing_port=6002";
    char encrypted_sdp[1024];
    CHECK(airplay_handlers_create(&config, &handlers));
    airplay_rtsp_session_init(&session, 42u);
    session.peer_ipv4_address = inet_addr("192.168.1.2");
    session.local_ipv4_address = inet_addr("192.168.1.3");
    dispatch(handlers, &session, "OPTIONS", NULL, NULL, 0u,
             "Apple-Challenge", "AAECAwQFBgcICQoLDA0ODw", &response, 200);
    CHECK(header(&response, "Apple-Response") != NULL);
    CHECK(strstr(header(&response, "Public"), "ANNOUNCE") != NULL);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "ANNOUNCE", "application/sdp",
             alac_sdp, strlen(alac_sdp), NULL, NULL, &response, 200);
    CHECK(airplay_handlers_session_is_legacy(&session));
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "SETUP", NULL, NULL, 0u, "Transport",
             "RTP/AVP/TCP;unicast;interleaved=0-1", &response, 461);
    CHECK(recorder.open_count == 0u);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "SETUP", NULL, NULL, 0u, "Transport",
             transport, &response, 200);
    CHECK(!recorder.encrypted && recorder.open_count == 1u && recorder.prepare_count == 1u);
    CHECK(strstr(header(&response, "Transport"), "server_port=7000") != NULL);
    CHECK(strcmp(header(&response, "Session"), "42") == 0);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "RECORD", NULL, NULL, 0u, NULL, NULL, &response, 200);
    CHECK(recorder.record_count == 1u && session.state == AIRPLAY_RTSP_SESSION_RECORDING);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "SET_PARAMETER", "text/parameters",
             "volume: -12.5\r\n", 15u, NULL, NULL, &response, 200);
    CHECK(recorder.volume_count == 1u && recorder.volume == -12.5f);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "SET_PARAMETER", "text/parameters",
             "volume: nan\r\n", 13u, NULL, NULL, &response, 400);
    CHECK(recorder.volume_count == 1u);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "GET_PARAMETER", "text/parameters",
             "volume\r\n", 8u, NULL, NULL, &response, 200);
    CHECK(response.body_length == strlen("volume: -12.500000\r\n"));
    CHECK(memcmp(response.body, "volume: -12.500000\r\n", response.body_length) == 0);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "GET_PARAMETER", NULL, NULL, 0u,
             NULL, NULL, &response, 200);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "FLUSH", NULL, NULL, 0u, "RTP-Info",
             "seq=65535;rtptime=4294967295", &response, 200);
    CHECK(recorder.flush_count == 1u);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "FLUSH", NULL, NULL, 0u, "RTP-Info",
             "seq=65536", &response, 400);
    CHECK(recorder.flush_count == 1u);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "SET_PARAMETER", "application/x-dmap-tagged",
             "mlit\x00\x00\x00\x10", 8u, NULL, NULL, &response, 400);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "TEARDOWN", NULL, NULL, 0u, NULL, NULL, &response, 200);
    CHECK(recorder.stop_count == 1u && response.close_connection);
    airplay_rtsp_response_clear(&response);
    airplay_handlers_session_closed(&session, handlers);
    CHECK(recorder.stop_count == 1u);

    airplay_rtsp_session_init(&session, 42u);
    session.peer_ipv4_address = inet_addr("192.168.1.2");
    snprintf(encrypted_sdp, sizeof(encrypted_sdp), "%sa=rsaaeskey:%s\r\n"
             "a=aesiv:AAECAwQFBgcICQoLDA0ODw\r\n", alac_sdp, wrapped);
    dispatch(handlers, &session, "ANNOUNCE", "application/sdp",
             encrypted_sdp, strlen(encrypted_sdp), NULL, NULL, &response, 200);
    airplay_rtsp_response_clear(&response);
    dispatch(handlers, &session, "SETUP", NULL, NULL, 0u, "Transport",
             transport, &response, 200);
    CHECK(recorder.encrypted && recorder.key[0] == 0u && recorder.key[15] == 15u);
    airplay_rtsp_response_clear(&response);
    airplay_handlers_session_closed(&session, handlers);
    airplay_handlers_destroy(handlers);
}

int main(void)
{
    char wrapped[345];
    rsa_tests(wrapped);
    sdp_tests();
    transcript(wrapped);
    if (failures)
        return EXIT_FAILURE;
    puts("AirPlay 1 RSA, SDP, RTSP transport, volume and FLUSH tests passed");
    return EXIT_SUCCESS;
}
