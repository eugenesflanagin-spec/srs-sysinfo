#!/usr/bin/env bash
# tests/bench-test.sh - correctness tests for the SRS Pi, Crypto and Prime sieve benchmarks, and the
# ask-first checks of bench / ipi / latency / pi / crypto / prime (and a tiny run of the first three).
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

echo "== every benchmark asks first (no terminal, no --yes: not started, nothing written)"
ASK=$TMP/ask-home
mkdir -p "$ASK"
for c in "bench pp --seconds 0.1" "ipi --cpus 2 --rounds 1000" "latency --max 1" "pi --digits 5000" "crypto --threads 1 --seconds 0.1" "prime --limit 1e7 --threads 1"; do
    out=$(HOME=$ASK XDG_CACHE_HOME=$ASK/.cache "$BIN" $c </dev/null 2>&1 >/dev/null); rc=$?
    [ "$rc" = 2 ] && grep -q "not started" <<<"$out" && grep -q "pass --yes" <<<"$out" && [ -z "$(ls -A "$ASK")" ] \
        && ok "${c%% *} asks first (exit 2, nothing written)" || bad "${c%% *} without --yes: exit $rc, files: $(ls -A "$ASK")"
done
for c in "bench pp --nope" "bench --nope pp" "ipi --nope" "latency --nope" "pi --nope 1" "crypto --nope 1" "prime --nope 1" "bench pp --seconds" "ipi --cpus" "latency --max" "bench" "bench zz --yes"; do
    HOME=$ASK XDG_CACHE_HOME=$ASK/.cache "$BIN" $c </dev/null >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && [ -z "$(ls -A "$ASK")" ] && ok "'$c' refused (exit 2, not started)" || bad "'$c': exit $rc, files: $(ls -A "$ASK")"
done
for c in bench ipi latency pi crypto prime; do
    out=$(HOME=$ASK XDG_CACHE_HOME=$ASK/.cache "$BIN" $c --help </dev/null 2>&1); rc=$?
    [ "$rc" = 0 ] && grep -q "^usage: sysreport-search $c .*--yes" <<<"$out" && [ -z "$(ls -A "$ASK")" ] \
        && ok "$c --help: usage, exit 0, not started" || bad "$c --help: exit $rc: $out"
done
if command -v script >/dev/null 2>&1; then   # on a terminal: "n" does not start, "y" does
    out=$(printf 'n\n' | HOME=$ASK XDG_CACHE_HOME=$ASK/.cache script -qec "'$BIN' pi --digits 5000" /dev/null 2>&1)
    grep -q "Start now? \[y/N\]" <<<"$out" && grep -q "pi: not started" <<<"$out" && [ -z "$(ls -A "$ASK")" ] \
        && ok "on a terminal pi asks; answer n: not started" || bad "pi on a terminal, answer n: $out"
    out=$(printf 'y\n' | HOME=$ASK XDG_CACHE_HOME=$ASK/.cache script -qec "'$BIN' pi --digits 5000" /dev/null 2>&1)
    grep -q "Start now? \[y/N\]" <<<"$out" && grep -q "first 100 digits right" <<<"$out" \
        && ok "on a terminal pi asks; answer y: it runs" || bad "pi on a terminal, answer y: $out"
else
    echo "  skip  terminal answers (no 'script' command)"
fi

echo "== tiny runs of bench / ipi / latency with --yes"
out=$("$BIN" bench pp --seconds 0.1 --yes); rc=$?
[ "$rc" = 0 ] && grep -q "MWIPS" <<<"$out" && [ -s "$HOME/.cache/srs/bench-benchmark-pp.txt" ] && ok "bench pp: $(grep '^Total' <<<"$out" | sed 's/  */ /g')" || bad "bench pp --yes: exit $rc"
out=$("$BIN" ipi --cpus 2 --rounds 2000 --yes); rc=$?
[ "$rc" = 0 ] && grep -q "CPU-1" <<<"$out" && [ -s "$HOME/.cache/srs/bench-ipi-delay.txt" ] && ok "ipi 2 CPUs ran" || bad "ipi --yes: exit $rc"
cp "$HOME/.cache/srs/bench-ipi-delay.txt" "$TMP/ipi-before.txt" 2>/dev/null
out=$("$BIN" ipi --cpus 1 --yes 2>&1); rc=$?
[ "$rc" = 2 ] && grep -q "at least two CPU threads" <<<"$out" && cmp -s "$TMP/ipi-before.txt" "$HOME/.cache/srs/bench-ipi-delay.txt" \
    && ok "ipi with one CPU: not started (exit 2), the last result is kept" || bad "ipi --cpus 1: exit $rc, result file changed or no reason"
out=$("$BIN" latency --max 4 --yes); rc=$?
[ "$rc" = 0 ] && grep -q " ns" <<<"$out" && [ -s "$HOME/.cache/srs/bench-latency.txt" ] && ok "latency up to 4 MB ran" || bad "latency --yes: exit $rc"

echo "== crypto self-test (FIPS 180-4 / FIPS 197 vectors, hardware vs plain C)"
out=$("$BIN" crypto --threads 1 --seconds 0.1 --yes) && grep -q "test vectors pass" <<<"$out" && ok "$(grep Check <<<"$out" | sed 's/  */ /g')" || bad "crypto self-test"

