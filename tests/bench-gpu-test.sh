#!/usr/bin/env bash
# tests/bench-gpu-test.sh - correctness test for the GPU benchmarks: GPU compute (bench_gpu.c), GPU Burnin
# (bench_gpuburn.c) and GPU Compute comparison (bench_gpucompare.c) with the shared Vulkan layer and the
# HIP / CUDA / OpenCL back ends.
#
#   tests/bench-gpu-test.sh [SYSREPORT-SEARCH]   (VERBOSE=1 also prints the rendered results)
#   With the path of a built sysreport-search (make test-bench passes it) the command-line checks run too.
#
# SAFETY: this test never touches a real GPU. Vulkan runs ONLY on lavapipe (the Mesa software renderer):
# VK_DRIVER_FILES and VK_ICD_FILENAMES point at the lvp ICD file, the engines are asked for software
# renderers only, and the test program refuses to dispatch anything unless every Vulkan device it sees is
# llvmpipe. HIP, CUDA and OpenCL are NOT started: only dlopen + dlsym (symbol binding) and the
# "library missing" / "no platform" paths are exercised (SRS_HIP_LIB / SRS_CUDA_LIB / SRS_OPENCL_LIB point
# at a library that does not exist; OCL_ICD_VENDORS points at an empty folder).
# Tiny sizes, a few seconds in all, 2 lavapipe threads. Results go to a throw-away HOME.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
LVP=${LVP_ICD:-/usr/share/vulkan/icd.d/lvp_icd.x86_64.json}
[ -r "$LVP" ] || { echo "SKIP: no lavapipe ICD ($LVP) - install vulkan-swrast"; exit 0; }
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME"
export VK_DRIVER_FILES=$LVP VK_ICD_FILENAMES=$LVP VK_LOADER_LAYERS_DISABLE='~implicit~' DISABLE_MANGOHUD=1 LP_NUM_THREADS=2 SRS_GPU_SOFTWARE_ONLY=1
unset OCL_ICD_FILENAMES OPENCL_VENDOR_PATH VK_ADD_DRIVER_FILES VK_ADD_LAYER_PATH VK_LAYER_PATH
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
show() { [ -n "${VERBOSE:-}" ] && printf '%s\n' "$1"; return 0; }

echo "== layout of the Vulkan types against the real header, 64-bit and 32-bit"
for m in -m64 -m32; do
    if gcc $m -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -DGVK_LAYOUT_CHECK -fsyntax-only -I"$SRC" "$SRC/gpu_vk.c" 2>"$TMP/lay.log"; then ok "gpu_vk.c layout ($m)"
    elif grep -q "bits/\|gnu/stubs\|No such file" "$TMP/lay.log"; then echo "  skip  $m (no 32-bit headers)"
    else cat "$TMP/lay.log"; bad "layout check ($m)"; fi
done

echo "== shaders are up to date"
if command -v glslangValidator >/dev/null; then "$SRC/shaders/gpu/build.sh" --check >/dev/null && ok "bench_gpu_spv.inc matches shaders/gpu/*.comp" || bad "bench_gpu_spv.inc is stale"
else echo "  skip  glslangValidator not installed"; fi

echo "== build the test program (address + undefined sanitizers on the GPU files)"
cat > "$TMP/harness.c" <<'C'
#define _GNU_SOURCE
#include "bench_gpu.h"
#include "bench_gpuburn.h"
#include "bench_gpucompare.h"
#include "burner.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile int cancel;
static void *stopper(void *p) { struct timespec d = { 0, (long)(size_t)p }; nanosleep(&d, NULL); cancel = 1; return NULL; }
static int live;
static void burn_cb(const gpuburn_snap *s, void *u) { (void)u; live++; if (live == 1) printf("LIVE|%d|%.1f\n", s->ndev, s->total); }

/* refuse to go on unless every Vulkan device is the software renderer */
static void only_lavapipe(void)
{
    gpu_dev d[GPU_MAXDEV];
    char why[200];
    int n = gpu_list(d, GPU_MAXDEV, why, sizeof why), i;
    if (n < 1) { printf("NODEV|%s\n", why); exit(3); }
    for (i = 0; i < n; i++)
        if (d[i].dtype != GPU_CPU || !strstr(d[i].name, "llvmpipe")) { printf("REFUSED|%s is not lavapipe\n", d[i].name); exit(4); }
}

