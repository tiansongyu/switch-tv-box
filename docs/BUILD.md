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
