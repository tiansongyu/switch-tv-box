#include "handlers.h"

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "protocol/airplay/media/remote_video.h"
#include "protocol/airplay/diagnostics.h"
#include "protocol/airplay/protocol/plist.h"
#include "protocol/airplay/protocol/sdp.h"
#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/security/legacy_rsa.h"
#include "protocol/airplay/trace.h"

#define AIRPLAY_PLIST_CONTENT_TYPE "application/x-apple-binary-plist"
#define AIRPLAY_OCTET_CONTENT_TYPE "application/octet-stream"
#define AIRPLAY_TEXT_CONTENT_TYPE "text/parameters"
#define AIRPLAY_XML_PLIST_CONTENT_TYPE "text/x-apple-plist+xml"

typedef struct
{
    AirPlayFairPlay *fairplay;
    bool initial_setup;
    bool mirror_setup;
    bool audio_setup;
    bool record_requested;
    bool recording_started;
    bool audio_recording_started;
    bool diagnostic_initial_setup_seen;
    bool diagnostic_initial_setup_ok;
    bool diagnostic_stream_setup_seen;
    bool diagnostic_mirror_setup_seen;
    bool diagnostic_mirror_setup_ok;
    bool diagnostic_audio_setup_seen;
    bool diagnostic_audio_setup_ok;
    bool diagnostic_record_seen;
    bool diagnostic_record_ok;
    bool diagnostic_remote_play_seen;
    bool diagnostic_remote_play_ok;
    bool diagnostic_teardown_seen;
    int diagnostic_last_status;
    uint8_t aes_key[16];
    uint8_t aes_iv[16];
    uint64_t stream_connection_id;
    bool legacy_announced;
    bool legacy_encrypted;
    AirPlayMirrorAudioFormat legacy_format;
    float volume_db;
} AirPlayHandlerSession;

struct AirPlayHandlers
{
    AirPlayHandlersConfig config;
    char friendly_name[AIRPLAY_HANDLER_NAME_MAX + 1u];
    char device_id[AIRPLAY_HANDLER_DEVICE_ID_STRING_SIZE];
    char pairing_id[AIRPLAY_HANDLER_PAIRING_ID_SIZE];
    uint8_t public_key[32];
    uint8_t airplay_txt[AIRPLAY_DNS_TXT_MAX];
};

static size_t bounded_string_length(const char *value, size_t capacity)
{
    size_t length;

    if (!value)
        return capacity;
    for (length = 0u; length < capacity && value[length] != '\0'; ++length)
    {
    }
    return length;
}

static bool content_type_is(const AirPlayRtspRequest *request, const char *expected)
{
    const char *value = airplay_rtsp_request_header(request, "Content-Type");
    size_t size = strlen(expected);

    return value && strncasecmp(value, expected, size) == 0 &&
           (value[size] == '\0' || value[size] == ';');
}

static bool dict_set(AirPlayPlistValue *dict, const char *key, AirPlayPlistValue *value)
{
    if (!value)
        return false;
    if (!airplay_plist_dict_set(dict, key, value))
    {
        airplay_plist_free(value);
        return false;
    }
    return true;
}

static bool array_append(AirPlayPlistValue *array, AirPlayPlistValue *value)
{
    if (!value)
        return false;
    if (!airplay_plist_array_append(array, value))
    {
        airplay_plist_free(value);
        return false;
    }
    return true;
}

static bool set_plist_body(AirPlayRtspResponse *response, AirPlayPlistValue *root)
{
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    AirPlayPlistError error;
    bool ok = airplay_plist_encode(root, &encoded, &encoded_size, &error) &&
              airplay_rtsp_response_set_body(response, encoded, encoded_size,
                                              AIRPLAY_PLIST_CONTENT_TYPE);
    airplay_plist_buffer_free(encoded);
    return ok;
}

static AirPlayHandlerSession *session_context(AirPlayRtspSession *session)
{
    AirPlayHandlerSession *context;

    if (!session)
        return NULL;
    context = session->protocol_context;
    if (context)
        return context;
    context = calloc(1, sizeof(*context));
    if (!context || !airplay_fairplay_create(&context->fairplay))
    {
        airplay_fairplay_destroy(context ? context->fairplay : NULL);
        free(context);
        return NULL;
    }
    session->protocol_context = context;
    return context;
}

bool airplay_handlers_session_is_legacy(const AirPlayRtspSession *session)
{
    const AirPlayHandlerSession *context = session ? session->protocol_context : NULL;
    return context && context->legacy_announced;
}

static void diagnostic_note_setup_streams(AirPlayHandlerSession *context,
                                          const AirPlayPlistValue *streams)
{
    size_t count;

    if (!context || !streams)
        return;
    context->diagnostic_stream_setup_seen = true;
    count = airplay_plist_array_size(streams);
    for (size_t index = 0u; index < count; ++index)
    {
        const AirPlayPlistValue *stream =
            airplay_plist_array_get(streams, index);
        uint64_t type = 0u;

        if (!airplay_plist_get_uint(airplay_plist_dict_get(stream, "type"),
                                    &type))
            continue;
        context->diagnostic_mirror_setup_seen |= type == 110u;
        context->diagnostic_audio_setup_seen |= type == 96u;
    }
}

#if defined(NXCAST_AIRPLAY_TRACE_VERBOSE) && NXCAST_AIRPLAY_TRACE_VERBOSE
static const char *diagnostic_first_missing(
    const AirPlayHandlerSession *context)
{
    if (!context)
        return "handler-context";
    if (context->diagnostic_remote_play_seen)
        return context->diagnostic_remote_play_ok
                   ? "remote-media-handoff"
                   : "remote-play-accept";
    if (context->diagnostic_mirror_setup_seen)
    {
        if (!context->diagnostic_initial_setup_ok)
            return "initial-setup-accept";
        if (!context->diagnostic_mirror_setup_ok)
            return "video-setup-accept";
        if (!context->diagnostic_record_seen)
            return "record-request";
        if (!context->diagnostic_record_ok)
            return "record-accept";
        return "mirror-media-first-packet";
    }
    if (context->diagnostic_audio_setup_seen)
        return context->diagnostic_audio_setup_ok
                   ? "video-setup-request-audio-only"
                   : "audio-setup-accept";
    if (context->diagnostic_initial_setup_seen)
        return context->diagnostic_initial_setup_ok
                   ? "video-setup-request"
                   : "initial-setup-accept";
    return "video-negotiation-request";
}
#endif

static void diagnostic_log_summary(const AirPlayRtspSession *session,
                                   const AirPlayHandlerSession *context)
{
#if defined(NXCAST_AIRPLAY_TRACE_VERBOSE) && NXCAST_AIRPLAY_TRACE_VERBOSE
    AIRPLAY_TRACE_SYNC(
        "[airplay-flow-summary] connection=%llu logical=%llu requests=%u "
        "initial=%u/%u stream=%u video=%u/%u audio=%u/%u record=%u/%u "
        "remote_play=%u/%u teardown=%u last_status=%d first_missing=%s\n",
        (unsigned long long)session->id,
        (unsigned long long)session->logical_session_id,
        session->request_count,
        context->diagnostic_initial_setup_seen ? 1u : 0u,
        context->diagnostic_initial_setup_ok ? 1u : 0u,
        context->diagnostic_stream_setup_seen ? 1u : 0u,
        context->diagnostic_mirror_setup_seen ? 1u : 0u,
        context->diagnostic_mirror_setup_ok ? 1u : 0u,
        context->diagnostic_audio_setup_seen ? 1u : 0u,
        context->diagnostic_audio_setup_ok ? 1u : 0u,
        context->diagnostic_record_seen ? 1u : 0u,
        context->diagnostic_record_ok ? 1u : 0u,
        context->diagnostic_remote_play_seen ? 1u : 0u,
        context->diagnostic_remote_play_ok ? 1u : 0u,
        context->diagnostic_teardown_seen ? 1u : 0u,
        context->diagnostic_last_status,
        diagnostic_first_missing(context));
#else
    (void)session;
    (void)context;
#endif
}

static AirPlayHandlerPhase handler_phase(const AirPlayRtspSession *session,
                                         const AirPlayHandlerSession *context)
{
    if (session && session->state == AIRPLAY_RTSP_SESSION_CLOSED)
        return AIRPLAY_HANDLER_PHASE_CLOSED;
    if (!context || !context->initial_setup)
        return AIRPLAY_HANDLER_PHASE_CONTROL;
    if (context->record_requested)
    {
        return context->mirror_setup || context->audio_setup
                   ? AIRPLAY_HANDLER_PHASE_RECORDING
                   : AIRPLAY_HANDLER_PHASE_RECORD_PENDING;
    }
    return context->mirror_setup || context->audio_setup
               ? AIRPLAY_HANDLER_PHASE_STREAM_READY
               : AIRPLAY_HANDLER_PHASE_TRANSPORT_READY;
}

AirPlayHandlerPhase airplay_handlers_session_phase(const AirPlayRtspSession *session)
{
    const AirPlayHandlerSession *context;

    if (!session)
        return AIRPLAY_HANDLER_PHASE_CONTROL;
    context = session->protocol_context;
    return handler_phase(session, context);
}

