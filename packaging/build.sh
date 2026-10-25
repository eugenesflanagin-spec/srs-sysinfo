#!/bin/bash
# Build one package in a fresh container of its distro.
#   build.sh DISTRO SOURCE_TARBALL OUTDIR
#     DISTRO          arch | debian-13 | ubuntu-24.04 | fedora-44 | opensuse-tumbleweed
#     SOURCE_TARBALL  srs-sysinfo-VER.tar.gz from `git archive --prefix=srs-sysinfo-VER/`
#     OUTDIR          gets pkg/<package file> plus build logs (lintian/rpmlint, file list)
# Used the same way by build-all.sh (your PC) and .github/workflows/release.yml.
# Needs docker. The container is removed afterwards (--rm).
set -euo pipefail
PKG=$(cd "$(dirname "$0")" && pwd)
. "$PKG/distros.sh"
D=${1:?usage: build.sh DISTRO SOURCE_TARBALL OUTDIR}; known_distro "$D"
TARBALL=$(readlink -f "${2:?source tarball}")
OUT=${3:?output dir}
VER=$(basename "$TARBALL" .tar.gz); VER=${VER#srs-sysinfo-}
[[ $VER =~ ^[0-9]+(\.[0-9]+)+$ ]] || { echo "cannot get the version from $TARBALL" >&2; exit 1; }

rm -rf "$OUT"; mkdir -p "$OUT/pkg"; OUT=$(readlink -f "$OUT")
name=srs-pkg-build-${D//./}-$$
echo "### $D: building srs-sysinfo $VER in ${BUILD_IMAGE[$D]}"
docker pull -q "${BUILD_IMAGE[$D]}" >/dev/null
# shellcheck disable=SC2086  # BUILD_ARGS is a word list on purpose
docker run --rm --name "$name" \
  -v "$TARBALL:/in/srs-sysinfo-$VER.tar.gz:ro" -v "$PKG:/pkg:ro" -v "$OUT:/out" \
  "${BUILD_IMAGE[$D]}" \
  bash -c "bash /pkg/build-${KIND[$D]}.sh $VER ${BUILD_ARGS[$D]}; rc=\$?; chown -R $(id -u):$(id -g) /out; exit \$rc"
ls "$OUT/pkg"
