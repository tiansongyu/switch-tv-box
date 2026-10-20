#include <arpa/inet.h>
#include <mbedtls/aes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "protocol/airplay/media/stream_bridge.h"
#include "protocol/airplay/mirror/audio.h"
#include "protocol/airplay/mirror/video.h"

static int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

typedef struct
{
    uint8_t payload[16][4096];
    size_t sizes[16];
    uint16_t sequences[16];
    bool discontinuities[16];
    atomic_size_t count;
    atomic_uint sync_count;
    atomic_uint sync_rtp;
    atomic_uint_fast64_t sync_ntp;
} AudioRecorder;

typedef struct
{
    uint8_t data[4096];
    size_t size;
} VideoFixture;

static int hex_value(int character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static uint8_t *read_hex(const char *path, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    uint8_t *output = NULL;
    size_t size = 0u;
    size_t capacity = 0u;
    int high = -1;
    int character;

    if (!file || !size_out)
        goto failure;
    while ((character = fgetc(file)) != EOF)
    {
        int value = hex_value(character);
        if (value < 0)
            continue;
        if (high < 0)
            high = value;
        else
        {
            if (size == capacity)
            {
                size_t next_capacity = capacity ? capacity * 2u : 64u;
                uint8_t *next = realloc(output, next_capacity);
                if (!next)
                    goto failure;
                output = next;
                capacity = next_capacity;
            }
            output[size++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    fclose(file);
    if (high >= 0 || size == 0u)
    {
        free(output);
        return NULL;
    }
    *size_out = size;
    return output;

failure:
    if (file)
        fclose(file);
    free(output);
    return NULL;
}

static void record_audio(const AirPlayMirrorAudioFrame *frame, void *user_data)
{
    AudioRecorder *recorder = user_data;
    size_t index = atomic_load_explicit(&recorder->count, memory_order_relaxed);

    CHECK(index < 16u && frame->size <= sizeof(recorder->payload[index]));
    if (index < 16u && frame->size <= sizeof(recorder->payload[index]))
    {
        memcpy(recorder->payload[index], frame->data, frame->size);
        recorder->sizes[index] = frame->size;
        recorder->sequences[index] = frame->sequence;
        recorder->discontinuities[index] = frame->discontinuity;
        atomic_store_explicit(&recorder->count, index + 1u,
                              memory_order_release);
    }
}

static void record_sync(uint32_t rtp_timestamp, uint64_t ntp_timestamp,
                        void *user_data)
{
    AudioRecorder *recorder = user_data;

    atomic_store_explicit(&recorder->sync_rtp, rtp_timestamp,
                          memory_order_relaxed);
    atomic_store_explicit(&recorder->sync_ntp, ntp_timestamp,
                          memory_order_relaxed);
    atomic_fetch_add_explicit(&recorder->sync_count, 1u, memory_order_release);
}

static bool encrypt_payload(const uint8_t key[16], const uint8_t iv[16],
                            const uint8_t *input, uint8_t *output, size_t size)
{
    mbedtls_aes_context aes;
    uint8_t working_iv[16];
    size_t encrypted_size = size / 16u * 16u;
    int result;

    memcpy(working_iv, iv, sizeof(working_iv));
    mbedtls_aes_init(&aes);
    result = mbedtls_aes_setkey_enc(&aes, key, 128u);
    if (result == 0 && encrypted_size)
        result = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, encrypted_size,
                                       working_iv, input, output);
    mbedtls_aes_free(&aes);
    if (result != 0)
        return false;
    memcpy(output + encrypted_size, input + encrypted_size, size - encrypted_size);
    return true;
}

static size_t make_packet(uint8_t *packet, size_t capacity, uint16_t sequence,
                          uint32_t timestamp, const uint8_t *payload,
                          size_t payload_size, const uint8_t key[16],
                          const uint8_t iv[16])
{
    if (capacity < 12u + payload_size)
        return 0u;
    memset(packet, 0, 12u + payload_size);
    packet[0] = 0x80u;
    packet[1] = 0x60u;
    packet[2] = (uint8_t)(sequence >> 8);
    packet[3] = (uint8_t)sequence;
    packet[4] = (uint8_t)(timestamp >> 24);
    packet[5] = (uint8_t)(timestamp >> 16);
    packet[6] = (uint8_t)(timestamp >> 8);
    packet[7] = (uint8_t)timestamp;
    if (!key && !iv)
        memcpy(packet + 12u, payload, payload_size);
    else if (!encrypt_payload(key, iv, payload, packet + 12u, payload_size))
        return 0u;
    return 12u + payload_size;
}

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L};
    nanosleep(&duration, NULL);
}

static void test_audio_packets(const uint8_t *payload, size_t payload_size)
{
    AirPlayMirrorAudioConfig config = {0};
    AirPlayMirrorAudio *audio = NULL;
    AudioRecorder recorder = {0};
    uint8_t key[16];
    uint8_t iv[16];
    uint8_t packet[AIRPLAY_MIRROR_AUDIO_MAX_PACKET];
    size_t packet_size;
    int socket_fd;
    struct sockaddr_in address;

    for (size_t index = 0u; index < 16u; ++index)
    {
        key[index] = (uint8_t)index;
        iv[index] = (uint8_t)(0xf0u + index);
    }
    config.session_id = 1u;
    config.aes_key = key;
    config.aes_iv = iv;
    config.compression_type = AIRPLAY_MIRROR_AUDIO_CT_AAC_LC;
    config.samples_per_frame = 1024u;
    config.sample_rate = 44100u;
    config.callback = record_audio;
    config.sync_callback = record_sync;
    config.callback_user_data = &recorder;
    CHECK(airplay_mirror_audio_create(&config, &audio));
    airplay_mirror_audio_set_recording(audio, true);
    packet_size = make_packet(packet, sizeof(packet), 100u, 44100u, payload,
                              payload_size, key, iv);
    CHECK(packet_size != 0u &&
          airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 1u &&
          recorder.sequences[0] == 100u);
    CHECK(recorder.sizes[0] == payload_size &&
          memcmp(recorder.payload[0], payload, payload_size) == 0);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 1u);
    for (uint16_t sequence = 102u; sequence <= 104u; ++sequence)
    {
        packet_size = make_packet(packet, sizeof(packet), sequence,
                                  44100u + (sequence - 100u) * 1024u,
                                  payload, payload_size, key, iv);
        CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    }
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 4u &&
          recorder.sequences[1] == 102u &&
          recorder.discontinuities[1]);
    packet[0] = 0u;
    CHECK(!airplay_mirror_audio_process_packet(audio, packet, packet_size));

    packet_size = make_packet(packet, sizeof(packet), 105u, 49220u, payload,
                              payload_size, key, iv);
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(socket_fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(airplay_mirror_audio_data_port(audio));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (socket_fd >= 0)
    {
        CHECK(sendto(socket_fd, packet, packet_size, 0,
                     (struct sockaddr *)&address, sizeof(address)) ==
              (ssize_t)packet_size);
        close(socket_fd);
    }
    for (unsigned attempt = 0u;
         atomic_load_explicit(&recorder.count, memory_order_acquire) < 5u &&
         attempt < 100u;
         ++attempt)
        sleep_milliseconds(5u);
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 5u &&
          recorder.sequences[4] == 105u);
    airplay_mirror_audio_set_recording(audio, false);
    packet_size = make_packet(packet, sizeof(packet), 106u, 50244u, payload,
                              payload_size, key, iv);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 5u);
    airplay_mirror_audio_set_recording(audio, true);
    packet_size = make_packet(packet, sizeof(packet), 107u, 51268u, payload,
                              payload_size, key, iv);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 6u &&
          recorder.sequences[5] == 107u);
    memset(packet, 0, 20u);
    packet[0] = 0x80u;
    packet[1] = 0xd4u;
    packet[4] = 0x12u;
    packet[5] = 0x34u;
    packet[6] = 0x56u;
    packet[7] = 0x78u;
    packet[8] = 0x00u;
    packet[9] = 0x00u;
    packet[10] = 0x00u;
    packet[11] = 0x64u;
    packet[12] = 0x80u;
    CHECK(airplay_mirror_audio_process_control_packet(audio, packet, 20u));
    CHECK(atomic_load_explicit(&recorder.sync_count, memory_order_acquire) == 1u);
    CHECK(atomic_load_explicit(&recorder.sync_rtp, memory_order_relaxed) ==
          UINT32_C(0x12345678));
    CHECK(atomic_load_explicit(&recorder.sync_ntp, memory_order_relaxed) ==
          UINT64_C(0x0000006480000000));
    packet[1] = 0u;
    CHECK(!airplay_mirror_audio_process_control_packet(audio, packet, 20u));
    packet_size = make_packet(packet + 4u, sizeof(packet) - 4u, 108u, 52292u,
                              payload, payload_size, key, iv);
    packet[0] = 0x80u;
    packet[1] = 0xd6u;
    packet[2] = 0u;
    packet[3] = 0u;
    CHECK(airplay_mirror_audio_process_control_packet(audio, packet,
                                                      packet_size + 4u));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 7u &&
          recorder.sequences[6] == 108u);
    airplay_mirror_audio_destroy(audio);

    audio = NULL;
    config.session_id = 2u;
    CHECK(airplay_mirror_audio_create(&config, &audio));
    airplay_mirror_audio_set_recording(audio, true);
    packet_size = make_packet(packet, sizeof(packet), 200u, 44100u, payload,
                              payload_size, key, iv);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 8u &&
          recorder.sequences[7] == 200u);
    airplay_mirror_audio_destroy(audio);
}