int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "";
    srs_out o;
    char why[300] = "";
    int rc, i;
    pthread_t t;
    so_text_init(&o, stdout);
    if (!strcmp(m, "novulkan")) {
        rc = gpu_available(why, sizeof why);
        printf("AVAIL|%d|%s\n", rc, why);
        { gpu_cfg c; gpu_cfg_default(&c); rc = bench_gpu(&c, NULL, NULL, why, sizeof why); printf("RC|%d|%s\n", rc, why); }
        return 0;
    }
    if (!strcmp(m, "nodevice")) {
        gpu_dev d[4];
        rc = gpu_list(d, 4, why, sizeof why);
        printf("LIST|%d|%s\n", rc, why);
        { gpu_cfg c; gpu_cfg_default(&c); rc = bench_gpu(&c, NULL, NULL, why, sizeof why); printf("RC|%d|%s\n", rc, why); }
        return 0;
    }
    if (!strcmp(m, "bind")) {   /* dlopen + dlsym only, nothing is started */
        for (i = GB_HIP; i < GB_N; i++) {
            char d[300];
            rc = gb_bind_check(i, d, sizeof d);
            printf("BIND|%s|%d|%s\n", gb_id[i], rc, d);
        }
        return 0;
    }
    if (!strcmp(m, "missing")) {   /* HIP, CUDA, OpenCL libraries missing: the table says so */
        gpucmp_cfg c;
        gpucmp_result *r = calloc(1, sizeof *r);
        gpucmp_cfg_default(&c);
        c.backends = (1u << GB_HIP) | (1u << GB_CUDA) | (1u << GB_OPENCL);
        rc = bench_gpucompare(&c, r, &o, NULL, why, sizeof why);
        printf("RC|%d|%s|cols=%d\n", rc, why, r->ncol);
        for (i = 0; i < r->ncol; i++) printf("COL|%s|shown=%d|%s\n", r->col[i].info.label, r->col[i].shown, r->col[i].hint);
        { gpuburn_cfg b; gpuburn_cfg_default(&b); b.backend = GB_HIP; b.seconds = 1; rc = bench_gpuburn(&b, NULL, NULL, why, sizeof why); printf("BURN|%d|%s\n", rc, why); }
        { gpuburn_cfg b; gpuburn_cfg_default(&b); b.backend = GB_OPENCL; b.seconds = 1; rc = bench_gpuburn(&b, NULL, NULL, why, sizeof why); printf("BURN|%d|%s\n", rc, why); }
        return 0;
    }
    if (!strcmp(m, "oclempty")) {   /* an OpenCL loader with no platforms registered (OCL_ICD_VENDORS = empty folder) */
        gb_info l[4];
        rc = gb_opencl.list(l, 4, why, sizeof why);
        printf("OCL|%d|%s\n", rc, why);
        return 0;
    }
    if (!strcmp(m, "cpuburn")) {   /* the CPU burner refuses while a GPU test holds the claim (one CPU only, stopped at once if it starts) */
        const char *who = NULL;
        burn_cfg b;
        burn_cfg_default(&b);
        for (i = 0; i < b.ncpu; i++) b.enabled[i] = i == 0;
        b.burn_s = 1;
        if (!gpu_claim("GPU Burnin", &who)) { printf("CLAIM|failed\n"); return 1; }
        rc = burn_start(&b);
        if (rc == 0) burn_stop();
        gpu_release();
        printf("CPUBURN|%d\n", rc);
        { burn_status st; burn_tick(&st); printf("CPUSTATUS|%s\n", st.status); }
        gpu_claim_hung();   /* after a driver call that never came back */
        { char t[200]; const char *w2 = NULL; if (gpu_claim("GPU benchmark", &w2)) printf("HUNG|claimed\n"); else { gpu_claim_text(w2, t, sizeof t); printf("HUNGTEXT|%s\n", t); } }
        rc = burn_start(&b);
        { burn_status st; burn_tick(&st); printf("HUNGBURN|%d|%s\n", rc, st.status); }
        gpu_release();
        return 0;
    }
    only_lavapipe();
    if (!strcmp(m, "list")) { printf("LAVAPIPE|ok\n"); return 0; }
    if (!strcmp(m, "nocpu")) {   /* outside the test mode a software renderer is never burned: refused before anything is opened */
        gpuburn_cfg c;
        int dv[3] = { GPUBURN_ALL, -1, 0 };
        for (i = 0; i < 3; i++) {
            gpuburn_cfg_default(&c);
            c.backend = GB_VULKAN; c.device = dv[i]; c.software_only = 0; c.seconds = 1; c.n_max = 64;
            rc = bench_gpuburn(&c, NULL, NULL, why, sizeof why);
            printf("NOCPU|%d|%s\n", rc, why);
        }
        return 0;
    }
    if (!strcmp(m, "bench") || !strcmp(m, "benchstop")) {
        gpu_cfg c;
        gpu_cfg_default(&c);
        c.software_only = 1; c.seconds = 0.1; c.mem_mib = 2;
        if (!strcmp(m, "benchstop")) pthread_create(&t, NULL, stopper, (void *)(size_t)150000000);
        rc = bench_gpu(&c, &o, &cancel, why, sizeof why);
        if (!strcmp(m, "benchstop")) pthread_join(t, NULL);
        printf("RC|%d|%s\n", rc, why);
        return 0;
    }
    if (!strcmp(m, "burn") || !strcmp(m, "burnstop") || !strcmp(m, "burn64") || !strcmp(m, "burnlimit")) {
        gpuburn_cfg c;
        gpuburn_cfg_default(&c);
        c.backend = GB_VULKAN; c.device = 0; c.software_only = 1; c.seconds = 2; c.n_max = 64; c.cb = burn_cb;
        if (!strcmp(m, "burn64")) c.prec = GP_FP64;
        if (!strcmp(m, "burnstop")) { c.seconds = 30; pthread_create(&t, NULL, stopper, (void *)(size_t)1200000000); }
        rc = bench_gpuburn(&c, &o, &cancel, why, sizeof why);
        if (!strcmp(m, "burnstop")) pthread_join(t, NULL);
        printf("RC|%d|%s|live=%d\n", rc, why, live);
        { gpuburn_cfg d; gpuburn_cfg_default(&d); d.backend = GB_VULKAN; d.software_only = 1; d.seconds = 1; d.n_max = 64;
          if (!strcmp(m, "burnlimit")) { char *a = getenv("SRS_TEST_BURN_NOTHING"); (void)a; }
          rc = bench_gpuburn(&d, NULL, NULL, why, sizeof why); printf("AGAIN|%d|%s\n", rc, why); }
        return 0;
    }
    if (!strcmp(m, "compare") || !strcmp(m, "comparestop")) {
        gpucmp_cfg c;
        gpucmp_result *r = calloc(1, sizeof *r);
        gpucmp_cfg_default(&c);
        c.backends = 1u << GB_VULKAN; c.software_only = 1; c.tiny = 1; c.seconds = 0.1;
        if (!strcmp(m, "comparestop")) pthread_create(&t, NULL, stopper, (void *)(size_t)200000000);
        rc = bench_gpucompare(&c, r, &o, &cancel, why, sizeof why);
        if (!strcmp(m, "comparestop")) pthread_join(t, NULL);
        printf("RC|%d|%s|cols=%d\n", rc, why, r->ncol);
        return 0;
    }
    printf("unknown mode %s\n", m);
    return 2;
}
C
SECS=$(make -s -C "$SRC" --no-print-directory --eval 'srs-print-secs:; @echo $(SECS)' srs-print-secs)
GPUSRC="gpu_vk.c gpu_backend.c gpuburn_vk.c gpuburn_rt.c gpuburn_cl.c bench_gpu.c bench_gpuburn.c bench_gpucompare.c"
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
CF="-std=c11 -Wall -Wextra -Wno-format-truncation -DSRS_HAVE_SECTIONS -D_DEFAULT_SOURCE -pthread -I$SRC"
: > "$TMP/build.log"
( cd "$SRC" && for f in $GPUSRC; do nice /usr/bin/gcc $CF -O1 -g $SAN -c -o "$TMP/${f%.c}.o" "$f" || exit 1; done ) 2>>"$TMP/build.log"
if [ -f "$TMP/bench_gpucompare.o" ]; then
    ( cd "$SRC" && nice /usr/bin/gcc $CF -O1 $SAN -DSRS_NO_MAIN -o "$TMP/harness" "$TMP/harness.c" core.c cli.c plat_posix.c $(printf '%s\n' $SECS | grep -vE '^(bench_gpu|gpu_|gpuburn_)') "$TMP"/gpu_vk.o "$TMP"/gpu_backend.o "$TMP"/gpuburn_*.o "$TMP"/bench_gpu*.o -lm ) 2>>"$TMP/build.log"
