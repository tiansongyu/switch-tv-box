# Build Switch TV Box

The local environment reuses `netease-switch/toolchain:local` as its devkitPro
base and adds NX-Cast's media dependencies. It builds a separate image:
`switch-tv-box/toolchain:local`. It does not modify netease-switch's SDK image.
Without that local base image, the Dockerfile uses the same pinned official
devkitPro image digest recorded in `netease-switch/docker/Dockerfile`.

```sh
./scripts/build-local.sh image    # prepare/cache the toolchain
./scripts/build-local.sh test     # host protocol, player, UI and safety tests
./scripts/build-local.sh build    # build NRO and SD ZIP
./scripts/build-local.sh release  # tests, NRO, verified SD ZIP
./scripts/build-local.sh shell    # interactive SDK shell
```

Outputs:

- `switch-tv-box.nro`
- `dist/switch-tv-box-sdmc.zip`
- `artifacts/validation/`: test/build logs, compiler, packages and SHA-256

The release build intentionally clears `build/`; validation logs live outside
that directory. Do not run release builds and host tests concurrently against
the same checkout. Docker runs as your UID/GID so generated files stay writable.
Set `BUILD_JOBS`, `TVBOX_BASE_IMAGE` or `TVBOX_TOOLCHAIN_IMAGE` to override defaults.

The SDK used for the initial build is devkitA64 r29.2, libnx 4.12.0, deko3d 0.5.0,
mbedTLS 2.28.10, libsodium 1.0.20, FFmpeg 7.1-4 and deko3d mpv 0.36.0-2.
The exact package list is recorded during each build. FFmpeg downloads are
SHA-256 checked. The codec gate checks audio, images, H.264, Matroska and random
source support rather than accepting a mock-only build.

The container's GCC 12 sanitizer checks use `-fno-pie -no-pie` for host test
executables. PIE sanitizer binaries intermittently failed before `main` in this
environment, including a trivial program; fixed mappings passed repeated runs.
ASan and UBSan remain enabled, and the Switch build and host ASLR policy are
unchanged. This matches the upstream [GCC sanitizer startup issue](https://gcc.gnu.org/pipermail/gcc-bugs/2024-March/857924.html).

For a native devkitPro installation, the inherited `scripts/dev.sh` remains
available. See [ffmpeg-mpv-toolchain.md](ffmpeg-mpv-toolchain.md) for the media
dependency recipe. The Docker commands are the supported local setup here.

## UI preview

```sh
python3 scripts/preview_home.py
```

Requires host C/C++, Python and Pillow. It compiles the actual production ImGui
home drawing functions and rasterizes their draw data with the packaged font.
Eight sample states are written to `artifacts/ui/`: normal, playing, music,
paused music, pairing, offline, English and long title. The sample LAN address
and channel counts are fixtures, not live receiver data.

## Repository history

The first commit preserves NX-Cast commit
`0efdba4281122cdd97de0c3c1a7ebde7485d6d15`. Subsequent commits document the fork's
offline specification archive, TV interface, AirPlay 1 implementation, fixes
and build/package changes. Upstream GPL and third-party attribution are retained.
Historical documents and plans describe NX-Cast; this file and AIRPLAY1.md
describe the fork.

Pushing `main` runs CI and uploads a build artifact. It does not automatically
publish a continuous release. Tagged releases remain an explicit separate action.

## HOS audio capacity fix

Both the standard Dockerfile and the Eden derivative rebuild the HOS output
driver from [averne/mpv commit d8b833e1](https://github.com/averne/mpv/blob/d8b833e1c6e7b6bd2e41507c46285bab95deede2/audio/out/ao_hos.c),
using the [mpv 0.36.0](https://github.com/mpv-player/mpv/tree/v0.36.0) headers.
The source archives and original SDK object are SHA-256 checked. This corrects
the available-sample calculation for a partly filled audio buffer: if three
8192-sample buffers are queued and a fourth contains 6144 samples, the driver
must report 2048 writable samples. The original reports 6144, and its next write
can fail after consuming only part of the supplied audio. The same calculation
applies on physical Switch hardware.

`scripts/build_mpv_hos_sdk.sh` builds just `ao_hos.c.21.o` with the Switch SDK,
checks the structure offsets used by the original object, and requires identical
driver data, callback relocations and external symbols. It also verifies that
every other member of `libmpv.a` remains unchanged. Unknown SDK object versions
are rejected. Running the installer again verifies the existing fixed object
and runs its regression tests without replacing it.

The regression compiles the real patched `get_state` and `audio_write` function
bodies with deterministic audrv test doubles. Its 447 ASan/UBSan cases cover
Free/Done buffers, 2/4/16 buffers, partial 0/2048/4096/6144/full samples, writes
across buffer boundaries, full queues and guarded storage. The unpatched source
must reproduce the failure as a negative control. These tests validate buffer
accounting; device playback still needs a Switch or emulator runtime check.

The container retains exact sources, license, patch, ABI verification and build
provenance in `/opt/switch-tv-box/mpv-hos/`. The original netease-switch image is
unchanged. CI includes every patch/build/test input in its toolchain cache key.