static void capture_video(const AirPlayMirrorAccessUnit *access_unit, void *user_data)
{
    VideoFixture *fixture = user_data;

    CHECK(access_unit->size <= sizeof(fixture->data));
    if (access_unit->size <= sizeof(fixture->data))
    {
        memcpy(fixture->data, access_unit->data, access_unit->size);
        fixture->size = access_unit->size;
    }
}

static void test_legacy_transport(void)
{
    const uint8_t samples[] = {0x00u, 0x01u, 0x7fu, 0xffu,
                              0x80u, 0x00u, 0xffu, 0xffu};
    AirPlayMirrorAudioConfig config = {0};
    AirPlayMirrorAudio *audio = NULL;
    AudioRecorder recorder = {0};
    struct sockaddr_in peer = {0};
    socklen_t peer_size = sizeof(peer);
    struct timeval timeout = {.tv_sec = 1};
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    uint8_t packet[128], reply[128], request[32];
    CHECK(fd >= 0);
    if (fd < 0)
        return;
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(fd, (struct sockaddr *)&peer, sizeof(peer)) == 0);
    CHECK(getsockname(fd, (struct sockaddr *)&peer, &peer_size) == 0);
    CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    config.session_id = 42u;
    config.compression_type = AIRPLAY_MIRROR_AUDIO_CT_PCM;
    config.sample_rate = 48000u;
    config.peer_ipv4_address = peer.sin_addr.s_addr;
    config.peer_control_port = ntohs(peer.sin_port);
    config.callback = record_audio;
    config.callback_user_data = &recorder;
    CHECK(airplay_mirror_audio_create(&config, &audio));
    airplay_mirror_audio_set_recording(audio, true);
    size_t size = make_packet(packet, sizeof(packet), 65534u, 100u,
                              samples, sizeof(samples), NULL, NULL);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, size));
    size = make_packet(packet, sizeof(packet), 0u, 104u,
                        samples, sizeof(samples), NULL, NULL);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, size));
    CHECK(atomic_load(&recorder.count) == 1u);
    ssize_t received = recv(fd, request, sizeof(request), 0);
    CHECK(received == 8 && request[0] == 0x80u && request[1] == 0xd5u);
    CHECK(request[4] == 0xffu && request[5] == 0xffu &&
          request[6] == 0u && request[7] == 1u);
    size = make_packet(reply + 4u, sizeof(reply) - 4u, 65535u, 102u,
                        samples, sizeof(samples), NULL, NULL);
    reply[0] = 0x80u;
    reply[1] = 0xd6u;
    reply[2] = 0u;
    reply[3] = 1u;
    CHECK(airplay_mirror_audio_process_control_packet(audio, reply, size + 4u));
    CHECK(atomic_load(&recorder.count) == 3u &&
          recorder.sequences[1] == 65535u && recorder.sequences[2] == 0u);
    CHECK(recorder.sizes[1] == sizeof(samples) &&
          memcmp(recorder.payload[1], samples, sizeof(samples)) == 0);
    airplay_mirror_audio_flush(audio, true, 0u, true, 104u);
    size = make_packet(packet, sizeof(packet), 0u, 104u,
                        samples, sizeof(samples), NULL, NULL);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, size));
    CHECK(atomic_load(&recorder.count) == 3u);
    size = make_packet(packet, sizeof(packet), 1u, 106u,
                        samples, sizeof(samples), NULL, NULL);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, size));
    CHECK(atomic_load(&recorder.count) == 4u && recorder.discontinuities[3]);

    /* A legal RTP packet may contain CSRC, extension and padding bytes. */
    size = make_packet(packet, sizeof(packet), 2u, 108u,
                        samples, sizeof(samples), NULL, NULL);
    memmove(packet + 24u, packet + 12u, sizeof(samples));
    memset(packet + 12u, 0, 12u);
    packet[0] = 0xb1u; /* V2, padding, extension, one CSRC */
    packet[19] = 1u;   /* One four-byte extension word */
    memset(packet + 32u, 0, 4u);
    packet[35] = 4u;
    CHECK(airplay_mirror_audio_process_packet(audio, packet, 36u));
    CHECK(atomic_load(&recorder.count) == 5u && recorder.sizes[4] == sizeof(samples));
    CHECK(memcmp(recorder.payload[4], samples, sizeof(samples)) == 0);
    packet[19] = 50u;
    CHECK(!airplay_mirror_audio_process_packet(audio, packet, 36u));
    packet[19] = 1u;
    packet[35] = 0u;
    CHECK(!airplay_mirror_audio_process_packet(audio, packet, 36u));
    size = make_packet(packet, sizeof(packet), 4u, 112u,
                        samples, sizeof(samples), NULL, NULL);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, size));
    for (unsigned i = 0u; i < 100u && atomic_load(&recorder.count) < 6u; ++i)
        sleep_milliseconds(5u);
    CHECK(atomic_load(&recorder.count) == 6u &&
          recorder.sequences[5] == 4u && recorder.discontinuities[5]);
    airplay_mirror_audio_destroy(audio);
    audio = NULL;
    AirPlayMirrorAudioFormat negotiated;
    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_PCM, 2u, 48000u,
                                      &negotiated));
    negotiated.rtp_payload_type = 97u;
    config.format = &negotiated;
    CHECK(airplay_mirror_audio_create(&config, &audio));
    airplay_mirror_audio_set_recording(audio, true);
    size = make_packet(packet, sizeof(packet), 10u, 200u,
                        samples, sizeof(samples), NULL, NULL);
    CHECK(!airplay_mirror_audio_process_packet(audio, packet, size));
    packet[1] = 0xe1u; /* Negotiated dynamic payload 97 plus marker bit. */
    CHECK(airplay_mirror_audio_process_packet(audio, packet, size));
    CHECK(atomic_load(&recorder.count) == 7u && recorder.sequences[6] == 10u);
    airplay_mirror_audio_destroy(audio);
    close(fd);
}

