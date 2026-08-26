#!/usr/bin/env bash
set -euo pipefail

root=$(git rev-parse --show-toplevel)
version=$(sed -n 's/^VERSION = //p' "$root/version.pri")
scratch=$(mktemp -d)
omarchy_pkgs=${OMARCHY_PKGS_DIR:-$scratch/omarchy-pkgs}
extract=$scratch/package
trap 'rm -rf "$scratch"' EXIT

for tool in bsdtar docker file git readelf sha256sum; do
  command -v "$tool" >/dev/null || {
    echo "missing required tool: $tool" >&2
    exit 1
  }
done

if [[ -z ${OMARCHY_PKGS_DIR:-} ]]; then
  git clone --depth 1 https://github.com/omacom-io/omarchy-pkgs.git \
    "$omarchy_pkgs"
fi

package_dir=$omarchy_pkgs/pkgbuilds/omapixel
archive=$package_dir/omapixel-$version.tar.gz
mkdir -p "$package_dir/.omarchy"
cp "$root/pkgbuild/PKGBUILD" "$package_dir/PKGBUILD"
printf '{"source":"local"}\n' > "$package_dir/.omarchy/package.json"
git -C "$root" archive --format=tar.gz --prefix="omapixel-$version/" HEAD \
  > "$archive"

checksum=$(sha256sum "$archive" | cut -d' ' -f1)
sed -i \
  -e "s|^source=.*|source=(\"omapixel-$version.tar.gz\")|" \
  -e "s|^sha256sums=.*|sha256sums=('$checksum')|" \
  "$package_dir/PKGBUILD"

if [[ $(uname -m) == x86_64 ]]; then
  registration=/proc/sys/fs/binfmt_misc/qemu-aarch64
  if [[ ! -r $registration ]] ||
    ! grep -q '^flags: .*P.*O.*C.*F' "$registration"; then
    echo 'ARM64 emulation needs a recent QEMU registration with POCF flags.' >&2
    echo 'Run pkgbuild/setup-aarch64-emulation.sh first.' >&2
    exit 1
  fi

  export OMAPIXEL_QEMU_AARCH64=1
  if ! grep -q 'OMAPIXEL_QEMU_AARCH64' "$omarchy_pkgs/bin/build"; then
    sed -i \
      '/-e PACKAGES="$PACKAGES"/a\  -e OMAPIXEL_QEMU_AARCH64="${OMAPIXEL_QEMU_AARCH64:-}"' \
      "$omarchy_pkgs/bin/build"
  fi
fi

"$omarchy_pkgs/bin/repo" build --arch aarch64 --package omapixel

shopt -s globstar nullglob
candidates=(
  "$omarchy_pkgs"/build-output/**/omapixel-*.pkg.tar.*
)
artifacts=()
for candidate in "${candidates[@]}"; do
  [[ $candidate == *.sig ]] || artifacts+=("$candidate")
done
if (( ${#artifacts[@]} != 1 )); then
  echo "expected one aarch64 package, found ${#artifacts[@]}" >&2
  exit 1
fi
artifact=${artifacts[0]}

mkdir -p "$extract"
bsdtar -xf "$artifact" -C "$extract"
grep -qx 'arch = aarch64' "$extract/.PKGINFO"
for binary in omapixel omapixel-studio; do
  file "$extract/usr/bin/$binary"
  readelf -h "$extract/usr/bin/$binary" | grep -q 'Machine:.*AArch64'
done

docker run --rm --platform linux/arm64 --user root \
  -v "$artifact:/tmp/omapixel.pkg.tar:ro" \
  omarchy-pkg-builder:latest-aarch64-edge bash -lc '
    pacman -Sy --noconfirm
    pacman -U --noconfirm /tmp/omapixel.pkg.tar
    install -d -o builder -g builder -m 700 /tmp/omapixel-runtime
    runuser -u builder -- env \
      HOME=/home/builder \
      XDG_RUNTIME_DIR=/tmp/omapixel-runtime \
      QT_QPA_PLATFORM=offscreen \
      QT_QPA_PLATFORMTHEME= \
      bash -lc '\''
        omapixel --version
        omapixel new /tmp/arm-smoke.omapixel --size 4x4
        omapixel check /tmp/arm-smoke.omapixel
        omapixel text /tmp/arm-smoke.omapixel
        OMAPIXEL_SHOT=/tmp/arm-studio.png omapixel-studio
        test -s /tmp/arm-studio.png
      '\''
  '

echo "aarch64 package validation passed: $artifact"
