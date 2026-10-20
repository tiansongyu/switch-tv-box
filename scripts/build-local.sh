#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=${TVBOX_TOOLCHAIN_IMAGE:-switch-tv-box/toolchain:local}
action=${1:-release}
case "$action" in
    image|test|build|release|shell) ;;
    *) echo "Usage: $0 {image|test|build|release|shell}" >&2; exit 2 ;;
esac
mkdir -p "$root/artifacts/validation"
if [[ "$action" == image ]] || ! docker image inspect "$image" >/dev/null 2>&1; then
    base=()
    if [[ -n "${TVBOX_BASE_IMAGE:-}" ]]; then
        base=(--build-arg "BASE_IMAGE=$TVBOX_BASE_IMAGE")
    elif docker image inspect netease-switch/toolchain:local >/dev/null 2>&1; then
        base=(--build-arg BASE_IMAGE=netease-switch/toolchain:local)
    fi
    docker build --progress=plain "${base[@]}" -t "$image" "$root" \
        2>&1 | tee "$root/artifacts/validation/toolchain-build.log"
    [[ "$action" != image ]] || exit 0
fi
if [[ "$action" == shell ]]; then
    exec docker run --rm -it --user "$(id -u):$(id -g)" \
        -v "$root:/workspace" -w /workspace "$image" bash
fi
docker run --rm --user "$(id -u):$(id -g)" \
    -e TVBOX_ACTION="$action" -e BUILD_JOBS="${BUILD_JOBS:-8}" \
    -v "$root:/workspace" -w /workspace "$image" \
    bash scripts/in-container-build.sh \
    2>&1 | tee "$root/artifacts/validation/$action.log"
