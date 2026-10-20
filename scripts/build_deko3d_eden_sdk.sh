#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

prefix=/opt/switch-tv-box/eden-macros
deko_commit=5cc144dcf5606fbcd78430f6c62923546e5f9cf4
tools_commit=c15523cea963c04fad77a46f23c3a96d26a86ab9
deko_archive_sha=6300269304464292536375f9373e5ce89815efbc2c9a5abeffac0d57dc183be7
tools_archive_sha=bc2b67928377eb676704a0568f0ef17dc20b9846fd586f286dcd0aa507895f81
work="$(mktemp -d /tmp/switch-tv-box-deko-sdk.XXXXXX)"
trap 'rm -rf "$work"' EXIT

curl --fail --location --silent --show-error --retry 3 \
    "https://codeload.github.com/devkitPro/deko3d/tar.gz/$deko_commit" \
    --output "$work/deko3d.tar.gz"
curl --fail --location --silent --show-error --retry 3 \
    "https://codeload.github.com/fincs/dekotools/tar.gz/$tools_commit" \
    --output "$work/dekotools.tar.gz"
printf '%s  %s\n%s  %s\n' \
    "$deko_archive_sha" "$work/deko3d.tar.gz" \
    "$tools_archive_sha" "$work/dekotools.tar.gz" | sha256sum --check -
tar -xzf "$work/deko3d.tar.gz" -C "$work"
tar -xzf "$work/dekotools.tar.gz" -C "$work"
deko="$work/deko3d-$deko_commit"
tools="$work/dekotools-$tools_commit"

# Assemble a pristine reference before applying the single compatibility patch.
meson setup "$tools/build" "$tools"
meson compile -C "$tools/build"
"$tools/build/dekodef" -h "$work/engine_3d.h" -m "$work/engine_3d.mme" \
    "$deko/source/maxwell/engine_3d.def"
"$tools/build/dekomme" -o "$prefix/mme_macros.original.h" \
    "$work/engine_3d.mme" "$deko"/source/maxwell/*.mme
patch --batch --fuzz=0 -d "$tools" -p1 < "$prefix/dekotools-macro-entry-suffix.patch"
meson compile -C "$tools/build"

# Keep all original instruction words and entry addresses, changing only how
# the instruction RAM upload is divided into commands.
export PATH="$tools/build:$PATH"
make -C "$deko" -j"${BUILD_JOBS:-4}" all
python3 "$prefix/verify_deko_macro_upload.py" \
    "$prefix/mme_macros.original.h" "$deko/release/mme_macros.h" \
    --output "$prefix/verification.json"
cmp "$deko/release/mme_macros.h" "$deko/debug/mme_macros.h"
cp "$deko/release/mme_macros.h" "$prefix/mme_macros.compat.h"

{
    printf 'deko3d_commit=%s\ndekotools_commit=%s\n' "$deko_commit" "$tools_commit"
    printf 'deko3d_archive_sha256=%s\ndekotools_archive_sha256=%s\n' \
        "$deko_archive_sha" "$tools_archive_sha"
    printf 'original_libraries:\n'
    sha256sum "$DEVKITPRO/libnx/lib/libdeko3d.a" \
        "$DEVKITPRO/libnx/lib/libdeko3dd.a"
    printf 'compatibility_patch:\n'
    sha256sum "$prefix/dekotools-macro-entry-suffix.patch"
    printf 'compatibility_libraries:\n'
    sha256sum "$deko/lib/libdeko3d.a" "$deko/lib/libdeko3dd.a"
} > "$prefix/provenance.txt"

# Installing only into this derivative image leaves the base SDK untouched.
install -m 0644 "$deko/lib/libdeko3d.a" "$DEVKITPRO/libnx/lib/libdeko3d.a"
install -m 0644 "$deko/lib/libdeko3dd.a" "$DEVKITPRO/libnx/lib/libdeko3dd.a"
cp "$deko/LICENSE" "$prefix/LICENSE.deko3d"
cp "$tools/LICENSE" "$prefix/LICENSE.dekotools"
cat "$prefix/verification.json"