const char *airplay_handler_phase_name(AirPlayHandlerPhase phase)
{
    switch (phase)
    {
    case AIRPLAY_HANDLER_PHASE_CONTROL:
        return "control";
    case AIRPLAY_HANDLER_PHASE_TRANSPORT_READY:
        return "transport-ready";
    case AIRPLAY_HANDLER_PHASE_RECORD_PENDING:
        return "record-pending";
    case AIRPLAY_HANDLER_PHASE_STREAM_READY:
        return "stream-ready";
    case AIRPLAY_HANDLER_PHASE_RECORDING:
        return "recording";
    case AIRPLAY_HANDLER_PHASE_CLOSED:
        return "closed";
    default:
        return "invalid";
    }
}

static bool handler_state_valid(const AirPlayRtspSession *session,
                                const AirPlayHandlerSession *context)
{
    bool has_stream;

    if (!session || !context)
        return false;
    has_stream = context->mirror_setup || context->audio_setup;
    if ((has_stream || context->record_requested ||
         context->recording_started || context->audio_recording_started) &&
        !context->initial_setup)
        return false;
    if ((context->recording_started || context->audio_recording_started) &&
        (!has_stream || !context->record_requested))
        return false;
    if (session->state == AIRPLAY_RTSP_SESSION_CLOSED)
        return !context->initial_setup && !has_stream &&
               !context->record_requested && !context->recording_started;
    if (context->initial_setup !=
        (session->state == AIRPLAY_RTSP_SESSION_SETUP ||
         session->state == AIRPLAY_RTSP_SESSION_RECORDING))
        return false;
    return context->record_requested ==
           (session->state == AIRPLAY_RTSP_SESSION_RECORDING);
}

static void trace_phase_transition(const AirPlayRtspSession *session,
                                   AirPlayHandlerPhase before,
                                   const char *event,
                                   bool ok)
{
    AirPlayHandlerPhase after = airplay_handlers_session_phase(session);

    (void)event;
    (void)ok;
    if (before == after)
        return;
    AIRPLAY_TRACE_SYNC(
        "[airplay-phase] connection=%llu logical=%llu event=%s from=%s to=%s result=%s\n",
        (unsigned long long)session->id,
        (unsigned long long)session->logical_session_id,
        event,
        airplay_handler_phase_name(before),
        airplay_handler_phase_name(after),
        ok ? "ok" : "failed");
}

bool airplay_handlers_create(const AirPlayHandlersConfig *config,
                             AirPlayHandlers **handlers_out)
{
    AirPlayHandlers *handlers;
    size_t name_size;
    size_t device_id_size;
    size_t pairing_id_size;

    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=validate begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    if (!config || !handlers_out || *handlers_out || !config->friendly_name ||
        !config->device_id || !config->pairing_id || !config->public_key)
        return false;
    name_size = bounded_string_length(config->friendly_name,
                                      AIRPLAY_HANDLER_NAME_MAX + 1u);
    device_id_size = bounded_string_length(
        config->device_id, AIRPLAY_HANDLER_DEVICE_ID_STRING_SIZE);
    pairing_id_size = bounded_string_length(
        config->pairing_id, AIRPLAY_HANDLER_PAIRING_ID_SIZE);
    if (name_size == 0u || name_size > AIRPLAY_HANDLER_NAME_MAX ||
        device_id_size != AIRPLAY_HANDLER_DEVICE_ID_STRING_SIZE - 1u ||
        pairing_id_size != AIRPLAY_HANDLER_PAIRING_ID_SIZE - 1u ||
        config->airplay_txt_size > AIRPLAY_DNS_TXT_MAX ||
        (config->airplay_txt_size != 0u && !config->airplay_txt))
        return false;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=validate done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=allocate begin bytes=%zu\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), sizeof(*handlers));
    handlers = calloc(1, sizeof(*handlers));
    if (!handlers)
        return false;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=allocate done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=copy begin txt_bytes=%zu\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(),
                       config->airplay_txt_size);
    handlers->config = *config;
    memcpy(handlers->friendly_name, config->friendly_name, name_size + 1u);
    memcpy(handlers->device_id, config->device_id, sizeof(handlers->device_id));
    memcpy(handlers->pairing_id, config->pairing_id, sizeof(handlers->pairing_id));
    memcpy(handlers->public_key, config->public_key, sizeof(handlers->public_key));
    if (config->airplay_txt_size)
        memcpy(handlers->airplay_txt, config->airplay_txt, config->airplay_txt_size);
    handlers->config.friendly_name = handlers->friendly_name;
    handlers->config.device_id = handlers->device_id;
    handlers->config.pairing_id = handlers->pairing_id;
    handlers->config.public_key = handlers->public_key;
    handlers->config.airplay_txt = handlers->airplay_txt;
    *handlers_out = handlers;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=copy done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    return true;
}

void airplay_handlers_destroy(AirPlayHandlers *handlers)
{
    if (!handlers)
        return;
    airplay_crypto_secure_zero(handlers, sizeof(*handlers));
    free(handlers);
}

static bool request_qualifies_txt(const AirPlayRtspRequest *request)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error;
    const AirPlayPlistValue *qualifier;
    const char *value;
    bool matches = false;

    if (!content_type_is(request, AIRPLAY_PLIST_CONTENT_TYPE) ||
        !airplay_plist_decode(request->body, request->body_length, &root, &error))
        return false;
    qualifier = airplay_plist_dict_get(root, "qualifier");
    value = airplay_plist_get_string(airplay_plist_array_get(qualifier, 0u));
    matches = value && strcmp(value, "txtAirPlay") == 0;
    airplay_plist_free(root);
    return matches;
}

static bool add_display_info(AirPlayPlistValue *root)
{
    AirPlayPlistValue *displays = airplay_plist_new_array();
    AirPlayPlistValue *display = airplay_plist_new_dict();
    bool ok;

    if (!displays || !display)
    {
        airplay_plist_free(displays);
        airplay_plist_free(display);
        return false;
    }
    ok = dict_set(display, "uuid", airplay_plist_new_string("5f45580b-6a34-45df-9b3f-006e78636173")) &&
         dict_set(display, "widthPhysical", airplay_plist_new_uint(0u)) &&
         dict_set(display, "heightPhysical", airplay_plist_new_uint(0u)) &&
         dict_set(display, "width", airplay_plist_new_uint(1280u)) &&
         dict_set(display, "height", airplay_plist_new_uint(720u)) &&
         dict_set(display, "widthPixels", airplay_plist_new_uint(1280u)) &&
         dict_set(display, "heightPixels", airplay_plist_new_uint(720u)) &&
         dict_set(display, "rotation", airplay_plist_new_bool(false)) &&
         dict_set(display, "refreshRate", airplay_plist_new_real(1.0 / 60.0)) &&
         dict_set(display, "maxFPS", airplay_plist_new_uint(60u)) &&
         dict_set(display, "overscanned", airplay_plist_new_bool(false)) &&
         dict_set(display, "features", airplay_plist_new_uint(14u));
    if (!ok)
    {
        airplay_plist_free(display);
        airplay_plist_free(displays);
        return false;
    }
    if (!array_append(displays, display))
    {
        airplay_plist_free(displays);
        return false;
    }
    if (!dict_set(root, "displays", displays))
        return false;
    return ok;
}

static AirPlayPlistValue *new_audio_latency(uint64_t type)
{
    AirPlayPlistValue *latency = airplay_plist_new_dict();

    if (!latency ||
        !dict_set(latency, "type", airplay_plist_new_uint(type)) ||
        !dict_set(latency, "inputLatencyMicros", airplay_plist_new_uint(0u)) ||
        !dict_set(latency, "audioType", airplay_plist_new_string("default")) ||
        !dict_set(latency, "outputLatencyMicros", airplay_plist_new_uint(0u)))
    {
        airplay_plist_free(latency);
        return NULL;
    }
    return latency;
}

static AirPlayPlistValue *new_audio_format(uint64_t type)
{
    AirPlayPlistValue *format = airplay_plist_new_dict();

    if (!format ||
        !dict_set(format, "type", airplay_plist_new_uint(type)) ||
        !dict_set(format, "audioInputFormats",
                  airplay_plist_new_uint(UINT64_C(0x3fffffc))) ||
        !dict_set(format, "audioOutputFormats",
                  airplay_plist_new_uint(UINT64_C(0x3fffffc))))
    {
        airplay_plist_free(format);
        return NULL;
    }
    return format;
}

static bool add_audio_info(AirPlayPlistValue *root)
{
    AirPlayPlistValue *latencies = airplay_plist_new_array();
    AirPlayPlistValue *formats = airplay_plist_new_array();
    bool attached;

    if (!latencies || !formats)
        goto failure;
    if (!array_append(latencies, new_audio_latency(100u)))
        goto failure;
    if (!array_append(latencies, new_audio_latency(101u)))
        goto failure;
    if (!array_append(formats, new_audio_format(100u)))
        goto failure;
    if (!array_append(formats, new_audio_format(101u)))
        goto failure;
    if (!dict_set(root, "initialVolume", airplay_plist_new_real(0.0)))
        goto failure;
    /* dict_set and array_append consume their value even when allocation fails. */
    attached = dict_set(root, "audioLatencies", latencies);
    latencies = NULL;
    if (!attached)
        goto failure;
    attached = dict_set(root, "audioFormats", formats);
    formats = NULL;
    return attached;

failure:
    airplay_plist_free(latencies);
    airplay_plist_free(formats);
    return false;
}

