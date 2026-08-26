#!/usr/bin/env bash
set -euo pipefail

scratch=$(mktemp -d)
container=
cleanup() {
  [[ -z $container ]] || docker rm -f "$container" >/dev/null
  rm -rf "$scratch"
}
trap cleanup EXIT

command -v docker >/dev/null || {
  echo 'missing required tool: docker' >&2
  exit 1
}

# The official installer lacks the credential flag required by sudo inside the
# package builder. Register its current QEMU binary through the fuller helper.
container=$(docker create tonistiigi/binfmt)
docker cp "$container:/usr/bin/qemu-aarch64" "$scratch/qemu-aarch64-static"
docker rm "$container" >/dev/null
container=

docker run --privileged --rm \
  -v "$scratch/qemu-aarch64-static:/usr/bin/qemu-aarch64-static:ro" \
  multiarch/qemu-user-static \
  --reset --credential yes --persistent yes --preserve-argv0 yes

grep -q '^flags: .*P.*O.*C.*F' \
  /proc/sys/fs/binfmt_misc/qemu-aarch64
docker run --rm --platform linux/arm64 alpine:3.21 /bin/true
echo 'ARM64 emulation registered with POCF flags'
