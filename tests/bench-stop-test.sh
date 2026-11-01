#!/usr/bin/env bash
# tests/bench-stop-test.sh - Stop on the benchmark pages (2.5: CPU Benchmark, IPI Delay, Latency and Crypto got one):
# under Xvfb each page is run once with its smallest settings (0.1 s, 1 thread; CPU Benchmark one CPU at a time) and
# Stop is pressed shortly after Run; the page must come back to idle (Run sensitive again) and say Stopped / show
# the stopped result. SRS_TEST_BENCH_AUTO drives the page, SRS_TEST_TERMS_ANSWER agrees to the load terms.
# Throw-away HOME; a few CPU threads busy for well under a second each.
#
#   tests/bench-stop-test.sh SRS-GUI
set -u
GUI=${1:-}
[ -x "$GUI" ] || { echo "usage: $0 SRS-GUI"; exit 2; }
command -v Xvfb >/dev/null || { echo "  skip  no Xvfb"; exit 0; }
TMP=$(mktemp -d)
cleanup() { [ -n "${G:-}" ] && kill "$G" 2>/dev/null; [ -n "${XPID:-}" ] && kill "$XPID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$XDG_CONFIG_HOME/srs"
printf '[SRSDEF]\nTheme=Light\n[Tabs]\nLineup=Expert\nLayout=side\n' >"$XDG_CONFIG_HOME/srs/SRSDEF.ini"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
d=:$((50 + RANDOM % 9))
Xvfb "$d" -screen 0 1400x1000x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
sleep 1
# page (as the result name), the tool to open, seconds, threads, stop after ms
while read -r page tool secs thr stopms; do
    echo "== $page: Run, then Stop after $stopms ms"
    env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS GDK_BACKEND=x11 DISPLAY=$d SRS_TEST_TERMS_ANSWER=agree \
        SRS_TEST_BENCH_AUTO="$page,$secs,$thr,$stopms" SRS_OPEN=$tool "$GUI" >"$TMP/log" 2>&1 & G=$!
    for i in $(seq 1 60); do grep -q "SRS-TEST: bench .* done" "$TMP/log" && break; sleep 0.5; done
    kill $G 2>/dev/null; wait $G 2>/dev/null; G=
    line=$(grep "SRS-TEST: bench .* done" "$TMP/log" | tail -1)
    grep -q "run pressed" "$TMP/log" && grep -q "stop pressed" "$TMP/log" || { bad "$page: the run or the Stop was not pressed"; grep SRS-TEST "$TMP/log"; continue; }
    [ -n "$line" ] || { bad "$page: no end of the run within 30 s"; continue; }
    grep -q " run=1 " <<<"$line" && ok "$page: back to idle (Run can be pressed again)" || bad "$page: Run still greyed: $line"
    grep -qi "stopped" <<<"$line" && ok "$page: ${line#*status=}" || bad "$page: not marked stopped: $line"
done <<'LIST'
benchmark bench 0.1 1 250
ipi-delay ipi 0.1 4 300
latency lat 0.1 1 300
crypto crypto 0.1 1 300
pi pi 0.1 1 10
prime prime 0.1 1 10
compute compute 0.1 1 400
work work 0.1 1 600
LIST
echo "== what a Stopped run leaves behind"
hl=$(grep -h "crypto" "$HOME/.cache/srs/bench-history.tsv" 2>/dev/null | tail -1)
if grep -q STOPPED <<<"$hl"; then
    grep -qE '(^|[ /])0(/| )' <<<"${hl#*aes-hw}" && bad "crypto history: a cell not measured reads 0: $hl" \
        || ok "crypto history: cells not measured are '-', never 0"
else ok "crypto history: the run was not stopped early enough to leave empty cells (nothing to check)"; fi
if grep -q "Stopped before the end" "$HOME/.cache/srs/bench-benchmark.txt" 2>/dev/null; then
    grep -q "Total (partial)" "$HOME/.cache/srs/bench-benchmark.txt" && ok "CPU Benchmark: the Total of a Stopped run says (partial)" \
        || bad "CPU Benchmark: Total not marked partial"
fi
[ $fail = 0 ] && echo "bench-stop-test: all passed" || echo "bench-stop-test: SOME FAILED"
exit $fail