static bool handle_info(AirPlayHandlers *handlers,
                        const AirPlayRtspRequest *request,
                        AirPlayRtspResponse *response)
{
    AirPlayPlistValue *root = airplay_plist_new_dict();
    bool ok = false;

    if (strcmp(request->method, "GET") != 0 || !root)
        goto cleanup;
    if (request_qualifies_txt(request))
    {
        ok = handlers->config.airplay_txt_size != 0u &&
             dict_set(root, "txtAirPlay",
                      airplay_plist_new_data(handlers->config.airplay_txt,
                                             handlers->config.airplay_txt_size));
    }
    else
    {
        ok = dict_set(root, "deviceID", airplay_plist_new_string(handlers->device_id)) &&
             dict_set(root, "macAddress", airplay_plist_new_string(handlers->device_id)) &&
             dict_set(root, "pk", airplay_plist_new_data(handlers->public_key, 32u)) &&
             dict_set(root, "features", airplay_plist_new_uint(handlers->config.features)) &&
             dict_set(root, "name", airplay_plist_new_string(handlers->friendly_name)) &&
             dict_set(root, "pi", airplay_plist_new_string(handlers->pairing_id)) &&
             dict_set(root, "vv", airplay_plist_new_uint(2u)) &&
             dict_set(root, "statusFlags", airplay_plist_new_uint(68u)) &&
             dict_set(root, "keepAliveLowPower", airplay_plist_new_uint(1u)) &&
             dict_set(root, "keepAliveSendStatsAsBody", airplay_plist_new_bool(true)) &&
             dict_set(root, "sourceVersion", airplay_plist_new_string("220.68")) &&
             dict_set(root, "model", airplay_plist_new_string("AppleTV3,2"));
        if (ok && (handlers->config.features & (UINT64_C(1) << 9)) != 0u)
            ok = add_audio_info(root);
        if (ok && (handlers->config.features & (UINT64_C(1) << 7)) != 0u)
            ok = add_display_info(root);
    }
    if (ok)
        ok = set_plist_body(response, root);

cleanup:
    airplay_plist_free(root);
    if (!ok)
        return airplay_rtsp_response_set_status(response,
                                                strcmp(request->method, "GET") == 0 ? 500 : 501);
    return true;
}

static bool handle_server_info(AirPlayHandlers *handlers,
                               const AirPlayRtspRequest *request,
                               AirPlayRtspResponse *response)
{
    char body[1024];
    int written;

    if (strcmp(request->method, "GET") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    written = snprintf(
        body, sizeof(body),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>features</key><integer>%llu</integer>"
        "<key>macAddress</key><string>%s</string>"
        "<key>model</key><string>AppleTV3,2</string>"
        "<key>osBuildVersion</key><string>12B435</string>"
        "<key>protovers</key><string>1.0</string>"
        "<key>srcvers</key><string>220.68</string>"
        "<key>vv</key><integer>2</integer>"
        "<key>deviceid</key><string>%s</string>"
        "</dict></plist>\n",
        (unsigned long long)handlers->config.features,
        handlers->device_id, handlers->device_id);
    if (written <= 0 || (size_t)written >= sizeof(body))
        return airplay_rtsp_response_set_status(response, 500);
    return airplay_rtsp_response_set_body(response, body, (size_t)written,
                                           AIRPLAY_XML_PLIST_CONTENT_TYPE);
}

static bool handle_fairplay(AirPlayHandlerSession *context,
                            const AirPlayRtspRequest *request,
                            AirPlayRtspResponse *response)
{
    uint8_t output[AIRPLAY_FAIRPLAY_STAGE1_RESPONSE_SIZE];
    size_t output_size = 0u;
    AirPlayFairPlayResult result;

    if (strcmp(request->method, "POST") != 0 ||
        !content_type_is(request, AIRPLAY_OCTET_CONTENT_TYPE))
        return airplay_rtsp_response_set_status(response, 400);
    result = airplay_fairplay_setup(context->fairplay, request->body,
                                    request->body_length, output, sizeof(output),
                                    &output_size);
    AIRPLAY_TRACE("[airplay] endpoint=fp-setup bytes=%zu result=%s\n",
                  request->body_length, airplay_fairplay_result_name(result));
    if (result == AIRPLAY_FAIRPLAY_UNSUPPORTED)
        return airplay_rtsp_response_set_status(response, 501);
    if (result != AIRPLAY_FAIRPLAY_OK)
        return airplay_rtsp_response_set_status(response, 400);
    return airplay_rtsp_response_set_body(response, output, output_size,
                                          AIRPLAY_OCTET_CONTENT_TYPE);
}

static bool handle_fairplay_setup2(const AirPlayRtspRequest *request,
                                   AirPlayRtspResponse *response)
{
    unsigned version = request->body && request->body_length > 4u
                           ? request->body[4]
                           : 0u;

    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    (void)version;
    AIRPLAY_TRACE_SYNC(
        "[airplay-http-compat] endpoint=fp-setup2 bytes=%zu version=%u status=421\n",
        request->body_length, version);
    return airplay_rtsp_response_add_header(response, "Content-Type",
                                            AIRPLAY_PLIST_CONTENT_TYPE) &&
           airplay_rtsp_response_set_status(response, 421);
}

static bool set_property_known(const char *property)
{
    static const char *const known[] = {
        "actionAtItemEnd",
        "selectedMediaArray",
        "reverseEndTime",
        "forwardEndTime",
        "interstitialEvents",
        "textMarkupArray",
        "isInterestedInDateRange"};

    for (size_t index = 0u; index < sizeof(known) / sizeof(known[0]); ++index)
    {
        if (strcmp(property, known[index]) == 0)
            return true;
    }
    return false;
}

static bool handle_set_property(const AirPlayRtspRequest *request,
                                AirPlayRtspResponse *response)
{
    static const char prefix[] = "/setProperty?";
    static const char success[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>errorCode</key><integer>0</integer>"
        "</dict></plist>\n";
    const char *property;
    bool known;

    if (strcmp(request->method, "PUT") != 0 ||
        strncmp(request->uri, prefix, sizeof(prefix) - 1u) != 0)
        return airplay_rtsp_response_set_status(response, 405);
    property = request->uri + sizeof(prefix) - 1u;
    known = set_property_known(property);
    AIRPLAY_TRACE_SYNC(
        "[airplay-http-compat] endpoint=setProperty property=%s known=%u status=200\n",
        property[0] != '\0' ? property : "(empty)", known ? 1u : 0u);
    if (!known)
        return true;
    return airplay_rtsp_response_set_body(
        response, success, sizeof(success) - 1u,
        AIRPLAY_XML_PLIST_CONTENT_TYPE);
}

static bool handle_get_property(const AirPlayRtspRequest *request,
                                AirPlayRtspResponse *response)
{
    static const char prefix[] = "/getProperty?";
    static const char *const known[] = {
        "playbackErrorLog", "playbackAccessLog", "selectedMediaArray"};
    const char *property;

    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    property = request->uri + sizeof(prefix) - 1u;
    for (size_t index = 0u; index < sizeof(known) / sizeof(known[0]); ++index)
    {
        if (strcmp(property, known[index]) == 0)
        {
            AIRPLAY_TRACE_SYNC(
                "[airplay-http-compat] endpoint=getProperty property=%s "
                "known=1 status=200\n",
                property);
            return true;
        }
    }
    AIRPLAY_TRACE_SYNC(
        "[airplay-http-compat] endpoint=getProperty property=%s known=0 "
        "status=501\n",
        property[0] != '\0' ? property : "(empty)");
    return airplay_rtsp_response_set_status(response, 501);
}

static bool handle_audio_mode(const AirPlayRtspRequest *request,
                              AirPlayRtspResponse *response)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error;
    const char *mode = NULL;
    bool ok;

    ok = strcmp(request->method, "POST") == 0 &&
         content_type_is(request, AIRPLAY_PLIST_CONTENT_TYPE) &&
         airplay_plist_decode(request->body, request->body_length, &root,
                              &error) &&
         airplay_plist_type(root) == AIRPLAY_PLIST_TYPE_DICT;
    if (ok)
        mode = airplay_plist_get_string(
            airplay_plist_dict_get(root, "audioMode"));
    ok = ok && mode && mode[0] != '\0';
    if (ok)
        AIRPLAY_TRACE("[airplay] audio mode acknowledged\n");
    airplay_plist_free(root);
    return ok ? true : airplay_rtsp_response_set_status(response, 400);
}

static AirPlayPlistValue *decode_dict(const AirPlayRtspRequest *request)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error;

    if (!content_type_is(request, AIRPLAY_PLIST_CONTENT_TYPE) ||
        !airplay_plist_decode(request->body, request->body_length, &root, &error) ||
        airplay_plist_type(root) != AIRPLAY_PLIST_TYPE_DICT)
    {
        airplay_plist_free(root);
        return NULL;
    }
    return root;
}

