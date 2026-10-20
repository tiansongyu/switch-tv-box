#define _POSIX_C_SOURCE 200809L
#include "app/protocol_coordinator.h"
#include "protocol/dlna/control/handler_internal.h"
#include "protocol/dlna/control/controller_session.h"
#include "player/trace.h"
#include "log/log.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PlayerOwnershipLease owner;
static unsigned begins, aborts, commands;
static PlayerCommandKind kinds[64];
static char last_open_uri[4096], last_open_metadata[4096];
static int last_seek;
static bool fail_open;

void log_debug(const char *format, ...) { (void)format; }
void log_info(const char *format, ...) { (void)format; }
void log_warn(const char *format, ...) { (void)format; }
void log_error(const char *format, ...) { (void)format; }
void vlog_write(LogLevel level, const char *format, va_list args)
{ (void)level; (void)format; (void)args; }

bool dlna_template_load_file_alloc(const char *path, char **out, size_t *size)
{
    char filename[256];
    snprintf(filename, sizeof(filename), "assets/dlna/%s", path);
    FILE *file = fopen(filename, "rb");
    if (!file)
        return false;
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    *out = malloc((size_t)length + 1);
    assert(*out);
    assert(fread(*out, 1, (size_t)length, file) == (size_t)length);
    (*out)[length] = 0;
    if (size)
        *size = (size_t)length;
    fclose(file);
    return true;
}

bool protocol_coordinator_media_current(PlayerOwnershipLease *out)
{
    *out = owner;
    return owner.owner != PLAYER_MEDIA_OWNER_NONE;
}
bool protocol_coordinator_media_guard_begin(PlayerMediaOwner requested, uint64_t token,
                                             ProtocolMediaGuard *out)
{
    if (owner.owner != requested || owner.token != token)
        return false;
    *out = (ProtocolMediaGuard){.active = true, .lease = owner};
    return true;
}
void protocol_coordinator_media_guard_end(ProtocolMediaGuard *guard) { guard->active = false; }
bool protocol_coordinator_media_begin(PlayerMediaOwner requested, uint64_t token,
                                      ProtocolMediaTransaction *out)
{
    ++begins;
    out->previous = owner;
    owner = (PlayerOwnershipLease){requested, token, owner.generation + 1};
    out->lease = owner;
    out->active = true;
    return true;
}
bool protocol_coordinator_media_wait_resources(const PlayerOwnershipLease *lease, uint32_t timeout)
{ (void)lease; (void)timeout; return true; }
void protocol_coordinator_media_abort(ProtocolMediaTransaction *transaction)
{ ++aborts; owner = transaction->previous; transaction->active = false; }
void protocol_coordinator_media_end(ProtocolMediaTransaction *transaction)
{ transaction->active = false; }
bool protocol_coordinator_media_release(const PlayerOwnershipLease *lease)
{ assert(owner.generation == lease->generation); owner.owner = PLAYER_MEDIA_OWNER_NONE; return true; }

PlayerCommandStatus player_submit_command_wait(const PlayerCommandRequest *request, uint32_t timeout)
{
    (void)timeout;
    assert(request->source == PLAYER_COMMAND_SOURCE_DLNA);
    assert(request->lease.owner == PLAYER_MEDIA_OWNER_DLNA);
    assert(commands < sizeof(kinds) / sizeof(kinds[0]));
    kinds[commands++] = request->kind;
    if (request->kind == PLAYER_COMMAND_OPEN)
    {
        assert(request->uri && request->uri[0]);
        snprintf(last_open_uri, sizeof(last_open_uri), "%s", request->uri);
        snprintf(last_open_metadata, sizeof(last_open_metadata), "%s", request->metadata);
        if (fail_open)
            return PLAYER_COMMAND_STATUS_EXECUTION_FAILED;
    }
    if (request->kind == PLAYER_COMMAND_SEEK_MS)
        last_seek = request->value;
    return PLAYER_COMMAND_STATUS_EXECUTED;
}
bool player_command_status_succeeded(PlayerCommandStatus status)
{ return status == PLAYER_COMMAND_STATUS_EXECUTED; }
const char *player_command_status_name(PlayerCommandStatus status)
{ (void)status; return "test-status"; }

static void action(SoapActionHandler handler, const char *body, int fault)
{
    SoapActionContext context = {.service_name = "AVTransport", .action_name = "test", .body = body};
    SoapActionOutput output = {0};
    assert(handler(&context, &output) == (fault == 0));
    assert(output.success == (fault == 0));
    assert(output.fault_code == fault);
    soap_writer_dispose(&output);
}

static void xml_value(const char *body, const char *expected)
{
    char *value = NULL;
    assert(soap_handler_extract_xml_value_alloc(body, "CurrentURI", &value));
    assert(strcmp(value, expected) == 0);
    free(value);
}

