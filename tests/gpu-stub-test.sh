#!/usr/bin/env bash
# tests/gpu-stub-test.sh - regression test for the HIP and CUDA back ends of GPU Burnin and the GPU Compute comparison
# (gpuburn_rt.c) against a FAKE GPU runtime (tests/gpu_stub.c). Needs no GPU, no driver, no ROCm / CUDA install.
#
#   tests/gpu-stub-test.sh SYSREPORT-SEARCH      (make test-bench passes the path)
#
# Why it exists: a kernel launch with a NULL function handle crashes a real driver (SIGSEGV in cuLaunchKernel). It happened when
# the vendor GEMM library (hipBLAS / cuBLAS) was measured while the own kernels had not been built, because the library variant
# checks every result with one of the own kernels. The stub ABORTS with a message on any NULL launch, so the test fails loudly.
#
# SAFETY: nothing here touches a real GPU. The CLI is started with
#   SRS_HIP_LIB / SRS_CUDA_LIB, SRS_HIP_RTC / SRS_CUDA_RTC, SRS_HIP_BLAS / SRS_CUDA_BLAS  = the stub (or a file that does not exist),
#   SRS_OPENCL_LIB = a file that does not exist, VK_DRIVER_FILES = a file that does not exist (no Vulkan driver at all),
# so none of the real libcuda (ZLUDA) / libamdhip64 / nvrtc / hiprtc / cublas / hipblas / OpenCL is looked up by name. As a second
# line, every run is traced with LD_DEBUG=libs and the test FAILS if any GPU runtime library other than the stub was loaded.
# (SRS_GPU_SOFTWARE_ONLY must be unset here: it makes HIP and CUDA refuse to open, which is the opposite of this test.)
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH"; exit 2; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

echo "== build the fake GPU runtime"
STUB=$TMP/libgpustub.so
if gcc -O2 -Wall -Wextra -shared -fPIC -fvisibility=hidden -o "$STUB" "$SRC/tests/gpu_stub.c" -lm 2>"$TMP/stub.log" && [ ! -s "$TMP/stub.log" ]; then ok "gpu_stub.c built, no warnings"
else cat "$TMP/stub.log"; bad "gpu_stub.c does not build cleanly"; exit 1; fi
if [ "${SRS_GPU_SOFTWARE_ONLY:-}" != "" ]; then echo "  note  SRS_GPU_SOFTWARE_ONLY is set in your environment; it is removed for these runs"; fi

# run_cli ENV... -- ARGS...   sets OUT (stdout+stderr), RC, LOG (launch counts), and checks which GPU libraries were loaded
n=0
run_cli() {
    local extra=()
    while [ "$1" != -- ]; do extra+=("$1"); shift; done
    shift
    n=$((n + 1))
    rm -f "$TMP/ld.$n".* "$TMP/stub.log.$n"
    OUT=$(env -u SRS_GPU_SOFTWARE_ONLY \
        SRS_CUDA_LIB="$STUB" SRS_CUDA_RTC="$STUB" SRS_CUDA_BLAS="$STUB" SRS_HIP_LIB="$STUB" SRS_HIP_RTC="$STUB" SRS_HIP_BLAS="$STUB" \
        SRS_OPENCL_LIB=/nonexistent/libOpenCL.so VK_DRIVER_FILES=/nonexistent VK_ICD_FILENAMES=/nonexistent VK_LOADER_DRIVERS_DISABLE='*' \
        DISABLE_MANGOHUD=1 STUB_LOG="$TMP/stub.log.$n" LD_DEBUG=libs LD_DEBUG_OUTPUT="$TMP/ld.$n" "${extra[@]}" \
        timeout 300 nice -n 15 "$BIN" "$@" 2>&1)
    RC=$?
    LOG=$(cat "$TMP/stub.log.$n" 2>/dev/null)
    local other
    other=$(cat "$TMP/ld.$n".* 2>/dev/null | grep "calling init" | grep -iE 'libcuda|libnvidia|libamdhip|libhsa|librocm|libhiprtc|libnvrtc|libcublas|libhipblas|libOpenCL|zluda' | grep -v "$STUB")
    if [ -n "$other" ]; then bad "a real GPU runtime library was loaded: $other"; fi
}
count() { sed -n "s/^launch $1 //p;s/^$1 //p" <<<"$LOG" | head -1; }
no_abort() { ! grep -q "NULL FUNCTION HANDLE\|core dumped\|Segmentation" <<<"$OUT" && [ "$RC" != 134 ] && [ "$RC" != 139 ]; }