static bool handle_announce(AirPlayHandlers *handlers,
                            AirPlayHandlerSession *context,
                            AirPlayRtspSession *session,
                            const AirPlayRtspRequest *request,
                            AirPlayRtspResponse *response)
{
    AirPlaySdp sdp;
    uint8_t key[16] = {0}, iv[16] = {0}, wrapped[72];
    size_t size = 0u;
    bool ok = false;

    if (session->state != AIRPLAY_RTSP_SESSION_CONNECTED || context->initial_setup)
        return airplay_rtsp_response_set_status(response, 455);
    if (!handlers->config.legacy_audio_open_callback)
        return airplay_rtsp_response_set_status(response, 501);
    if (!content_type_is(request, "application/sdp") ||
        !airplay_sdp_parse(request->body, request->body_length, &sdp))
        return airplay_rtsp_response_set_status(response, 400);
    if (sdp.iv[0] &&
        (!airplay_legacy_base64_decode(sdp.iv, iv, sizeof(iv), &size) ||
         size != sizeof(iv)))
        goto cleanup;
    if (sdp.rsa_key[0])
    {
        if (!airplay_legacy_rsa_unwrap(sdp.rsa_key, key))
            goto cleanup;
    }
    else if (sdp.fairplay_key[0])
    {
        if (!airplay_legacy_base64_decode(sdp.fairplay_key, wrapped,
                                         sizeof(wrapped), &size) ||
            size != sizeof(wrapped))
            goto cleanup;
        if (handlers->config.unwrap_key_callback)
            ok = handlers->config.unwrap_key_callback(
                wrapped, key, handlers->config.callback_user_data);
        else
            ok = airplay_fairplay_unwrap_key(context->fairplay, wrapped, key) ==
                 AIRPLAY_FAIRPLAY_OK;
        if (!ok)
            goto cleanup;
    }
    context->legacy_format = sdp.format;
    context->legacy_encrypted = sdp.iv[0] != '\0';
    context->legacy_announced = true;
    memcpy(context->aes_key, key, sizeof(key));
    memcpy(context->aes_iv, iv, sizeof(iv));
    ok = true;
cleanup:
    airplay_crypto_secure_zero(key, sizeof(key));
    airplay_crypto_secure_zero(iv, sizeof(iv));
    airplay_crypto_secure_zero(&sdp, sizeof(sdp));
    return ok ? true : airplay_rtsp_response_set_status(response, 400);
}

static bool parse_uint(const char *text, uint32_t limit, uint32_t *value)
{
    char *end;
    unsigned long parsed;
    if (!text || !isdigit((unsigned char)*text))
        return false;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno || *end || parsed > limit)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

/* Iterate semicolon-separated transport/RTP parameters without strtok state. */
static bool next_parameter(const char **cursor, char *value, size_t capacity)
{
    const char *start = *cursor;
    const char *end;
    size_t length;
    while (*start == ';' || *start == ' ' || *start == '\t')
        ++start;
    if (!*start)
    {
        *cursor = start;
        value[0] = '\0';
        return true;
    }
    end = strchr(start, ';');
    if (!end)
        end = start + strlen(start);
    *cursor = end;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        --end;
    length = (size_t)(end - start);
    if (!length || length >= capacity)
        return false;
    memcpy(value, start, length);
    value[length] = '\0';
    return true;
}

static bool handle_legacy_setup(AirPlayHandlers *handlers,
                                 AirPlayHandlerSession *context,
                                 AirPlayRtspSession *session,
                                 const AirPlayRtspRequest *request,
                                 AirPlayRtspResponse *response)
{
    const char *cursor = airplay_rtsp_request_header(request, "Transport");
    char token[256], transport_response[256], session_id[32];
    uint32_t peer_control = 0u, peer_timing = 0u;
    uint16_t data_port = 0u, control_port = 0u, timing_port = 0u;
    bool unicast = false, prepared = false;
    AirPlayTransportSetup setup = {0};

    if (!context->legacy_announced || context->initial_setup ||
        session->state != AIRPLAY_RTSP_SESSION_CONNECTED)
        return airplay_rtsp_response_set_status(response, 455);
    if (!cursor || request->body_length ||
        !next_parameter(&cursor, token, sizeof(token)) ||
        strcasecmp(token, "RTP/AVP/UDP") != 0)
        return airplay_rtsp_response_set_status(response, 461);
    while (*cursor)
    {
        if (!next_parameter(&cursor, token, sizeof(token)))
            return airplay_rtsp_response_set_status(response, 461);
        if (!token[0])
            break;
        if (strcasecmp(token, "unicast") == 0)
            unicast = true;
        else if (strncasecmp(token, "control_port=", 13u) == 0)
        {
            if (peer_control || !parse_uint(token + 13u, UINT16_MAX, &peer_control) ||
                !peer_control)
                return airplay_rtsp_response_set_status(response, 461);
        }
        else if (strncasecmp(token, "timing_port=", 12u) == 0)
        {
            if (peer_timing || !parse_uint(token + 12u, UINT16_MAX, &peer_timing) ||
                !peer_timing)
                return airplay_rtsp_response_set_status(response, 461);
        }
        else if (strcasecmp(token, "mode=record") != 0 &&
                 strcasecmp(token, "mode=\"record\"") != 0 &&
                 strcasecmp(token, "interleaved=0-1") != 0)
            return airplay_rtsp_response_set_status(response, 461);
    }
    if (!unicast || !peer_control || !peer_timing ||
        !handlers->config.transport_prepare_callback ||
        !handlers->config.legacy_audio_open_callback)
        return airplay_rtsp_response_set_status(response, 461);
    setup.session_id = session->logical_session_id;
    setup.peer_ipv4_address = session->peer_ipv4_address;
    setup.peer_timing_port = (uint16_t)peer_timing;
    setup.uses_ntp_timing = true;
    /* Transport stores key bytes even when the RTP stream is unencrypted. */
    setup.key = context->aes_key;
    setup.iv = context->aes_iv;
    prepared = handlers->config.transport_prepare_callback(
        &setup, &timing_port, handlers->config.callback_user_data);
    if (!prepared || !timing_port ||
        !handlers->config.legacy_audio_open_callback(
            session->logical_session_id,
            context->legacy_encrypted ? context->aes_key : NULL,
            context->legacy_encrypted ? context->aes_iv : NULL,
            &context->legacy_format, session->peer_ipv4_address,
            (uint16_t)peer_control, &data_port, &control_port,
            handlers->config.callback_user_data) || !data_port || !control_port)
        goto failure;
    snprintf(transport_response, sizeof(transport_response),
             "RTP/AVP/UDP;unicast;mode=record;server_port=%u;control_port=%u;timing_port=%u",
             data_port, control_port, timing_port);
    snprintf(session_id, sizeof(session_id), "%llu",
             (unsigned long long)session->logical_session_id);
    if (!airplay_rtsp_response_add_header(response, "Transport", transport_response) ||
        !airplay_rtsp_response_add_header(response, "Session", session_id))
        goto failure;
    context->initial_setup = true;
    context->audio_setup = true;
    context->diagnostic_initial_setup_ok = true;
    context->diagnostic_audio_setup_ok = true;
    session->state = AIRPLAY_RTSP_SESSION_SETUP;
    return true;
failure:
    if (prepared && handlers->config.mirror_stop_callback)
        handlers->config.mirror_stop_callback(
            session->logical_session_id, handlers->config.callback_user_data);
    return airplay_rtsp_response_set_status(response, 461);
}

static bool derive_session_key(AirPlayHandlers *handlers,
                               AirPlayHandlerSession *context,
                               const AirPlayRtspSession *session,
                               const uint8_t wrapped_key[72],
                               uint8_t output[16])
{
    uint8_t unwrapped[16];
    uint8_t shared[32];
    uint8_t input[48];
    uint8_t hash[64];
    bool ok;

    if (handlers->config.unwrap_key_callback)
        ok = handlers->config.unwrap_key_callback(wrapped_key, unwrapped,
                                                  handlers->config.callback_user_data);
    else
        ok = airplay_fairplay_unwrap_key(context->fairplay, wrapped_key, unwrapped) ==
             AIRPLAY_FAIRPLAY_OK;
    if (!ok)
        goto cleanup;
    if (handlers->config.shared_secret_callback)
    {
        if (!handlers->config.shared_secret_callback(session, shared,
                                                     handlers->config.callback_user_data))
        {
            ok = false;
            goto cleanup;
        }
        memcpy(input, unwrapped, sizeof(unwrapped));
        memcpy(input + sizeof(unwrapped), shared, sizeof(shared));
        /* Paired media uses SHA-512(key || ECDH secret), truncated to 16 bytes. */
        ok = airplay_crypto_sha512(input, sizeof(input), hash);
        if (ok)
            memcpy(output, hash, 16u);
    }
    else
        memcpy(output, unwrapped, 16u);

    if (ok)
        AIRPLAY_TRACE(
            "[airplay-crypto] session=%llu media-key kdf=%s\n",
            (unsigned long long)session->id,
            handlers->config.shared_secret_callback ? "sha512-truncate16"
                                                     : "unwrapped");

cleanup:
    airplay_crypto_secure_zero(unwrapped, sizeof(unwrapped));
    airplay_crypto_secure_zero(shared, sizeof(shared));
    airplay_crypto_secure_zero(input, sizeof(input));
    airplay_crypto_secure_zero(hash, sizeof(hash));
    return ok;
}

