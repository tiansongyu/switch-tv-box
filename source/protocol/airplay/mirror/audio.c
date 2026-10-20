#if !defined(__SWITCH__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "audio.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#include "app/platform_compat.h"
typedef Thread AirPlayAudioThread;
typedef Mutex AirPlayAudioMutex;
typedef void (*AirPlayAudioThreadEntry)(void *argument);
#define AIRPLAY_AUDIO_THREAD_RETURN void
#define AIRPLAY_AUDIO_THREAD_FINISH() return
#define AIRPLAY_AUDIO_THREAD_STACK_SIZE 0x18000u
#define AIRPLAY_SOCKET_SELECT tvbox_socket_select
#else
#include <pthread.h>
typedef pthread_t AirPlayAudioThread;
typedef pthread_mutex_t AirPlayAudioMutex;
typedef void *(*AirPlayAudioThreadEntry)(void *argument);
#define AIRPLAY_AUDIO_THREAD_RETURN void *
#define AIRPLAY_AUDIO_THREAD_FINISH() return NULL
#define AIRPLAY_SOCKET_SELECT select
#endif

#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/diagnostics.h"
#include "protocol/airplay/trace.h"

#define AIRPLAY_AUDIO_REORDER_THRESHOLD 3u
#define AIRPLAY_AUDIO_POLL_MS 10u
#define AIRPLAY_AUDIO_RESEND_INTERVAL_MS 15u
#define AIRPLAY_AUDIO_GAP_TIMEOUT_MS 80u

typedef struct
{
    uint8_t *data;
    size_t size;
    uint16_t sequence;
    uint32_t timestamp;
    bool filled;
} AirPlayAudioSlot;