fi
if [ -x "$TMP/harness" ]; then
    grep -E "gpu|bench_gpu.*warning" "$TMP/build.log" | grep -q "warning" && { grep warning "$TMP/build.log" | head; bad "compiler warnings"; } || ok "built, no warnings"
else
    cat "$TMP/build.log" | head -30; bad "test program does not build"; exit 1
fi
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1
H="$TMP/harness"
leftlibs() { grep -c . <<<"$1"; }

echo "== Vulkan loader missing / no device"
out=$(SRS_VULKAN_LIB=/nonexistent/libvulkan.so.9 "$H" novulkan 2>&1)
grep -q '^AVAIL|0|the Vulkan loader' <<<"$out" && ok "gpu_available says the loader is not installed" || bad "AVAIL: $out"
grep -q '^RC|-1|the Vulkan loader' <<<"$out" && ok "bench_gpu: not started, says why" || bad "RC: $out"
out=$(VK_DRIVER_FILES=/nonexistent VK_ICD_FILENAMES=/nonexistent VK_LOADER_DRIVERS_DISABLE="*" "$H" nodevice 2>&1)
grep -q '^LIST|' <<<"$out" && grep -q '^RC|-1|' <<<"$out" && ok "no Vulkan driver: listed 0 / not started" || bad "nodevice: $out"

