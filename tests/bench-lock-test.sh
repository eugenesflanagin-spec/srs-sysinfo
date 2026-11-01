#!/usr/bin/env bash
# tests/bench-lock-test.sh - one load at a time (2.5): bench.lock across SRS processes and the greyed Run buttons.
#  CLI: no lock -> runs and leaves no lock; a stale lock (dead PID) is ignored; a live other holder -> refused without a
#  terminal, warned and run with --yes, and that holder's lock is put back afterwards.
#  GUI (Xvfb, when given): during a short Crypto run every other page's Run / Start is greyed; with a live other
#  holder the page asks first (SRS_TEST_LOCK_ANSWER=no -> not started).
# Tiny runs only (Crypto, 1 thread, 0.1 s), throw-away HOME, the lock file in the throw-away folder (SRS_TEST_BENCH_LOCK).
#
#   tests/bench-lock-test.sh SYSREPORT-SEARCH [SRS-GUI]
set -u
BIN=${1:-}; GUI=${2:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH [SRS-GUI]"; exit 2; }
TMP=$(mktemp -d)
cleanup() { [ -n "${H:-}" ] && kill "$H" 2>/dev/null; [ -n "${G:-}" ] && kill "$G" 2>/dev/null; [ -n "${XPID:-}" ] && kill "$XPID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
export SRS_TEST_BENCH_LOCK=$TMP/bench.lock
mkdir -p "$XDG_CONFIG_HOME/srs"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
run() { "$BIN" crypto --threads 1 --seconds 0.1 "$@" </dev/null 2>&1; }

echo "== CLI: no other holder"
out=$(run --yes); rc=$?
[ $rc = 0 ] && ! grep -q "another SRS benchmark" <<<"$out" && ok "ran without a warning" || bad "rc=$rc: $out"
[ ! -e "$SRS_TEST_BENCH_LOCK" ] && ok "no lock left behind" || bad "lock left: $(cat "$SRS_TEST_BENCH_LOCK")"

echo "== CLI: stale lock (a PID that is gone)"
sleep 0 & dead=$!; wait $dead
echo "$dead Crypto Benchmark" >"$SRS_TEST_BENCH_LOCK"
out=$(run --yes); rc=$?
[ $rc = 0 ] && ! grep -q "another SRS benchmark" <<<"$out" && ok "stale lock ignored" || bad "rc=$rc: $out"

echo "== CLI: a live other holder"
sleep 60 & H=$!
echo "$H Crypto Benchmark" >"$SRS_TEST_BENCH_LOCK"
out=$(run); rc=$?
[ $rc = 2 ] && grep -q "another SRS benchmark (Crypto Benchmark, PID $H)" <<<"$out" && ok "refused without a terminal (exit 2), names the holder" || bad "rc=$rc: $out"
out=$(run --yes); rc=$?
[ $rc = 0 ] && grep -q "warning - another SRS benchmark (Crypto Benchmark, PID $H)" <<<"$out" && ok "--yes: warned and ran" || bad "rc=$rc: $out"
grep -q "^$H Crypto Benchmark" "$SRS_TEST_BENCH_LOCK" 2>/dev/null && ok "the other holder's lock was put back" || bad "lock now: $(cat "$SRS_TEST_BENCH_LOCK" 2>&1)"

echo "== where the lock lives (no SRS_TEST_BENCH_LOCK): the user's own folder, never through a symlink"
(
    unset SRS_TEST_BENCH_LOCK
    echo keep >"$TMP/victim"
    mkdir -p "$TMP/rt/srs"; chmod 700 "$TMP/rt/srs"
    ln -s "$TMP/victim" "$TMP/rt/srs/bench.lock"
    out=$(XDG_RUNTIME_DIR=$TMP/rt run --yes); rc=$?
    [ $rc = 0 ] && [ "$(cat "$TMP/victim")" = keep ] && [ -L "$TMP/rt/srs/bench.lock" ] \
        && ok "a planted symlink as the lock is never written through (O_NOFOLLOW)" || bad "symlink: rc=$rc victim=$(cat "$TMP/victim")"
    mkdir -p "$TMP/rt2/srs"; chmod 777 "$TMP/rt2/srs"
    out=$(XDG_RUNTIME_DIR=$TMP/rt2 run --yes); rc=$?
    [ $rc = 0 ] && [ ! -e "$TMP/rt2/srs/bench.lock" ] && ok "a lock folder others can write to is not used" || bad "0777 folder: rc=$rc"
    out=$(env -u XDG_RUNTIME_DIR "$BIN" crypto --threads 1 --seconds 0.1 --yes </dev/null 2>&1); rc=$?
    [ $rc = 0 ] && [ -d "$HOME/.cache/srs" ] && [ "$(stat -c %a "$HOME/.cache/srs")" != 777 ] && [ ! -e "$HOME/.cache/srs/bench.lock" ] \
        && ok "no XDG_RUNTIME_DIR: the lock goes to ~/.cache/srs (not /tmp) and is gone afterwards" || bad "fallback: rc=$rc"
    exit $fail
) || fail=1

if [ -n "$GUI" ] && [ -x "$GUI" ] && command -v Xvfb >/dev/null; then
    d=:$((50 + RANDOM % 9))
    Xvfb "$d" -screen 0 1400x1000x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
    sleep 1
    printf '[SRSDEF]\nTheme=Light\n[Tabs]\nLineup=Expert\nLayout=side\n' >"$XDG_CONFIG_HOME/srs/SRSDEF.ini"
    gui() { env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS GDK_BACKEND=x11 DISPLAY=$d SRS_TEST_TERMS_ANSWER=agree "$@" "$GUI" >"$TMP/log" 2>&1 & G=$!; }
    echo "== GUI: a live other holder, answer No"
    gui SRS_TEST_LOCK_ANSWER=no SRS_TEST_BENCH_AUTO="crypto,0.1,1,0" SRS_OPEN=crypto
    for i in $(seq 1 30); do grep -q "lock ask" "$TMP/log" && break; sleep 0.5; done
    sleep 1; kill $G 2>/dev/null; wait $G 2>/dev/null; G=
    grep -q "lock ask: Another SRS benchmark (Crypto Benchmark, PID $H) is running - start anyway? -> no" "$TMP/log" && ok "asked first, naming the holder" || bad "no question: $(grep SRS-TEST "$TMP/log")"
    grep -q "bench crypto done" "$TMP/log" && bad "started although the answer was No" || ok "not started on No"
    kill $H 2>/dev/null; wait $H 2>/dev/null; H=; rm -f "$SRS_TEST_BENCH_LOCK"
    echo "== GUI: the other pages' Run / Start are greyed while Crypto runs (Benchmarks & Stress page, every tool tab)"
    gui SRS_TEST_BENCH_AUTO="crypto,0.1,1,0" SRS_OPEN=crypto,cachemem,pi,prime,compute,work,stressburn
    for i in $(seq 1 60); do grep -q "bench crypto done" "$TMP/log" && break; sleep 0.5; done
    sleep 1.5; kill $G 2>/dev/null; wait $G 2>/dev/null; G=
    busy=$(grep "SRS-TEST: load Crypto Benchmark" "$TMP/log" | head -1)
    idle=$(grep "SRS-TEST: load none" "$TMP/log" | tail -1)
    if [ -n "$busy" ] && [[ "$busy" =~ other-pages=([0-9]+)\ pressable=([0-9]+) ]] && [ "${BASH_REMATCH[1]}" -ge 3 ] && [ "${BASH_REMATCH[2]}" = 0 ]; then
        ok "during the run: ${BASH_REMATCH[1]} other pages, none can start"
    else bad "during the run: ${busy:-no line}"; fi
    if [[ "$idle" =~ other-pages=([0-9]+)\ pressable=([0-9]+) ]] && [ "${BASH_REMATCH[2]}" = "${BASH_REMATCH[1]}" ]; then
        ok "after the run: all ${BASH_REMATCH[1]} pages can start again"
    else bad "after the run: ${idle:-no line}"; fi
fi
[ $fail = 0 ] && echo "bench-lock-test: all passed" || echo "bench-lock-test: SOME FAILED"
exit $fail