static bool setup_initial(AirPlayHandlers *handlers,
                          AirPlayHandlerSession *context,
                          AirPlayRtspSession *session,
                          const AirPlayPlistValue *root,
                          AirPlayPlistValue *response_root)
{
    const uint8_t *wrapped_key;
    const uint8_t *iv;
    const char *timing_protocol;
    size_t wrapped_size;
    size_t iv_size;
    uint64_t peer_timing_port = 0u;
    AirPlayTransportSetup transport = {0};
    uint16_t timing_port = 0u;

    wrapped_key = airplay_plist_get_data(airplay_plist_dict_get(root, "ekey"),
                                         &wrapped_size);
    iv = airplay_plist_get_data(airplay_plist_dict_get(root, "eiv"), &iv_size);
    if (!wrapped_key || wrapped_size != AIRPLAY_FAIRPLAY_WRAPPED_KEY_SIZE ||
        !iv || iv_size != sizeof(context->aes_iv) || context->initial_setup ||
        !derive_session_key(handlers, context, session, wrapped_key, context->aes_key))
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=transport stage=format\n",
            (unsigned long long)session->id);
        return false;
    }
    memcpy(context->aes_iv, iv, sizeof(context->aes_iv));
    timing_protocol = airplay_plist_get_string(
        airplay_plist_dict_get(root, "timingProtocol"));
    if (airplay_plist_get_uint(airplay_plist_dict_get(root, "timingPort"),
                               &peer_timing_port) &&
        peer_timing_port > UINT16_MAX)
        return false;
    transport.session_id = session->logical_session_id;
    transport.peer_ipv4_address = session->peer_ipv4_address;
    transport.peer_timing_port = (uint16_t)peer_timing_port;
    transport.uses_ntp_timing = peer_timing_port != 0u &&
                                (!timing_protocol ||
                                 strcmp(timing_protocol, "NTP") == 0);
    transport.key = context->aes_key;
    transport.iv = context->aes_iv;
    if (handlers->config.transport_prepare_callback &&
        !handlers->config.transport_prepare_callback(&transport, &timing_port,
                                                     handlers->config.callback_user_data))
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=timing stage=runtime-state\n",
            (unsigned long long)session->id);
        return false;
    }
    context->initial_setup = true;
    if (!dict_set(response_root, "timingPort",
                  airplay_plist_new_uint(timing_port)) ||
        !dict_set(response_root, "eventPort", airplay_plist_new_uint(0u)))
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=transport stage=response-plist\n",
            (unsigned long long)session->id);
        return false;
    }
    return true;
}

static bool setup_streams(AirPlayHandlers *handlers,
                          AirPlayHandlerSession *context,
                          AirPlayRtspSession *session,
                          const AirPlayPlistValue *streams,
                          AirPlayPlistValue *response_root)
{
    AirPlayPlistValue *response_streams;
    size_t count;

    if (!context->initial_setup || airplay_plist_type(streams) != AIRPLAY_PLIST_TYPE_ARRAY)
        return false;
    count = airplay_plist_array_size(streams);
    if (count == 0u || count > 4u)
        return false;
    response_streams = airplay_plist_new_array();
    if (!response_streams)
        return false;
    for (unsigned pass = 0u; pass < 2u; ++pass)
    {
        uint64_t expected_type = pass == 0u ? 110u : 96u;

        for (size_t index = 0u; index < count; ++index)
        {
            const AirPlayPlistValue *stream = airplay_plist_array_get(streams, index);
            AirPlayPlistValue *response_stream = NULL;
            uint64_t type;
            uint64_t value;
            uint16_t data_port = 0u;

            if (!airplay_plist_get_uint(airplay_plist_dict_get(stream, "type"), &type) ||
                (type != 110u && type != 96u))
                goto failure;
            if (type != expected_type)
                continue;
            if (type == 110u)
            {
                if (!airplay_plist_get_uint(
                        airplay_plist_dict_get(stream, "streamConnectionID"),
                        &value) ||
                    value == 0u || context->mirror_setup ||
                    !handlers->config.mirror_open_callback)
                {
                    AIRPLAY_OBSERVE(
                        "[airplay-setup-failure] session=%llu stream=video stage=format\n",
                        (unsigned long long)session->id);
                    goto failure;
                }
                AIRPLAY_OBSERVE(
                    "[airplay-setup] session=%llu stream=video connection_id=%llu stage=negotiate\n",
                    (unsigned long long)session->id,
                    (unsigned long long)value);
                if (!handlers->config.mirror_open_callback(
                        session->logical_session_id, context->aes_key, value, &data_port,
                        handlers->config.callback_user_data) ||
                    data_port == 0u)
                    goto failure;
                context->stream_connection_id = value;
                context->mirror_setup = true;
                response_stream = airplay_plist_new_dict();
            }
            else
            {
                uint16_t control_port = 0u;
                uint8_t compression_type;
                uint16_t samples_per_frame;
                uint32_t sample_rate = 44100u;

                if (context->audio_setup ||
                    !handlers->config.audio_open_callback ||
                    !airplay_plist_get_uint(
                        airplay_plist_dict_get(stream, "ct"), &value) ||
                    value > UINT8_MAX)
                {
                    AIRPLAY_OBSERVE(
                        "[airplay-setup-failure] session=%llu stream=audio stage=format field=ct\n",
                        (unsigned long long)session->id);
                    goto failure;
                }
                compression_type = (uint8_t)value;
                if (!airplay_plist_get_uint(airplay_plist_dict_get(stream, "spf"),
                                            &value) || value > UINT16_MAX)
                {
                    AIRPLAY_OBSERVE(
                        "[airplay-setup-failure] session=%llu stream=audio stage=format field=spf ct=%u\n",
                        (unsigned long long)session->id, compression_type);
                    goto failure;
                }
                samples_per_frame = (uint16_t)value;
                if (airplay_plist_get_uint(airplay_plist_dict_get(stream, "sr"),
                                           &value))
                {
                    if (value > UINT32_MAX)
                    {
                        AIRPLAY_OBSERVE(
                            "[airplay-setup-failure] session=%llu stream=audio stage=format field=sr ct=%u spf=%u\n",
                            (unsigned long long)session->id,
                            compression_type, samples_per_frame);
                        goto failure;
                    }
                    sample_rate = (uint32_t)value;
                }
                AIRPLAY_OBSERVE(
                    "[airplay-setup] session=%llu stream=audio ct=%u spf=%u sr=%u stage=negotiate\n",
                    (unsigned long long)session->id, compression_type,
                    samples_per_frame, sample_rate);
                if (!handlers->config.audio_open_callback(
                        session->logical_session_id, context->aes_key, context->aes_iv,
                        compression_type, samples_per_frame, sample_rate,
                        &data_port, &control_port,
                        handlers->config.callback_user_data) ||
                    data_port == 0u || control_port == 0u)
                    goto failure;
                context->audio_setup = true;
                response_stream = airplay_plist_new_dict();
                if (!response_stream ||
                    !dict_set(response_stream, "controlPort",
                              airplay_plist_new_uint(control_port)))
                {
                    AIRPLAY_OBSERVE(
                        "[airplay-setup-failure] session=%llu stream=audio stage=response-plist\n",
                        (unsigned long long)session->id);
                    airplay_plist_free(response_stream);
                    goto failure;
                }
            }
            if (!response_stream ||
                !dict_set(response_stream, "dataPort",
                          airplay_plist_new_uint(data_port)) ||
                !dict_set(response_stream, "type", airplay_plist_new_uint(type)))
            {
                AIRPLAY_OBSERVE(
                    "[airplay-setup-failure] session=%llu stream=%s stage=response-plist\n",
                    (unsigned long long)session->id,
                    type == 110u ? "video" : "audio");
                airplay_plist_free(response_stream);
                goto failure;
            }
            if (!array_append(response_streams, response_stream))
                goto failure;
        }
    }
    if (!dict_set(response_root, "streams", response_streams))
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=all stage=response-plist\n",
            (unsigned long long)session->id);
        return false;
    }
    return true;

failure:
    airplay_plist_free(response_streams);
    return false;
}

