#!/usr/bin/env python3
"""Compile the real ao_hos functions with deterministic audrv test doubles.

No capacity arithmetic is copied from the driver. The three complete function
bodies and private structure are taken verbatim from the specified source.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def extract(source: str, signature: str, trailing: str = "") -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + trailing


PREAMBLE = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define MPMIN(a,b) ((a) > (b) ? (b) : (a))
#define R_FAILED(rc) ((rc) != 0)
typedef unsigned Result;
enum { AudioDriverWaveBufState_Free=0, AudioDriverWaveBufState_Waiting=1,
       AudioDriverWaveBufState_Playing=3, AudioDriverWaveBufState_Done=4 };
typedef struct { void *data_raw; int state; } AudioDriverWaveBuf;
typedef struct { uint32_t played; unsigned additions; } AudioDriver;
struct ao { void *priv; int samplerate, sstride; };
struct mp_pcm_state {
    int free_samples, queued_samples;
    double delay;
    bool playing;
};
static Result audrvUpdate(AudioDriver *driver) { (void)driver; return 0; }
static uint32_t audrvVoiceGetPlayedSampleCount(AudioDriver *d, int id) {
    (void)id; return d->played;
}
static bool audrvVoiceIsPlaying(AudioDriver *d, int id) {
    (void)d; (void)id; return true;
}
static void armDCacheFlush(void *p, size_t bytes) { (void)p; (void)bytes; }
static bool audrvVoiceAddWaveBuf(AudioDriver *d, int id, AudioDriverWaveBuf *b) {
    (void)id;
    assert(b->state == AudioDriverWaveBufState_Free ||
           b->state == AudioDriverWaveBufState_Done);
    b->state = AudioDriverWaveBufState_Waiting;
    d->additions++;
    return true;
}
"""

TESTS = r"""
static unsigned cases;
static void capacity_case(int count, int busy, int partial, int size, bool done) {
    const int stride = 4, guard = 32;
    size_t bytes = (size_t)count * size * stride;
    uint8_t *storage = malloc(bytes + 2 * guard);
    uint8_t *input = malloc(bytes);
    AudioDriverWaveBuf *buffers = calloc(count, sizeof(*buffers));
    assert(storage && input && buffers);
    memset(storage, 0xA5, bytes + 2 * guard);
    memset(input, 0x3C, bytes);
    for (int i = 0; i < count; ++i) {
        buffers[i].data_raw = storage + guard;
        buffers[i].state = i < busy ? AudioDriverWaveBufState_Playing :
            (done ? AudioDriverWaveBufState_Done : AudioDriverWaveBufState_Free);
    }
    struct priv priv = {
        .num_buffers=count, .num_samples=size, .buffers=buffers,
        .cur_buf_idx=-1, .total_queued_samples=(uint32_t)(busy*size),
    };
    struct ao ao = { .priv=&priv, .samplerate=48000, .sstride=stride };
    void *data[] = { input };
    assert(audio_write(&ao, data, partial));
    struct mp_pcm_state state = {0};
    get_state(&ao, &state);
    int expected = (count-busy)*size-partial;
    if (state.free_samples != expected) {
        fprintf(stderr, "capacity mismatch: buffers=%d busy=%d partial=%d "
                "size=%d done=%d: got=%d expected=%d\n",
                count, busy, partial, size, done, state.free_samples, expected);
        exit(1);
    }
    assert(state.queued_samples == busy*size+partial);
    assert(state.free_samples + state.queued_samples == count*size);
    assert(audio_write(&ao, data, state.free_samples));
    get_state(&ao, &state);
    assert(state.free_samples == 0);
    assert(state.queued_samples == count*size);
    uint32_t queued = priv.total_queued_samples;
    assert(!audio_write(&ao, data, 1));
    assert(priv.total_queued_samples == queued);
    for (int i = 0; i < guard; ++i) {
        assert(storage[i] == 0xA5);
        assert(storage[guard+bytes+i] == 0xA5);
    }
    for (size_t i = (size_t)busy*size*stride; i < bytes; ++i)
        assert(storage[guard+i] == 0x3C);
    free(buffers); free(input); free(storage);
    ++cases;
}

int main(void) {
    // The observed failure: three queued buffers and a fourth 3/4 full.
    capacity_case(4, 3, 6144, 8192, false);
    const int counts[] = {2, 4, 16};
    const int sizes[] = {256, 8192};
    for (unsigned n = 0; n < sizeof(counts)/sizeof(counts[0]); ++n) {
        for (unsigned s = 0; s < sizeof(sizes)/sizeof(sizes[0]); ++s) {
            int count=counts[n], size=sizes[s];
            for (int busy=0; busy<count; ++busy)
                for (int quarter=0; quarter<=4; ++quarter)
                    for (int done=0; done<=1; ++done)
                        capacity_case(count, busy, size*quarter/4, size, done);
            capacity_case(count, count, 0, size, false);
        }
    }
    printf("ao_hos capacity: %u cases passed "
           "(real get_state/audio_write; partial 0/2048/4096/6144/full)\n", cases);
    return 0;
}
"""


def run(source_path: Path, expect_failure: bool) -> None:
    source = source_path.read_text()
    program = PREAMBLE + extract(source, "struct priv {", ";")
    for signature in ("static int find_free_wavebuf(",
                      "static bool audio_write(", "static void get_state("):
        program += "\n" + extract(source, signature)
    program += TESTS
    with tempfile.TemporaryDirectory(prefix="tvbox-hos-capacity-") as directory:
        directory = Path(directory)
        cfile = directory / "capacity.c"
        binary = directory / "capacity"
        cfile.write_text(program)
        subprocess.run([
            os.environ.get("HOST_CC", "gcc"), "-std=gnu11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-fno-pie", "-no-pie", str(cfile), "-o", str(binary),
        ], check=True)
        result = subprocess.run([str(binary)], text=True, capture_output=True,
                                timeout=15,
                                env={**os.environ, "ASAN_OPTIONS":
                                     "detect_leaks=0:abort_on_error=1"})
        if expect_failure:
            if result.returncode != 1 or "capacity mismatch:" not in result.stderr:
                raise RuntimeError("Original driver did not reproduce capacity bug: "
                                   + result.stdout + result.stderr)
            print("Original source regression reproduced: " + result.stderr.strip())
        else:
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--expect-failure", action="store_true")
    args = parser.parse_args()
    run(args.source, args.expect_failure)
