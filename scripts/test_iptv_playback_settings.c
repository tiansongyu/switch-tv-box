#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail_install;
static bool fail_backup;
static bool fail_rollback;
static bool fail_close;

static int settings_test_rename(const char *from, const char *to)
{
    bool temporary = strstr(from, ".tmp") != NULL;
    bool backup = strstr(from, ".bak") != NULL;
    if ((temporary && fail_install) || (backup && fail_rollback) ||
        (!temporary && !backup && fail_backup))
    {
        errno = EIO;
        return -1;
    }
    /* FAT/SD filesystems need an explicit backup before replacement. */
    if (access(to, F_OK) == 0)
    {
        errno = EEXIST;
        return -1;
    }
    return rename(from, to);
}

static int settings_test_fclose(FILE *file)
{
    int result = fclose(file);
    if (fail_close)
    {
        errno = EIO;
        return EOF;
    }
    return result;
}

#define rename settings_test_rename
#define fclose settings_test_fclose
#include "../source/iptv/playback_settings.c"
#undef fclose
#undef rename

static void expect_settings(IptvQuality quality, IptvBufferMode buffer)
{
    IptvPlaybackSettings actual = iptv_playback_settings_get();
    assert(actual.quality == quality && actual.buffer == buffer);
}

static void write_config(const char *text)
{
    FILE *file = fopen(SETTINGS_PATH, "wb");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static atomic_bool reader_stop;
static atomic_uint reader_count;

static void *snapshot_reader(void *unused)
{
    (void)unused;
    do
    {
        IptvPlaybackSettings current = iptv_playback_settings_get();
        assert((current.quality == IPTV_QUALITY_ORIGINAL &&
                current.buffer == IPTV_BUFFER_FAST) ||
               (current.quality == IPTV_QUALITY_SMOOTH &&
                current.buffer == IPTV_BUFFER_STABLE));
        atomic_fetch_add(&reader_count, 1u);
    } while (!atomic_load(&reader_stop));
    return NULL;
}

int main(void)
{
    char directory[] = "/tmp/tvbox-playback-settings-XXXXXX";
    char previous[4096];
    assert(getcwd(previous, sizeof(previous)));
    assert(mkdtemp(directory));
    assert(chdir(directory) == 0);
    expect_settings(IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_STABLE);
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_STABLE);
    IptvPlaybackSettings changed = {IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE};
    assert(!iptv_playback_settings_set(changed)); /* Missing SD path. */
    expect_settings(IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_STABLE);
    assert(mkdir("sdmc:", 0700) == 0);
    assert(mkdir("sdmc:/switch", 0700) == 0);
    assert(mkdir("sdmc:/switch/switch-tv-box", 0700) == 0);
    assert(mkdir(IPTV_ROOT_DIR, 0700) == 0);

    assert(!iptv_playback_settings_set(
        (IptvPlaybackSettings){(IptvQuality)-1, IPTV_BUFFER_FAST}));
    assert(errno == EINVAL);
    assert(!iptv_playback_settings_set(
        (IptvPlaybackSettings){IPTV_QUALITY_ORIGINAL, (IptvBufferMode)3}));
    assert(errno == EINVAL);
    assert(access(SETTINGS_PATH, F_OK) != 0);

    assert(iptv_playback_settings_set(changed));
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE);
    IptvPlaybackSettings next = {IPTV_QUALITY_BALANCED, IPTV_BUFFER_FAST};
    fail_close = true;
    assert(!iptv_playback_settings_set(next));
    assert(errno == EIO);
    fail_close = false;
    expect_settings(IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE);
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE);
    fail_backup = true;
    assert(!iptv_playback_settings_set(next));
    fail_backup = false;
    expect_settings(IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE);
    fail_install = true;
    assert(!iptv_playback_settings_set(next));
    fail_install = false;
    assert(access(SETTINGS_PATH, F_OK) == 0);
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE);
    fail_install = fail_rollback = true;
    assert(!iptv_playback_settings_set(next));
    fail_install = fail_rollback = false;
    expect_settings(IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE);
    assert(access(SETTINGS_PATH, F_OK) != 0);
    assert(access(SETTINGS_BACKUP, F_OK) == 0);
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE);
    assert(iptv_playback_settings_set(next));
    assert(access(SETTINGS_BACKUP, F_OK) != 0);
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_BALANCED, IPTV_BUFFER_FAST);

    const char *invalid[] = {
        "", "quality=smooth\n", "buffer=stable\n",
        "quality=unknown\nbuffer=stable\n",
        "quality=original\nbuffer=3\n",
        "quality=original\nbuffer=fast\nquality=smooth\n",
        "quality=original\nbuffer=fast\njunk=1\n",
        "quality=original-but-not-really-because-this-value-is-too-long-to-fit-in-the-line-buffer\nbuffer=fast\n",
    };
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    {
        write_config(invalid[index]);
        iptv_playback_settings_init();
        expect_settings(IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_STABLE);
    }
    write_config("buffer=stable\r\nquality=balanced\r\n");
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_BALANCED, IPTV_BUFFER_STABLE);
    /* A saved choice from an earlier release wins over the new default. */
    write_config("quality=original\nbuffer=balanced\n");
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_BALANCED);
    write_config("quality=smooth\nbuffer=fast");
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_SMOOTH, IPTV_BUFFER_FAST);

    assert(iptv_playback_settings_set(
        (IptvPlaybackSettings){IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_FAST}));
    pthread_t reader;
    assert(pthread_create(&reader, NULL, snapshot_reader, NULL) == 0);
    for (unsigned int index = 0; index < 64u; ++index)
    {
        IptvPlaybackSettings pair = index & 1u
            ? (IptvPlaybackSettings){IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_FAST}
            : (IptvPlaybackSettings){IPTV_QUALITY_SMOOTH, IPTV_BUFFER_STABLE};
        assert(iptv_playback_settings_set(pair));
    }
    atomic_store(&reader_stop, true);
    assert(pthread_join(reader, NULL) == 0);
    assert(atomic_load(&reader_count) > 0u);
    iptv_playback_settings_init();
    expect_settings(IPTV_QUALITY_ORIGINAL, IPTV_BUFFER_FAST);
    assert(chdir(previous) == 0);
    printf("IPTV playback settings passed: defaults, validation, SD replacement "
           "rollback/recovery, restart and coherent concurrent snapshots.\nFixture: %s\n",
           directory);
    return 0;
}