static void trace_setup_metadata(const AirPlayRtspSession *session,
                                 const AirPlayPlistValue *root,
                                 const AirPlayPlistValue *streams,
                                 bool has_initial)
{
    const char *timing_protocol = airplay_plist_get_string(
        airplay_plist_dict_get(root, "timingProtocol"));
    const char *timing_label = "missing";
    uint64_t timing_port = 0u;
    bool remote_control_only = false;
    bool has_mirror = false;
    bool has_audio = false;
    bool audio_using_screen = false;
    bool audio_is_media = false;
    uint64_t audio_ct = 0u;
    uint64_t audio_spf = 0u;
    uint64_t audio_sr = 0u;
    uint64_t audio_format = 0u;
    unsigned audio_fields = 0u;
    size_t stream_count = airplay_plist_array_size(streams);

    if (timing_protocol)
    {
        if (strcmp(timing_protocol, "NTP") == 0)
            timing_label = "NTP";
        else if (strcmp(timing_protocol, "None") == 0)
            timing_label = "None";
        else
            timing_label = "other";
    }
    (void)airplay_plist_get_uint(airplay_plist_dict_get(root, "timingPort"),
                                 &timing_port);
    (void)airplay_plist_get_bool(
        airplay_plist_dict_get(root, "isRemoteControlOnly"),
        &remote_control_only);
    for (size_t index = 0u; index < stream_count; ++index)
    {
        const AirPlayPlistValue *stream =
            airplay_plist_array_get(streams, index);
        uint64_t type = 0u;

        if (!airplay_plist_get_uint(airplay_plist_dict_get(stream, "type"),
                                    &type))
            continue;
        has_mirror = has_mirror || type == 110u;
        has_audio = has_audio || type == 96u;
        if (type != 96u)
            continue;
        if (airplay_plist_get_uint(airplay_plist_dict_get(stream, "ct"),
                                   &audio_ct))
            audio_fields |= 1u << 0;
        if (airplay_plist_get_uint(airplay_plist_dict_get(stream, "spf"),
                                   &audio_spf))
            audio_fields |= 1u << 1;
        if (airplay_plist_get_uint(airplay_plist_dict_get(stream, "sr"),
                                   &audio_sr))
            audio_fields |= 1u << 2;
        if (airplay_plist_get_uint(
                airplay_plist_dict_get(stream, "audioFormat"),
                &audio_format))
            audio_fields |= 1u << 3;
        if (airplay_plist_get_bool(
                airplay_plist_dict_get(stream, "usingScreen"),
                &audio_using_screen))
            audio_fields |= 1u << 4;
        if (airplay_plist_get_bool(airplay_plist_dict_get(stream, "isMedia"),
                                   &audio_is_media))
            audio_fields |= 1u << 5;
    }
    (void)session;
    (void)has_initial;
    (void)timing_label;
    (void)timing_port;
    (void)remote_control_only;
    (void)has_mirror;
    (void)has_audio;
    (void)audio_using_screen;
    (void)audio_is_media;
    (void)audio_ct;
    (void)audio_spf;
    (void)audio_sr;
    (void)audio_format;
    (void)audio_fields;
    AIRPLAY_TRACE_SYNC(
        "[airplay] t_ms=%llu setup metadata session=%llu keys=%zu "
        "initial=%u timing=%s timing_port=%llu remote_control=%u "
        "streams=%zu mirror=%u audio=%u audio_fields=0x%x "
        "ct=%llu spf=%llu sr=%llu format=%llu using_screen=%u is_media=%u\n",
        (unsigned long long)AIRPLAY_TRACE_NOW_MS(),
        (unsigned long long)session->id, airplay_plist_dict_size(root),
        has_initial ? 1u : 0u, timing_label,
        (unsigned long long)timing_port, remote_control_only ? 1u : 0u,
        stream_count, has_mirror ? 1u : 0u, has_audio ? 1u : 0u,
        audio_fields, (unsigned long long)audio_ct,
        (unsigned long long)audio_spf, (unsigned long long)audio_sr,
        (unsigned long long)audio_format, audio_using_screen ? 1u : 0u,
        audio_is_media ? 1u : 0u);
}

static bool handle_setup(AirPlayHandlers *handlers,
                         AirPlayHandlerSession *context,
                         AirPlayRtspSession *session,
                         const AirPlayRtspRequest *request,
                         AirPlayRtspResponse *response)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistValue *response_root = NULL;
    const AirPlayPlistValue *streams;
    AirPlayHandlerPhase phase_before;
    bool has_initial;
    bool was_recording;
    bool audio_record_failed = false;
    bool ok = false;

    phase_before = airplay_handlers_session_phase(session);
    if (session->state != AIRPLAY_RTSP_SESSION_CONNECTED &&
        session->state != AIRPLAY_RTSP_SESSION_SETUP &&
        session->state != AIRPLAY_RTSP_SESSION_RECORDING)
    {
        context->diagnostic_last_status = 455;
        return airplay_rtsp_response_set_status(response, 455);
    }
    was_recording = session->state == AIRPLAY_RTSP_SESSION_RECORDING;
    root = decode_dict(request);
    response_root = airplay_plist_new_dict();
    if (!root || !response_root)
        goto cleanup;
    has_initial = airplay_plist_dict_get(root, "ekey") || airplay_plist_dict_get(root, "eiv");
    streams = airplay_plist_dict_get(root, "streams");
    context->diagnostic_initial_setup_seen |= has_initial;
    diagnostic_note_setup_streams(context, streams);
    trace_setup_metadata(session, root, streams, has_initial);
    if (!has_initial && !streams)
        goto cleanup;
    if (has_initial)
    {
        if (!setup_initial(handlers, context, session, root, response_root))
            goto cleanup;
        context->diagnostic_initial_setup_ok = true;
    }
    if (streams && !setup_streams(handlers, context, session, streams, response_root))
        goto cleanup;
    if (!set_plist_body(response, response_root))
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=all stage=response-plist\n",
            (unsigned long long)session->id);
        goto cleanup;
    }
    session->state = was_recording ? AIRPLAY_RTSP_SESSION_RECORDING
                                   : AIRPLAY_RTSP_SESSION_SETUP;
    if (context->record_requested)
    {
        if (context->mirror_setup && !context->recording_started)
        {
            context->recording_started = true;
            if (handlers->config.media_record_callback)
                handlers->config.media_record_callback(
                    session->logical_session_id,
                    handlers->config.callback_user_data);
        }
        else if (context->audio_setup &&
                 !context->audio_recording_started)
        {
            context->audio_recording_started = true;
            if (!handlers->config.audio_record_callback ||
                !handlers->config.audio_record_callback(
                    session->logical_session_id,
                    handlers->config.callback_user_data))
                audio_record_failed = true;
        }
    }
    ok = handler_state_valid(session, context) && !audio_record_failed;
    context->diagnostic_mirror_setup_ok |= ok && context->mirror_setup;
    context->diagnostic_audio_setup_ok |= ok && context->audio_setup;

cleanup:
    airplay_plist_free(root);
    airplay_plist_free(response_root);
    AIRPLAY_TRACE("[airplay] session=%llu method=SETUP initial=%u mirror=%u record=%u result=%s\n",
                  (unsigned long long)session->id, context->initial_setup ? 1u : 0u,
                  context->mirror_setup ? 1u : 0u,
                  context->record_requested ? 1u : 0u,
                  ok ? "ok" : "failed");
    context->diagnostic_last_status = ok ? 200 : 461;
    if (!ok)
    {
        if (context->initial_setup && handlers->config.mirror_stop_callback)
            handlers->config.mirror_stop_callback(session->logical_session_id,
                                                  handlers->config.callback_user_data);
        context->initial_setup = false;
        context->mirror_setup = false;
        context->audio_setup = false;
        context->record_requested = false;
        context->recording_started = false;
        context->audio_recording_started = false;
        context->stream_connection_id = 0u;
        session->state = AIRPLAY_RTSP_SESSION_CONNECTED;
        airplay_crypto_secure_zero(context->aes_key, sizeof(context->aes_key));
        airplay_crypto_secure_zero(context->aes_iv, sizeof(context->aes_iv));
        trace_phase_transition(session, phase_before, "setup", false);
        return airplay_rtsp_response_set_status(response, 461);
    }
    trace_phase_transition(session, phase_before, "setup", true);
    return true;
}

static bool handle_record(AirPlayHandlers *handlers,
                          AirPlayHandlerSession *context,
                          AirPlayRtspSession *session,
                          AirPlayRtspResponse *response)
{
    AirPlayHandlerPhase phase_before = airplay_handlers_session_phase(session);

    context->diagnostic_record_seen = true;
    if ((session->state != AIRPLAY_RTSP_SESSION_SETUP &&
         session->state != AIRPLAY_RTSP_SESSION_RECORDING) ||
        !context->initial_setup)
    {
        context->diagnostic_last_status = 455;
        return airplay_rtsp_response_set_status(response, 455);
    }
    context->record_requested = true;
    session->state = AIRPLAY_RTSP_SESSION_RECORDING;
    if (context->mirror_setup && !context->recording_started)
    {
        context->recording_started = true;
        if (handlers->config.media_record_callback)
            handlers->config.media_record_callback(
                session->logical_session_id, handlers->config.callback_user_data);
    }
    else if (context->audio_setup && !context->audio_recording_started)
    {
        context->audio_recording_started = true;
        if (!handlers->config.audio_record_callback ||
            !handlers->config.audio_record_callback(
                session->logical_session_id,
                handlers->config.callback_user_data))
        {
            AIRPLAY_OBSERVE(
                "[airplay-setup-failure] session=%llu stream=audio "
                "stage=record callback=failed\n",
                (unsigned long long)session->logical_session_id);
            if (context->initial_setup &&
                handlers->config.mirror_stop_callback)
                handlers->config.mirror_stop_callback(
                    session->logical_session_id,
                    handlers->config.callback_user_data);
            context->initial_setup = false;
            context->mirror_setup = false;
            context->audio_setup = false;
            context->record_requested = false;
            context->recording_started = false;
            context->audio_recording_started = false;
            context->stream_connection_id = 0u;
            session->state = AIRPLAY_RTSP_SESSION_CONNECTED;
            airplay_crypto_secure_zero(context->aes_key,
                                       sizeof(context->aes_key));
            airplay_crypto_secure_zero(context->aes_iv,
                                       sizeof(context->aes_iv));
            trace_phase_transition(session, phase_before, "record", false);
            context->diagnostic_last_status = 461;
            return airplay_rtsp_response_set_status(response, 461);
        }
    }
    AIRPLAY_TRACE("[airplay] session=%llu method=RECORD mirror=%u deferred=%u\n",
                  (unsigned long long)session->id,
                  context->mirror_setup ? 1u : 0u,
                  context->recording_started ? 0u : 1u);
    context->diagnostic_record_ok = true;
    context->diagnostic_last_status = 200;
    trace_phase_transition(session, phase_before, "record", true);
    return airplay_rtsp_response_add_header(response, "Audio-Latency",
                                            context->legacy_announced ? "11025" : "0") &&
           airplay_rtsp_response_add_header(response, "Audio-Jack-Status",
                                            "connected; type=analog");
}