static void check_container(const char *path,
                            enum AVCodecID expected_audio_codec,
                            unsigned expected_video_streams)
{
    AVFormatContext *format = NULL;
    unsigned video_streams = 0u;
    unsigned audio_streams = 0u;

    CHECK(avformat_open_input(&format, path, NULL, NULL) >= 0);
    if (!format)
        return;
    CHECK(format->iformat && strstr(format->iformat->name, "matroska"));
    CHECK(avformat_find_stream_info(format, NULL) >= 0);
    for (unsigned index = 0u; index < format->nb_streams; ++index)
    {
        const AVCodecParameters *codec = format->streams[index]->codecpar;

        if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_streams++;
            CHECK(codec->codec_id == AV_CODEC_ID_H264);
        }
        else if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_streams++;
            CHECK(codec->codec_id == expected_audio_codec);
        }
    }
    CHECK(video_streams == expected_video_streams);
    CHECK(audio_streams == 1u);
    avformat_close_input(&format);
}

static void test_dual_stream(uint8_t compression_type,
                             uint16_t samples_per_frame,
                             const uint8_t *audio, size_t audio_size,
                             const char *output_path,
                             enum AVCodecID expected_audio_codec)
{
    AirPlayMirrorVideo *video = NULL;
    AirPlayStreamBridge *bridge = NULL;
    AirPlayMirrorAudioFormat format;
    AirPlayMirrorAccessUnit video_frame = {0};
    AirPlayMirrorAudioFrame audio_frame = {0};
    AirPlayStreamBridgeStats stats = {0};
    VideoFixture fixture = {0};
    uint8_t *avcc = NULL;
    uint8_t *idr = NULL;
    size_t avcc_size = 0u;
    size_t idr_size = 0u;
    uint8_t buffer[1024];
    int64_t amount;
    FILE *output;