int main(void)
{
    dlna_protocol_state_init();
    assert(dlna_protocol_state_variable_count() > 10);
    xml_value("<CurrentURI/>", "");
    xml_value("<x:CurrentURI ><![CDATA[https://host/v?raw=a&amp;b&token=+/=%2F]]></x:CurrentURI>",
              "https://host/v?raw=a&amp;b&token=+/=%2F");
    xml_value("<CurrentURI>https://host/v?a=1&#38;b=2&#x26;c=%2F+</CurrentURI>",
              "https://host/v?a=1&b=2&c=%2F+");
    xml_value("<CurrentURI><![CDATA[https://host/v?text=</CurrentURI>]]></CurrentURI>",
              "https://host/v?text=</CurrentURI>");

    const char *signed_uri = "https://video.test/movie.mp4?token=abc%2F+%3D&expires=123";
    action(avtransport_set_uri,
        "<InstanceID>0</InstanceID><CurrentURI>https://video.test/movie.mp4?token=abc%2F+%3D"
        "&amp;expires=123</CurrentURI><CurrentURIMetaData>&lt;DIDL-Lite&gt;"
        "&lt;dc:title&gt;&#x7535;&#x5F71;&lt;/dc:title&gt;&lt;/DIDL-Lite&gt;</CurrentURIMetaData>", 0);
    assert(strcmp(last_open_uri, signed_uri) == 0);
    assert(strcmp(last_open_metadata, "<DIDL-Lite><dc:title>电影</dc:title></DIDL-Lite>") == 0);
    action(avtransport_play, "<InstanceID>0</InstanceID><Speed>1</Speed>", 0);
    action(avtransport_pause, "<InstanceID>0</InstanceID>", 0);
    action(avtransport_seek,
        "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>00:00:01.250</Target>", 0);
    assert(last_seek == 1250);
    assert(kinds[commands - 1] == PLAYER_COMMAND_SEEK_MS);
    action(avtransport_stop, "<InstanceID>0</InstanceID>", 0);
    assert(owner.owner == PLAYER_MEDIA_OWNER_NONE);
    /* The main/UI thread must not receive a second stop request. If consumed
     * after the following Play, that stale request stops the reopened video. */
    assert(!dlna_controller_session_consume_home_request());
    unsigned before = commands;
    action(avtransport_play, "<InstanceID>0</InstanceID><Speed>1</Speed>", 0);
    assert(commands == before + 2 && kinds[before] == PLAYER_COMMAND_OPEN &&
           kinds[before + 1] == PLAYER_COMMAND_PLAY);
    assert(strcmp(last_open_uri, signed_uri) == 0);
    assert(!dlna_controller_session_consume_home_request());
    action(avtransport_stop, "<InstanceID>0</InstanceID>", 0);

    /* Another protocol has replaced the backend media after Stop. Play must
     * reload the exact saved URL and metadata, including temporary tokens. */
    owner.owner = PLAYER_MEDIA_OWNER_IPTV;
    owner.token = 99;
    before = commands;
    action(avtransport_play, "<InstanceID>0</InstanceID><Speed>1</Speed>", 0);
    assert(commands == before + 2 && kinds[before] == PLAYER_COMMAND_OPEN &&
           kinds[before + 1] == PLAYER_COMMAND_PLAY);
    assert(strcmp(last_open_uri, signed_uri) == 0);

    dlna_protocol_state_set_transport_timing(180000, 90000);
    action(avtransport_set_uri,
        "<InstanceID>0</InstanceID><CurrentURI><![CDATA[http://video.test/next?token=2&part=3]]>"
        "</CurrentURI><CurrentURIMetaData/>", 0);
    assert(strcmp(last_open_uri, "http://video.test/next?token=2&part=3") == 0);
    assert(strcmp(dlna_protocol_state_view()->current_media_duration, "00:00:00") == 0);
    assert(strcmp(dlna_protocol_state_view()->relative_time_position, "00:00:00") == 0);

    before = commands;
    unsigned prior_begins = begins;
    action(avtransport_set_uri, "<InstanceID>3</InstanceID><CurrentURI>http://h/v</CurrentURI>", 718);
    action(avtransport_set_uri, "<InstanceID>0</InstanceID><CurrentURI>file:///secret</CurrentURI>", 716);
    action(avtransport_set_uri, "<InstanceID>0</InstanceID><CurrentURI>http:///video</CurrentURI>", 716);
    action(avtransport_play, "<InstanceID>0</InstanceID><Speed>2</Speed>", 717);
    action(avtransport_seek, "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit>", 402);
    action(avtransport_seek, "<InstanceID>0</InstanceID><Target>invalid</Target>", 711);
    action(avtransport_seek, "<InstanceID>0</InstanceID><Unit>BYTES</Unit><Target>1</Target>", 710);
    assert(commands == before && begins == prior_begins);

    action(avtransport_set_uri, "<InstanceID>0</InstanceID><CurrentURI/>", 0);
    assert(kinds[commands - 1] == PLAYER_COMMAND_STOP && owner.owner == PLAYER_MEDIA_OWNER_NONE);
    assert(!dlna_controller_session_consume_home_request());
    assert(dlna_protocol_state_view()->av_transport_uri[0] == 0);
    assert(strcmp(dlna_protocol_state_view()->transport_state, "NO_MEDIA_PRESENT") == 0);
    action(avtransport_play, "<InstanceID>0</InstanceID><Speed>1</Speed>", 701);

    owner.owner = PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO;
    before = commands;
    action(avtransport_set_uri, "<InstanceID>0</InstanceID><CurrentURI/>", 0);
    assert(commands == before && owner.owner == PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO);
    action(avtransport_set_uri, "<InstanceID>0</InstanceID><CurrentURI>http://video.test/final</CurrentURI>", 0);
    action(avtransport_stop, "<InstanceID>0</InstanceID>", 0);
    fail_open = true;
    before = commands;
    action(avtransport_play, "<InstanceID>0</InstanceID><Speed>1</Speed>", 704);
    assert(commands == before + 1 && kinds[before] == PLAYER_COMMAND_OPEN && aborts == 1);
    dlna_protocol_state_reset();
    puts("DLNA AVTransport: signed URLs, video controls, handoff, empty media and faults passed");
    return 0;
}