static bool handle_get_parameter(const AirPlayHandlerSession *context,
                                 const AirPlayRtspRequest *request,
                                 AirPlayRtspResponse *response)
{
    char volume[64];
    static const char needle[] = "volume";
    bool requests_volume = false;

    if (!request->body_length)
        return true; /* Empty GET_PARAMETER is an RTSP keepalive. */
    if (!content_type_is(request, AIRPLAY_TEXT_CONTENT_TYPE))
        return airplay_rtsp_response_set_status(response, 400);
    for (size_t index = 0u; index + sizeof(needle) - 1u <= request->body_length; ++index)
    {
        if (memcmp(request->body + index, needle, sizeof(needle) - 1u) == 0)
        {
            requests_volume = true;
            break;
        }
    }
    if (requests_volume)
    {
        int size = snprintf(volume, sizeof(volume), "volume: %.6f\r\n",
                            (double)context->volume_db);
        return size > 0 && (size_t)size < sizeof(volume) &&
               airplay_rtsp_response_set_body(response, volume, (size_t)size,
                                              AIRPLAY_TEXT_CONTENT_TYPE);
    }
    return true;
}

static bool valid_dmap(const uint8_t *body, size_t size)
{
    size_t offset = 0u;
    while (offset < size)
    {
        uint32_t length;
        if (size - offset < 8u)
            return false;
        length = ((uint32_t)body[offset + 4u] << 24u) |
                 ((uint32_t)body[offset + 5u] << 16u) |
                 ((uint32_t)body[offset + 6u] << 8u) | body[offset + 7u];
        offset += 8u;
        if (length > size - offset)
            return false;
        offset += length;
    }
    return true;
}

static bool handle_set_parameter(AirPlayHandlers *handlers,
                                 AirPlayHandlerSession *context,
                                 const AirPlayRtspSession *session,
                                 const AirPlayRtspRequest *request,
                                 AirPlayRtspResponse *response)
{
    const char *type = airplay_rtsp_request_header(request, "Content-Type");
    if (!request->body_length)
        return true;
    if (content_type_is(request, AIRPLAY_TEXT_CONTENT_TYPE))
    {
        char *body, *cursor;
        bool has_volume = false, valid = true;
        float volume = 0.0f;
        if (request->body_length > 16384u ||
            memchr(request->body, 0, request->body_length))
            return airplay_rtsp_response_set_status(response, 400);
        body = malloc(request->body_length + 1u);
        if (!body)
            return false;
        memcpy(body, request->body, request->body_length);
        body[request->body_length] = '\0';
        cursor = body;
        while (*cursor && valid)
        {
            char *line = cursor, *end;
            cursor += strcspn(cursor, "\r\n");
            if (*cursor)
            {
                *cursor++ = '\0';
                while (*cursor == '\r' || *cursor == '\n')
                    ++cursor;
            }
            if (strncmp(line, "volume:", 7u) == 0)
            {
                errno = 0;
                volume = strtof(line + 7u, &end);
                while (*end == ' ' || *end == '\t')
                    ++end;
                valid = !has_volume && end != line + 7u && !errno && !*end &&
                        isfinite(volume) &&
                        ((volume >= -30.0f && volume <= 0.0f) || volume == -144.0f);
                has_volume = true;
            }
            else if (strncmp(line, "progress:", 9u) == 0)
            {
                /* Progress timestamps wrap at 32 bits; ordering is not numeric. */
                unsigned long long start, current, finish;
                int used = 0;
                valid = sscanf(line + 9u, " %llu/%llu/%llu %n",
                               &start, &current, &finish, &used) == 3 &&
                        used && !line[9u + (size_t)used] &&
                        start <= UINT32_MAX && current <= UINT32_MAX &&
                        finish <= UINT32_MAX;
            }
            /* Future text parameters can be ignored without breaking playback. */
        }
        free(body);
        if (!valid)
            return airplay_rtsp_response_set_status(response, 400);
        if (has_volume)
        {
            if (handlers->config.audio_volume_callback &&
                !handlers->config.audio_volume_callback(
                    session->logical_session_id, volume,
                    handlers->config.callback_user_data))
                return airplay_rtsp_response_set_status(response, 500);
            context->volume_db = volume;
        }
    }
    else if (content_type_is(request, "application/x-dmap-tagged"))
    {
        if (!valid_dmap(request->body, request->body_length))
            return airplay_rtsp_response_set_status(response, 400);
    }
    else if (!content_type_is(request, "image/jpeg") &&
             !content_type_is(request, "image/png"))
        return airplay_rtsp_response_set_status(response, 415);
    if (handlers->config.audio_metadata_callback)
        handlers->config.audio_metadata_callback(
            session->logical_session_id, type, request->body,
            request->body_length, handlers->config.callback_user_data);
    return true;
}

static bool handle_flush(AirPlayHandlers *handlers,
                          AirPlayHandlerSession *context,
                          const AirPlayRtspSession *session,
                          const AirPlayRtspRequest *request,
                          AirPlayRtspResponse *response)
{
    const char *cursor = airplay_rtsp_request_header(request, "RTP-Info");
    uint32_t sequence = 0u, timestamp = 0u;
    bool has_sequence = false, has_timestamp = false;
    char parameter[128];
    if (session->state != AIRPLAY_RTSP_SESSION_RECORDING &&
        session->state != AIRPLAY_RTSP_SESSION_SETUP)
        return airplay_rtsp_response_set_status(response, 455);
    while (cursor && *cursor)
    {
        if (!next_parameter(&cursor, parameter, sizeof(parameter)))
            return airplay_rtsp_response_set_status(response, 400);
        if (!parameter[0])
            break;
        if (strncmp(parameter, "seq=", 4u) == 0)
        {
            if (has_sequence || !parse_uint(parameter + 4u, UINT16_MAX, &sequence))
                return airplay_rtsp_response_set_status(response, 400);
            has_sequence = true;
        }
        else if (strncmp(parameter, "rtptime=", 8u) == 0)
        {
            if (has_timestamp || !parse_uint(parameter + 8u, UINT32_MAX, &timestamp))
                return airplay_rtsp_response_set_status(response, 400);
            has_timestamp = true;
        }
        else
            return airplay_rtsp_response_set_status(response, 400);
    }
    if (context->audio_setup && handlers->config.audio_flush_callback &&
        !handlers->config.audio_flush_callback(
            session->logical_session_id, has_sequence, (uint16_t)sequence,
            has_timestamp, timestamp, handlers->config.callback_user_data))
        return airplay_rtsp_response_set_status(response, 500);
    return true;
}

static bool handle_reverse(const AirPlayRtspRequest *request,
                           AirPlayRtspResponse *response)
{
    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    if (strcmp(request->protocol, "HTTP/1.1") != 0)
        return airplay_rtsp_response_set_status(response, 400);
    if (!airplay_rtsp_response_set_status(response, 101) ||
        !airplay_rtsp_response_add_header(response, "Connection", "Upgrade") ||
        !airplay_rtsp_response_add_header(response, "Upgrade", "PTTH/1.0"))
    {
        return false;
    }
    response->register_reverse_connection = true;
    return true;
}

