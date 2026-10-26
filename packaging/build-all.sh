#!/bin/bash
# Build + install-test every release file on your own PC, the same way the
# GitHub workflow (.github/workflows/release.yml) does: same images, same scripts.
#
#   packaging/build-all.sh [-o OUTDIR] [SOURCE_TARBALL] [DISTRO...]
#     SOURCE_TARBALL  srs-sysinfo-VER.tar.gz (git archive, prefix srs-sysinfo-VER/).
#                     Default: made from the current git HEAD (committed files only).
#     DISTRO          any of: arch debian-13 ubuntu-24.04 fedora-44 opensuse-tumbleweed (default: all)
#     OUTDIR          default: dist/release-VER in the repo (dist/ is git-ignored)
#
# Result:
#   OUTDIR/<distro>/pkg/<package>   OUTDIR/<distro>/{build.log,test.log,lintian.txt|rpmlint.txt,...}
#   OUTDIR/appimage/                the AppImage + .zsync, appimage.log, appimage-test.log
#                                   (skip it with SRS_NO_APPIMAGE=1)
#   OUTDIR/release/                 exactly the files a release gets, plus SHA256SUMS and release-notes.md
# Needs docker. Containers are --rm and named srs-pkg-*; nothing is pushed or uploaded.
set -euo pipefail
PKG=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$PKG")
. "$PKG/distros.sh"
OUT=
if [ "${1:-}" = -o ]; then OUT=$2; shift 2; fi

if [ $# -gt 0 ] && [[ $1 == *.tar.gz ]]; then
  TARBALL=$(readlink -f "$1"); shift
  VER=$(basename "$TARBALL" .tar.gz); VER=${VER#srs-sysinfo-}
else
  VER=$("$PKG/release-info.sh" version)
  TARBALL=
fi
[[ $VER =~ ^[0-9]+(\.[0-9]+)+$ ]] || { echo "cannot get the version (got '$VER')"; exit 1; }
OUT=$(mkdir -p "${OUT:-$ROOT/dist/release-$VER}" && readlink -f "${OUT:-$ROOT/dist/release-$VER}")
if [ -z "$TARBALL" ]; then
  [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ] && echo "note: uncommitted changes are NOT in the build (git archive uses HEAD)"
  TARBALL=$OUT/srs-sysinfo-$VER.tar.gz
  git -C "$ROOT" archive --format=tar.gz --prefix="srs-sysinfo-$VER/" -o "$TARBALL" HEAD
  echo "source: git archive of $(git -C "$ROOT" rev-parse --short HEAD) -> $TARBALL"
fi
DISTROS=("$@"); [ ${#DISTROS[@]} -eq 0 ] && DISTROS=("${ALL_DISTROS[@]}")
for d in "${DISTROS[@]}"; do known_distro "$d"; done

declare -A RESULT
for d in "${DISTROS[@]}"; do
  echo "===== $d"
  mkdir -p "$OUT/$d"
  if "$PKG/build.sh" "$d" "$TARBALL" "$OUT/$d" >"$OUT/$d.build.log" 2>&1; then
    mv "$OUT/$d.build.log" "$OUT/$d/build.log"
    echo "build OK: $(ls "$OUT/$d/pkg")"
  else
    mkdir -p "$OUT/$d"; mv "$OUT/$d.build.log" "$OUT/$d/build.log"
    echo "BUILD FAILED ($d), tail of $OUT/$d/build.log:"; tail -25 "$OUT/$d/build.log"; RESULT[$d]="build FAIL"; continue
  fi
  if "$PKG/test-in-container.sh" "$d" "$OUT/$d/pkg" "$VER" >"$OUT/$d/test.log" 2>&1; then
    RESULT[$d]="build OK, test PASS"
  else
    RESULT[$d]="build OK, test FAIL"
  fi
  tail -1 "$OUT/$d/test.log"
done

# generic tarball + release folder (only when the Arch package was built)
REL=$OUT/release
if [ -f "$OUT/arch/pkg/srs-sysinfo-$VER-1-x86_64.pkg.tar.zst" ]; then
  echo "===== generic tarball"
  if "$PKG/make-tarball.sh" "$OUT/ubuntu-24.04/pkg/srs-sysinfo_$VER-1.ubuntu24.04_amd64.deb" "$TARBALL" "$OUT/tarball" >"$OUT/tarball.log" 2>&1; then
    tail -1 "$OUT/tarball.log"; RESULT[tarball]="OK"
  else
    tail -20 "$OUT/tarball.log"; RESULT[tarball]="FAIL"
  fi
  if [ -z "${SRS_NO_APPIMAGE:-}" ]; then
    echo "===== appimage"
    if "$PKG/build-appimage.sh" "$TARBALL" "$OUT/appimage" >"$OUT/appimage.log" 2>&1; then
      tail -1 "$OUT/appimage.log"
      if "$PKG/test-appimage.sh" "$OUT/appimage/srs-sysinfo-$VER-x86_64.AppImage" "$VER" >"$OUT/appimage-test.log" 2>&1; then
        RESULT[appimage]="build OK, test PASS"
      else
        RESULT[appimage]="build OK, test FAIL"
      fi
      grep -E '^(PASS|FAIL):' "$OUT/appimage-test.log" || true
    else
      tail -20 "$OUT/appimage.log"; RESULT[appimage]="build FAIL"
    fi
  fi
  rm -rf "$REL"; mkdir -p "$REL"
  for d in "${DISTROS[@]}"; do [ -d "$OUT/$d/pkg" ] && cp "$OUT/$d"/pkg/* "$REL/"; done
  cp "$OUT"/tarball/*.tar.gz "$REL/" 2>/dev/null || true
  cp "$OUT"/appimage/*.AppImage "$OUT"/appimage/*.AppImage.zsync "$REL/" 2>/dev/null || true
  tar xzOf "$TARBALL" "srs-sysinfo-$VER/docs/SRS-User-Guide.pdf" >"$REL/SRS-User-Guide.pdf"
  (cd "$REL" && sha256sum -- * >SHA256SUMS)
  "$PKG/release-info.sh" notes "v$VER" >"$OUT/release-notes.md"
  echo "release files in $REL:"; (cd "$REL" && ls -l)
fi

echo "===== summary ($VER)"
for d in "${DISTROS[@]}" ${RESULT[tarball]:+tarball} ${RESULT[appimage]:+appimage}; do printf '%-22s %s\n' "$d" "${RESULT[$d]}"; done
for d in "${DISTROS[@]}" ${RESULT[tarball]:+tarball} ${RESULT[appimage]:+appimage}; do [[ ${RESULT[$d]} == *PASS || ${RESULT[$d]} == OK ]] || exit 1; done
