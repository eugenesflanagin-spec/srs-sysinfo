#!/bin/bash
# Small helper for the release workflow and build-all.sh. Run from anywhere.
#   release-info.sh version        print SRS_VERSION from srs.h (the one place the version lives)
#   release-info.sh check TAG      fail unless TAG is "v" + SRS_VERSION and CHANGELOG.md has a "## VER" section
#   release-info.sh notes TAG      print the release notes: packaging/release-notes/TAG.md if it exists,
#                                  else the "## VER" section of CHANGELOG.md
#   release-info.sh title TAG      print the release title (first "# " line of the notes, else "SRS VER")
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
version() { sed -nE 's/^#define SRS_VERSION "([^"]+)".*/\1/p' "$ROOT/srs.h"; }
changelog_section() {  # text between "## VER" and the next "## " heading
  awk -v v="$1" '/^## / { if (on) exit; if ($2 == v || $2 == "v" v) { on = 1; next } } on' "$ROOT/CHANGELOG.md"
}
notes() {
  local tag=$1 ver=${1#v} f="$ROOT/packaging/release-notes/$1.md"
  if [ -f "$f" ]; then cat "$f"
  else
    echo "# SRS $ver"; echo
    changelog_section "$ver"
    echo; echo "Each package was built and install-tested in a clean container for its distro."
  fi
}
case ${1:-} in
  version) version ;;
  check)
    tag=${2:?tag}; ver=$(version)
    [ -n "$ver" ] || { echo "::error::could not read SRS_VERSION from srs.h"; exit 1; }
    [ "$tag" = "v$ver" ] || { echo "::error::tag $tag does not match SRS_VERSION $ver in srs.h (expected tag v$ver)"; exit 1; }
    grep -qE "^## v?$ver( |\$)" "$ROOT/CHANGELOG.md" || { echo "::error::CHANGELOG.md has no '## $ver - DATE' section"; exit 1; }
    if [ -n "$(changelog_section Unreleased | grep -v '^[[:space:]]*$' || true)" ]; then
      echo "::warning::CHANGELOG.md still has entries under '## Unreleased' (move them into '## $ver' before tagging)"
    fi
    echo "OK: tag $tag matches SRS_VERSION $ver" ;;
  notes) notes "${2:?tag}" ;;
  title)
    t=$(notes "${2:?tag}" | sed -n 's/^# //p' | head -1)
    echo "${t:-SRS ${2#v}}" ;;
  *) sed -n '2,8p' "$0"; exit 2 ;;
esac
