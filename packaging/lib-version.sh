# shellcheck shell=bash
# Version stamping helpers, sourced by the build scripts inside the build
# containers (needs only bash, sed, awk and GNU date).
#
# The committed debian/changelog, rpm/srs-sysinfo.spec and man/*.1 are
# templates: at build time the version from the source (SRS_VERSION in srs.h,
# checked against the git tag by the workflow) is written into a copy of them.
# Nothing has to be edited by hand for a new release.

PKG_MAINT="Eugene Flanagin <eflanagin@pm.me>"

# release_date VER CHANGELOG.md -> YYYY-MM-DD from the "## VER - YYYY-MM-DD"
# heading, or today's date (UTC) if there is no such heading yet.
release_date() {
  local d
  d=$(sed -nE "s/^## +v?$1 +- +([0-9]{4}-[0-9]{2}-[0-9]{2}).*/\1/p" "$2" | head -1)
  [ -n "$d" ] || d=$(date -u +%F)
  echo "$d"
}

# stamp_debian_changelog FILE VER DATE: add a "VER-1" entry on top unless the
# top entry is already VER-1.
stamp_debian_changelog() {
  local f=$1 ver=$2 date=$3 tmp
  head -1 "$f" | grep -q "^srs-sysinfo ($ver-1)" && return 0
  tmp=$(mktemp)
  {
    echo "srs-sysinfo ($ver-1) unstable; urgency=medium"
    echo
    echo "  * Package of upstream release $ver (see CHANGELOG.md)."
    echo
    echo " -- $PKG_MAINT  $(LC_ALL=C date -u -R -d "$date 12:00:00")"
    echo
    cat "$f"
  } >"$tmp"
  cat "$tmp" >"$f"; rm -f "$tmp"
}

# stamp_spec FILE VER DATE: set Version: and add a %changelog entry for VER-1
# unless one is there already.
stamp_spec() {
  local f=$1 ver=$2 date=$3 line
  sed -i -E "s/^(Version:[[:space:]]+).*/\1$ver/" "$f"
  grep -qE "^\* .* - $ver-1\$" "$f" && return 0
  line="* $(LC_ALL=C date -u -d "$date" '+%a %b %d %Y') $PKG_MAINT - $ver-1"
  awk -v l1="$line" -v l2="- Package of upstream release $ver (see CHANGELOG.md)" \
    '{ print } /^%changelog/ { print l1; print l2; print "" }' "$f" >"$f.new" && mv "$f.new" "$f"
}

# stamp_man VER DATE FILE...: set the version and month in the .TH line.
stamp_man() {
  local ver=$1 date=$2 month; shift 2
  month=$(LC_ALL=C date -u -d "$date" '+%B %Y')
  sed -i -E "1s/^(\.TH [^ ]+ [0-9]+) \"[^\"]*\" \"srs-sysinfo [^\"]*\"/\1 \"$month\" \"srs-sysinfo $ver\"/" "$@"
}