echo "== prime sieve against known prime counts"
for spec in "1e7 1" "1e8 1" "1e8 4" "1e9 4" "12345 1"; do
    set -- $spec
    out=$("$BIN" prime --limit "$1" --threads "$2" --yes); rc=$?
    cnt=$(sed -n 's/^Primes found *//p' <<<"$out")
    case $1 in
        1e7) want=664579 ;; 1e8) want=5761455 ;; 1e9) want=50847534 ;; 12345) want=1474 ;;
    esac
    [ "$rc" = 0 ] && [ "$cnt" = "$want" ] && ok "pi($1) = $cnt with $2 thread(s)" || bad "pi($1): got '$cnt', want $want (exit $rc)"
done

echo "== SRS Pi: built-in check"
for spec in "128k chudnovsky 1" "1m chudnovsky 1" "1m chudnovsky 4" "128k agm 1" "1m agm 1" "5000 chudnovsky 2" "16ki chudnovsky 1" "1mi chudnovsky 4"; do
    set -- $spec
    out=$("$BIN" pi --digits "$1" --method "$2" --threads "$3" --save-digits "$TMP/pi-$1-$2-$3.txt" --yes); rc=$?
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

echo "== Cache & Memory (small: 4 threads, L1/L2 only, 0.2 s per cell)"
if [ "$(uname -m)" = x86_64 ]; then
    out=$("$BIN" cachemem --threads 4 --seconds 0.2 --only l1,l2 --tests read,write,copy --yes); rc=$?
    l1=$(awk '/^L1 cache/{print $3}' <<<"$out"); l2=$(awk '/^L2 cache/{print $3}' <<<"$out")
    [ "$rc" = 0 ] && [ -n "$l1" ] && [ -n "$l2" ] && awk -v a="$l1" -v b="$l2" 'BEGIN{exit !(a>b && b>0)}' \
        && ok "L1 read $l1 GB/s > L2 read $l2 GB/s" || bad "cachemem bandwidth: exit $rc, L1 '$l1', L2 '$l2'"
    out=$("$BIN" cachemem --only l1 --tests latency --seconds 0.2 --yes); lat=$(awk '/^L1 cache/{print $6}' <<<"$out")
    awk -v a="$lat" 'BEGIN{exit !(a>0.1 && a<5)}' && ok "L1 latency $lat ns" || bad "L1 latency '$lat'"
    "$BIN" cachemem --only l1 --tests read </dev/null >/dev/null 2>&1; [ $? = 2 ] && ok "cachemem asks first (no --yes, no terminal: not started)" || bad "cachemem started without --yes"
else
    echo "  skip  not x86-64"
fi

echo "== Storage: refusals (nothing is written)"
if [ -d /dev/shm ] && [ "$(stat -f -c %T /dev/shm)" = tmpfs ]; then
    out=$("$BIN" storage --dir /dev/shm --yes 2>&1); rc=$?
    [ "$rc" = 2 ] && grep -q "in RAM" <<<"$out" && ok "a RAM folder (/dev/shm) is refused" || bad "storage on tmpfs: exit $rc: $out"
fi
if [ -n "${SRS_TEST_STORAGE_DIR:-}" ]; then   # a drive folder, so the run gets past the RAM check to the question
    out=$("$BIN" storage --dir "$SRS_TEST_STORAGE_DIR" </dev/null 2>&1); rc=$?
    [ "$rc" = 2 ] && grep -q "pass --yes" <<<"$out" && ok "storage asks first (no --yes, no terminal: not started)" || bad "storage without --yes: exit $rc: $out"
fi
"$BIN" storage --dir /nonexistent-srs-test --yes >/dev/null 2>&1; [ $? = 2 ] && ok "a missing folder is refused" || bad "missing folder accepted"
# a real (small) run only on request: SRS_TEST_STORAGE_DIR=folder on a drive (64 MB, 1 s per test, Light profile)
if [ -n "${SRS_TEST_STORAGE_DIR:-}" ]; then
    before=$(ls -A "$SRS_TEST_STORAGE_DIR" | wc -l)
    out=$("$BIN" storage --dir "$SRS_TEST_STORAGE_DIR" --size 64 --seconds 1 --profile light --yes); rc=$?
    seq=$(awk '/^Sequential 1 MiB/{print $6}' <<<"$out")
    after=$(ls -A "$SRS_TEST_STORAGE_DIR" | wc -l)
    [ "$rc" = 0 ] && [ -n "$seq" ] && [ "$before" = "$after" ] && ok "storage run in $SRS_TEST_STORAGE_DIR: sequential read $seq MB/s; no file left behind" \
        || bad "storage run: exit $rc, seq '$seq', files before $before after $after"
else
    echo "  skip  real storage run (set SRS_TEST_STORAGE_DIR=folder-on-a-drive to include it)"
fi

echo "== bad input is refused"
"$BIN" pi --digits 50 --yes >/dev/null 2>&1 && bad "pi --digits 50 accepted" || ok "pi --digits 50 refused"
"$BIN" prime --limit 2e11 --yes >/dev/null 2>&1 && bad "prime --limit 2e11 accepted" || ok "prime --limit 2e11 refused"
[ -s "$HOME/.cache/srs/bench-history.tsv" ] && ok "history written ($(($(wc -l <"$HOME/.cache/srs/bench-history.tsv") - 1)) runs)" || bad "no bench-history.tsv"

[ $fail = 0 ] && echo "ALL PASSED" || echo "SOME TESTS FAILED"
exit $fail
