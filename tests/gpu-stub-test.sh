#!/usr/bin/env bash
# tests/gpu-stub-test.sh - regression test for the HIP, CUDA and OpenCL back ends of GPU Burnin and the GPU Compute
# comparison (gpuburn_rt.c, gpuburn_cl.c) against a FAKE GPU runtime (tests/gpu_stub.c). Needs no GPU, no driver, no
# ROCm / CUDA / OpenCL install. It also checks the CUDA kernels built into SRS as PTX (gpu_kernels_ptx.inc) offline.
#
#   tests/gpu-stub-test.sh SYSREPORT-SEARCH      (make test-bench passes the path)
#
# Why it exists: a kernel launch with a NULL function handle crashes a real driver (SIGSEGV in cuLaunchKernel). It happened when
# the vendor GEMM library (hipBLAS / cuBLAS) was measured while the own kernels had not been built, because the library variant
# checks every result with one of the own kernels. The stub ABORTS with a message on any NULL launch, so the test fails loudly.
#
# SAFETY: nothing here touches a real GPU. The CLI is started with
#   SRS_HIP_LIB / SRS_CUDA_LIB, SRS_HIP_RTC / SRS_CUDA_RTC, SRS_HIP_BLAS / SRS_CUDA_BLAS  = the stub (or a file that does not exist),
#   SRS_OPENCL_LIB / SRS_CLBLAST_LIB = a file that does not exist (or the stub), VK_DRIVER_FILES = a file that does not exist
# (no Vulkan driver at all), so none of the real libcuda (ZLUDA) / libamdhip64 / nvrtc / hiprtc / cublas / hipblas / OpenCL /
# CLBlast is looked up by name. As a second
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
        SRS_OPENCL_LIB=/nonexistent/libOpenCL.so SRS_CLBLAST_LIB=/nonexistent/libclblast.so VK_DRIVER_FILES=/nonexistent VK_ICD_FILENAMES=/nonexistent VK_LOADER_DRIVERS_DISABLE='*' \
        DISABLE_MANGOHUD=1 STUB_LOG="$TMP/stub.log.$n" LD_DEBUG=libs LD_DEBUG_OUTPUT="$TMP/ld.$n" "${extra[@]}" \
        timeout 300 nice -n 15 "$BIN" "$@" 2>&1)
    RC=$?
    LOG=$(cat "$TMP/stub.log.$n" 2>/dev/null)
    local other
    other=$(cat "$TMP/ld.$n".* 2>/dev/null | grep "calling init" | grep -iE 'libcuda|libnvidia|libamdhip|libhsa|librocm|libhiprtc|libnvrtc|libcublas|libhipblas|libOpenCL|libclblast|libRusticl|libpocl|zluda' | grep -v "$STUB")
    if [ -n "$other" ]; then bad "a real GPU runtime library was loaded: $other"; fi
}
count() { sed -n "s/^launch $1 //p;s/^$1 //p" <<<"$LOG" | head -1; }
no_abort() { ! grep -q "NULL FUNCTION HANDLE\|core dumped\|Segmentation" <<<"$OUT" && [ "$RC" != 134 ] && [ "$RC" != 139 ]; }

