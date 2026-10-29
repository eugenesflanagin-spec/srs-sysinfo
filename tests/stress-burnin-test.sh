#!/usr/bin/env bash
# tests/stress-burnin-test.sh - the Stress & Burnin window (2.3.1): the built-in GPU Burnin child (srs-gui --gpu-burn-child)
# and a GPU-only and a CPU + GPU run of the window, WITHOUT the real GPU.
#
#   tests/stress-burnin-test.sh SRS-GUI
#
# SAFETY: Vulkan sees only lavapipe (Mesa's software renderer) on 1 thread with 64 x 64 matrices, SRS_GPU_SOFTWARE_ONLY=1
# refuses anything else, the HIP / CUDA / OpenCL libraries point at files that do not exist, and every run is traced with
# LD_DEBUG=libs: the test FAILS if a real GPU library (radv, amdgpu, HIP, CUDA, OpenCL) is loaded. The CPU part uses ONE CPU
# for at most 3 seconds (SRS_TEST_MAXSECS). HOME and XDG_* are a throw-away folder. The GUI part runs on its own Xvfb display.
set -u
GUI=${1:-}
[ -x "$GUI" ] || { echo "usage: $0 SRS-GUI"; exit 2; }
GUI=$(cd "$(dirname "$GUI")" && pwd)/$(basename "$GUI")
TMP=$(mktemp -d)
cleanup() { [ -n "${XPID:-}" ] && kill "$XPID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME" "$XDG_CONFIG_HOME/srs"
printf '[SRSDEF]\nAccepted Load Test Terms=1\n' >"$XDG_CONFIG_HOME/srs/SRSDEF.ini"   # the terms have their own test (load-terms-test.sh)
LVP=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
[ -f "$LVP" ] || { echo "  skip  no lavapipe ($LVP)"; exit 0; }
export VK_DRIVER_FILES=$LVP VK_ICD_FILENAMES=$LVP VK_LOADER_LAYERS_DISABLE='~implicit~' DISABLE_MANGOHUD=1 LP_NUM_THREADS=1
export SRS_GPU_SOFTWARE_ONLY=1 SRS_TEST_GB_NMAX=64 SRS_CUDA_LIB=/nonexistent/libcuda.so SRS_CUDA_RTC=/nonexistent/libnvrtc.so
export SRS_HIP_LIB=/nonexistent/libamdhip64.so SRS_HIP_BLAS=/nonexistent SRS_OPENCL_LIB=/nonexistent/libOpenCL.so SRS_CLBLAST_LIB=/nonexistent/libclblast.so
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
REAL='libvulkan_radeon|libamdhip64|libcuda\.so|libnvcuda|libOpenCL|libamdocl|libnvidia|libhsa-runtime'
guard() {   # $1 = LD_DEBUG trace file
    if grep -Eq "calling init: .*($REAL)" "$1"*; then bad "a real GPU library was loaded ($(grep -Eho "calling init: [^ ]*($REAL)[^ ]*" "$1"* | head -1))"
    else ok "no real GPU library loaded"; fi
}
LIVE=$TMP/live.bin

echo "== child: 2 s on lavapipe"
LD_DEBUG=libs LD_DEBUG_OUTPUT=$TMP/ld1 "$GUI" --gpu-burn-child --backend 0 --device -1 --pci "" --prec 0 --seconds 2 --limit 100 --live "$LIVE" >"$TMP/c1.log" 2>&1
rc=$?
[ $rc = 0 ] && ok "exit 0 (PASS)" || { bad "exit $rc"; tail -5 "$TMP/c1.log"; }
[ -s "$LIVE" ] && ok "live file written" || bad "no live file"
[ -s "$XDG_CACHE_HOME/srs/bench-gpuburn.txt" ] && grep -q "PASS" "$XDG_CACHE_HOME/srs/bench-gpuburn.txt" && ok "bench-gpuburn.txt says PASS" || bad "no PASS in bench-gpuburn.txt"
grep -qi "lavapipe\|llvmpipe" "$TMP/c1.log" && ok "ran on the software renderer" || bad "software renderer not named in the report"
guard "$TMP/ld1"

echo "== child: SIGTERM ends it cleanly (Stop)"
LD_DEBUG=libs LD_DEBUG_OUTPUT=$TMP/ld2 "$GUI" --gpu-burn-child --backend 0 --device -1 --pci "" --prec 0 --seconds 60 --limit 100 --live "$LIVE" >"$TMP/c2.log" 2>&1 &
P=$!
sleep 1.5
kill -TERM $P
for i in $(seq 1 50); do kill -0 $P 2>/dev/null || break; sleep 0.1; done
if kill -0 $P 2>/dev/null; then bad "still running 5 s after SIGTERM"; kill -KILL $P; else wait $P; rc=$?; [ $rc = 10 ] && ok "exit 10 (stopped)" || bad "exit $rc after SIGTERM"; fi
guard "$TMP/ld2"

echo "== child: no Vulkan device -> not started"
VK_DRIVER_FILES=/nonexistent.json VK_ICD_FILENAMES=/nonexistent.json "$GUI" --gpu-burn-child --backend 0 --device -1 --pci "" --prec 0 --seconds 2 --limit 100 --live "$LIVE" >"$TMP/c3.log" 2>&1
rc=$?
[ $rc = 12 ] && ok "exit 12 (not started)" || bad "exit $rc (want 12)"
grep -q "GPU Burnin:" "$TMP/c3.log" && ok "says why: $(grep -m1 'GPU Burnin:' "$TMP/c3.log" | cut -c1-90)" || bad "no reason in the log"

if command -v Xvfb >/dev/null && command -v xwininfo >/dev/null; then
    d=:$((90 + RANDOM % 9))
    Xvfb "$d" -screen 0 1400x1000x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 & XPID=$!
    sleep 1
    run_gui() {   # $1 = tag, $2 = what to open, $3 = CPUs for the autostart; the rest = env
        local tag=$1 open=$2 n=$3; shift 3
        rm -f "$XDG_CACHE_HOME"/srs/burnin-*.txt
        env -u WAYLAND_DISPLAY GDK_BACKEND=x11 DISPLAY=$d LD_DEBUG=libs LD_DEBUG_OUTPUT=$TMP/ld-$tag SRS_TEST_NOCONFIRM=1 SRS_TEST_MAXSECS=3 \
            SRS_TEST_GPU_BACKEND=vulkan SRS_TEST_BURN_AUTOSTART=$n SRS_OPEN=$open "$@" "$GUI" >"$TMP/g-$tag.log" 2>&1 &
        local G=$!
        for i in $(seq 1 120); do ls "$XDG_CACHE_HOME"/srs/burnin-*.txt >/dev/null 2>&1 && break; sleep 0.25; done
        sleep 0.5
        [ -n "${SRS_SHOT_DIR:-}" ] && command -v import >/dev/null && \
            import -display "$d" -window root "$SRS_SHOT_DIR/stress-burnin-$tag.png" 2>/dev/null
        kill $G 2>/dev/null; wait $G 2>/dev/null
        cat "$XDG_CACHE_HOME"/srs/burnin-*.txt 2>/dev/null
    }
    echo "== GUI: GPU only (the GPU Burnin entry), 2 minutes asked, capped at 3 s"
    S=$(run_gui gpu gpuburn 0)
    if [ -z "$S" ]; then bad "no run summary saved"; tail -5 "$TMP/g-gpu.log"
    else
        echo "$S" | grep -q "GPU only\|GPU Burnin" && ok "GPU-only run ran and was saved" || bad "summary does not mention the GPU part"
        echo "$S" | grep -E "^(Pass|Fail|Stopped)|Status" | head -2 | sed 's/^/        /'
    fi
    guard "$TMP/ld-gpu"
    echo "== GUI: CPU (1 CPU) + GPU synced, capped at 3 s"
    printf '[SRSDEF]\nAccepted Load Test Terms=1\nStress GPU Burnin=1\nStress GPU Sync=1\nStress Until Stop=1\n' >"$XDG_CONFIG_HOME/srs/SRSDEF.ini"
    S=$(run_gui both stressburn 1)
    if [ -z "$S" ]; then bad "no run summary saved"; tail -5 "$TMP/g-both.log"
    else
        echo "$S" | grep -q "1 worker thread\|CPU-0" && ok "CPU part used 1 thread" || bad "CPU part not seen in the summary"
        echo "$S" | grep -q "GPU Burnin (built-in)" && ok "GPU part reported: $(echo "$S" | grep -m1 'GPU Burnin (built-in)' | cut -c1-90)" || bad "GPU part not reported"
    fi
    for i in $(seq 1 50); do ps -eo args | grep -q "[-]-gpu-burn-child" || break; sleep 0.1; done   # it ends cleanly on SIGTERM
    ps -eo args | grep -q "[-]-gpu-burn-child" && bad "a GPU child is still running 5 s after SRS ended" || ok "no GPU child left after SRS ended"
    guard "$TMP/ld-both"
else echo "  skip  no Xvfb / xwininfo"; fi

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
