#!/usr/bin/env bash
# tests/srs-root-args-test.sh - the input checks of `srs-root --acpi-eval METHOD [ARG...]` (2.4). srs-root runs as root
# and hands the method and arguments to the acpi_call module, so these two checks are what stands between a caller and
# root: only an ACPI name path, and only numbers, "simple strings" and bXXXX buffers as arguments.
#
#   tests/srs-root-args-test.sh [ANY]      (the argument is ignored; make test-bench passes the CLI path)
#
# 2.6.0 also: the checks that come before anything is touched (srs-root without root, with --help, with an unknown
# option or with --user and no name used to run a full collection over the user's good cache), and the step that puts
# a finished collection in place of the cache.
#
# SAFETY: no collection is ever run, and nothing is run as root.
#   - The two [[ =~ ]] patterns are copied out of the script text and applied here with the same bash quoting rules
#     (eval of the very same text), so a change to either pattern in srs-root.sh is what gets tested.
#   - The script itself is started only (a) when this test is NOT root, (b) after its text has been checked: the
#     argument check and the root check come before the first line that could create or change anything, and (c) with
#     SRS_ROOT_TEST_HOME pointing at a throw-away home, so even a broken check could not reach the real cache. The runs
#     either end at "needs root" or, with SRS_ROOT_TEST_DRYRUN=1, at "dry run", where the work would begin.
#   - install_cache (the step that runs as the user) is copied out of the script and run here on throw-away folders.
# The root-only part (collecting in /run, the switch to the user, the lock) cannot be tested without root.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT=$SRC/srs-root.sh
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

echo "== srs-root.sh parses"
bash -n "$SCRIPT" 2>"${TMPDIR:-/tmp}/srs-root-n.$$" && ok "bash -n srs-root.sh" || bad "bash -n: $(cat "${TMPDIR:-/tmp}/srs-root-n.$$")"
rm -f "${TMPDIR:-/tmp}/srs-root-n.$$"

echo "== the two patterns, taken from srs-root.sh"
MRE=$(sed -n 's/^ *\[\[ \$m =~ \(.*\) \]\] || { echo "srs-root: not an ACPI method path.*/\1/p' "$SCRIPT")
ARE=$(sed -n 's/^ *\[\[ \$a =~ \(.*\) \]\] || { echo "srs-root: argument not allowed.*/\1/p' "$SCRIPT")
[ "$(wc -l <<<"$MRE")" = 1 ] && [ -n "$MRE" ] && ok "method pattern: $MRE" || { bad "the method pattern was not found once in srs-root.sh"; exit 1; }
[ "$(wc -l <<<"$ARE")" = 1 ] && [ -n "$ARE" ] && ok "argument pattern: $ARE" || { bad "the argument pattern was not found once in srs-root.sh"; exit 1; }
grep -q '\[ \$# -le 7 \]' "$SCRIPT" && ok "at most 7 arguments (the check is in the script)" || bad "the 7-argument limit is gone"
# the call is built only from the checked values, and written with printf '%s' (no shell expansion of them)
grep -q "printf '%s%s' \"\$m\" \"\$args\" > /proc/acpi/call" "$SCRIPT" && ok "the call is written with printf '%s', from the checked values" || bad "the /proc/acpi/call write changed"

m_ok() { local s=$1; eval "[[ \$s =~ $MRE ]]"; }
a_ok() { local s=$1; eval "[[ \$s =~ $ARE ]]"; }

echo "== method paths"
for s in '\_SB.PCI0.GPP0._STA' '_TZ.TZ01._TMP' '\_SB' '^^_SB.PCI0' 'SB.AMW0.WMAX' '\_SB_.PCI0.SBRG.EC0_._Q11'; do
    m_ok "$s" && ok "allowed: $s" || bad "refused but valid: $s"
