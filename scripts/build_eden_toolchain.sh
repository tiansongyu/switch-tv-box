#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
base_image="${BASE_IMAGE:-switch-tv-box/toolchain:local}"
output_image="${EDEN_TOOLCHAIN_IMAGE:-switch-tv-box/toolchain:eden-macros}"

if [[ "$output_image" == "$base_image" || "$output_image" == netease-switch/* ||
      "$output_image" == cloudtune-nx/* ]]; then
    echo "Use an isolated Switch TV Box image tag for this compatibility SDK." >&2
    exit 1
fi

base_image_id="$(docker image inspect --format '{{.Id}}' "$base_image")"
docker build --file "$repo_root/Dockerfile.eden-macros" \
    --build-arg "BASE_IMAGE=$base_image" \
    --build-arg "BASE_IMAGE_ID=$base_image_id" \
    --tag "$output_image" "$repo_root"
docker run --rm --entrypoint cat "$output_image" \
    /opt/switch-tv-box/eden-macros/verification.json
docker run --rm --entrypoint cat "$output_image" \
    /opt/switch-tv-box/mpv-hos/mpv-hos-verification.json
printf 'Compatibility SDK ready: %s (base %s)\n' "$output_image" "$base_image_id"