echo "== HIP, CUDA, OpenCL: libraries missing (nothing is started)"
out=$(SRS_HIP_LIB=/nonexistent/libhip.so SRS_CUDA_LIB=/nonexistent/libcuda.so SRS_OPENCL_LIB=/nonexistent/libOpenCL.so "$H" missing 2>&1); show "$out"
[ "$(grep -c '^COL|.*|shown=0|not installed' <<<"$out")" = 3 ] && ok "three columns, each 'not installed' with a hint" || bad "missing columns: $out"
grep -q "ROCm / HIP.*not installed - install ROCm" <<<"$out" && grep -q "install the NVIDIA driver" <<<"$out" && grep -q "install an OpenCL driver" <<<"$out" && ok "one-line install hints" || bad "hints"
[ "$(grep -c '^BURN|-1|' <<<"$out")" = 2 ] && ok "GPU Burnin with a missing back end: not started, says why" || bad "burn missing: $out"
grep -q "not installed" "$HOME/.cache/srs/bench-gpucompare.txt" && ok "result file says not installed" || bad "no bench-gpucompare.txt"

echo "== OpenCL loader with no platform registered (empty OCL_ICD_VENDORS)"
mkdir -p "$TMP/noicd"
out=$(OCL_ICD_VENDORS="$TMP/noicd" "$H" oclempty 2>&1)
grep -qE '^OCL\|0\|.*no OpenCL platform' <<<"$out" && ok "0 devices, plain-English reason" || bad "oclempty: $out"

echo "== binding of the real libraries (dlopen + dlsym only, no runtime started)"
out=$("$H" bind 2>&1)
while IFS='|' read -r _ id rc detail; do
    case "$rc" in
        0) ok "$id: $detail" ;;
        -1) echo "  skip  $id: not installed on this machine" ;;
        *) bad "$id: $detail" ;;
    esac
done < <(grep '^BIND|' <<<"$out")

