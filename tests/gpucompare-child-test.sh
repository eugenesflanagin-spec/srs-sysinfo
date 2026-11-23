#!/usr/bin/env bash
. "$(dirname "$0")/private-bus.sh"   # 2.6.0: a private D-Bus session bus, never the desktop's (tray, notifications)
# tests/gpucompare-child-test.sh - 2.6.0: the GPU Compute comparison runs in a child process (--gpu-compare-child),
# so a crash inside a GPU runtime (ROCm 10.1's HIP crashed in hipMemcpyHtoD on an RX 9070 XT) ends only the child.
#   1. SRS_TEST_GPUCMP_CRASH=1: the child crashes on purpose -> the window stays alive, the status line says it crashed
#      and names the step; the GPU is free again (Run can be pressed).
#   2. A normal run on software renderers only (SRS_GPU_SOFTWARE_ONLY=1, lavapipe): finishes through the child.
#   3. Stop during a run: the child is told to stop (SIGTERM) and the page says Stopped.
#   4. A back end that crashes while STARTING (SRS_TEST_GPUCMP_CRASH=cuda) is left out and the run retried once without it.
# Under Xvfb, throw-away HOME; never a real GPU: Vulkan is pinned to lavapipe (Mesa's software renderer), so the real
# GPU's Vulkan driver is not even loaded for the device list, and SRS_GPU_SOFTWARE_ONLY=1 leaves HIP, CUDA and OpenCL out.
#
#   tests/gpucompare-child-test.sh SRS-GUI      (make test-bench passes sysreport-search: the srs-gui beside it is used)
set -u
GUI=${1:-}
[ -x "$GUI" ] || { echo "usage: $0 SRS-GUI"; exit 2; }
[ "$(basename "$GUI")" = sysreport-search ] && GUI=$(dirname "$GUI")/srs-gui
[ -x "$GUI" ] || { echo "  skip  no srs-gui beside sysreport-search (make native builds both)"; exit 0; }
command -v Xvfb >/dev/null || { echo "  skip  no Xvfb"; exit 0; }
LVP=${LVP_ICD:-/usr/share/vulkan/icd.d/lvp_icd.x86_64.json}
[ -r "$LVP" ] || { echo "  skip  no lavapipe ICD ($LVP) - install vulkan-swrast"; exit 0; }
export VK_DRIVER_FILES=$LVP VK_ICD_FILENAMES=$LVP VK_LOADER_LAYERS_DISABLE='~implicit~' DISABLE_MANGOHUD=1 LP_NUM_THREADS=2
unset OCL_ICD_FILENAMES OPENCL_VENDOR_PATH VK_ADD_DRIVER_FILES VK_ADD_LAYER_PATH VK_LAYER_PATH
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
run() {   # $1 = stop-ms, then extra env
    local stopms=$1; shift
    env -u WAYLAND_DISPLAY GDK_BACKEND=x11 DISPLAY=$d SRS_TEST_TERMS_ANSWER=agree SRS_GPU_SOFTWARE_ONLY=1 "$@" \
        SRS_TEST_BENCH_AUTO="${AUTO:-gpucompare,0.1,1,$stopms}" SRS_OPEN=gpucompare "$GUI" >"$TMP/log" 2>&1 & G=$!
    for i in $(seq 1 240); do grep -q "SRS-TEST: bench .* done" "$TMP/log" && break; sleep 0.5; done
    alive=0; kill -0 $G 2>/dev/null && alive=1
    kill $G 2>/dev/null; wait $G 2>/dev/null; G=
    line=$(grep "SRS-TEST: bench .* done" "$TMP/log" | tail -1)
}