    avcc = read_hex("scripts/fixtures/airplay/mirror/h264-avcc.hex", &avcc_size);
    idr = read_hex("scripts/fixtures/airplay/mirror/h264-idr-au.hex", &idr_size);
    CHECK(avcc && idr &&
          airplay_mirror_video_create(capture_video, &fixture, &video));
    CHECK(airplay_mirror_video_process_config(video, avcc, avcc_size, 0u) ==
          AIRPLAY_MIRROR_VIDEO_OK);
    CHECK(airplay_mirror_video_process_access_unit(video, idr, idr_size,
                                                    UINT64_C(1) << 32) ==
          AIRPLAY_MIRROR_VIDEO_OK);
    CHECK(airplay_stream_bridge_create(0u, &bridge));
    CHECK(airplay_mirror_audio_format(compression_type, samples_per_frame,
                                      44100u, &format));
    CHECK(airplay_stream_bridge_configure_audio(bridge, &format));
    CHECK(airplay_stream_bridge_update_audio_sync(
        bridge, 44100u, UINT64_C(1) << 32));
    video_frame.data = fixture.data;
    video_frame.size = fixture.size;
    video_frame.timestamp = UINT64_C(1) << 32;
    video_frame.config_generation = 1u;
    video_frame.keyframe = true;
    audio_frame.data = audio;
    audio_frame.size = audio_size;
    audio_frame.sequence = 1u;
    audio_frame.rtp_timestamp = 44100u;
    for (unsigned index = 0u; index < 1u; ++index)
    {
        video_frame.timestamp = (UINT64_C(1) << 32) +
                                ((uint64_t)index << 32) / 30u;
        audio_frame.rtp_timestamp = 44100u + index * samples_per_frame;
        CHECK(airplay_stream_bridge_push_video(bridge, &video_frame));
        CHECK(airplay_stream_bridge_push_audio(bridge, &audio_frame));
    }
    CHECK(airplay_stream_bridge_finish(bridge));
    CHECK(airplay_stream_bridge_claim_reader(bridge));
    output = fopen(output_path, "wb");
    CHECK(output != NULL);
    while ((amount = airplay_stream_bridge_read(bridge, buffer, sizeof(buffer))) > 0)
    {
        if (output)
            CHECK(fwrite(buffer, 1u, (size_t)amount, output) == (size_t)amount);
    }
    CHECK(amount == 0);
    if (output)
        fclose(output);
    check_container(output_path, expected_audio_codec, 1u);
    CHECK(airplay_stream_bridge_get_stats(bridge, &stats));
    CHECK(stats.video_packets == 1u && stats.audio_packets == 1u);
    airplay_stream_bridge_release_reader(bridge);
    airplay_stream_bridge_release(bridge);
    airplay_mirror_video_destroy(video);
    free(avcc);
    free(idr);
}

