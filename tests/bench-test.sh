#!/usr/bin/env bash
# tests/bench-test.sh - correctness tests for the SRS Pi, Crypto and Prime sieve benchmarks.
# Short runs only (about 15 s in all, at most 4 threads), so it is safe on a desktop.
#
#   tests/bench-test.sh [BINARY]        default: dist/native/sysreport-search (make native)
#
# SRS Pi is checked twice: by its own built-in SHA-256 check, and - when python3 + mpmath are
# installed - digit for digit against digits computed independently by mpmath.
# Results go to a throw-away HOME, so ~/.cache/srs and the run history are not touched.
set -u
BIN=${1:-dist/native/sysreport-search}
BIN=$(readlink -f "$BIN")
[ -x "$BIN" ] || { echo "no binary: $BIN (run make native)"; exit 2; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

echo "== crypto self-test (FIPS 180-4 / FIPS 197 vectors, hardware vs plain C)"
out=$("$BIN" crypto --threads 1 --seconds 0.1) && grep -q "test vectors pass" <<<"$out" && ok "$(grep Check <<<"$out" | sed 's/  */ /g')" || bad "crypto self-test"

echo "== prime sieve against known prime counts"
for spec in "1e7 1" "1e8 1" "1e8 4" "1e9 4" "12345 1"; do
    set -- $spec
    out=$("$BIN" prime --limit "$1" --threads "$2"); rc=$?
    cnt=$(sed -n 's/^Primes found *//p' <<<"$out")
    case $1 in
        1e7) want=664579 ;; 1e8) want=5761455 ;; 1e9) want=50847534 ;; 12345) want=1474 ;;
    esac
    [ "$rc" = 0 ] && [ "$cnt" = "$want" ] && ok "pi($1) = $cnt with $2 thread(s)" || bad "pi($1): got '$cnt', want $want (exit $rc)"
done

echo "== SRS Pi: built-in check"
for spec in "128k chudnovsky 1" "1m chudnovsky 1" "1m chudnovsky 4" "128k agm 1" "1m agm 1" "5000 chudnovsky 2" "16ki chudnovsky 1" "1mi chudnovsky 4"; do
    set -- $spec
    out=$("$BIN" pi --digits "$1" --method "$2" --threads "$3" --save-digits "$TMP/pi-$1-$2-$3.txt"); rc=$?
    t=$(sed -n 's/^Time *//p' <<<"$out")
    chk=$(sed -n 's/^Check *//p' <<<"$out")
    if [ "$1" = 5000 ]; then   # no checksum for odd sizes: the first 100 digits are checked
        [ "$rc" = 0 ] && grep -q "first 100 digits right" <<<"$chk" && ok "$1 digits, $2, $3 thread(s): $t, $chk" || bad "$1 $2 $3: $chk (exit $rc)"
    else
        [ "$rc" = 0 ] && grep -q "every digit right" <<<"$chk" && ok "$1 digits, $2, $3 thread(s): $t" || bad "$1 $2 $3: $chk (exit $rc)"
    fi
done

echo "== SRS Pi: digit for digit against mpmath (independent library)"
if python3 -I -c "import mpmath" 2>/dev/null; then
    python3 -I - "$TMP" <<'EOF'
import sys, glob, os, mpmath
from mpmath.libmp import to_fixed
tmp = sys.argv[1]
mpmath.mp.prec = int(1048640 * 3.3219280948873626) + 64
man = to_fixed(mpmath.mpf(mpmath.pi)._mpf_, mpmath.mp.prec)
ref = str((man * 10**1048600) >> mpmath.mp.prec)[1:]
bad = 0
for f in sorted(glob.glob(os.path.join(tmp, "pi-*.txt"))):
    d = open(f).read()
    good = ref[:len(d)] == d
    bad |= not good
    print("  %-5s %s: %d digits %s" % ("ok" if good else "FAIL", os.path.basename(f), len(d), "identical to mpmath" if good else "DIFFER from mpmath"))
sys.exit(bad)
EOF
    [ $? = 0 ] || fail=1
else
    echo "  skip  python3 mpmath not installed"
fi

echo "== bad input is refused"
"$BIN" pi --digits 50 >/dev/null 2>&1 && bad "pi --digits 50 accepted" || ok "pi --digits 50 refused"
"$BIN" prime --limit 2e11 >/dev/null 2>&1 && bad "prime --limit 2e11 accepted" || ok "prime --limit 2e11 refused"
[ -s "$HOME/.cache/srs/bench-history.tsv" ] && ok "history written ($(($(wc -l <"$HOME/.cache/srs/bench-history.tsv") - 1)) runs)" || bad "no bench-history.tsv"

[ $fail = 0 ] && echo "ALL PASSED" || echo "SOME TESTS FAILED"
exit $fail
