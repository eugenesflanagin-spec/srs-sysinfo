#!/usr/bin/env bash
# tests/hot-limit-test.sh - stop limits over 100 °C (2.4): the CPU and GPU limits go up to 110, a value over 100 is asked
# about once (Yes / No), No puts it back to 100, Yes is kept in SRSDEF.ini ("Accepted High Temperature Limits") and not asked
# again; a limit over 100 in SRSDEF.ini without the yes loads as 100. The question is answered by SRS_TEST_HOT_ANSWER,
# the limits are typed in by SRS_TEST_LIMITS=CPU,GPU just before the test presses Start.
#
#   tests/hot-limit-test.sh SRS-GUI [SYSREPORT-SEARCH]
#
# SAFETY: as tests/load-terms-test.sh - GPU only (no CPU ticked), lavapipe, 64 x 64 matrices, 3 s cap, no real GPU runtime,
# throw-away HOME and XDG_*, its own Xvfb display.
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
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
key() { sed -n "s/^$1=//p" "$INI"; }
if [ -x "$CLI" ]; then   # refused before anything starts (no CPU is picked, so nothing could run anyway)
    echo "== CLI: over 100 needs --accept-high-limit, over 110 never"
    out=$("$CLI" burn --limit 105 --seconds 1 --cpus 9999 2>&1); rc=$?
    [ $rc = 2 ] && grep -q "needs --accept-high-limit" <<<"$out" && ok "burn --limit 105: refused, says why" || bad "burn 105: rc=$rc $out"
    out=$("$CLI" burn --limit 111 --accept-high-limit --seconds 1 --cpus 9999 2>&1); rc=$?
    [ $rc = 2 ] && grep -q "50..110" <<<"$out" && ok "burn --limit 111: refused even with the flag" || bad "burn 111: rc=$rc $out"
    out=$("$CLI" gpu-burn --limit 105 --seconds 1 </dev/null 2>&1); rc=$?
    [ $rc = 2 ] && grep -q "needs --accept-high-limit" <<<"$out" && ok "gpu-burn --limit 105: refused, says why" || bad "gpu-burn 105: rc=$rc $out"
    "$CLI" gpu-burn --help 2>&1 | grep -q -- "--accept-high-limit" && "$CLI" burn --help 2>&1 | grep -q -- "--accept-high-limit" && ok "--help of both names the flag" || bad "--help lacks --accept-high-limit"
fi
command -v Xvfb >/dev/null || { echo "  skip  no Xvfb"; exit 0; }
d=:$((80 + RANDOM % 9))
Xvfb "$d" -screen 0 1400x1000x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 & XPID=$!
sleep 1
run() {   # $1 tag, $2 answer ("" = no hook), $3 limits "CPU,GPU" ("" = as loaded)
    local tag=$1 ans=$2 lim=$3 G
    rm -f "$XDG_CACHE_HOME"/srs/burnin-*.txt
    env -u WAYLAND_DISPLAY GDK_BACKEND=x11 DISPLAY=$d SRS_TEST_NOCONFIRM=1 SRS_TEST_MAXSECS=3 SRS_TEST_GPU_BACKEND=vulkan \
        ${ans:+SRS_TEST_HOT_ANSWER=$ans} ${lim:+SRS_TEST_LIMITS=$lim} SRS_TEST_BURN_AUTOSTART=0 SRS_OPEN=gpuburn "$GUI" >"$TMP/$tag.log" 2>&1 &
    G=$!
    for i in $(seq 1 40); do ls "$XDG_CACHE_HOME"/srs/burnin-*.txt >/dev/null 2>&1 && break; sleep 0.25; done
    sleep 1
    kill $G 2>/dev/null; wait $G 2>/dev/null
}
asked() { grep -c "high temperature limit question shown" "$TMP/$1.log"; }
ran()   { ls "$XDG_CACHE_HOME"/srs/burnin-*.txt >/dev/null 2>&1; }

echo "== an old SRSDEF.ini with limits over 100 and no yes: loaded as 100, not asked"
printf '[SRSDEF]\nStress GPU Burnin=1\nAccepted Load Test Terms=1\nStress GPU Limit=115\nStress CPU Limit=108\n[Tabs]\nLineup=Enthusiast\n' >"$INI"
run o1 no ""
[ "$(asked o1)" = 0 ] && ok "not asked" || bad "asked for loaded values"
ran && ok "the run started" || bad "no run"
[ "$(key 'Stress GPU Limit')" = 100 ] && [ "$(key 'Stress CPU Limit')" = 100 ] && ok "both limits saved as 100" || bad "limits: GPU $(key 'Stress GPU Limit') CPU $(key 'Stress CPU Limit')"

echo "== CPU 105 / GPU 110 typed in, No: back to 100, nothing kept"
run n1 no "105,110"
[ "$(asked n1)" = 1 ] && ok "asked once for both limits" || bad "asked $(asked n1)x"
ran && ok "the run started (at 100)" || bad "no run"
[ "$(key 'Stress GPU Limit')" = 100 ] && [ "$(key 'Stress CPU Limit')" = 100 ] && ok "No: both limits back to 100" || bad "after No: GPU $(key 'Stress GPU Limit') CPU $(key 'Stress CPU Limit')"
[ "$(key 'Accepted High Temperature Limits')" = 1 ] && bad "acceptance stored after No" || ok "no acceptance stored"

echo "== typed in again, Yes: kept, stored once"
run y1 yes "105,110"
[ "$(asked y1)" = 1 ] && ok "asked exactly once for both limits" || bad "asked $(asked y1)x"
[ "$(key 'Accepted High Temperature Limits')" = 1 ] && ok "SRSDEF.ini: Accepted High Temperature Limits=1" || bad "not stored"
[ "$(key 'Stress GPU Limit')" = 110 ] && [ "$(key 'Stress CPU Limit')" = 105 ] && ok "limits 105 / 110 kept" || bad "after Yes: GPU $(key 'Stress GPU Limit') CPU $(key 'Stress CPU Limit')"
grep -q "temperature limit 110 °C" "$XDG_CACHE_HOME"/srs/bench-gpuburn.txt 2>/dev/null && ok "GPU Burnin ran with the 110 °C limit" || bad "GPU result: $(grep -i limit "$XDG_CACHE_HOME"/srs/bench-gpuburn.txt 2>/dev/null | head -2)"

echo "== restart: not asked again, 110 loaded"
run r1 no ""
[ "$(asked r1)" = 0 ] && ok "not asked after a restart" || bad "asked again"
[ "$(key 'Stress GPU Limit')" = 110 ] && ok "110 still set" || bad "GPU limit now $(key 'Stress GPU Limit')"

echo "== nothing above 110"
run m1 yes "130,130"
[ "$(key 'Stress GPU Limit')" = 110 ] && [ "$(key 'Stress CPU Limit')" = 110 ] && ok "130 typed in -> 110 (the most)" || bad "max: GPU $(key 'Stress GPU Limit') CPU $(key 'Stress CPU Limit')"

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