struct AirPlayMirrorAudio
{
    uint64_t session_id;
    uint8_t key[16];
    uint8_t iv[16];
    bool encrypted;
    AirPlayAudioMutex mutex;
    bool mutex_ready;
    struct sockaddr_in peer_control;
    uint64_t gap_started_ms;
    uint64_t resend_sent_ms;
    uint16_t resend_sequence;
    bool flush_timestamp_ready;
    uint32_t flush_timestamp;
    AirPlayMirrorAudioFormat format;
    AirPlayMirrorAudioCallback callback;
    AirPlayMirrorAudioSyncCallback sync_callback;
    void *callback_user_data;
    AirPlayAudioSlot slots[AIRPLAY_MIRROR_AUDIO_WINDOW];
    size_t buffered;
    uint16_t expected_sequence;
    bool sequence_ready;
    bool discontinuity;
    AirPlayAudioThread thread;
    atomic_bool running;
    atomic_bool recording;
    atomic_uint_fast64_t flush_request;
    atomic_int data_fd;
    atomic_int control_fd;
    uint16_t data_port;
    uint16_t control_port;
    bool thread_started;
    uint32_t diagnostic_thread_generation;
    uint64_t data_received;
    uint64_t data_rejected;
    uint64_t control_received;
    uint64_t control_rejected;
    uint64_t frames_emitted;
};

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] << 8) | data[1];
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint64_t read_be64(const uint8_t *data)
{
    return ((uint64_t)read_be32(data) << 32) | read_be32(data + 4u);
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static bool audio_mutex_init(AirPlayAudioMutex *mutex)
{
#ifdef __SWITCH__
    mutexInit(mutex);
    return true;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

static void audio_mutex_lock(AirPlayAudioMutex *mutex)
{
#ifdef __SWITCH__
    mutexLock(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

static void audio_mutex_unlock(AirPlayAudioMutex *mutex)
{
#ifdef __SWITCH__
    mutexUnlock(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

static uint64_t audio_now_ms(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick()) / UINT64_C(1000000);
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0u;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

/* SDP fmtp values map directly to Apple's 24-byte ALACSpecificConfig. */
bool airplay_mirror_audio_format_alac(const uint32_t fmtp[11],
                                      AirPlayMirrorAudioFormat *format_out)
{
    if (!fmtp || !format_out || fmtp[0] == 0u || fmtp[0] > 4096u ||
        fmtp[1] != 0u || (fmtp[2] != 16u && fmtp[2] != 24u) ||
        fmtp[3] > 255u || fmtp[4] > 255u || fmtp[5] > 31u ||
        (fmtp[6] != 1u && fmtp[6] != 2u) || fmtp[7] > 65535u ||
        fmtp[8] > AIRPLAY_MIRROR_AUDIO_MAX_PACKET ||
        fmtp[10] < 8000u || fmtp[10] > 96000u)
        return false;
    memset(format_out, 0, sizeof(*format_out));
    format_out->codec = AIRPLAY_MIRROR_AUDIO_CODEC_ALAC;
    format_out->compression_type = AIRPLAY_MIRROR_AUDIO_CT_ALAC;
    format_out->rtp_payload_type = 96u;
    format_out->sample_rate = fmtp[10];
    format_out->channels = (uint16_t)fmtp[6];
    format_out->bits_per_sample = (uint16_t)fmtp[2];
    format_out->samples_per_frame = (uint16_t)fmtp[0];
    format_out->codec_config_size = 36u;
    uint8_t *cookie = format_out->codec_config;
    write_be32(cookie, 36u);
    memcpy(cookie + 4u, "alac", 4u);
    write_be32(cookie + 12u, fmtp[0]);
    for (size_t i = 1u; i <= 6u; ++i)
        cookie[15u + i] = (uint8_t)fmtp[i];
    write_be16(cookie + 22u, (uint16_t)fmtp[7]);
    write_be32(cookie + 24u, fmtp[8]);
    write_be32(cookie + 28u, fmtp[9]);
    write_be32(cookie + 32u, fmtp[10]);
    return true;
}

bool airplay_mirror_audio_format(uint8_t compression_type,
                                 uint16_t samples_per_frame,
                                 uint32_t sample_rate,
                                 AirPlayMirrorAudioFormat *format_out)
{
    static const uint32_t rates[] = {
        96000u, 88200u, 64000u, 48000u, 44100u, 32000u, 24000u,
        22050u, 16000u, 12000u, 11025u, 8000u};
    unsigned rate_index = 0u;
    if (!format_out)
        return false;
    if (!sample_rate)
        sample_rate = 44100u;
    if (compression_type == AIRPLAY_MIRROR_AUDIO_CT_ALAC)
    {
        uint32_t fmtp[11] = {samples_per_frame ? samples_per_frame : 352u,
                            0u, 16u, 40u, 10u, 14u, 2u, 255u, 0u, 0u,
                            sample_rate};
        return airplay_mirror_audio_format_alac(fmtp, format_out);
    }
    for (; rate_index < sizeof(rates) / sizeof(rates[0]); ++rate_index)
        if (rates[rate_index] == sample_rate)
            break;
    if (rate_index == sizeof(rates) / sizeof(rates[0]))
        return false;
    memset(format_out, 0, sizeof(*format_out));
    format_out->compression_type = compression_type;
    format_out->rtp_payload_type = 96u;
    format_out->sample_rate = sample_rate;
    format_out->channels = 2u;
    format_out->bits_per_sample = 16u;
    if (compression_type == AIRPLAY_MIRROR_AUDIO_CT_PCM)
    {
        format_out->codec = AIRPLAY_MIRROR_AUDIO_CODEC_PCM_S16BE;
        format_out->samples_per_frame = samples_per_frame ? samples_per_frame : 352u;
    }
    else if (compression_type == AIRPLAY_MIRROR_AUDIO_CT_AAC_LC)
    {
        format_out->codec = AIRPLAY_MIRROR_AUDIO_CODEC_AAC;
        format_out->samples_per_frame = samples_per_frame ? samples_per_frame : 1024u;
        /* AudioSpecificConfig: AAC-LC (2), frequency index, stereo (2). */
        format_out->codec_config[0] = (uint8_t)(0x10u | (rate_index >> 1));
        format_out->codec_config[1] = (uint8_t)((rate_index << 7) | 0x10u);
        format_out->codec_config_size = 2u;
    }
    else if (compression_type == AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD)
    {
        if (sample_rate != 44100u)
            return false;
        static const uint8_t eld[] = {0xf8u, 0xe8u, 0x50u, 0x00u};
        format_out->codec = AIRPLAY_MIRROR_AUDIO_CODEC_AAC;
        format_out->samples_per_frame = samples_per_frame ? samples_per_frame : 480u;
        memcpy(format_out->codec_config, eld, sizeof(eld));
        format_out->codec_config_size = sizeof(eld);
    }
    else
        return false;
    return true;
}

static void audio_slots_clear(AirPlayMirrorAudio *audio)
{
    for (size_t index = 0u; index < AIRPLAY_MIRROR_AUDIO_WINDOW; ++index)
    {
        free(audio->slots[index].data);
        memset(&audio->slots[index], 0, sizeof(audio->slots[index]));
    }
    audio->buffered = 0u;
}

static void audio_apply_flush_locked(AirPlayMirrorAudio *audio)
{
    uint64_t request = atomic_exchange(&audio->flush_request, 0u);
    if (!(request & (UINT64_C(1) << 63)))
        return;
    audio_slots_clear(audio);
    audio->expected_sequence = (uint16_t)((request >> 32) + 1u);
    audio->sequence_ready = (request & (UINT64_C(1) << 62)) != 0u;
    audio->flush_timestamp = (uint32_t)request;
    audio->flush_timestamp_ready = (request & (UINT64_C(1) << 61)) != 0u;
    audio->discontinuity = true;
    audio->gap_started_ms = 0u;
    audio->resend_sent_ms = 0u;
}

static void audio_emit_ready(AirPlayMirrorAudio *audio)
{
    while (audio->sequence_ready)
    {
        AirPlayAudioSlot *slot = &audio->slots[
            audio->expected_sequence % AIRPLAY_MIRROR_AUDIO_WINDOW];
        AirPlayMirrorAudioFrame frame;

        if (!slot->filled || slot->sequence != audio->expected_sequence)
            break;
        frame.data = slot->data;
        frame.size = slot->size;
        frame.sequence = slot->sequence;
        frame.rtp_timestamp = slot->timestamp;
        frame.discontinuity = audio->discontinuity;
        audio->discontinuity = false;
        if (audio->callback && atomic_load(&audio->recording))
        {
            if (audio->frames_emitted++ == 0u)
                AIRPLAY_OBSERVE(
                    "[airplay-audio] session=%llu stage=first-frame ct=%u sequence=%u bytes=%zu\n",
                    (unsigned long long)audio->session_id,
                    audio->format.compression_type, frame.sequence, frame.size);
            audio->callback(&frame, audio->callback_user_data);
        }
        free(slot->data);
        memset(slot, 0, sizeof(*slot));
        audio->buffered--;
        audio->expected_sequence++;
    }
}

static void audio_skip_gap_if_needed(AirPlayMirrorAudio *audio)
{
    uint16_t nearest = 0u;
    uint16_t nearest_distance = UINT16_MAX;
    uint64_t now = audio_now_ms();
    bool can_resend = audio->peer_control.sin_port != 0u;

    if (!audio->buffered)
    {
        audio->gap_started_ms = 0u;
        audio->resend_sent_ms = 0u;
        return;
    }
    for (size_t index = 0u; index < AIRPLAY_MIRROR_AUDIO_WINDOW; ++index)
    {
        AirPlayAudioSlot *slot = &audio->slots[index];
        uint16_t distance;

        if (!slot->filled)
            continue;
        distance = (uint16_t)(slot->sequence - audio->expected_sequence);
        if (distance < nearest_distance)
        {
            nearest = slot->sequence;
            nearest_distance = distance;
        }
    }
    if (nearest_distance != UINT16_MAX && nearest_distance != 0u)
    {
        if (can_resend)
        {
            if (!audio->gap_started_ms)
                audio->gap_started_ms = now;
            if (!audio->resend_sent_ms ||
                now - audio->resend_sent_ms >= AIRPLAY_AUDIO_RESEND_INTERVAL_MS)
            {
                /* RAOP's resend request uses an 8-byte control header:
                 * request sequence, first missing sequence, packet count. */
                uint8_t request[8] = {0x80u, 0xd5u};
                write_be16(request + 2u, ++audio->resend_sequence);
                write_be16(request + 4u, audio->expected_sequence);
                write_be16(request + 6u, nearest_distance);
                int fd = atomic_load(&audio->control_fd);
                if (fd >= 0)
                    (void)sendto(fd, request, sizeof(request), 0,
                                 (const struct sockaddr *)&audio->peer_control,
                                 sizeof(audio->peer_control));
                audio->resend_sent_ms = now;
            }
            if (now - audio->gap_started_ms < AIRPLAY_AUDIO_GAP_TIMEOUT_MS &&
                audio->buffered < AIRPLAY_MIRROR_AUDIO_WINDOW - 1u)
                return;
        }
        else if (audio->buffered < AIRPLAY_AUDIO_REORDER_THRESHOLD)
            return;
        audio->expected_sequence = nearest;
        audio->discontinuity = true;
        audio->gap_started_ms = 0u;
        audio->resend_sent_ms = 0u;
    }
}

static bool audio_process_packet_locked(AirPlayMirrorAudio *audio,
                                         const uint8_t *packet,
                                         size_t packet_size)
{
    static const uint8_t empty_marker[] = {0x00u, 0x68u, 0x34u, 0x00u};
    AirPlayAudioSlot *slot;
    uint8_t *payload;
    size_t payload_size;
    size_t encrypted_size;
    size_t header_size;
    uint16_t sequence;
    uint32_t timestamp;
    int16_t distance;

    if (!audio || !packet || packet_size < AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE ||
        packet_size > AIRPLAY_MIRROR_AUDIO_MAX_PACKET ||
        packet[0] >> 6 != 2u ||
        (packet[1] & 0x7fu) != audio->format.rtp_payload_type)
        return false;
    header_size = AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE +
                  (packet[0] & 0x0fu) * 4u;
    if (header_size > packet_size)
        return false;
    if (packet[0] & 0x10u)
    {
        if (packet_size - header_size < 4u)
            return false;
        size_t extension_size = 4u + (size_t)read_be16(packet + header_size + 2u) * 4u;
        if (extension_size > packet_size - header_size)
            return false;
        header_size += extension_size;
    }
    payload_size = packet_size - header_size;
    if (packet[0] & 0x20u)
    {
        if (!payload_size || !packet[packet_size - 1u] ||
            packet[packet_size - 1u] > payload_size)
            return false;
        payload_size -= packet[packet_size - 1u];
    }
    if (payload_size == 0u ||
        (audio->format.codec == AIRPLAY_MIRROR_AUDIO_CODEC_AAC &&
         payload_size == sizeof(empty_marker) &&
         memcmp(packet + header_size,
                empty_marker, sizeof(empty_marker)) == 0))
        return true;
    sequence = read_be16(packet + 2u);
    timestamp = read_be32(packet + 4u);
    if (audio->flush_timestamp_ready &&
        (int32_t)(timestamp - audio->flush_timestamp) <= 0)
        return true;
    audio->flush_timestamp_ready = false;
    if (!audio->sequence_ready)
    {
        audio->expected_sequence = sequence;
        audio->sequence_ready = true;
    }
    distance = (int16_t)(sequence - audio->expected_sequence);
    if (distance < 0)
        return true;
    if ((uint16_t)distance >= AIRPLAY_MIRROR_AUDIO_WINDOW)
    {
        audio_slots_clear(audio);
        audio->expected_sequence = sequence;
        audio->discontinuity = true;
    }
    slot = &audio->slots[sequence % AIRPLAY_MIRROR_AUDIO_WINDOW];
    if (slot->filled && slot->sequence == sequence)
        return true;
    if (slot->filled)
    {
        free(slot->data);
        memset(slot, 0, sizeof(*slot));
        audio->buffered--;
        audio->discontinuity = true;
    }
    payload = malloc(payload_size);
    if (!payload)
        return false;
    encrypted_size = audio->encrypted
                         ? payload_size / AIRPLAY_CRYPTO_AES_BLOCK_SIZE *
                               AIRPLAY_CRYPTO_AES_BLOCK_SIZE
                         : 0u;
    if (encrypted_size &&
        !airplay_crypto_aes_cbc_decrypt(audio->key, audio->iv,
                                        packet + header_size,
                                        payload, encrypted_size))
    {
        free(payload);
        return false;
    }
    memcpy(payload + encrypted_size,
           packet + header_size + encrypted_size,
           payload_size - encrypted_size);
    if (audio->format.codec == AIRPLAY_MIRROR_AUDIO_CODEC_PCM_S16BE &&
        payload_size % (2u * audio->format.channels) != 0u)
    {
        free(payload);
        return false;
    }
    slot->data = payload;
    slot->size = payload_size;
    slot->sequence = sequence;
    slot->timestamp = timestamp;
    slot->filled = true;
    audio->buffered++;
    audio_emit_ready(audio);
    audio_skip_gap_if_needed(audio);
    audio_emit_ready(audio);
    return true;
}

static bool audio_process_control_locked(AirPlayMirrorAudio *audio,
                                                 const uint8_t *packet,
                                                 size_t packet_size)
{
    uint8_t packet_type;
    uint32_t rtp_timestamp;
    uint64_t ntp_timestamp;

    if (!audio || !packet || packet_size < 4u || packet[0] >> 6 != 2u)
        return false;
    packet_type = packet[1] & 0x7fu;
    if (packet_type == 0x56u)
        return packet_size >= 4u + AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE &&
               audio_process_packet_locked(audio, packet + 4u,
                                                    packet_size - 4u);
    if (packet_type != 0x54u || packet_size < 20u)
        return false;
    rtp_timestamp = read_be32(packet + 4u);
    ntp_timestamp = read_be64(packet + 8u);
    if (!ntp_timestamp)
        return false;
    if (audio->sync_callback)
        audio->sync_callback(rtp_timestamp, ntp_timestamp,
                             audio->callback_user_data);
    return true;
}

bool airplay_mirror_audio_process_packet(AirPlayMirrorAudio *audio,
                                         const uint8_t *packet,
                                         size_t packet_size)
{
    if (!audio)
        return false;
    audio_mutex_lock(&audio->mutex);
    audio_apply_flush_locked(audio);
    bool result = audio_process_packet_locked(audio, packet, packet_size);
    audio_mutex_unlock(&audio->mutex);
    return result;
}

bool airplay_mirror_audio_process_control_packet(AirPlayMirrorAudio *audio,
                                                 const uint8_t *packet,
                                                 size_t packet_size)
{
    if (!audio)
        return false;
    audio_mutex_lock(&audio->mutex);
    audio_apply_flush_locked(audio);
    bool result = audio_process_control_locked(audio, packet, packet_size);
    audio_mutex_unlock(&audio->mutex);
    return result;
}

static bool audio_thread_start(AirPlayAudioThread *thread,
                               AirPlayAudioThreadEntry entry, void *argument)
{
#ifdef __SWITCH__
    Result result = threadCreate(thread, entry, argument, NULL,
                                 AIRPLAY_AUDIO_THREAD_STACK_SIZE, 0x2b, -2);
    if (R_FAILED(result))
        return false;
    result = threadStart(thread);
    if (R_FAILED(result))
    {
        threadClose(thread);
        return false;
    }
    return true;
#else
    return pthread_create(thread, NULL, entry, argument) == 0;
#endif
}

static void audio_thread_join(AirPlayAudioThread *thread)
{
#ifdef __SWITCH__
    threadWaitForExit(thread);
    threadClose(thread);
#else
    pthread_join(*thread, NULL);
#endif
}

static void close_owned_socket(atomic_int *owner)
{
    int socket_fd = atomic_exchange(owner, -1);

    if (socket_fd >= 0)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
        airplay_diagnostics_socket_closed(
            NETWORK_DIAGNOSTIC_AIRPLAY_AUDIO);
    }
}

static AIRPLAY_AUDIO_THREAD_RETURN audio_thread(void *argument)
{
    AirPlayMirrorAudio *audio = argument;
    uint8_t *packet = malloc(AIRPLAY_MIRROR_AUDIO_MAX_PACKET);

    if (!packet)
        AIRPLAY_AUDIO_THREAD_FINISH();

    while (atomic_load(&audio->running))
    {
        int data_fd = atomic_load(&audio->data_fd);
        int control_fd = atomic_load(&audio->control_fd);
        int maximum = data_fd > control_fd ? data_fd : control_fd;
        fd_set read_set;
        struct timeval timeout = {0, AIRPLAY_AUDIO_POLL_MS * 1000u};
        int result;

        audio_mutex_lock(&audio->mutex);
        audio_apply_flush_locked(audio);
        audio_skip_gap_if_needed(audio);
        audio_emit_ready(audio);
        audio_mutex_unlock(&audio->mutex);
        if (maximum < 0)
            break;
        FD_ZERO(&read_set);
        if (data_fd >= 0)
            FD_SET(data_fd, &read_set);
        if (control_fd >= 0)
            FD_SET(control_fd, &read_set);
        result = AIRPLAY_SOCKET_SELECT(maximum + 1, &read_set, NULL, NULL, &timeout);
        if (result < 0 && errno != EINTR)
        {
            if (atomic_load(&audio->running))
                AIRPLAY_OBSERVE(
                    "[airplay-audio] session=%llu stage=select-failed errno=%d\n",
                    (unsigned long long)audio->session_id, errno);
            break;
        }
        if (result <= 0)
            continue;
        if (data_fd >= 0 && FD_ISSET(data_fd, &read_set))
        {
            struct sockaddr_in sender = {0};
            socklen_t sender_size = sizeof(sender);
            ssize_t received = recvfrom(data_fd, packet,
                                        AIRPLAY_MIRROR_AUDIO_MAX_PACKET,
                                        0, (struct sockaddr *)&sender, &sender_size);
            if (audio->peer_control.sin_addr.s_addr &&
                sender.sin_addr.s_addr != audio->peer_control.sin_addr.s_addr)
                continue;
            if (received > 0)
            {
                if (audio->data_received++ == 0u)
                    AIRPLAY_OBSERVE(
                        "[airplay-audio] session=%llu stage=first-data bytes=%zd type=%u recording=%u\n",
                        (unsigned long long)audio->session_id, received,
                        received >= 2 ? packet[1] & 0x7fu : 0u,
                        atomic_load(&audio->recording) ? 1u : 0u);
                bool accepted = received >= 2 && (packet[1] & 0x7fu) == 0x56u
                    ? airplay_mirror_audio_process_control_packet(audio, packet,
                                                                  (size_t)received)
                    : airplay_mirror_audio_process_packet(audio, packet,
                                                          (size_t)received);
                if (!accepted &&
                    audio->data_rejected++ == 0u)
                    AIRPLAY_OBSERVE(
                        "[airplay-audio] session=%llu stage=data-rejected bytes=%zd type=%u\n",
                        (unsigned long long)audio->session_id, received,
                        received >= 2 ? packet[1] & 0x7fu : 0u);
            }
        }
        if (control_fd >= 0 && FD_ISSET(control_fd, &read_set))
        {
            struct sockaddr_in sender = {0};
            socklen_t sender_size = sizeof(sender);
            ssize_t received = recvfrom(control_fd, packet,
                                        AIRPLAY_MIRROR_AUDIO_MAX_PACKET,
                                        0, (struct sockaddr *)&sender, &sender_size);
            if (audio->peer_control.sin_addr.s_addr &&
                sender.sin_addr.s_addr != audio->peer_control.sin_addr.s_addr)
                continue;
            if (received > 0)
            {
                if (audio->control_received++ == 0u)
                    AIRPLAY_OBSERVE(
                        "[airplay-audio] session=%llu stage=first-control bytes=%zd type=%u\n",
                        (unsigned long long)audio->session_id, received,
                        received >= 2 ? packet[1] & 0x7fu : 0u);
                if (!airplay_mirror_audio_process_control_packet(
                        audio, packet, (size_t)received) &&
                    audio->control_rejected++ == 0u)
                    AIRPLAY_OBSERVE(
                        "[airplay-audio] session=%llu stage=control-rejected bytes=%zd type=%u\n",
                        (unsigned long long)audio->session_id, received,
                        received >= 2 ? packet[1] & 0x7fu : 0u);
            }
        }
    }
    free(packet);
    AIRPLAY_AUDIO_THREAD_FINISH();
}

static bool create_udp_socket(atomic_int *owner, uint16_t *port_out)
{
    struct sockaddr_in address;
    socklen_t address_size = sizeof(address);
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0)
        return false;
    airplay_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_AIRPLAY_AUDIO);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(0u);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(socket_fd, (struct sockaddr *)&address, &address_size) != 0)
    {
        close(socket_fd);
        airplay_diagnostics_socket_closed(
            NETWORK_DIAGNOSTIC_AIRPLAY_AUDIO);
        return false;
    }
    *port_out = ntohs(address.sin_port);
    atomic_store(owner, socket_fd);
    return *port_out != 0u;
}

bool airplay_mirror_audio_create(const AirPlayMirrorAudioConfig *config,
                                 AirPlayMirrorAudio **audio_out)
{
    AirPlayMirrorAudio *audio;
    const char *failure_stage = "config";

    if (!config || !audio_out || *audio_out || config->session_id == 0u ||
        (!config->aes_key != !config->aes_iv) || !config->callback)
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=audio stage=config\n",
            (unsigned long long)(config ? config->session_id : 0u));
        return false;
    }
    audio = calloc(1, sizeof(*audio));
    if (!audio)
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=audio stage=format reason=no-memory\n",
            (unsigned long long)config->session_id);
        return false;
    }
    audio->session_id = config->session_id;
    audio->encrypted = config->aes_key != NULL;
    if (audio->encrypted)
    {
        memcpy(audio->key, config->aes_key, sizeof(audio->key));
        memcpy(audio->iv, config->aes_iv, sizeof(audio->iv));
    }
    audio->peer_control.sin_family = AF_INET;
    audio->peer_control.sin_addr.s_addr = config->peer_ipv4_address;
    audio->peer_control.sin_port = htons(config->peer_control_port);
    audio->callback = config->callback;
    audio->sync_callback = config->sync_callback;
    audio->callback_user_data = config->callback_user_data;
    atomic_init(&audio->running, false);
    atomic_init(&audio->recording, false);
    atomic_init(&audio->flush_request, 0u);
    atomic_init(&audio->data_fd, -1);
    atomic_init(&audio->control_fd, -1);
    if (!audio_mutex_init(&audio->mutex))
        goto failure;
    audio->mutex_ready = true;
    failure_stage = "format";
    if (config->format)
        audio->format = *config->format;
    else if (!airplay_mirror_audio_format(config->compression_type,
                                     config->samples_per_frame,
                                     config->sample_rate, &audio->format))
        goto failure;
    if (!audio->format.rtp_payload_type)
        audio->format.rtp_payload_type = 96u;
    if (audio->format.sample_rate < 8000u || audio->format.sample_rate > 96000u ||
        audio->format.rtp_payload_type < 96u || audio->format.rtp_payload_type > 127u ||
        audio->format.channels < 1u || audio->format.channels > 2u ||
        !audio->format.samples_per_frame ||
        audio->format.codec_config_size > AIRPLAY_MIRROR_AUDIO_CODEC_CONFIG_MAX ||
        audio->format.codec == AIRPLAY_MIRROR_AUDIO_CODEC_INVALID)
        goto failure;
    failure_stage = "socket-data";
    if (!create_udp_socket(&audio->data_fd, &audio->data_port))
        goto failure;
    failure_stage = "socket-control";
    if (!create_udp_socket(&audio->control_fd, &audio->control_port))
        goto failure;
    atomic_store(&audio->running, true);
    failure_stage = "thread-create";
    if (!audio_thread_start(&audio->thread, audio_thread, audio))
    {
        airplay_diagnostics_thread_create_failed(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_AUDIO);
        atomic_store(&audio->running, false);
        goto failure;
    }
    audio->thread_started = true;
    audio->diagnostic_thread_generation =
        airplay_diagnostics_thread_created(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_AUDIO);
    *audio_out = audio;
    return true;

