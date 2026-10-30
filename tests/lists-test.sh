#!/usr/bin/env bash
# tests/lists-test.sh - Update hardware lists (2.4, lists.c; CLI update-lists): a download is checked before it replaces
# the copy in use (its header, a sane number of entries, not a web page), the previous copy is kept as <name>.old, a
# pnp.ids (no version line) is taken only when it has more entries than the copy in use, and --list names the copy in
# use (the user's or the system's, whichever is newer).
#
#   tests/lists-test.sh SYSREPORT-SEARCH      (make test-bench passes the path)
#
# SAFETY: NO network. SRS_TEST_LISTS_DIR makes SRS take the three lists from file:// URLs in a folder made here, and the
# proxy variables point at a closed local port as a second line, so a real download could not get out either. The
# lists are written to a throw-away XDG_DATA_HOME. The system's /usr/share/hwdata lists are only read (their version
# and the number of pnp.ids entries), never changed.
set -u
BIN=${1:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH"; exit 2; }
command -v curl >/dev/null || { echo "  skip  no curl (the test serves the lists as file:// URLs, which only curl reads)"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP XDG_STATE_HOME=$TMP/home/.local/state
export http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 HTTP_PROXY=http://127.0.0.1:9 HTTPS_PROXY=http://127.0.0.1:9 ALL_PROXY=http://127.0.0.1:9
unset no_proxy NO_PROXY
mkdir -p "$HOME"
SRV=$TMP/serve
LISTS=$TMP/srs/lists
SYS=/usr/share/hwdata
mkdir -p "$SRV"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
skip() { printf '  skip  %s\n' "$*"; }

# made-up lists in the real files' format
mk_pci() {   # VERSION ENTRIES
    printf '#\n#\tList of PCI ID'"'"'s\n#\n#\tVersion: %s\n#\tDate:    %s 03:15:02\n#\n' "$1" "${1//./-}"
    seq 1 "$2" | awk '{ printf "%04x  Test vendor %d\n", $1 % 65536, $1 }'
}
mk_usb() {   # VERSION ENTRIES
    printf '#\n#\tList of USB ID'"'"'s\n#\n# Version: %s\n# Date:    %s 20:34:01\n#\n' "$1" "${1//./-}"
    seq 1 "$2" | awk '{ printf "%04x  Test maker %d\n", $1 % 65536, $1 }'
}
mk_pnp() {   # ENTRIES (no header, no version: like hwdata's pnp.ids)
    seq 1 "$1" | awk '{ printf "%c%c%c\tTest maker %d\n", 65 + int($1 / 676) % 26, 65 + int($1 / 26) % 26, 65 + $1 % 26, $1 }'
}
entries() { grep -cvE '^(#|$)' "$1"; }
run() { OUT=$(SRS_TEST_LISTS_DIR=$SRV timeout 120 "$BIN" update-lists "$@" 2>&1 </dev/null); RC=$?; }
has() { grep -qE "$1" <<<"$OUT"; }
leftovers() { ls -A "$LISTS" 2>/dev/null | grep -q '\.download$'; }

[ -r "$SYS/pci.ids" ] || { echo "  skip  no $SYS/pci.ids on this system"; exit 0; }
SPNP=-1; [ -r "$SYS/pnp.ids" ] && SPNP=$(entries "$SYS/pnp.ids")

echo "== CLI basics"
OUT=$("$BIN" update-lists --help 2>&1); [ $? = 0 ] && grep -q -- "--list" <<<"$OUT" && ok "--help" || bad "--help: $OUT"
"$BIN" update-lists --bogus >/dev/null 2>&1; [ $? = 2 ] && ok "an unknown option: exit 2" || bad "unknown option not refused"
mk_pci 2099.01.01 20500 > "$SRV/pci.ids"
mk_usb 2099.01.01 10500 > "$SRV/usb.ids"
mk_pnp 1600 > "$SRV/pnp.ids"
run
[ $RC = 1 ] && has "not downloaded" && [ ! -e "$LISTS/pci.ids" ] && ok "no --yes and no terminal to answer: nothing downloaded" || bad "without --yes: rc=$RC $OUT"
has "Download fresh copies from:" && has "pci-ids.ucw.cz" && ok "the question names the real sources" || bad "the question does not name the sources"

echo "== 1: newer pci.ids and usb.ids are taken, a pnp.ids with fewer entries than the system's is not"
run --yes
[ $RC = 0 ] && ok "exit 0" || bad "exit $RC: $OUT"
has '^pci\.ids +2099\.01\.01 \(was [0-9.]+, newer\)' && ok "pci.ids 2099.01.01, newer" || bad "pci.ids report: $(grep '^pci' <<<"$OUT")"
has '^usb\.ids +2099\.01\.01 \(was [0-9.]+, newer\)' && ok "usb.ids 2099.01.01, newer" || bad "usb.ids report: $(grep '^usb' <<<"$OUT")"
cmp -s "$SRV/pci.ids" "$LISTS/pci.ids" && cmp -s "$SRV/usb.ids" "$LISTS/usb.ids" && ok "both kept in XDG_DATA_HOME/srs/lists, byte for byte" || bad "the lists were not stored"
if [ "$SPNP" -gt 1600 ]; then
    has '^pnp\.ids +1600 entries, the same as the copy in use: no newer version yet' && [ ! -e "$LISTS/pnp.ids" ] && ok "pnp.ids with 1600 entries (system: $SPNP) is not taken" || bad "pnp.ids fewer: $(grep '^pnp' <<<"$OUT")"
else skip "the system pnp.ids has $SPNP entries: the 'fewer' case needs more than 1600"; fi
! leftovers && ok "no .download files left behind" || bad "a .download file was left"
ls "$LISTS"/*.old >/dev/null 2>&1 && bad "a .old copy without an earlier copy" || ok "no .old yet (there was no earlier copy)"
OUT=$("$BIN" update-lists --list 2>&1); RC=$?
[ $RC = 0 ] && grep -qE "^pci\.ids +2099\.01\.01 +$LISTS/pci\.ids +\[your copy \(Update hardware lists\)\]" <<<"$OUT" && ok "--list: pci.ids 2099.01.01, your copy" || bad "--list pci: $(grep '^pci' <<<"$OUT")"
grep -qE "^pnp\.ids +[0-9]+ entries +$SYS/pnp\.ids +\[system \(hwdata\)\]" <<<"$OUT" || [ "$SPNP" -lt 0 ] && ok "--list: pnp.ids is the system's" || bad "--list pnp: $(grep '^pnp' <<<"$OUT")"

echo "== 2: a web page and a too-short list are refused, the copies in use stay"
printf '<!DOCTYPE html>\n<html><head><title>502 Bad Gateway</title></head>\n<body>Bad Gateway</body></html>\n' > "$SRV/pci.ids"
mk_usb 2099.03.01 50 > "$SRV/usb.ids"
[ "$SPNP" -ge 1500 ] && mk_pnp "$SPNP" > "$SRV/pnp.ids"
run --yes
has '^pci\.ids +not updated: the server sent a web page, not the list \(the copy in use stays\)' && ok "an HTML error page is refused" || bad "HTML: $(grep '^pci' <<<"$OUT")"
has '^usb\.ids +not updated: only 50 entries \(at least 10000 expected\)' && ok "a 50-entry usb.ids is refused" || bad "short usb: $(grep '^usb' <<<"$OUT")"
cmp -s "$LISTS/pci.ids" <(mk_pci 2099.01.01 20500) && cmp -s "$LISTS/usb.ids" <(mk_usb 2099.01.01 10500) && ok "the copies in use are unchanged" || bad "a refused download replaced the copy in use"
ls "$LISTS"/*.old >/dev/null 2>&1 && bad "a refused download made a .old copy" || ok "no .old made by a refused download"
if [ "$SPNP" -ge 1500 ]; then
    has "^pnp\.ids +$SPNP entries, the same as the copy in use: no newer version yet" && [ ! -e "$LISTS/pnp.ids" ] && ok "pnp.ids with as many entries as the system's ($SPNP) is not taken" || bad "pnp.ids equal: $(grep '^pnp' <<<"$OUT")"
else skip "the system pnp.ids has $SPNP entries (fewer than the 1500 a download needs): the 'equal' case is not checked"; fi
! leftovers && ok "no .download files left behind" || bad "a .download file was left"

echo "== 3: a newer pci.ids replaces the old one (kept as pci.ids.old); a pnp.ids with more entries is taken"
mk_pci 2099.02.01 20600 > "$SRV/pci.ids"
rm -f "$SRV/usb.ids"
NP=$(( (SPNP > 1600 ? SPNP : 1600) + 40 ))
mk_pnp "$NP" > "$SRV/pnp.ids"
run --yes
[ $RC = 0 ] && ok "exit 0" || bad "exit $RC"
has '^pci\.ids +2099\.02\.01 \(was 2099\.01\.01, newer\)' && ok "pci.ids 2099.02.01 (was 2099.01.01, newer)" || bad "pci.ids: $(grep '^pci' <<<"$OUT")"
cmp -s "$LISTS/pci.ids" "$SRV/pci.ids" && cmp -s "$LISTS/pci.ids.old" <(mk_pci 2099.01.01 20500) && ok "pci.ids is the new one, pci.ids.old the one before" || bad "pci.ids / pci.ids.old wrong"
has '^usb\.ids +not updated: the download failed \(exit [0-9]+' && cmp -s "$LISTS/usb.ids" <(mk_usb 2099.01.01 10500) && ok "a failed download (no file): refused, usb.ids stays" || bad "missing usb: $(grep '^usb' <<<"$OUT")"
has "^pnp\.ids +$NP entries \(was ([0-9]+ entries|none), newer\)" && cmp -s "$LISTS/pnp.ids" "$SRV/pnp.ids" && ok "pnp.ids with $NP entries is taken" || bad "pnp more: $(grep '^pnp' <<<"$OUT")"
OUT=$("$BIN" update-lists --list 2>&1)
grep -qE "^pnp\.ids +$NP entries +$LISTS/pnp\.ids +\[your copy" <<<"$OUT" && ok "--list: pnp.ids is now your copy ($NP entries)" || bad "--list pnp: $(grep '^pnp' <<<"$OUT")"
grep -qE "^pci\.ids +2099\.02\.01 " <<<"$OUT" && ok "--list: pci.ids 2099.02.01" || bad "--list pci: $(grep '^pci' <<<"$OUT")"

echo "== 4: a list that does not start like pci.ids is refused; the same pnp.ids again is not taken"
{ echo "# Some other list"; seq 1 21000 | awk '{ printf "%04x  x\n", $1 % 65536 }'; } > "$SRV/pci.ids"
run --yes
has '^pci\.ids +not updated: it does not start like pci\.ids' && cmp -s "$LISTS/pci.ids" <(mk_pci 2099.02.01 20600) && ok "no 'List of PCI ID' header: refused, the copy stays" || bad "no magic: $(grep '^pci' <<<"$OUT")"
has "^pnp\.ids +$NP entries, the same as the copy in use: no newer version yet" && ok "the same pnp.ids again: not taken" || bad "pnp same: $(grep '^pnp' <<<"$OUT")"

echo "== 5: a copy older than the system's is not used"
mk_usb 2000.01.01 10500 > "$LISTS/usb.ids"
OUT=$("$BIN" update-lists --list 2>&1)
grep -qE "^usb\.ids +[0-9.]+ +$SYS/usb\.ids +\[system \(hwdata\)\]" <<<"$OUT" && ok "usb.ids 2000.01.01 in your folder: the system's newer one is used" || bad "older copy: $(grep '^usb' <<<"$OUT")"

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