echo "== CPU Burnin refuses while a GPU test runs"
out=$("$H" cpuburn 2>&1)
grep -q '^CPUBURN|-1' <<<"$out" && ok "burn_start refused while the GPU claim is held" || bad "cpuburn: $out"
grep -q '^CPUSTATUS|.*not started: GPU Burnin is running' <<<"$out" && ok "CPU Burnin says why it did not start" || bad "cpuburn status: $out"
grep -q '^HUNGTEXT|a GPU call did not return earlier; restart SRS' <<<"$out" && ok "after a hung driver call the next GPU test says to restart SRS" || bad "hung text: $out"
grep -q '^HUNGBURN|-1|.*not started: a GPU call did not return' <<<"$out" && ok "CPU Burnin says the same after a hung driver call" || bad "hung burn: $out"

echo "== lavapipe only"
out=$(VK_LOADER_DEBUG=driver "$H" list 2>&1)
grep -q '^LAVAPIPE|ok' <<<"$out" && ok "every Vulkan device is llvmpipe" || bad "device check: $out"
grep -i "radeon\|radv" <<<"$out" | grep -qi "loading\|found" && bad "a real driver was loaded" || ok "only the lvp driver was loaded"

echo "== GPU Burnin never burns a software renderer outside the test mode"
out=$("$H" nocpu 2>&1)
[ "$(grep -c '^NOCPU|-1|.*software renderer' <<<"$out")" = 3 ] && ok "All GPUs, the default and an explicit pick of llvmpipe: refused" || bad "nocpu: $out"
grep -q "^LIVE" <<<"$out" && bad "nocpu: a run started"

echo "== GPU compute benchmark, all tests, tiny sizes"
t0=$(date +%s.%N)
out=$("$H" bench 2>&1); show "$out"
t1=$(date +%s.%N)
grep -q '^RC|0|' <<<"$out" && ok "return 0 in $(printf '%.1f' "$(echo "$t1 - $t0" | bc)") s" || bad "bench: $(tail -5 <<<"$out")"
for t in "FP32 multiply-add" "INT32 multiply-add" "Video memory read" "Video memory write" "Video memory copy" "Upload" "Download" "Dispatch latency"; do
    grep -q "$t" <<<"$out" && ! grep "$t" <<<"$out" | grep -q "WRONG\|failed" && ok "$t measured" || bad "$t: $(grep "$t" <<<"$out")"
done
grep -q "WRONG RESULT" <<<"$out" && bad "a result was wrong"
grep -q "alike SRS builds" <<<"$out" && grep -q "Build" <<<"$out" && ok "result names the build and says to compare alike builds only" || bad "no build line / comparability note"
grep -q "GPU timestamps\|host clock" <<<"$out" && ok "result says how it was timed" || bad "no timing line"
grep -qiE "$(printf 'clea%s-room|ai%sa' n d)" <<<"$out" && bad "forbidden wording in the result"
[ -s "$HOME/.cache/srs/bench-gpu.txt" ] && ok "result file written" || bad "no bench-gpu.txt"
grep -q $'\tgpu\t' "$HOME/.cache/srs/bench-history.tsv" 2>/dev/null && ok "history line added" || bad "no history line"
grep -E "MYPC|$(hostname)" "$HOME/.cache/srs/bench-gpu.txt" >/dev/null 2>&1 && echo "  note  host name appears in the saved file (header line, redacted by -save --redact)"
out=$("$H" benchstop 2>&1)
grep -q '^RC|1|' <<<"$out" && grep -q "Stopped before the end" <<<"$out" && ok "Stop: return 1, result says stopped" || bad "benchstop: $(tail -3 <<<"$out")"