echo "== 1. the child crashes on purpose"
run 0 SRS_TEST_GPUCMP_CRASH=1
grep -q "SRS-TEST: gpucompare child [0-9]" "$TMP/log" && ok "the comparison ran in a child process" || bad "no child process started"
grep -q "child crashed signal 11 stage 'HIP (test" "$TMP/log" && ok "the crash (SIGSEGV) and its step were seen" || { bad "crash not reported"; grep SRS-TEST "$TMP/log" | tail -5; }
[ $alive = 1 ] && ok "the SRS window survived the crash" || bad "SRS itself died"
grep -qi "crashed" <<<"$line" && grep -q "HIP (test" <<<"$line" && ok "status: ${line#*status=}" | cut -c1-150 || bad "status does not explain the crash: $line"
grep -q " run=1 " <<<"$line" && ok "Run can be pressed again (the GPU claim was released)" || bad "Run still greyed: $line"
ls "$HOME/.cache/srs"/gpucmp-* >/dev/null 2>&1 && bad "stage/why files left behind" || ok "no stage/why files left behind"

echo "== 2. a normal run on software renderers"
run 0
grep -q "SRS-TEST: gpucompare child [0-9]" "$TMP/log" && ok "ran through the child" || bad "no child"
grep -q "child crashed" "$TMP/log" && bad "the child crashed: $(grep 'child crashed' "$TMP/log")" || ok "no crash"
[ -n "$line" ] && ! grep -qi "crashed\|failed during" <<<"$line" && ok "status: ${line#*status=}" || bad "run did not finish cleanly: $line"
[ -s "$HOME/.cache/srs/bench-gpucompare.txt" ] && ok "the child saved the result page" || bad "no bench-gpucompare.txt"

echo "== 3. Stop during a run"
run 150
grep -q "stop pressed" "$TMP/log" && ok "Stop pressed" || bad "Stop not pressed"
[ -n "$line" ] && grep -q " run=1 " <<<"$line" && ok "back to idle after Stop: ${line#*status=}" | cut -c1-150 || bad "not idle after Stop: $line"

echo "== 4. a back end crashes while STARTING (as CUDA under ZLUDA + ROCm 10.1 did): retried without it"
run 0 SRS_TEST_GPUCMP_CRASH=cuda
grep -q "SRS-TEST: gpucompare retry without CUDA" "$TMP/log" && ok "retried without CUDA" || { bad "no retry without CUDA"; grep SRS-TEST "$TMP/log" | tail -4; }
grep -qi "finished" <<<"$line" && grep -q "CUDA crashed while starting" <<<"$line" && ok "finished; status names the back end left out" || bad "status after retry: $line"
[ $alive = 1 ] && ok "the SRS window survived" || bad "SRS itself died"

echo "== 5. a language with a decimal comma: Seconds per test keeps its fraction on the way to the child"
# 2.6.0 as first built wrote the number in the window's language ("0,500") and the child read it as 0
if locale -a 2>/dev/null | grep -qi '^de_DE\.utf-\?8$'; then
    AUTO="gpucompare,0,5,1,0" run 0 LC_ALL=de_DE.UTF-8      # the test hook reads its own numbers in that language too: 0,5 = 0.5 s
    grep -q "SRS-TEST: gpucompare child [0-9]* seconds-arg=0\.500$" "$TMP/log" && ok "the window hands the child 0.500 (a point, not 0,500)" || { bad "the seconds argument: $(grep -o 'seconds-arg=.*' "$TMP/log" | head -1)"; }
    grep -q "SRS-TEST: gpucompare child got seconds=0\.500$" "$TMP/log" && ok "the child reads 0.5 s" || bad "the child read: $(grep -o 'got seconds=.*' "$TMP/log" | head -1)"
    [ -n "$line" ] && ! grep -qi "crashed\|failed during" <<<"$line" && ok "and the run finishes" || bad "the de_DE run did not finish cleanly: $line"
else echo "  skip  no de_DE.UTF-8 locale on this system"; fi

[ $fail = 0 ] && echo "gpucompare-child-test: all passed" || echo "gpucompare-child-test: SOME FAILED"
exit $fail
