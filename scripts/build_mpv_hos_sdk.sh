#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

prefix=/opt/switch-tv-box/mpv-hos
mpv_version=0.36.0
mpv_archive_sha=29abc44f8ebee013bb2f9fe14d80b30db19b534c679056e4851ceadf5a5e8bf6
hos_commit=d8b833e1c6e7b6bd2e41507c46285bab95deede2
hos_source_sha=87750bb1608be0977d46a5b9be6faed5fefa224b143708a053861f36a589c5b1
member=ao_hos.c.21.o
work="$(mktemp -d /tmp/switch-tv-box-hos-sdk.XXXXXX)"
trap 'rm -rf "$work"' EXIT

source "$DEVKITPRO/switchvars.sh"
library="$PORTLIBS_PREFIX/lib/libmpv.a"
aarch64-none-elf-ar p "$library" "$member" > "$work/ao_hos.sdk.o"
if [[ -f "$prefix/mpv-hos-verification.json" ]] &&
    python3 - "$prefix" "$work/ao_hos.sdk.o" <<'PY'
import hashlib
import json
from pathlib import Path
import sys
prefix = Path(sys.argv[1])
record = json.loads((prefix / "mpv-hos-verification.json").read_text())
def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
valid = (
    record.get("rebuilt_object_sha256") == sha(sys.argv[2]) and
    record.get("patch_sha256") == sha(prefix / "mpv-hos-buffer-capacity.patch") and
    record.get("hos_commit") == "d8b833e1c6e7b6bd2e41507c46285bab95deede2" and
    record.get("original_object_sha256") ==
        "3453001ca2834b0f93d74fca7ffe6abcda307838057b3a16970d71ec1940b0b5"
)
sys.exit(0 if valid else 1)
PY
then
    python3 "$prefix/test_mpv_hos_capacity.py" "$prefix/ao_hos.original.c" --expect-failure
    python3 "$prefix/test_mpv_hos_capacity.py" "$prefix/ao_hos.patched.c"
    echo "Pinned mpv HOS capacity fix already installed and verified."
    exit 0
fi

# Refuse to apply this source/ABI recipe to any other prebuilt SDK version.
printf '%s  %s\n' \
    3453001ca2834b0f93d74fca7ffe6abcda307838057b3a16970d71ec1940b0b5 \
    "$work/ao_hos.sdk.o" | sha256sum --check -

curl --fail --location --silent --show-error --retry 3 \
    "https://codeload.github.com/mpv-player/mpv/tar.gz/refs/tags/v$mpv_version" \
    --output "$work/mpv.tar.gz"
curl --fail --location --silent --show-error --retry 3 \
    "https://raw.githubusercontent.com/averne/mpv/$hos_commit/audio/out/ao_hos.c" \
    --output "$work/ao_hos.original.c"
printf '%s  %s\n%s  %s\n' \
    "$mpv_archive_sha" "$work/mpv.tar.gz" \
    "$hos_source_sha" "$work/ao_hos.original.c" | sha256sum --check -
tar -xzf "$work/mpv.tar.gz" -C "$work"
mpv="$work/mpv-$mpv_version"
cp "$work/ao_hos.original.c" "$mpv/audio/out/ao_hos.c"

# These tests use the exact get_state/audio_write bodies from both source
# versions. ASan+UBSan guard the writes while an audrv double queues buffers.
python3 "$prefix/test_mpv_hos_capacity.py" "$mpv/audio/out/ao_hos.c" --expect-failure
patch --batch --fuzz=0 -d "$mpv" -p1 < "$prefix/mpv-hos-buffer-capacity.patch"
python3 "$prefix/test_mpv_hos_capacity.py" "$mpv/audio/out/ao_hos.c"

# Only HAVE_STDATOMIC is consulted by this translation unit's mpv headers.
# No generated driver table, mpv core, or other optional subsystem is rebuilt.
printf '#define HAVE_STDATOMIC 1\n' > "$mpv/config.h"
cp "$mpv/audio/out/ao_hos.c" "$prefix/ao_hos.patched.c"
cp "$work/ao_hos.original.c" "$prefix/ao_hos.original.c"
cat >> "$mpv/audio/out/ao_hos.c" <<'EOF'