static void test_audio_only(const uint8_t *audio, size_t audio_size)
{
    AirPlayStreamBridge *bridge = NULL;
    AirPlayMirrorAudioFormat format;
    AirPlayMirrorAudioFrame frame = {
        .data = audio,
        .size = audio_size,
        .sequence = 1u,
        .rtp_timestamp = 44100u};
    AirPlayStreamBridgeStats stats = {0};
    uint8_t buffer[1024];
    int64_t amount;
    FILE *output;

    CHECK(airplay_stream_bridge_create_profile(
        0u, AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY, &bridge));
    CHECK(airplay_stream_bridge_profile(bridge) ==
          AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY);
    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_AAC_LC,
                                      1024u, 44100u, &format));
    CHECK(airplay_stream_bridge_configure_audio(bridge, &format));
    CHECK(airplay_stream_bridge_update_audio_sync(
        bridge, 44100u, UINT64_C(1) << 32));
    CHECK(airplay_stream_bridge_push_audio(bridge, &frame));
    CHECK(airplay_stream_bridge_finish(bridge));
    CHECK(airplay_stream_bridge_claim_reader(bridge));
    output = fopen("build/tests/airplay-audio-only.mkv", "wb");
    CHECK(output != NULL);
    while ((amount = airplay_stream_bridge_read(
                bridge, buffer, sizeof(buffer))) > 0)
    {
        if (output)
            CHECK(fwrite(buffer, 1u, (size_t)amount, output) ==
                  (size_t)amount);
    }
    CHECK(amount == 0);
    if (output)
        fclose(output);
    check_container("build/tests/airplay-audio-only.mkv", AV_CODEC_ID_AAC,
                    0u);
    CHECK(airplay_stream_bridge_get_stats(bridge, &stats));
    CHECK(stats.profile == AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY);
    CHECK(stats.video_packets == 0u && stats.audio_packets == 1u);
    airplay_stream_bridge_release_reader(bridge);
    airplay_stream_bridge_release(bridge);
}