for be in cuda hip; do
    case $be in cuda) LIBN=libnvrtc; BLASN=cuBLAS; U=CUDA ;; hip) LIBN=libhiprtc; BLASN=hipBLAS; U=HIP ;; esac
    echo "== $be, gpu-compare: stub runtime with compiler and GEMM library"
    run_cli X=1 -- gpu-compare --backends $be --seconds 0.1 --yes
    if [ $RC = 0 ] && no_abort; then ok "$be compare: exit 0, no NULL launch"; else bad "$be compare: rc=$RC $(tail -3 <<<"$OUT")"; fi
    [ "$(count gemm_r)" -gt 0 ] && [ "$(count cmp_u32)" -gt 0 ] && [ "$(count blas)" -gt 0 ] && ok "$be compare: own GEMM, library GEMM and the error check all ran" || bad "$be compare: launch counts: $LOG"
    grep -q "vendor library (GFLOPS) *[0-9]" <<<"$OUT" && ! grep -q "WRONG RESULT\|failed" <<<"$OUT" && ok "$be compare: every library row has a speed, none failed or wrong" || bad "$be compare rows: $(grep -E 'library|WRONG|failed' <<<"$OUT" | head -4)"

    echo "== $be, gpu-compare: the runtime compiler rejects the kernels (own kernels cannot be built; the library rows must not crash)"
    run_cli STUB_RTC_FAIL=1 -- gpu-compare --backends $be --seconds 0.1 --yes
    if no_abort && [ $RC = 0 ]; then ok "$be compare, compiler fails: exit 0, no NULL launch"; else bad "$be compare, compiler fails: rc=$RC $(tail -3 <<<"$OUT")"; fi
    grep -q "vendor library: the kernel did not compile" <<<"$OUT" && ok "$be compare, compiler fails: the library row says why" || bad "$be compare, compiler fails: no reason on the library row"
    [ "$(count cmp_u32)" = 0 ] && ok "$be compare, compiler fails: nothing was launched" || bad "launches after a failed compile: $LOG"

    echo "== $be, gpu-compare: a kernel the module should hold comes back NULL"
    run_cli STUB_NO_FN=cmp_u32 -- gpu-compare --backends $be --seconds 0.1 --yes
    if no_abort && [ $RC = 0 ] && grep -q "returned no kernel" <<<"$OUT"; then ok "$be compare, NULL kernel handle: refused with a message"; else bad "$be compare, NULL kernel: rc=$RC $(tail -3 <<<"$OUT")"; fi

    echo "== $be, gpu-compare: no runtime compiler installed"
    run_cli SRS_${U}_RTC=/nonexistent/$LIBN.so -- gpu-compare --backends $be --seconds 0.1 --yes
    if no_abort && [ $RC = 0 ]; then ok "$be compare, no compiler: exit 0, no NULL launch"; else bad "$be compare, no compiler: rc=$RC $(tail -3 <<<"$OUT")"; fi
    grep -q "$BLASN needs the runtime compiler ($LIBN) for the error check" <<<"$OUT" && ok "$be compare, no compiler: library row says it needs the compiler for the error check" || bad "$be compare, no compiler message: $(grep -i 'vendor\|compiler' <<<"$OUT" | head -3)"
    [ "$(count blas)" = 0 ] && ok "$be compare, no compiler: the library was not run unchecked" || bad "library ran without the error check: $LOG"

    echo "== $be, gpu-burn"
    run_cli X=1 -- gpu-burn --backend $be --seconds 1 --yes
    if [ $RC = 0 ] && no_abort && grep -q "PASS" <<<"$OUT" && [ "$(count cmp_u32)" -gt 0 ]; then ok "$be burn FP32: PASS, exit 0"; else bad "$be burn: rc=$RC $(tail -4 <<<"$OUT")"; fi
    run_cli X=1 -- gpu-burn --backend $be --fp64 --seconds 1 --yes
    if [ $RC = 0 ] && no_abort && grep -q "PASS" <<<"$OUT"; then ok "$be burn FP64: PASS, exit 0"; else bad "$be burn FP64: rc=$RC $(tail -4 <<<"$OUT")"; fi
    run_cli SRS_${U}_RTC=/nonexistent/$LIBN.so -- gpu-burn --backend $be --seconds 1 --yes
    if no_abort && [ $RC = 1 ] && grep -q "needs the runtime compiler ($LIBN) for the error check" <<<"$OUT"; then ok "$be burn, no compiler: ended with a clear message, no crash"; else bad "$be burn, no compiler: rc=$RC $(tail -4 <<<"$OUT")"; fi
    run_cli STUB_RTC_FAIL=1 -- gpu-burn --backend $be --seconds 1 --yes
    if no_abort && [ $RC = 1 ] && grep -q "did not compile" <<<"$OUT"; then ok "$be burn, compiler fails: ended with a clear message, no crash"; else bad "$be burn, compiler fails: rc=$RC $(tail -4 <<<"$OUT")"; fi
done

echo "== the stub itself aborts on a NULL launch (the test can see the bug)"
cat > "$TMP/nulllaunch.c" <<'C'
#include <dlfcn.h>
#include <stdio.h>
int main(int c, char **v)
{
    void *h = dlopen(v[1], RTLD_NOW);
    int (*l)(void *, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, void *, void **, void **) = h ? dlsym(h, "cuLaunchKernel") : 0;
    (void)c;
    if (!l) return 3;
    l(NULL, 1024, 1, 1, 256, 1, 1, 0, NULL, NULL, NULL);
    return 0;
}
C
if gcc -o "$TMP/nulllaunch" "$TMP/nulllaunch.c" -ldl 2>/dev/null; then
    ( ulimit -c 0; "$TMP/nulllaunch" "$STUB" >/dev/null 2>&1 ) 2>/dev/null; rc=$?
    [ $rc = 134 ] && ok "a NULL function handle aborts the stub (exit 134)" || bad "NULL launch did not abort: rc=$rc"
else echo "  skip  no compiler for the helper"; fi

exit $fail