echo "== the CUDA kernels built into SRS as PTX (gpu_kernels_ptx.inc), checked offline: no GPU, no ptxas needed"
INC=$SRC/gpu_kernels_ptx.inc
cat > "$TMP/ptxdump.c" <<'C'
#include <stdio.h>
#include <string.h>
#include "gpu_kernels_ptx.inc"
int main(int c, char **v) { (void)c; fputs(strcmp(v[1], "64") ? gk_ptx_fp32 : gk_ptx_fp64, stdout); return 0; }
C
if gcc -Wall -Wextra -I"$SRC" -o "$TMP/ptxdump" "$TMP/ptxdump.c" 2>"$TMP/pd.log" && [ ! -s "$TMP/pd.log" ]; then
    # what the NVRTC source declares: kernel names in order and their parameter count
    want=$(grep -o 'KERNEL void [a-z0-9_]*([^)]*)' "$SRC/gpu_kernels.h" | sed 's/KERNEL void \([a-z0-9_]*\)(\(.*\))/\1 \2/' | awk '{n=split(substr($0, index($0," ")+1), a, ","); print $1, n}')
    for pr in 32 64; do
        "$TMP/ptxdump" $pr > "$TMP/k$pr.ptx"
        real=$([ $pr = 64 ] && echo f64 || echo f32)
        grep -q "SRS own kernels, FP$pr" "$TMP/k$pr.ptx" || bad "PTX FP$pr: header comment missing"
        { sed -n 1,12p "$TMP/k$pr.ptx" | grep -qx '.version 6.0' && sed -n 1,12p "$TMP/k$pr.ptx" | grep -qx '.target sm_50' && grep -qx '.address_size 64' "$TMP/k$pr.ptx"; } \
            && ok "PTX FP$pr: .version 6.0, .target sm_50, .address_size 64" || bad "PTX FP$pr header: $(sed -n 1,12p "$TMP/k$pr.ptx" | grep '^\.')"
        # name + number of parameters of every .entry, in order
        have=$(awk '/^\.visible \.entry /{n=$3; sub(/\(.*/, "", n); c=0; inp=1; next} inp && /\.param /{c++} inp && /^\)/{print n, c; inp=0}' "$TMP/k$pr.ptx")
        [ "$have" = "$want" ] && ok "PTX FP$pr: the five entries match the NVRTC source (names, order, parameter counts)" || bad "PTX FP$pr entries: have [$have] want [$want]"
        types=$(awk '/^\.visible \.entry /{n=$3; sub(/\(.*/, "", n); inp=1; next} inp && /\.param /{printf "%s:%s ", n, $2} inp && /^\)/{inp=0}' "$TMP/k$pr.ptx")
        expect="gemm_r:.u64 gemm_r:.u64 gemm_r:.u64 gemm_r:.u32 fma_r:.u64 fma_r:.$real fma_r:.$real fma_r:.$real fma_r:.$real fma_r:.u32 fma_r:.u32 fma_r:.u32 copy_u4:.u64 copy_u4:.u64 copy_u4:.u32 cmp_u32:.u64 cmp_u32:.u64 cmp_u32:.u64 cmp_u32:.u32 tick:.u64 "
        [ "$types" = "$expect" ] && ok "PTX FP$pr: parameter types match (pointers .u64, REAL .$real, int / unsigned .u32)" || bad "PTX FP$pr types: $types"
        grep -qE '^\s*(call|\.extern|\.func)' "$TMP/k$pr.ptx" && bad "PTX FP$pr calls an outside function (unresolvable without libdevice)" || ok "PTX FP$pr: self-contained (no call / .extern / .func)"
        [ "$(grep -c '^{' "$TMP/k$pr.ptx")" = 5 ] && [ "$(grep -c '^}' "$TMP/k$pr.ptx")" = 5 ] && [ "$(grep -c 'ret;' "$TMP/k$pr.ptx")" -ge 5 ] && ok "PTX FP$pr: five bodies, each ends in ret" || bad "PTX FP$pr: body structure"
        [ $pr = 32 ] && { grep -q '\.f64' "$TMP/k$pr.ptx" && bad "PTX FP32 uses FP64 instructions" || ok "PTX FP32: no FP64 instruction"; }
        [ $pr = 64 ] && { grep -q 'fma\.rn\.f64' "$TMP/k$pr.ptx" && ok "PTX FP64: uses fma.rn.f64" || bad "PTX FP64: no fma.rn.f64"; }
    done
    grep -q 'ld\.global\.v4' "$TMP/k32.ptx" && ok "PTX: copy_u4 moves 16 bytes per load (v4)" || bad "PTX: copy_u4 not vectorised"
    # the binary carries the same text
    grep -aqF "$(sed -n '/\.entry fma_r(/,/^)/p' "$TMP/k64.ptx" | head -3 | tr -d '\n')" <(tr -d '\n' < "$BIN") && ok "the program file carries the built-in PTX" || bad "the built-in PTX is not in $BIN"