static bool handle_teardown(AirPlayHandlers *handlers,
                            AirPlayHandlerSession *context,
                            AirPlayRtspSession *session,
                            const AirPlayRtspRequest *request,
                            AirPlayRtspResponse *response)
{
    AirPlayHandlerPhase phase_before = airplay_handlers_session_phase(session);
    bool video = false;
    bool audio = false;
    bool scoped = false;

    if (request->body_length != 0u)
    {
        AirPlayPlistValue *root = decode_dict(request);
        const AirPlayPlistValue *streams = airplay_plist_dict_get(root, "streams");
        bool valid = root && (!streams ||
            airplay_plist_type(streams) == AIRPLAY_PLIST_TYPE_ARRAY);
        size_t count = airplay_plist_array_size(streams);

        scoped = count != 0u;
        for (size_t index = 0u; valid && index < count; ++index)
        {
            uint64_t type = 0u;
            valid = airplay_plist_get_uint(airplay_plist_dict_get(
                airplay_plist_array_get(streams, index), "type"), &type);
            if (!valid)
                break;
            video |= type == 110u;
            audio |= type == 96u;
            AIRPLAY_OBSERVE(
                "[airplay-teardown] session=%llu selector=%llu\n",
                (unsigned long long)session->id, (unsigned long long)type);
        }
        airplay_plist_free(root);
        if (!valid)
            return airplay_rtsp_response_set_status(response, 400);
    }

    context->diagnostic_teardown_seen = true;
    if (session->state == AIRPLAY_RTSP_SESSION_CLOSED)
    {
        context->diagnostic_last_status = 455;
        return airplay_rtsp_response_set_status(response, 455);
    }
    AIRPLAY_OBSERVE(
        "[airplay-teardown] session=%llu scope=%s video=%u audio=%u active_video=%u active_audio=%u\n",
        (unsigned long long)session->id, scoped ? "streams" : "session",
        video ? 1u : 0u, audio ? 1u : 0u,
        context->mirror_setup ? 1u : 0u, context->audio_setup ? 1u : 0u);
    if (scoped)
    {
        bool stop_video = video && context->mirror_setup;
        bool stop_audio = audio && context->audio_setup;
        if ((stop_video || stop_audio) &&
            (!handlers->config.stream_stop_callback ||
             !handlers->config.stream_stop_callback(
                 session->logical_session_id, stop_video, stop_audio,
                 handlers->config.callback_user_data)))
            return airplay_rtsp_response_set_status(response, 500);
        if (video)
        {
            context->mirror_setup = false;
            context->recording_started = false;
            context->stream_connection_id = 0u;
        }
        if (audio)
        {
            context->audio_setup = false;
            context->audio_recording_started = false;
        }
        /* Stream removal is not a transport/pairing close. RECORD survives. */
        context->diagnostic_last_status = 200;
        trace_phase_transition(session, phase_before, "stream-teardown", true);
        AIRPLAY_OBSERVE(
            "[airplay-teardown] session=%llu result=streams-removed remaining_video=%u remaining_audio=%u transport=retained\n",
            (unsigned long long)session->id, context->mirror_setup ? 1u : 0u,
            context->audio_setup ? 1u : 0u);
        return true;
    }
    if (context->initial_setup && handlers->config.mirror_stop_callback)
        handlers->config.mirror_stop_callback(session->logical_session_id,
                                              handlers->config.callback_user_data);
    context->mirror_setup = false;
    context->audio_setup = false;
    context->initial_setup = false;
    context->record_requested = false;
    context->recording_started = false;
    context->audio_recording_started = false;
    airplay_crypto_secure_zero(context->aes_key, sizeof(context->aes_key));
    airplay_crypto_secure_zero(context->aes_iv, sizeof(context->aes_iv));
    session->state = AIRPLAY_RTSP_SESSION_CLOSED;
    response->close_connection = true;
    context->diagnostic_last_status = 200;
    trace_phase_transition(session, phase_before, "teardown", true);
    return true;
}

bool airplay_handlers_route(AirPlayRtspSession *session,
                            const AirPlayRtspRequest *request,
                            AirPlayRtspResponse *response,
                            void *user_data)
{
    AirPlayHandlers *handlers = user_data;
    AirPlayHandlerSession *context;

    if (!handlers || !session || !request || !response)
        return false;
    context = session_context(session);
    if (!context)
        return false;
    {
        const char *challenge = airplay_rtsp_request_header(request, "Apple-Challenge");
        if (challenge)
        {
            char challenge_response[345];
            if (!airplay_legacy_rsa_challenge(
                    challenge, session->local_ipv4_address, handlers->device_id,
                    challenge_response))
                return airplay_rtsp_response_set_status(response, 400);
            if (!airplay_rtsp_response_add_header(response, "Apple-Response",
                                                  challenge_response))
                return false;
        }
    }
    if (!handler_state_valid(session, context))
    {
        AIRPLAY_TRACE_WARN(
            "[airplay-phase] connection=%llu logical=%llu event=validate phase=%s result=invalid\n",
            (unsigned long long)session->id,
            (unsigned long long)session->logical_session_id,
            airplay_handler_phase_name(handler_phase(session, context)));
        response->close_connection = true;
        return airplay_rtsp_response_set_status(response, 500);
    }
    AIRPLAY_TRACE("[airplay] session=%llu request=%u method=%s uri=%s bytes=%zu state=%s\n",
                  (unsigned long long)session->id, session->request_count,
                  request->method, request->uri, request->body_length,
                  airplay_rtsp_session_state_name(session->state));
    if (strcmp(request->uri, "/reverse") == 0)
        return handle_reverse(request, response);
    if (handlers->config.remote_video)
    {
        bool handled = false;
        bool play_request = strcmp(request->uri, "/play") == 0;
        bool routed;

        context->diagnostic_remote_play_seen |= play_request;
        routed = airplay_remote_video_route(handlers->config.remote_video,
                                            session->logical_session_id,
                                            request, response, &handled);
        if (play_request)
        {
            context->diagnostic_remote_play_ok |=
                routed && handled && response->status_code < 400;
            context->diagnostic_last_status = response->status_code;
            AIRPLAY_TRACE_SYNC(
                "[airplay-flow] connection=%llu logical=%llu "
                "stage=remote-play result=%s status=%d\n",
                (unsigned long long)session->id,
                (unsigned long long)session->logical_session_id,
                context->diagnostic_remote_play_ok ? "accepted" : "rejected",
                response->status_code);
        }
        if (!routed)
            return false;
        if (handled)
            return true;
    }
    if (strcmp(request->method, "OPTIONS") == 0)
        return airplay_rtsp_response_add_header(
            response, "Public",
            "ANNOUNCE, SETUP, RECORD, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, GET, POST");
    if (strcmp(request->uri, "/server-info") == 0)
        return handle_server_info(handlers, request, response);
    if (strcmp(request->uri, "/info") == 0)
        return handle_info(handlers, request, response);
    if (strcmp(request->uri, "/fp-setup") == 0)
        return handle_fairplay(context, request, response);
    if (strcmp(request->uri, "/fp-setup2") == 0)
        return handle_fairplay_setup2(request, response);
    if (strncmp(request->uri, "/setProperty?", 13u) == 0)
        return handle_set_property(request, response);
    if (strncmp(request->uri, "/getProperty?", 13u) == 0)
        return handle_get_property(request, response);
    if (strcmp(request->uri, "/feedback") == 0 && strcmp(request->method, "POST") == 0)
        return true;
    if (strcmp(request->uri, "/audioMode") == 0)
        return handle_audio_mode(request, response);
    if (strcmp(request->method, "ANNOUNCE") == 0)
        return handle_announce(handlers, context, session, request, response);
    if (strcmp(request->method, "SETUP") == 0)
    {
        if (context->legacy_announced)
            return handle_legacy_setup(handlers, context, session, request, response);
        return handle_setup(handlers, context, session, request, response);
    }
    if (strcmp(request->method, "RECORD") == 0)
        return handle_record(handlers, context, session, response);
    if (strcmp(request->method, "GET_PARAMETER") == 0)
    {
        if (session->state != AIRPLAY_RTSP_SESSION_SETUP &&
            session->state != AIRPLAY_RTSP_SESSION_RECORDING)
            return airplay_rtsp_response_set_status(response, 455);
        return handle_get_parameter(context, request, response);
    }
    if (strcmp(request->method, "SET_PARAMETER") == 0)
    {
        if (session->state != AIRPLAY_RTSP_SESSION_SETUP &&
            session->state != AIRPLAY_RTSP_SESSION_RECORDING)
            return airplay_rtsp_response_set_status(response, 455);
        return handle_set_parameter(handlers, context, session, request, response);
    }
    if (strcmp(request->method, "FLUSH") == 0)
        return handle_flush(handlers, context, session, request, response);
    if (strcmp(request->method, "TEARDOWN") == 0)
        return handle_teardown(handlers, context, session, request, response);
    return airplay_rtsp_response_set_status(response, 501);
}

void airplay_handlers_session_closed(AirPlayRtspSession *session, void *user_data)
{
    AirPlayHandlers *handlers = user_data;
    AirPlayHandlerSession *context;
    AirPlayHandlerPhase phase_before;

    if (!session)
        return;
    phase_before = airplay_handlers_session_phase(session);
    context = session->protocol_context;
    session->protocol_context = NULL;
    session->state = AIRPLAY_RTSP_SESSION_CLOSED;
    if (!context)
    {
        trace_phase_transition(session, phase_before, "connection-close", true);
        return;
    }
    diagnostic_log_summary(session, context);
    if (context->initial_setup && handlers && handlers->config.mirror_stop_callback)
        handlers->config.mirror_stop_callback(session->logical_session_id,
                                              handlers->config.callback_user_data);
    airplay_fairplay_destroy(context->fairplay);
    airplay_crypto_secure_zero(context, sizeof(*context));
    free(context);
    trace_phase_transition(session, phase_before, "connection-close", true);
}
