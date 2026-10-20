#include "iptv/playback_settings.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "iptv/iptv.h"

#define SETTINGS_PATH IPTV_ROOT_DIR "/playback-settings.conf"
#define SETTINGS_TEMP SETTINGS_PATH ".tmp"
#define SETTINGS_BACKUP SETTINGS_PATH ".bak"
#define SETTINGS_DEFAULT ((unsigned int)IPTV_BUFFER_STABLE << 8)

static pthread_mutex_t g_settings_writer = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint g_settings = SETTINGS_DEFAULT;
static const char *const quality_names[] = {"original", "balanced", "smooth"};
static const char *const buffer_names[] = {"fast", "balanced", "stable"};

static bool settings_valid(IptvPlaybackSettings settings)
{
    return settings.quality >= IPTV_QUALITY_ORIGINAL &&
           settings.quality <= IPTV_QUALITY_SMOOTH &&
           settings.buffer >= IPTV_BUFFER_FAST &&
           settings.buffer <= IPTV_BUFFER_STABLE;
}

static void settings_publish(IptvPlaybackSettings settings)
{
    atomic_store_explicit(&g_settings,
                          (unsigned int)settings.quality |
                              ((unsigned int)settings.buffer << 8),
                          memory_order_release);
}

static bool settings_read(FILE *file, IptvPlaybackSettings *settings)
{
    char line[64];
    bool quality_seen = false;
    bool buffer_seen = false;
    size_t line_count = 0u;

    while (fgets(line, sizeof(line), file))
    {
        if (++line_count > 16u ||
            (!strchr(line, '\n') && !feof(file)))
            return false;
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        const char *value = strchr(line, '=');
        if (!value)
            return false;
        ++value;
        bool matched = false;
        if (strncmp(line, "quality=", 8u) == 0 && !quality_seen)
        {
            for (size_t index = 0; index < sizeof(quality_names) / sizeof(quality_names[0]); ++index)
            {
                if (strcmp(value, quality_names[index]) == 0)
                {
                    settings->quality = (IptvQuality)index;
                    quality_seen = matched = true;
                    break;
                }
            }
        }
        else if (strncmp(line, "buffer=", 7u) == 0 && !buffer_seen)
        {
            for (size_t index = 0; index < sizeof(buffer_names) / sizeof(buffer_names[0]); ++index)
            {
                if (strcmp(value, buffer_names[index]) == 0)
                {
                    settings->buffer = (IptvBufferMode)index;
                    buffer_seen = matched = true;
                    break;
                }
            }
        }
        if (!matched)
            return false;
    }
    return !ferror(file) && quality_seen && buffer_seen;
}

void iptv_playback_settings_init(void)
{
    IptvPlaybackSettings settings = {IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_STABLE};

    pthread_mutex_lock(&g_settings_writer);
    FILE *file = fopen(SETTINGS_PATH, "rb");
    if (!file && errno == ENOENT)
        file = fopen(SETTINGS_BACKUP, "rb");
    if (file)
    {
        IptvPlaybackSettings loaded = settings;
        bool valid = settings_read(file, &loaded);
        if (fclose(file) != 0)
            valid = false;
        if (valid)
            settings = loaded;
    }
    settings_publish(settings);
    pthread_mutex_unlock(&g_settings_writer);
}

IptvPlaybackSettings iptv_playback_settings_get(void)
{
    unsigned int packed = atomic_load_explicit(&g_settings, memory_order_acquire);
    IptvPlaybackSettings settings = {
        .quality = (IptvQuality)(packed & 0xffu),
        .buffer = (IptvBufferMode)((packed >> 8) & 0xffu),
    };
    return settings;
}

static bool settings_install(void)
{
    struct stat st;
    bool existed = stat(SETTINGS_PATH, &st) == 0;

    if (existed && !S_ISREG(st.st_mode))
    {
        errno = EISDIR;
        return false;
    }
    if (!existed && errno != ENOENT)
        return false;
    if (existed)
    {
        if (remove(SETTINGS_BACKUP) != 0 && errno != ENOENT)
            return false;
        if (rename(SETTINGS_PATH, SETTINGS_BACKUP) != 0)
            return false;
    }
    if (rename(SETTINGS_TEMP, SETTINGS_PATH) != 0)
    {
        int error = errno;
        if (existed)
            (void)rename(SETTINGS_BACKUP, SETTINGS_PATH);
        errno = error;
        return false;
    }
    (void)remove(SETTINGS_BACKUP);
    return true;
}

bool iptv_playback_settings_set(IptvPlaybackSettings settings)
{
    if (!settings_valid(settings))
    {
        errno = EINVAL;
        return false;
    }

    pthread_mutex_lock(&g_settings_writer);
    FILE *file = fopen(SETTINGS_TEMP, "wb");
    bool saved = false;
    int error = errno;
    if (file)
    {
        saved = fprintf(file, "quality=%s\nbuffer=%s\n",
                        quality_names[settings.quality],
                        buffer_names[settings.buffer]) >= 0;
        error = saved ? 0 : errno;
        if (fclose(file) != 0)
        {
            saved = false;
            error = errno;
        }
        if (saved)
        {
            saved = settings_install();
            error = saved ? 0 : errno;
        }
    }
    if (saved)
        settings_publish(settings);
    else
        (void)remove(SETTINGS_TEMP);
    pthread_mutex_unlock(&g_settings_writer);
    if (!saved)
        errno = error;
    return saved;
}