static void test_pcm_decode(void)
{
    const uint8_t pcm[] = {0x00u, 0x64u, 0xffu, 0x9cu,
                           0x01u, 0x00u, 0xffu, 0x00u};
    const int16_t expected[] = {100, -100, 256, -256};
    AirPlayStreamBridge *bridge = NULL;
    AirPlayMirrorAudioFormat format;
    AirPlayStreamBridgeStats stats = {0};
    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_PCM, 2u,
                                      48000u, &format));
    CHECK(airplay_stream_bridge_create_profile(
        0u, AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY, &bridge));
    CHECK(airplay_stream_bridge_configure_audio(bridge, &format));
    for (unsigned i = 0u; i < 3u; ++i)
    {
        AirPlayMirrorAudioFrame frame = {
            .data = pcm, .size = sizeof(pcm), .sequence = (uint16_t)i,
            .rtp_timestamp = 48000u + i * 96000u};
        CHECK(airplay_stream_bridge_push_audio(bridge, &frame));
    }
    CHECK(airplay_stream_bridge_get_stats(bridge, &stats));
    CHECK(stats.audio_packets == 3u && stats.clock.skew_audio_drops == 0u);
    CHECK(airplay_stream_bridge_finish(bridge));
    CHECK(airplay_stream_bridge_claim_reader(bridge));
    const char *path = "build/tests/airplay-pcm.mkv";
    FILE *output = fopen(path, "wb");
    CHECK(output != NULL);
    uint8_t buffer[4096];
    int64_t size;
    while ((size = airplay_stream_bridge_read(bridge, buffer, sizeof(buffer))) > 0)
        if (output)
            CHECK(fwrite(buffer, 1u, (size_t)size, output) == (size_t)size);
    if (output)
        fclose(output);
    airplay_stream_bridge_release_reader(bridge);
    airplay_stream_bridge_release(bridge);
    AVFormatContext *input = NULL;
    AVCodecContext *decoder = NULL;
    AVPacket *packet = av_packet_alloc();
    AVFrame *decoded = av_frame_alloc();
    CHECK(avformat_open_input(&input, path, NULL, NULL) >= 0);
    unsigned decoded_count = 0u;
    if (input)
    {
        CHECK(avformat_find_stream_info(input, NULL) >= 0);
        CHECK(input->nb_streams == 1u);
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_PCM_S16BE);
        CHECK(codec != NULL);
        decoder = avcodec_alloc_context3(codec);
        CHECK(decoder && avcodec_parameters_to_context(decoder,
              input->streams[0]->codecpar) >= 0);
        CHECK(decoder && avcodec_open2(decoder, codec, NULL) >= 0);
        if (decoder && packet && decoded)
            while (av_read_frame(input, packet) >= 0)
            {
                CHECK(avcodec_send_packet(decoder, packet) >= 0);
                while (avcodec_receive_frame(decoder, decoded) >= 0)
                {
                    CHECK(decoded->format == AV_SAMPLE_FMT_S16 &&
                          decoded->nb_samples == 2);
                    CHECK(memcmp(decoded->data[0], expected, sizeof(expected)) == 0);
                    decoded_count++;
                    av_frame_unref(decoded);
                }
                av_packet_unref(packet);
            }
    }
    CHECK(decoded_count == 3u);
    avcodec_free_context(&decoder);
    av_packet_free(&packet);
    av_frame_free(&decoded);
    avformat_close_input(&input);
}

