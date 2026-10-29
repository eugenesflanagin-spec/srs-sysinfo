#!/usr/bin/env bash
# tests/load-terms-test.sh - the load test terms (2.3.1): asked before the first stress test / benchmark, "I Disagree" blocks,
# "I Agree" is kept across restarts, Options > Load test terms withdraws it. The dialog is answered by SRS_TEST_TERMS_ANSWER.
#
#   tests/load-terms-test.sh SRS-GUI [SYSREPORT-SEARCH]
#
# SAFETY: the runs are GPU only (no CPU load) on lavapipe (1 thread, 64 x 64 matrices, 3 s cap), the HIP / CUDA / OpenCL
# libraries point at files that do not exist, HOME and XDG_* are a throw-away folder, and the GUI runs on its own Xvfb display.
set -u
GUI=${1:-}
CLI=${2:-$(dirname "$GUI")/sysreport-search}
[ -x "$GUI" ] || { echo "usage: $0 SRS-GUI [SYSREPORT-SEARCH]"; exit 2; }
GUI=$(cd "$(dirname "$GUI")" && pwd)/$(basename "$GUI")
TMP=$(mktemp -d)
cleanup() { [ -n "${XPID:-}" ] && kill "$XPID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$XDG_CONFIG_HOME/srs"
LVP=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
export VK_DRIVER_FILES=$LVP VK_ICD_FILENAMES=$LVP VK_LOADER_LAYERS_DISABLE='~implicit~' DISABLE_MANGOHUD=1 LP_NUM_THREADS=1
export SRS_GPU_SOFTWARE_ONLY=1 SRS_TEST_GB_NMAX=64 SRS_CUDA_LIB=/nonexistent/libcuda.so SRS_CUDA_RTC=/nonexistent/libnvrtc.so
export SRS_HIP_LIB=/nonexistent/libamdhip64.so SRS_OPENCL_LIB=/nonexistent/libOpenCL.so SRS_CLBLAST_LIB=/nonexistent/libclblast.so
INI=$XDG_CONFIG_HOME/srs/SRSDEF.ini
printf '[SRSDEF]\nStress GPU Burnin=1\n[Tabs]\nLineup=Enthusiast\n' >"$INI"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
terms() { sed -n 's/^Accepted Load Test Terms=//p' "$INI"; }

if [ -x "$CLI" ]; then
    echo "== CLI: the start prompt carries the notice; --help says --yes means agreeing"
    "$CLI" burn --help 2>&1 | grep -qi "own risk" && ok "burn --help: own risk / --yes" || bad "burn --help lacks the notice"
    "$CLI" gpu-burn --help 2>&1 | grep -qi "own risk" && ok "gpu-burn --help: own risk / --yes" || bad "gpu-burn --help lacks the notice"
    out=$("$CLI" gpu-burn --seconds 1 </dev/null 2>&1); echo "$out" | grep -qi "own risk" && ok "the prompt text names the risk" || bad "prompt without the notice: $(echo "$out" | head -1)"
fi

command -v Xvfb >/dev/null || { echo "  skip  no Xvfb"; [ $fail = 0 ] && echo "ALL OK"; exit $fail; }
d=:$((80 + RANDOM % 9))
Xvfb "$d" -screen 0 1400x1000x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 & XPID=$!
sleep 1
run() {   # $1 tag, $2 what to open, $3 answer ("" = no hook), $4 autostart (yes/no)
    local tag=$1 open=$2 ans=$3 as=$4 G
    rm -f "$XDG_CACHE_HOME"/srs/burnin-*.txt
    env -u WAYLAND_DISPLAY GDK_BACKEND=x11 DISPLAY=$d SRS_TEST_NOCONFIRM=1 SRS_TEST_MAXSECS=3 SRS_TEST_GPU_BACKEND=vulkan \
        ${ans:+SRS_TEST_TERMS_ANSWER=$ans} ${as:+SRS_TEST_BURN_AUTOSTART=0} SRS_OPEN=$open "$GUI" >"$TMP/$tag.log" 2>&1 &
    G=$!
    for i in $(seq 1 40); do ls "$XDG_CACHE_HOME"/srs/burnin-*.txt >/dev/null 2>&1 && break; sleep 0.25; done
    sleep 1
    kill $G 2>/dev/null; wait $G 2>/dev/null
}
shown() { grep -q "load test terms dialog shown" "$TMP/$1.log"; }
ran()   { ls "$XDG_CACHE_HOME"/srs/burnin-*.txt >/dev/null 2>&1; }

echo "== first start, I Disagree: asked, nothing runs, nothing kept"
run d1 gpuburn disagree yes
shown d1 && ok "the terms were asked before the first run" || bad "not asked"
ran && bad "a run started after I Disagree" || ok "nothing started"
[ "$(terms)" = "1" ] && bad "agreement stored after I Disagree" || ok "no agreement stored"
echo "== Start again (still not agreed): asked again"
run d2 gpuburn disagree yes
shown d2 && ok "asked again on the next Start" || bad "Start not blocked by the dialog"
ran && bad "a run started" || ok "still blocked"
echo "== I Agree: the run starts and the agreement is kept"
run a1 gpuburn agree yes
shown a1 && ok "asked" || bad "not asked"
ran && ok "the run started after I Agree" || bad "no run after I Agree"
[ "$(terms)" = "1" ] && ok "SRSDEF.ini: Accepted Load Test Terms=1" || bad "agreement not stored ($(terms))"
echo "== restart: not asked again"
run a2 gpuburn "" yes
shown a2 && bad "asked again after agreeing" || ok "not asked after a restart"
ran && ok "the run started" || bad "no run"
echo "== Options > Load test terms, I Disagree: withdrawn, Start asks again"
run w1 loadterms disagree ""
shown w1 && ok "Options entry shows the terms" || bad "Options entry did not show them"
[ "$(terms)" = "0" ] && ok "agreement withdrawn (0)" || bad "not withdrawn ($(terms))"
run w2 gpuburn disagree yes
shown w2 && ok "Start asks again after withdrawing" || bad "not asked after withdrawing"
ran && bad "a run started" || ok "blocked again"

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