/* ABI offsets used by the pinned SDK object's machine code. */
_Static_assert(sizeof(void *) == 8, "AArch64 ABI");
_Static_assert(sizeof(struct priv) == 112, "SDK private structure ABI");
_Static_assert(offsetof(struct priv, num_buffers) == 72, "num_buffers ABI");
_Static_assert(offsetof(struct priv, num_samples) == 76, "num_samples ABI");
_Static_assert(offsetof(struct priv, pool) == 80, "pool ABI");
_Static_assert(offsetof(struct priv, buffers) == 88, "buffers ABI");
_Static_assert(offsetof(struct priv, cur_buf_idx) == 96, "cur_buf_idx ABI");
_Static_assert(offsetof(struct priv, cur_queued_samples) == 100, "partial ABI");
_Static_assert(offsetof(struct priv, total_queued_samples) == 104, "queued ABI");
_Static_assert(sizeof(AudioDriverWaveBuf) == 56, "wavebuf ABI");
/* state is an 8-bit bit-field immediately before is_looping. */
_Static_assert(offsetof(AudioDriverWaveBuf, is_looping) == 41, "wavebuf state ABI");
_Static_assert(offsetof(struct ao, samplerate) == 0, "ao samplerate ABI");
_Static_assert(offsetof(struct ao, channels) == 4, "ao channels ABI");
_Static_assert(offsetof(struct ao, format) == 24, "ao format ABI");
_Static_assert(offsetof(struct ao, sstride) == 32, "ao stride ABI");
_Static_assert(offsetof(struct ao, device_buffer) == 44, "ao device_buffer ABI");
_Static_assert(offsetof(struct ao, priv) == 64, "ao priv ABI");
_Static_assert(offsetof(struct ao, log) == 104, "ao log ABI");
EOF

# Keep the original archive for an exact check that every other member stays
# unchanged. This copy is transient and never modifies the base Docker image.
cp "$library" "$work/libmpv.original.a"
(
    cd "$mpv"
    # Upstream has signed comparisons in MPMIN; retain its code and explicitly
    # suppress just that warning while checking all other warnings as errors.
    aarch64-none-elf-gcc $CFLAGS $CPPFLAGS -std=gnu23 -Wall -Wextra -Werror \
        -Wno-sign-compare -DNDEBUG -D_POSIX_VERSION=200809L -I. -Iaudio/out \
        -c audio/out/ao_hos.c -o "$work/$member"
)
python3 "$prefix/verify_mpv_hos_object.py" "$work/ao_hos.sdk.o" "$work/$member" \
    --output "$prefix/mpv-hos-verification.json"
python3 - "$prefix" "$hos_commit" <<'PY'
import hashlib
import json
from pathlib import Path
import sys
prefix = Path(sys.argv[1])
path = prefix / "mpv-hos-verification.json"
record = json.loads(path.read_text())
record["hos_commit"] = sys.argv[2]
record["patch_sha256"] = hashlib.sha256(
    (prefix / "mpv-hos-buffer-capacity.patch").read_bytes()).hexdigest()
path.write_text(json.dumps(record, indent=2) + "\n")
PY
aarch64-none-elf-ar r "$library" "$work/$member"
aarch64-none-elf-ranlib "$library"

python3 - "$work/libmpv.original.a" "$library" "$member" <<'PY'
import hashlib
from pathlib import Path
import sys

def members(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"!<arch>\n"
    offset, names, result = 8, b"", []
    while offset < len(data):
        header = data[offset:offset+60]
        assert header[58:] == b"`\n"
        size = int(header[48:58])
        name = header[:16].decode().strip()
        content = data[offset+60:offset+60+size]
        offset += 60 + size + size % 2
        if name == "//":
            names = content
            continue
        if name in ("/", "/SYM64/"):
            continue
        if name.startswith("/"):
            start = int(name[1:])
            name = names[start:names.index(b"/\n", start)].decode()
        else:
            name = name.rstrip("/")
        result.append((name, hashlib.sha256(content).hexdigest()))
    return result

before, after = members(sys.argv[1]), members(sys.argv[2])
assert len(before) == len(after), "archive member count changed"
changes = [(a[0], b[0]) for a, b in zip(before, after) if a != b]
assert changes == [(sys.argv[3], sys.argv[3])], changes
print(f"Archive verified: only {sys.argv[3]} changed; "
      f"{len(before)-1} other members identical")
PY

{
    printf 'mpv_version=%s\nmpv_archive_sha256=%s\n' "$mpv_version" "$mpv_archive_sha"
    printf 'hos_commit=%s\nhos_original_source_sha256=%s\n' "$hos_commit" "$hos_source_sha"
    aarch64-none-elf-gcc --version | head -1
    printf 'original_library:\n'
    sha256sum "$work/libmpv.original.a"
    printf 'capacity_patch:\n'
    sha256sum "$prefix/mpv-hos-buffer-capacity.patch"
    printf 'fixed_library:\n'
    sha256sum "$library"
} > "$prefix/mpv-hos-provenance.txt"
cp "$mpv/LICENSE.LGPL" "$prefix/LICENSE.mpv.LGPL"
# Preserve the precise source/headers/config used to build the LGPL object.
tar -czf "$prefix/mpv-hos-source.tar.gz" -C "$work" "mpv-$mpv_version"