done
for s in '\_SB.PCI0; rm -rf /' '$(id)' 'a;b' '1 2' '' '\_sb.pci0' '\_SB.TOOLONG' '\_SB..X' '\\_SB' '\_SB.PCI0.' '_SB/PCI0' \
         $'\\_SB.PCI0\n_STA' '`id`' '\_SB.PCI0 0x1' '0X1F'; do
    m_ok "$s" && bad "allowed but must be refused: [$s]" || ok "refused: [${s//$'\n'/\\n}]"
done

echo "== arguments"
for s in 0x1F 42 0 0xFFFFFFFFFFFFFFFF '"abc def"' '""' '"A-1_b.c"' b0102 b0102ff b00; do
    a_ok "$s" && ok "allowed: $s" || bad "refused but valid: $s"
done
# b0102f is an odd number of hex digits (b0102ff is three whole bytes, so it is allowed above)
for s in b0102f '\_SB.PCI0; rm -rf /' '$(id)' 'a;b' '1 2' '' 0x 0x12345678901234567 123456789012345678901 '"abc"def"' '"a;b"' \
         '"$(id)"' "'single'" b b0 -1 0xG1 $'1\n2' '"a\"b"' '`id`' '"tab	here"' "\"$(printf 'x%.0s' $(seq 1 65))\"" B0102 b01xy; do
    a_ok "$s" && bad "allowed but must be refused: [$s]" || ok "refused: [${s//$'\n'/\\n}]"
done
echo "== nothing dangerous passes either pattern"
for s in '\_SB.PCI0; rm -rf /' '$(id)' 'a;b' '1 2' '`id`' '$HOME' '|cat' '>/etc/x' '&&' $'x\ny'; do
    { m_ok "$s" || a_ok "$s"; } && bad "a pattern lets [$s] through" || ok "neither pattern lets [${s//$'\n'/\\n}] through"
done


# ------------------------------------------------------------------ 2.6.0: checked before anything is touched
echo "== the script's text: arguments and root are checked before the first line that changes anything"
G=$(grep -n 'srs-root: needs root (use: pkexec srs-root). Nothing was changed.' "$SCRIPT" | head -1 | cut -d: -f1)
C=$(grep -n '^case "\${1:-}" in' "$SCRIPT" | head -1 | cut -d: -f1)
[ -n "$G" ] && [ -n "$C" ] && [ "$C" -lt "$G" ] && ok "the argument check (line $C) comes before the root check (line $G)" || { bad "argument check / root check not found in that order"; G=0; }
# before the root check: no command that creates, changes or deletes (comments and >&2 do not count)
early=$(head -n "$G" "$SCRIPT" | grep -v '^ *#' | grep -nE '(^|[;&|{ ])(mkdir|mktemp|rm|mv|cp|chown|chmod|touch|install|tee|cd|trap|modprobe|dd|flock|exec)( |$)|>>|[^0-9&]>[^&]')
[ "$G" -gt 0 ] && [ -z "$early" ] && ok "nothing before the root check creates, changes or deletes anything" || { bad "a command before the root check:"; printf '        %s\n' "$early"; G=0; }
grep -q '^if \[ "\$me" != 0 \]; then$' "$SCRIPT" && grep -q '^  test_home=\${SRS_ROOT_TEST_HOME:-}$' "$SCRIPT" && ok "the test seams are read only when not root" || { bad "the test seams are not inside the not-root branch"; G=0; }
# root never writes inside the user's home: no chown / mkdir / redirect on \$out or \$home outside install_cache and as_user
outside=$(sed '/^install_cache() {$/,/^}$/d' "$SCRIPT" | grep -v '^ *#' | grep -nE '(^|[;&|{ ])(chown|chmod|mkdir|rm|mv|cp|touch|ln) [^|]*"?\$(out|home)|[^-]>>? *"?\$(out|home)' | grep -v 'as_user')
[ -z "$outside" ] && ok "outside the step that runs as the user, nothing creates or changes a file in the user's home" || { bad "root touches the user's home:"; printf '        %s\n' "$outside"; }
grep -q '^chown -R "\$uid:\$gid" "\$new"$' "$SCRIPT" && grep -q 'new=\$(mktemp -d /run/srs-root.XXXXXX' "$SCRIPT" && ok "the collection goes into a new folder in /run, given to the user when complete" || bad "the staging folder changed"
grep -q "^if as_user bash -c \"\\\$(declare -f install_cache)\"'; install_cache \"\\\$1\" \"\\\$2\"' bash \"\\\$out\" \"\\\$new\"; then\$" "$SCRIPT" && ok "the cache is replaced by install_cache, run as the user" || bad "the install step changed"
grep -q '^lock 180 || ' "$SCRIPT" && grep -q 'exec 8< /run/srs-root.lock' "$SCRIPT" && ok "one srs-root at a time (a lock in /run)" || bad "the lock is gone"