else cat "$TMP/pd.log"; bad "gpu_kernels_ptx.inc does not compile cleanly"; fi
if command -v clang >/dev/null && clang -print-targets 2>/dev/null | grep -q nvptx64; then
    "$SRC/tools/gen-cuda-ptx.sh" --check >"$TMP/gen.log" 2>&1; grc=$?
    case $grc in
    0) ok "gpu_kernels_ptx.inc is what clang makes of gpu_kernels.h now (tools/gen-cuda-ptx.sh --check)" ;;
    1) bad "$(cat "$TMP/gen.log")" ;;
    *) echo "  skip  $(tail -1 "$TMP/gen.log")" ;;
    esac
else echo "  skip  no clang with the NVPTX target: the .inc was not recompiled for comparison"; fi

for be in cuda hip; do
    case $be in cuda) LIBN=libnvrtc; BLASN=cuBLAS; U=CUDA ;; hip) LIBN=libhiprtc; BLASN=hipBLAS; U=HIP ;; esac
    echo "== $be, gpu-compare: stub runtime with compiler and GEMM library"
    run_cli X=1 -- gpu-compare --backends $be --seconds 0.1 --yes
    if [ $RC = 0 ] && no_abort; then ok "$be compare: exit 0, no NULL launch"; else bad "$be compare: rc=$RC $(tail -3 <<<"$OUT")"; fi
    [ "$(count gemm_r)" -gt 0 ] && [ "$(count cmp_u32)" -gt 0 ] && [ "$(count blas)" -gt 0 ] && ok "$be compare: own GEMM, library GEMM and the error check all ran" || bad "$be compare: launch counts: $LOG"
    grep -q "vendor library (GFLOPS) *[0-9]" <<<"$OUT" && ! grep -q "WRONG RESULT\|failed" <<<"$OUT" && ok "$be compare: every library row has a speed, none failed or wrong" || bad "$be compare rows: $(grep -E 'library|WRONG|failed' <<<"$OUT" | head -4)"

    if [ $be = cuda ]; then
        [ "$(count ptxload)" -gt 0 ] && [ "$(count rtccompile)" = 0 ] && ok "cuda compare: the built-in PTX was loaded, NVRTC not used" || bad "cuda compare: PTX/NVRTC counts: $LOG"
        echo "== cuda, gpu-compare: NO runtime compiler (libnvrtc), as with ZLUDA: every row must still run"
        run_cli SRS_CUDA_RTC=/nonexistent/libnvrtc.so -- gpu-compare --backends cuda --seconds 0.1 --yes
        if [ $RC = 0 ] && no_abort; then ok "cuda compare, no NVRTC: exit 0, no NULL launch"; else bad "cuda compare, no NVRTC: rc=$RC $(tail -3 <<<"$OUT")"; fi
        for k in gemm_r fma_r copy_u4 cmp_u32 tick blas ptxload; do [ "$(count $k)" -gt 0 ] || bad "cuda compare, no NVRTC: $k never ran: $LOG"; done
        [ "$(count rtccompile)" = 0 ] && ok "cuda compare, no NVRTC: own kernels (all five) and cuBLAS rows ran from the built-in PTX" || bad "rtc used: $LOG"
        [ "$(grep 'not available' <<<"$OUT" | grep -vc 'FP16 matrix multiply, own kernel')" = 0 ] && grep -q "not available *FP16 matrix multiply, own kernel: FP16 is measured through the GEMM library only" <<<"$OUT" \
            && ok "cuda compare, no NVRTC: only the FP16 own-kernel row (never offered) is not available" || bad "cuda compare, no NVRTC rows: $(grep -E 'not available|failed' <<<"$OUT" | head -5)"
        grep -q "own kernels built in (PTX), no NVRTC" <<<"$OUT" && ok "cuda compare, no NVRTC: the software line says the kernels are built in" || bad "software line: $(grep software <<<"$OUT")"
        ! grep -q "WRONG\|failed" <<<"$OUT" && ok "cuda compare, no NVRTC: nothing wrong or failed" || bad "$(grep -E 'WRONG|failed' <<<"$OUT" | head -3)"

        echo "== cuda, gpu-compare: the driver refuses the built-in PTX, NVRTC present -> the same kernels are compiled instead"
        run_cli STUB_PTX_FAIL=1 -- gpu-compare --backends cuda --seconds 0.1 --yes
        if [ $RC = 0 ] && no_abort && [ "$(count rtccompile)" -gt 0 ] && [ "$(count ptxload)" = 0 ] && [ "$(count blas)" -gt 0 ] && ! grep -q "failed" <<<"$OUT"; then ok "cuda compare, PTX refused: NVRTC fallback ran every row"
        else bad "cuda compare, PTX refused: rc=$RC $LOG $(grep -E 'failed|not available' <<<"$OUT" | head -3)"; fi

        echo "== cuda, gpu-compare: the driver refuses the built-in PTX and there is no NVRTC"
        run_cli STUB_PTX_FAIL=1 SRS_CUDA_RTC=/nonexistent/libnvrtc.so -- gpu-compare --backends cuda --seconds 0.1 --yes
        if [ $RC = 0 ] && no_abort && grep -q "the CUDA driver refused the built-in kernels (stub error (error 218)) and libnvrtc is not installed: see Options > NVIDIA CUDA toolkit" <<<"$OUT" && [ "$(count blas)" = 0 ]
        then ok "cuda compare, PTX refused + no NVRTC: not available with both reasons, library not run unchecked"; else bad "cuda compare, PTX refused + no NVRTC: rc=$RC $(grep -E 'not available|failed' <<<"$OUT" | head -3)"; fi

        echo "== cuda, gpu-compare: PTX refused and NVRTC rejects the kernels"
        run_cli STUB_PTX_FAIL=1 STUB_RTC_FAIL=1 -- gpu-compare --backends cuda --seconds 0.1 --yes
        if no_abort && [ $RC = 0 ] && grep -q "vendor library: the kernel did not compile: stub: the test compiler said no (after the built-in kernels were refused" <<<"$OUT" && [ "$(count cmp_u32)" = 0 ]
        then ok "cuda compare, both fail: the row says both reasons, nothing launched"; else bad "cuda compare, both fail: rc=$RC $(grep -E 'library|compile' <<<"$OUT" | head -3) $LOG"; fi

        echo "== cuda, gpu-compare: a kernel the module should hold comes back NULL"
        run_cli STUB_NO_FN=cmp_u32 -- gpu-compare --backends cuda --seconds 0.1 --yes
        if no_abort && [ $RC = 0 ] && grep -q "returned no kernel" <<<"$OUT"; then ok "cuda compare, NULL kernel handle: refused with a message"; else bad "cuda compare, NULL kernel: rc=$RC $(tail -3 <<<"$OUT")"; fi

        echo "== cuda, gpu-burn"
        run_cli X=1 -- gpu-burn --backend cuda --seconds 1 --yes
        if [ $RC = 0 ] && no_abort && grep -q "PASS" <<<"$OUT" && [ "$(count cmp_u32)" -gt 0 ]; then ok "cuda burn FP32: PASS, exit 0"; else bad "cuda burn: rc=$RC $(tail -4 <<<"$OUT")"; fi
        run_cli X=1 -- gpu-burn --backend cuda --fp64 --seconds 1 --yes
        if [ $RC = 0 ] && no_abort && grep -q "PASS" <<<"$OUT"; then ok "cuda burn FP64: PASS, exit 0"; else bad "cuda burn FP64: rc=$RC $(tail -4 <<<"$OUT")"; fi
        run_cli SRS_CUDA_RTC=/nonexistent/libnvrtc.so -- gpu-burn --backend cuda --seconds 1 --yes
        if [ $RC = 0 ] && no_abort && grep -q "PASS" <<<"$OUT" && [ "$(count ptxload)" -gt 0 ]; then ok "cuda burn, no NVRTC: PASS from the built-in PTX"; else bad "cuda burn, no NVRTC: rc=$RC $(tail -4 <<<"$OUT")"; fi
        run_cli SRS_CUDA_RTC=/nonexistent/libnvrtc.so -- gpu-burn --backend cuda --fp64 --seconds 1 --yes
        if [ $RC = 0 ] && no_abort && grep -q "PASS" <<<"$OUT"; then ok "cuda burn FP64, no NVRTC: PASS"; else bad "cuda burn FP64, no NVRTC: rc=$RC $(tail -4 <<<"$OUT")"; fi
        run_cli STUB_PTX_FAIL=1 SRS_CUDA_RTC=/nonexistent/libnvrtc.so -- gpu-burn --backend cuda --seconds 1 --yes
        if no_abort && [ $RC = 1 ] && grep -q "refused the built-in kernels" <<<"$OUT"; then ok "cuda burn, PTX refused + no NVRTC: ended with a clear message, no crash"; else bad "cuda burn, PTX refused + no NVRTC: rc=$RC $(tail -4 <<<"$OUT")"; fi
        continue
    fi

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

