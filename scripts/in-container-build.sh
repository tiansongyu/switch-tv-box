#!/usr/bin/env bash
set -euo pipefail
mkdir -p artifacts/validation
dkp-pacman -Q > artifacts/validation/toolchain-packages.txt
aarch64-none-elf-gcc --version > artifacts/validation/compiler.txt
./scripts/verify_switch_ffmpeg_airplay.sh "$DEVKITPRO/portlibs/switch"
host_pc="/usr/lib/$(gcc -print-multiarch)/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"
if [[ "${TVBOX_ACTION:-release}" == test || "${TVBOX_ACTION:-release}" == release ]]; then
    # GCC 12 ASan can crash before main in PIE executables on high-ASLR hosts.
    # Keep ASan/UBSan enabled and use fixed executable mappings for host tests.
    # These flags do not affect the Switch executable or the host's ASLR policy.
    PKG_CONFIG_PATH="$host_pc" make test-ui test-iptv-data test-airplay test-c-safety-sanitize \
        HOST_SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie"
    sh scripts/test_airplay_photo.sh
    python3 scripts/test_dev_workflow.py
    python3 scripts/test_ffmpeg_install.py
    python3 scripts/test_ssdp_receive.py
    python3 scripts/test_dlna_address.py
    python3 scripts/test_dlna_avtransport.py
    python3 scripts/test_platform_compat.py
    python3 scripts/test_eden_socket_compat.py
    python3 scripts/test_eden_airplay_advertise.py
    python3 scripts/test_eden_iptv_direct.py
fi
if [[ "${TVBOX_ACTION:-release}" == build || "${TVBOX_ACTION:-release}" == release ]]; then
    make RELEASE_JOBS="${BUILD_JOBS:-8}" release-build
    NXCAST_MIN_NRO_SIZE=5000000 sh scripts/package_release.sh
    sha256sum switch-tv-box.nro dist/switch-tv-box-sdmc.zip \
        > artifacts/validation/artifact-sha256.txt
fi