echo "== GPU Burnin (Vulkan on lavapipe, 2 s, 64 x 64)"
out=$("$H" burn 2>&1); show "$out"
grep -q '^RC|0|' <<<"$out" && ok "PASS, return 0" || bad "burn: $(tail -5 <<<"$out")"
grep -q "PASS" <<<"$out" && grep -q "0 wrong results found" <<<"$out" && ok "result: PASS with 0 errors" || bad "no PASS / error count"
grep -qE 'live=[1-9]' <<<"$out" && ok "live callback called" || bad "no live callback"
grep -q '^AGAIN|0|' <<<"$out" && ok "the lock was released: a second run started" || bad "second run: $(grep AGAIN <<<"$out")"
grep -q "Highest temperatures" <<<"$out" && ok "sensors line present (values or 'not available')" || bad "no sensors line"
grep -q $'\tgpuburn\t.*PASS' "$HOME/.cache/srs/bench-history.tsv" && ok "history line records PASS and the back end" || bad "no gpuburn history"
grep -q "backend=Vulkan" "$HOME/.cache/srs/bench-history.tsv" && ok "history records the back end" || bad "back end not in history"
out=$("$H" burn64 2>&1)
if grep -q "no FP64\|cannot run that precision" <<<"$out"; then echo "  note  lavapipe reports no FP64 here"; else grep -q '^RC|0|' <<<"$out" && ok "FP64 run passes" || bad "burn64: $(tail -4 <<<"$out")"; fi
t0=$(date +%s)
out=$("$H" burnstop 2>&1)
t1=$(date +%s)
grep -q '^RC|1|' <<<"$out" && [ $((t1 - t0)) -le 8 ] && ok "Stop ended a 30 s run after ~1.2 s" || bad "burnstop: $(tail -3 <<<"$out") ($((t1 - t0)) s)"

echo "== GPU Compute: Vulkan column, tiny sizes"
out=$("$H" compare 2>&1); show "$out"
grep -q '^RC|0|' <<<"$out" && ok "return 0" || bad "compare: $(tail -5 <<<"$out")"
grep -q "FP32 matrix multiply, own kernel" <<<"$out" && grep -q "vendor library" <<<"$out" && ok "own-kernel and library rows present" || bad "rows"
grep -q "not available" <<<"$out" && ok "Vulkan has no library variant: shown as not available" || bad "library row"
grep -q "WRONG RESULT" <<<"$out" && bad "a result was wrong"
grep -q "\*" <<<"$out" && ok "fastest marked" || bad "no fastest marker"
grep -q $'\tgpucompare\t' "$HOME/.cache/srs/bench-history.tsv" && ok "history line added" || bad "no gpucompare history"
out=$("$H" comparestop 2>&1)
grep -q '^RC|1|' <<<"$out" && ok "Stop: return 1" || bad "comparestop: $(tail -3 <<<"$out")"


BIN=${1:-}
if [ -n "$BIN" ] && [ -x "$BIN" ]; then
echo "== command line: $BIN"
"$BIN" gpu --help | grep -q "usage: sysreport-search gpu" && ok "gpu --help" || bad "gpu --help"
"$BIN" gpu --bogus >/dev/null 2>&1; [ $? = 2 ] && ok "gpu: unknown option -> exit 2, nothing started" || bad "gpu --bogus exit code"
"$BIN" gpu --seconds 99 >/dev/null 2>&1; [ $? = 2 ] && ok "gpu: --seconds out of range -> exit 2" || bad "gpu --seconds 99"
out=$("$BIN" gpu --list 2>&1)
if grep -q . <<<"$out" && ! grep -v llvmpipe <<<"$out" | grep -q '^[0-9]'; then
    ok "gpu --list shows only llvmpipe"
    out=$("$BIN" gpu --seconds 0.1 </dev/null 2>&1); rc=$?
    [ $rc = 2 ] && grep -q "not started" <<<"$out" && grep -q -- "--yes" <<<"$out" && ok "gpu: no terminal and no --yes -> not started, exit 2" || bad "gpu no tty: $rc $out"
    out=$(nice "$BIN" gpu --yes --seconds 0.1 --mem-mib 2 --only fp32,read,latency 2>&1); rc=$?; show "$out"
    [ $rc = 0 ] && grep -q "FP32 multiply-add" <<<"$out" && ok "gpu --yes on lavapipe: exit 0, result printed" || bad "gpu --yes: $rc $(tail -3 <<<"$out")"
else
    bad "gpu --list shows a device that is not llvmpipe - the command-line runs are skipped: $out"
fi