failure:
    (void)failure_stage;
    AIRPLAY_OBSERVE(
        "[airplay-setup-failure] session=%llu stream=audio stage=%s ct=%u spf=%u sr=%u\n",
        (unsigned long long)config->session_id, failure_stage,
        config->compression_type, config->samples_per_frame,
        config->sample_rate);
    airplay_mirror_audio_destroy(audio);
    return false;
}

void airplay_mirror_audio_set_recording(AirPlayMirrorAudio *audio, bool recording)
{
    if (audio)
        atomic_store(&audio->recording, recording);
}

void airplay_mirror_audio_flush(AirPlayMirrorAudio *audio,
                                bool has_sequence, uint16_t sequence,
                                bool has_timestamp, uint32_t timestamp)
{
    if (!audio)
        return;
    /* Apply on the packet thread, avoiding runtime/audio lock inversion. */
    uint64_t request = (UINT64_C(1) << 63) | ((uint64_t)sequence << 32) |
                       timestamp;
    if (has_sequence)
        request |= UINT64_C(1) << 62;
    if (has_timestamp)
        request |= UINT64_C(1) << 61;
    atomic_store(&audio->flush_request, request);
}

void airplay_mirror_audio_destroy(AirPlayMirrorAudio *audio)
{
    if (!audio)
        return;
    atomic_store(&audio->running, false);
    int data_fd = atomic_load(&audio->data_fd);
    int control_fd = atomic_load(&audio->control_fd);
    if (data_fd >= 0)
        shutdown(data_fd, SHUT_RDWR);
    if (control_fd >= 0)
        shutdown(control_fd, SHUT_RDWR);
    if (audio->thread_started)
    {
        audio_thread_join(&audio->thread);
        airplay_diagnostics_thread_joined(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_AUDIO,
            audio->diagnostic_thread_generation);
    }
    /* Join before closing so another thread cannot reuse a descriptor still
     * present in this worker's select/recvfrom call. */
    close_owned_socket(&audio->data_fd);
    close_owned_socket(&audio->control_fd);
    AIRPLAY_OBSERVE(
        "[airplay-audio-summary] session=%llu data=%llu rejected=%llu control=%llu rejected=%llu delivered=%llu\n",
        (unsigned long long)audio->session_id,
        (unsigned long long)audio->data_received,
        (unsigned long long)audio->data_rejected,
        (unsigned long long)audio->control_received,
        (unsigned long long)audio->control_rejected,
        (unsigned long long)audio->frames_emitted);
    audio_slots_clear(audio);
#ifndef __SWITCH__
    if (audio->mutex_ready)
        pthread_mutex_destroy(&audio->mutex);
#endif
    airplay_crypto_secure_zero(audio->key, sizeof(audio->key));
    airplay_crypto_secure_zero(audio->iv, sizeof(audio->iv));
    free(audio);
}

uint16_t airplay_mirror_audio_data_port(const AirPlayMirrorAudio *audio)
{
    return audio && atomic_load(&audio->running) ? audio->data_port : 0u;
}

uint16_t airplay_mirror_audio_control_port(const AirPlayMirrorAudio *audio)
{
    return audio && atomic_load(&audio->running) ? audio->control_port : 0u;
}