echo "== opencl: a device WITHOUT FP64 (Rusticl often): FP64 rows are 'not available' with the reason, never built"
CLENV=(SRS_OPENCL_LIB="$STUB" SRS_CLBLAST_LIB="$STUB" SRS_CUDA_LIB=/nonexistent/libcuda.so SRS_HIP_LIB=/nonexistent/libamdhip64.so)
run_cli "${CLENV[@]}" -- gpu-compare --backends opencl --seconds 0.1 --yes
if [ $RC = 0 ] && no_abort; then ok "opencl compare, no FP64: exit 0"; else bad "opencl compare, no FP64: rc=$RC $(tail -3 <<<"$OUT")"; fi
for r in "FP64 matrix multiply, own kernel" "FP64 matrix multiply, vendor library" "FP64 multiply-add chain"; do
    grep -q "^$r.* not available" <<<"$OUT" || bad "opencl, no FP64: '$r' is not 'not available': $(grep "^$r" <<<"$OUT")"
done
grep -q "this OpenCL driver has no FP64 (double precision)" <<<"$OUT" && ok "opencl, no FP64: own, CLBlast and multiply-add FP64 rows say why" || bad "opencl, no FP64: reason missing"
[ "$(count clbuild64)" = 0 ] && [ "$(count clbuild32)" -gt 0 ] && ok "opencl, no FP64: the FP64 program was never built" || bad "opencl FP64 program built: $LOG"
! grep -q "failed\|WRONG" <<<"$OUT" && grep -q "^FP32 matrix multiply, own kernel (GFLOPS) *[0-9]" <<<"$OUT" && ok "opencl, no FP64: FP32 rows ran, nothing failed" || bad "opencl rows: $(grep -E 'failed|WRONG|FP32' <<<"$OUT" | head -4)"