static void test_alac_roundtrip(void)
{
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_ALAC);
    AVCodecContext *encode = avcodec_alloc_context3(encoder);
    AVFrame *source = av_frame_alloc();
    AVPacket *encoded = av_packet_alloc();
    int16_t expected[2][352];
    CHECK(encoder && encode && source && encoded);
    if (!encoder || !encode || !source || !encoded)
        goto cleanup;
    encode->sample_rate = 44100;
    encode->sample_fmt = AV_SAMPLE_FMT_S16P;
    encode->bits_per_raw_sample = 16;
    av_channel_layout_default(&encode->ch_layout, 2);
    CHECK(avcodec_open2(encode, encoder, NULL) >= 0);
    source->format = encode->sample_fmt;
    source->sample_rate = encode->sample_rate;
    source->nb_samples = 352;
    CHECK(av_channel_layout_copy(&source->ch_layout, &encode->ch_layout) >= 0);
    CHECK(av_frame_get_buffer(source, 0) >= 0);
    for (unsigned i = 0u; i < 352u; ++i)
    {
        expected[0][i] = (int16_t)((int)i * 97 - 16384);
        expected[1][i] = (int16_t)(16384 - (int)i * 83);
    }
    memcpy(source->data[0], expected[0], sizeof(expected[0]));
    memcpy(source->data[1], expected[1], sizeof(expected[1]));
    CHECK(avcodec_send_frame(encode, source) >= 0);
    CHECK(avcodec_receive_packet(encode, encoded) >= 0);
    if (encoded->size <= 0)
        goto cleanup;

    uint8_t key[16] = {1u, 2u, 3u}, iv[16] = {9u, 8u, 7u};
    AirPlayMirrorAudioConfig config = {
        .session_id = 99u, .aes_key = key, .aes_iv = iv,
        .compression_type = AIRPLAY_MIRROR_AUDIO_CT_ALAC,
        .callback = record_audio};
    AudioRecorder recorder = {0};
    AirPlayMirrorAudio *audio = NULL;
    config.callback_user_data = &recorder;
    CHECK(airplay_mirror_audio_create(&config, &audio));
    airplay_mirror_audio_set_recording(audio, true);
    uint8_t rtp[4096];
    size_t size = make_packet(rtp, sizeof(rtp), 5u, 44100u,
                              encoded->data, (size_t)encoded->size, key, iv);
    CHECK(size && airplay_mirror_audio_process_packet(audio, rtp, size));
    CHECK(atomic_load(&recorder.count) == 1u &&
          recorder.sizes[0] == (size_t)encoded->size);
    airplay_mirror_audio_destroy(audio);
    test_dual_stream(AIRPLAY_MIRROR_AUDIO_CT_ALAC, 352u,
                     recorder.payload[0], recorder.sizes[0],
                     "build/tests/airplay-alac-roundtrip.mkv", AV_CODEC_ID_ALAC);
    av_packet_unref(encoded);

    AVFormatContext *input = NULL;
    AVCodecContext *decode = NULL;
    AVFrame *output = av_frame_alloc();
    CHECK(avformat_open_input(&input, "build/tests/airplay-alac-roundtrip.mkv",
                               NULL, NULL) >= 0);
    unsigned count = 0u;
    if (input)
    {
        CHECK(avformat_find_stream_info(input, NULL) >= 0);
        int index = av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1,
                                        NULL, 0);
        CHECK(index >= 0);
        const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_ALAC);
        decode = avcodec_alloc_context3(decoder);
        if (index >= 0 && decode && output)
        {
            CHECK(avcodec_parameters_to_context(decode,
                  input->streams[index]->codecpar) >= 0);
            CHECK(avcodec_open2(decode, decoder, NULL) >= 0);
            while (av_read_frame(input, encoded) >= 0)
            {
                if (encoded->stream_index == index)
                {
                    CHECK(avcodec_send_packet(decode, encoded) >= 0);
                    while (avcodec_receive_frame(decode, output) >= 0)
                    {
                        CHECK(output->format == AV_SAMPLE_FMT_S16P &&
                              output->nb_samples == 352);
                        CHECK(memcmp(output->data[0], expected[0],
                                      sizeof(expected[0])) == 0);
                        CHECK(memcmp(output->data[1], expected[1],
                                      sizeof(expected[1])) == 0);
                        count++;
                        av_frame_unref(output);
                    }
                }
                av_packet_unref(encoded);
            }
        }
    }
    CHECK(count == 1u);
    av_frame_free(&output);
    avcodec_free_context(&decode);
    avformat_close_input(&input);