if [ "$(id -u)" = 0 ]; then
    echo "  skip  running the script: this test is root, and srs-root is never run as root by a test"
elif [ "$G" = 0 ]; then
    bad "the script is not started: its text did not pass the checks above"
else
    T=$(mktemp -d)
    trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
    H=$T/home
    mkdir -p "$H/.cache/srs/root/acpi-tables" "$H/.config/srs"
    printf 'SENTINEL dmidecode\n' >"$H/.cache/srs/root/dmidecode.txt"; printf 'SENTINEL aod\n' >"$H/.cache/srs/root/aod.bin"
    printf 'SENTINEL table\n' >"$H/.cache/srs/root/acpi-tables/DSDT"; printf 'SENTINEL call\n' >"$H/.cache/srs/root/acpi-calls.txt"
    printf 'SENTINEL ec\n' >"$H/.cache/srs/root/ec.bin"; printf 'SENTINEL umc\n' >"$H/.cache/srs/root/umc.txt"
    # every name, kind, mode and link target, every file's time and content (a folder's own time changes when a
    # temporary name comes and goes beside the cache, which is not a change to the cache)
    fp() { (cd "$1" && find . \( -type d -printf 'd %m %p\n' \) -o \( -printf '%y %m %T@ %p -> %l\n' \) | sort; find . -type f -print0 | sort -z | xargs -0 sha256sum 2>&1) | sha256sum; }
    FP=$(fp "$H")
    # R WANT-EXIT WANT-TEXT TITLE ENV... -- ARGS...
    R() {
        local want=$1 text=$2 title=$3 extra=() out rc; shift 3
        while [ "$1" != -- ]; do extra+=("$1"); shift; done
        shift
        out=$(cd "$T" && env -u PKEXEC_UID -u SUDO_UID SRS_ROOT_TEST_HOME="$H" "${extra[@]}" bash "$SCRIPT" "$@" 2>&1); rc=$?
        if [ "$rc" != "$want" ]; then bad "$title: exit $rc (want $want): $(head -2 <<<"$out")"
        elif ! grep -qF -- "$text" <<<"$out"; then bad "$title: no '$text' in: $(head -2 <<<"$out")"
        elif [ "$(fp "$H")" != "$FP" ]; then bad "$title: the cache was changed"
        else ok "$title"; fi
    }
    echo "== not root: refused, and the cache is as it was"
    R 2 "needs root (use: pkexec srs-root). Nothing was changed." "no arguments, not root: refused (exit 2)" --
    R 2 "needs root" "--user NAME, not root: refused" -- --user "$(id -un)"
    R 2 "needs root" "--acpi-eval, not root: refused" -- --acpi-eval '\_SB.PCI0._STA'
    echo "== --help, unknown options, --user without a name"
    R 0 "usage: pkexec srs-root" "--help: the usage text, exit 0, nothing collected" -- --help
    R 0 "usage: pkexec srs-root" "-h: the same" -- -h
    R 2 "unknown option: --bogus" "an unknown option: exit 2, nothing collected" -- --bogus
    R 2 "unknown option: --users" "a misspelt option: exit 2" -- --users nobody
    R 2 "unknown option: collect" "a stray word: exit 2" -- collect
    R 2 "--user needs one name" "--user with no name: exit 2, nothing collected" -- --user
    R 2 "--user needs one name" "--user with an empty name: exit 2" -- --user ""
    R 2 "--user needs one name" "--user with two names: exit 2" -- --user a b
    R 2 "an empty argument" "an empty argument: exit 2" -- ""
    R 2 "unknown option: --help-me" "--help-me is not --help" -- --help-me
    echo "== the same checks with the dry run (where a root run would start working)"
    D=(SRS_ROOT_TEST_DRYRUN=1)
    R 0 "dry run (test): would collect for $(id -un) into $H/.cache/srs/root" "no arguments: accepted, would collect into the user's cache" "${D[@]}" --
    R 0 "dry run (test): would collect for $(id -un) into" "--user NAME: accepted" "${D[@]}" -- --user "$(id -un)"
    R 2 "no such user: no-such-user-srs-test" "--user with an unknown name: exit 2" "${D[@]}" -- --user no-such-user-srs-test
    R 0 'dry run (test): would evaluate \_SB.PCI0._STA 0x1 "a b"' "--acpi-eval with a valid call: accepted" "${D[@]}" -- --acpi-eval '_SB.PCI0._STA' 0x1 '"a b"'
    R 2 "not an ACPI method path" "--acpi-eval with a bad method: exit 2" "${D[@]}" -- --acpi-eval '_SB.PCI0; id'
    R 2 "argument not allowed" "--acpi-eval with a bad argument: exit 2" "${D[@]}" -- --acpi-eval '\_SB._STA' '$(id)'
    R 2 "at most 7 arguments" "--acpi-eval with 8 arguments: exit 2" "${D[@]}" -- --acpi-eval '\_SB._STA' 1 2 3 4 5 6 7 8
    R 2 "not an ACPI method path" "--acpi-eval with no method: exit 2" "${D[@]}" -- --acpi-eval
    R 2 "unknown option: --bogus" "the dry run does not let an unknown option through" "${D[@]}" -- --bogus
    R 0 "usage: pkexec srs-root" "PKEXEC_UID / SUDO_UID from the caller's environment are not used when not root" "${D[@]}" PKEXEC_UID=0 SUDO_UID=0 -- --help
    [ -z "$(find "$T" -mindepth 1 -maxdepth 1 ! -name home)" ] && [ ! -e "$H/.cache/srs/root.new" ] && ok "none of these runs left a file behind" || bad "files were left behind: $(ls -A "$T")"

    echo "== install_cache: a finished collection replaces the cache (run here as this user, on throw-away folders)"
    FN=$(sed -n '/^install_cache() {$/,/^}$/p' "$SCRIPT")
    [ -n "$FN" ] && ok "install_cache copied out of srs-root.sh ($(wc -l <<<"$FN") lines)" || bad "install_cache not found"
    ic() { bash -c "$FN"'; install_cache "$1" "$2"' bash "$1" "$2"; }   # as srs-root runs it (there through runuser)
    mknew() { rm -rf "$T/new"; mkdir -p "$T/new/acpi-tables"; printf 'NEW dmidecode\n' >"$T/new/dmidecode.txt"; printf 'NEW table\n' >"$T/new/acpi-tables/DSDT"; date >"$T/new/collected.txt"; }
    left() { find "$(dirname "$1")" -maxdepth 1 \( -name 'root.new.*' -o -name 'root.old.*' \) | grep -q .; }
    O=$T/u1/.cache/srs/root
    mknew; ic "$O" "$T/new"; rc=$?
    [ $rc = 0 ] && [ "$(cat "$O/dmidecode.txt")" = "NEW dmidecode" ] && [ -s "$O/collected.txt" ] && [ -f "$O/acpi-tables/DSDT" ] && ! left "$O" && ok "no cache yet: created, with its folders" || bad "first install: rc $rc"
    [ -f "$T/new/dmidecode.txt" ] && ok "the collected folder itself is left alone (srs-root removes it)" || bad "the source folder was changed"
    O=$H/.cache/srs/root
    mknew; ic "$O" "$T/new"; rc=$?
    [ $rc = 0 ] && [ "$(cat "$O/dmidecode.txt")" = "NEW dmidecode" ] && [ ! -e "$O/aod.bin" ] && [ ! -e "$O/ec.bin" ] && ! left "$O" && ok "an existing cache: replaced whole (files the new run did not write are gone)" || bad "replace: rc $rc: $(ls "$O" 2>&1 | tr '\n' ' ')"
    [ "$(cat "$O/acpi-calls.txt" 2>/dev/null)" = "SENTINEL call" ] && ok "acpi-calls.txt is carried over" || bad "acpi-calls.txt was lost"
    FP=$(fp "$H")
    mknew; rm -f "$T/new/collected.txt"; ic "$O" "$T/new" 2>/dev/null; rc=$?
    [ $rc != 0 ] && [ "$(fp "$H")" = "$FP" ] && ok "a collection that is not complete (no collected.txt): refused, the cache is as it was" || bad "incomplete collection: rc $rc"
    mknew; printf 'x\n' >"$T/new/unreadable.bin"; chmod 000 "$T/new/unreadable.bin"; ic "$O" "$T/new" 2>/dev/null; rc=$?
    [ $rc != 0 ] && [ "$(fp "$H")" = "$FP" ] && ok "a copy that fails half way: refused, the cache is as it was, nothing left behind" || bad "failed copy: rc $rc; $(ls -A "$H/.cache/srs" | tr '\n' ' ')"
    chmod 600 "$T/new/unreadable.bin"
    # links planted by the user must never be written through
    V=$T/victim; mkdir -p "$V"; printf 'VICTIM\n' >"$V/passwd"; printf 'VICTIM\n' >"$V/dmidecode.txt"; VFP=$(fp "$V")
    O=$T/u2/.cache/srs/root; mkdir -p "$O"; ln -s "$V/passwd" "$O/dmidecode.txt"; ln -s "$V/passwd" "$O/collected.txt"; ln -s "$V/passwd" "$O/acpi-calls.txt"
    mknew; ic "$O" "$T/new"; rc=$?
    [ $rc = 0 ] && [ "$(fp "$V")" = "$VFP" ] && [ ! -L "$O/dmidecode.txt" ] && [ "$(cat "$O/dmidecode.txt")" = "NEW dmidecode" ] && ok "links planted inside the cache are replaced, never written through" || bad "a planted link: rc $rc"
    [ ! -e "$O/acpi-calls.txt" ] && ok "an acpi-calls.txt that is a link is not carried over" || bad "a linked acpi-calls.txt was carried over"
    O=$T/u3/.cache/srs/root; mkdir -p "$(dirname "$O")"; ln -s "$V" "$O"
    mknew; ic "$O" "$T/new"; rc=$?
    [ $rc = 0 ] && [ "$(fp "$V")" = "$VFP" ] && [ ! -L "$O" ] && [ -d "$O" ] && ok "a cache folder that is itself a link: the link is replaced by a real folder, its target is untouched" || bad "cache-is-a-link: rc $rc"
    O=$T/u4/.cache/srs/root; mkdir -p "$O.old.abc123/root"; printf 'OLD\n' >"$O.old.abc123/root/dmidecode.txt"
    mknew; rm -f "$T/new/collected.txt"; ic "$O" "$T/new" 2>/dev/null; rc=$?
    [ $rc != 0 ] && [ "$(cat "$O/dmidecode.txt" 2>/dev/null)" = "OLD" ] && ! left "$O" && ok "an earlier run cut off in the middle of the swap: its old cache is put back" || bad "cut-off recovery: rc $rc"
    mknew; ic "$O" "$T/new"; rc=$?
    [ $rc = 0 ] && [ "$(stat -c %a "$O")" = 700 ] && ok "the cache folder is the user's alone (mode 700)" || bad "mode: $(stat -c %a "$O" 2>&1)"
fi

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
