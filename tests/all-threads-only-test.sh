#!/usr/bin/env bash
# tests/all-threads-only-test.sh - All Cores / --all-threads-only on Crypto, CPU Compute and Everyday Work:
# only the all-threads pass runs, the 1-thread column says "skipped", the report says so, the history line has
# "-" (never 0) for the 1-thread values plus 1-THREAD-SKIPPED; without the flag both passes run as before.
# Tiny runs only (0.1 s per measurement, 2 threads, one or two tests), throw-away HOME.
#
#   tests/all-threads-only-test.sh [SYSREPORT-SEARCH]     default: dist/native/sysreport-search
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-$SRC/dist/native/sysreport-search}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH"; exit 2; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME"
H=$HOME/.cache/srs/bench-history.tsv
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

check() {   # name, test word in the history, CLI words...
    local name=$1 word=$2 out last; shift 2
    echo "== $name --all-threads-only"
    out=$("$BIN" "$@" --all-threads-only --yes 2>&1) || { bad "$name: exit $?"; echo "$out" | head -20; return; }
    grep -q "skipped" <<<"$out" && ok "$name: the 1-thread column says skipped" || bad "$name: no 'skipped' cell"
    grep -q "1-thread pass skipped (All Cores)" <<<"$out" && ok "$name: the report says the 1-thread pass was skipped" || bad "$name: no report line"
    last=$(grep -P "\t$word" "$H" | tail -1)
    grep -q "all-threads-only" <<<"$last" && ok "$name: history settings say all-threads-only" || bad "$name: history settings: $last"
    grep -q "1-THREAD-SKIPPED" <<<"$last" && ok "$name: history result says 1-THREAD-SKIPPED" || bad "$name: history result: $last"
    grep -qE '(^|[ \t,])[a-z0-9-]+ 0(\.0)?/' <<<"$last" && bad "$name: a 1-thread 0 in the history: $last" || ok "$name: no 1-thread value written as 0"
    grep -qE ' -/[0-9]' <<<"$last" && ok "$name: 1-thread values written as '-'" || bad "$name: no '-/N' values: $last"
    echo "== $name (both passes, as before)"
    out=$("$BIN" "$@" --yes 2>&1) || { bad "$name: exit $?"; return; }
    grep -q "1-thread pass skipped" <<<"$out" && bad "$name: says skipped without the flag" || ok "$name: no skip line without the flag"
    last=$(grep -P "\t$word" "$H" | tail -1)
    grep -q "1-THREAD-SKIPPED\|all-threads-only" <<<"$last" && bad "$name: history marked skipped without the flag" || ok "$name: history has both values"
}

check "Crypto" crypto crypto --threads 2 --seconds 0.1
check "CPU Compute" compute compute --threads 2 --seconds 0.1 --tests fma-sp,mandelbrot
check "Everyday Work" work-hash work --threads 2 --seconds 0.1 --only hash

echo "== --single and --all-threads-only together"
for c in compute work; do
    "$BIN" $c --single --all-threads-only --yes >/dev/null 2>&1; rc=$?
    [ $rc = 2 ] && ok "$c: refused (exit 2)" || bad "$c: exit $rc"
done
[ $fail = 0 ] && echo "all-threads-only-test: all passed" || echo "all-threads-only-test: SOME FAILED"
exit $fail