"$BIN" gpu-burn --help | grep -q "usage: sysreport-search gpu-burn" && ok "gpu-burn --help" || bad "gpu-burn --help"
"$BIN" gpu-burn --bogus >/dev/null 2>&1; [ $? = 2 ] && ok "gpu-burn: unknown option -> exit 2" || bad "gpu-burn --bogus"
"$BIN" gpu-burn --backend metal >/dev/null 2>&1; [ $? = 2 ] && ok "gpu-burn: unknown back end -> exit 2" || bad "gpu-burn --backend metal"
"$BIN" gpu-burn --seconds 0 >/dev/null 2>&1; [ $? = 2 ] && ok "gpu-burn: --seconds 0 -> exit 2" || bad "gpu-burn --seconds 0"
"$BIN" gpu-burn --limit 200 >/dev/null 2>&1; [ $? = 2 ] && ok "gpu-burn: --limit 200 -> exit 2" || bad "gpu-burn --limit 200"
out=$(SRS_HIP_LIB=/nonexistent/libhip.so "$BIN" gpu-burn --backend hip --seconds 1 --yes 2>&1); rc=$?
[ $rc = 2 ] && grep -q "not installed" <<<"$out" && ok "gpu-burn --backend hip with no HIP: not started, says not installed" || bad "gpu-burn hip missing: $rc $out"
out=$("$BIN" gpu-burn --list --backend vulkan 2>&1)
if grep -q "^Vulkan:" <<<"$out" && ! grep '^  [0-9]' <<<"$out" | grep -vq llvmpipe; then
    ok "gpu-burn --list --backend vulkan shows only llvmpipe"
    out=$("$BIN" gpu-burn --backend vulkan --seconds 1 </dev/null 2>&1); rc=$?
    [ $rc = 2 ] && grep -q "not started" <<<"$out" && ok "gpu-burn: no terminal and no --yes -> not started, exit 2" || bad "gpu-burn no tty: $rc $out"
    out=$(nice "$BIN" gpu-burn --backend vulkan --seconds 1 --yes 2>&1); rc=$?; show "$out"
    [ $rc = 0 ] && grep -q "PASS" <<<"$out" && ok "gpu-burn --yes on lavapipe: PASS, exit 0" || bad "gpu-burn --yes: $rc $(tail -4 <<<"$out")"
else
    bad "gpu-burn --list shows a device that is not llvmpipe - the command-line run is skipped: $out"
fi

"$BIN" gpu-compare --help | grep -q "usage: sysreport-search gpu-compare" && ok "gpu-compare --help" || bad "gpu-compare --help"
"$BIN" gpu-compare --bogus >/dev/null 2>&1; [ $? = 2 ] && ok "gpu-compare: unknown option -> exit 2" || bad "gpu-compare --bogus"
"$BIN" gpu-compare --backends vulkan,metal >/dev/null 2>&1; [ $? = 2 ] && ok "gpu-compare: unknown back end -> exit 2" || bad "gpu-compare --backends metal"
"$BIN" gpu-compare --seconds 50 >/dev/null 2>&1; [ $? = 2 ] && ok "gpu-compare: --seconds 50 -> exit 2" || bad "gpu-compare --seconds 50"
out=$(SRS_HIP_LIB=/nonexistent/libhip.so "$BIN" gpu-compare --backend hip --yes 2>&1); rc=$?
[ $rc = 2 ] && grep -q "none of the chosen back ends is installed" <<<"$out" && ok "gpu-compare --backend hip with no HIP: not started" || bad "gpu-compare hip missing: $rc $out"
out=$("$BIN" gpu-compare --backends vulkan --seconds 0.1 </dev/null 2>&1); rc=$?
[ $rc = 2 ] && grep -q "not started" <<<"$out" && ok "gpu-compare: no terminal and no --yes -> not started, exit 2" || bad "gpu-compare no tty: $rc $out"
if "$BIN" gpu --list 2>&1 | grep '^[0-9]' | grep -vq llvmpipe; then
    bad "a Vulkan device that is not llvmpipe is visible - gpu-compare run skipped"
else
    out=$(nice "$BIN" gpu-compare --backends vulkan --seconds 0.1 --yes 2>&1); rc=$?; show "$out"
    [ $rc = 0 ] && grep -q "FP32 matrix multiply, own kernel" <<<"$out" && ok "gpu-compare --yes on lavapipe: exit 0, table printed" || bad "gpu-compare --yes: $rc $(tail -4 <<<"$out")"
    grep -q "not installed - not installed" <<<"$out" && bad "doubled 'not installed' wording"
fi
fi

exit $fail