echo "== opencl: a device WITH FP64: the FP64 rows run"
run_cli "${CLENV[@]}" STUB_CL_FP64=1 -- gpu-compare --backends opencl --seconds 0.1 --yes
if [ $RC = 0 ] && no_abort && [ "$(count clbuild64)" -gt 0 ] && grep -q "^FP64 matrix multiply, own kernel (GFLOPS) *[0-9]" <<<"$OUT" && grep -q "^FP64 matrix multiply, vendor library (GFLOPS) *[0-9]" <<<"$OUT"
then ok "opencl, FP64: own and CLBlast FP64 rows have a speed"; else bad "opencl FP64: rc=$RC $(grep FP64 <<<"$OUT")"; fi

echo "== opencl: the kernel build fails with an EMPTY build log: the reason is never empty"
run_cli "${CLENV[@]}" STUB_CL_BUILD_FAIL=1 -- gpu-compare --backends opencl --seconds 0.1 --yes
if no_abort && [ $RC = 0 ] && grep -q "the OpenCL kernel did not build (OpenCL error -11; the driver gave no build log)" <<<"$OUT" && ! grep -q "did not build: *$" <<<"$OUT"
then ok "opencl, build fails silently: the error code is shown"; else bad "opencl build fail: rc=$RC $(grep -i 'build' <<<"$OUT" | head -3)"; fi

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