cleanup:
    av_packet_free(&encoded);
    av_frame_free(&source);
    avcodec_free_context(&encode);
}

int main(void)
{
    static const uint8_t alac_cookie[] = {
        0x00u, 0x00u, 0x00u, 0x24u, 0x61u, 0x6cu, 0x61u, 0x63u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x60u,
        0x00u, 0x10u, 0x28u, 0x0au, 0x0eu, 0x02u, 0x00u, 0xffu,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0xacu, 0x44u};
    static const uint8_t alac_packet[] = {0x20u, 0x00u, 0x00u, 0x00u};
    AirPlayMirrorAudioFormat format;
    uint8_t *aac;
    size_t aac_size;

    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD, 0u, 0u,
                                      &format));
    CHECK(format.codec == AIRPLAY_MIRROR_AUDIO_CODEC_AAC);
    CHECK(format.codec_config_size == 4u && format.samples_per_frame == 480u);
    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_ALAC, 0u, 0u,
                                      &format));
    CHECK(format.codec == AIRPLAY_MIRROR_AUDIO_CODEC_ALAC);
    CHECK(format.samples_per_frame == 352u);
    CHECK(format.codec_config_size == sizeof(alac_cookie));
    CHECK(memcmp(format.codec_config, alac_cookie, sizeof(alac_cookie)) == 0);
    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_ALAC, 1024u,
                                       44100u, &format));
    CHECK(format.codec_config[14] == 4u && format.codec_config[15] == 0u);
    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_ALAC, 352u,
                                       48000u, &format));
    CHECK(format.sample_rate == 48000u && format.codec_config[34] == 0xbbu);
    CHECK(!airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_ALAC, 5000u,
                                       48000u, &format));
    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_PCM, 352u,
                                      48000u, &format));
    CHECK(format.codec == AIRPLAY_MIRROR_AUDIO_CODEC_PCM_S16BE &&
          format.codec_config_size == 0u);
    test_legacy_transport();
    test_pcm_decode();
    test_alac_roundtrip();
    aac = read_hex("scripts/fixtures/airplay/mirror/aac-lc-frame.hex", &aac_size);
    CHECK(aac != NULL);
    if (aac)
    {
        test_audio_packets(aac, aac_size);
        test_audio_only(aac, aac_size);
        test_dual_stream(AIRPLAY_MIRROR_AUDIO_CT_AAC_LC, 1024u,
                         aac, aac_size,
                         "build/tests/airplay-mirror-aac.mkv",
                         AV_CODEC_ID_AAC);
        test_dual_stream(AIRPLAY_MIRROR_AUDIO_CT_ALAC, 352u,
                         alac_packet, sizeof(alac_packet),
                         "build/tests/airplay-mirror-alac.mkv",
                         AV_CODEC_ID_ALAC);
    }
    free(aac);
    if (g_failures)
    {
        fprintf(stderr, "%d AirPlay audio checks failed\n", g_failures);
        return 1;
    }
    puts("AirPlay audio checks passed");
    return 0;
}
