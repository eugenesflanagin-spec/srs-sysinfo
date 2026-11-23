#!/usr/bin/env bash
# tests/gpu-burn-json-test.sh - gpu-burn for programs (2.6.0): --json, --json-live, --pci, --watch-kernel and the exit
# codes 0 / 10 / 11 / 12 / 13 / 14 / 15 (only with --json; without it the old 0 / 1 / 2 stay), WITHOUT the real GPU.
#
#   tests/gpu-burn-json-test.sh SYSREPORT-SEARCH      (make test-bench passes the path)
#
# SAFETY: nothing here touches a real GPU.
#   part 1 (Vulkan): Vulkan sees only lavapipe (Mesa's software renderer) on 1 thread with 64 x 64 matrices, and
#           SRS_GPU_SOFTWARE_ONLY=1 refuses anything else;
#   part 2 (HIP):    the HIP runtime is tests/gpu_stub.c (a fake GPU runtime in host memory); Vulkan, CUDA and OpenCL point
#           at files that do not exist. The stub makes the wrong results and the runtime error on request.
# SRS_TEST_SYSFS_ROOT points the GPU sensors at a made-up folder (the temperature-limit case), never at /sys.
# --watch-kernel reads SRS_TEST_KMSG (text files from tests/kmsg_fixtures, or one written here), never /dev/kmsg:
# every --watch-kernel run below sets it, and "run" refuses to start one without it.
# Every run is traced with LD_DEBUG=libs and the test FAILS if a real GPU library was loaded. Throw-away HOME.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH"; exit 2; }
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
TMP=$(mktemp -d)
BG=
cleanup() { [ -n "$BG" ] && kill "$BG" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME" "$TMP/sysroot"
export SRS_TEST_SYSFS_ROOT=$TMP/sysroot DISABLE_MANGOHUD=1
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
REAL='libvulkan_radeon|libvulkan_intel|libvulkan_nouveau|libamdhip64|libcuda\.so|libnvcuda|libOpenCL|libamdocl|libnvidia|libhsa-runtime|libhiprtc|libnvrtc|libRusticl|libpocl|zluda'
n=0
# run TAG ENV... -- ARGS...   sets OUT (standard output), ERR (file), RC; fails when a real GPU library was loaded
run() {
    local tag=$1 extra=(); shift
    while [ "$1" != -- ]; do extra+=("$1"); shift; done
    shift
    n=$((n + 1))
    case " $* " in *" --watch-kernel "*) case " ${extra[*]} " in *" SRS_TEST_KMSG="*) ;; *) bad "$tag: --watch-kernel without SRS_TEST_KMSG (the test would read the real kernel log)"; RC=-1; OUT=; ERR=/dev/null; return ;; esac ;; esac
    ERR=$TMP/err.$n
    OUT=$(env "${BASE[@]}" "${extra[@]}" LD_DEBUG=libs LD_DEBUG_OUTPUT="$TMP/ld.$n" timeout 120 nice -n 15 "$BIN" gpu-burn "$@" 2>"$ERR")
    RC=$?
    if cat "$TMP/ld.$n".* 2>/dev/null | grep "calling init" | grep -v "${STUB:-/nonexistent}" | grep -Eq "$REAL"; then
        bad "$tag: a real GPU library was loaded ($(cat "$TMP/ld.$n".* | grep "calling init" | grep -Eo "[^ ]*($REAL)[^ ]*" | head -1))"
    fi
}
# js EXPR   a Python expression over d (the last line of OUT as JSON); true = ok. The expressions are this test's own text.
js() { python3 -I -c 'import json,sys
d = json.loads(sys.argv[1].strip().splitlines()[-1])
sys.exit(0 if eval(sys.argv[2]) else 1)' "$OUT" "$1" 2>/dev/null; }
want() {   # want RC TEXT [PYTHON-EXPR]
    if [ "$RC" != "$1" ]; then bad "$2: exit $RC (want $1)"; sed 's/^/        /' "$ERR" | tail -4; return; fi
    if [ -n "${3:-}" ] && ! js "$3"; then bad "$2: exit $1, but the JSON is not as expected ($3)"; printf '        %s\n' "$OUT" | tail -2; return; fi
    ok "$2"
}

echo "== guard: the test seams are set before anything runs"
[ -n "${SRS_TEST_SYSFS_ROOT:-}" ] && [ "${HOME#"$TMP"}" != "$HOME" ] && ok "made-up sysfs root and throw-away HOME" || { bad "test seams missing"; exit 1; }

# ---------------------------------------------------------------- part 1: Vulkan on lavapipe
LVP=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
if [ -f "$LVP" ]; then
    BASE=(VK_DRIVER_FILES=$LVP VK_ICD_FILENAMES=$LVP VK_LOADER_LAYERS_DISABLE='~implicit~' LP_NUM_THREADS=1 SRS_GPU_SOFTWARE_ONLY=1 SRS_TEST_GB_NMAX=64
          SRS_CUDA_LIB=/nonexistent/libcuda.so SRS_CUDA_RTC=/nonexistent/libnvrtc.so SRS_HIP_LIB=/nonexistent/libamdhip64.so SRS_HIP_BLAS=/nonexistent
          SRS_OPENCL_LIB=/nonexistent/libOpenCL.so SRS_CLBLAST_LIB=/nonexistent/libclblast.so)
    echo "== lavapipe: --json, a 2 s run"
    run pass -- --backend vulkan --seconds 2 --yes --json
    want 0 "exit 0 and one JSON object: PASS" 'd["tool"] == "srs gpu-burn" and d["result"] == "pass" and d["exit"] == 0 and d["errors"] == 0 and d["end"] == "completed" and d["reason"] == ""'
    [ "$(printf '%s\n' "$OUT" | grep -c .)" = 1 ] && ok "standard output is the JSON alone (1 line)" || bad "standard output has $(printf '%s\n' "$OUT" | grep -c .) lines"
    js 'd["backend"] == "vulkan" and d["iterations"] > 0 and d["planned_seconds"] == 2 and 1.5 < d["seconds"] < 20 and d["precision"] == "fp32"' && ok "back end, multiplications, seconds, precision" || bad "numbers: $OUT"
    js '("llvmpipe" in d["device"] or "lavapipe" in d["device"].lower()) and len(d["devices"]) == 1 and d["devices"][0]["state"] == "done"' && ok "the software renderer is named; one device, state done" || bad "device: $OUT"
    js 'set(d["max"]) == {"edge_c", "junction_c", "power_w", "clock_mhz"} and d["max"]["edge_c"] is None' && ok "sensors a software renderer does not have are null" || bad "max: $OUT"
    js '"version" in d and d["gflops_avg"] >= 0 and d["limit_c"] == 100' && ok "version, average speed, limit" || bad "fields: $OUT"
    grep -q "PASS" "$XDG_CACHE_HOME/srs/bench-gpuburn.txt" 2>/dev/null && ok "the report is still saved (bench-gpuburn.txt says PASS)" || bad "no saved report"

    echo "== lavapipe: --json-live"
    run live -- --backend vulkan --seconds 3 --yes --json-live
    want 0 "exit 0, the last line is the result" 'd["result"] == "pass"'
    python3 -I -c 'import json,sys
L = [json.loads(l) for l in sys.argv[1].strip().splitlines()]
S = [x for x in L[:-1]]
assert len(S) >= 3, len(S)
assert all(x.get("event") == "sample" for x in S)
assert all(set(x) >= {"t_s", "planned_seconds", "iterations", "errors", "gflops", "edge_c", "junction_c", "power_w", "clock_mhz"} for x in S)
T = [x["t_s"] for x in S]
assert T == sorted(T) and all(b - a >= 0.4 for a, b in zip(T, T[1:])), T
assert "event" not in L[-1]' "$OUT" 2>"$TMP/py.err" && ok "one sample line about every half second, each one JSON, in time order" || { bad "live lines"; cat "$TMP/py.err" | tail -2; }

    echo "== lavapipe: stopped by a signal (SIGTERM, as a program ends it)"
    env "${BASE[@]}" "$BIN" gpu-burn --backend vulkan --seconds 60 --yes --json >"$TMP/term.out" 2>"$TMP/term.err" &
    BG=$!
    sleep 2
    kill -TERM $BG
    for i in $(seq 1 80); do kill -0 $BG 2>/dev/null || break; sleep 0.1; done
    if kill -0 $BG 2>/dev/null; then bad "still running 8 s after SIGTERM"; kill -KILL $BG
    else
        wait $BG; RC=$?; OUT=$(cat "$TMP/term.out"); ERR=$TMP/term.err
        want 10 "exit 10 and JSON: stopped" 'd["result"] == "stopped" and d["exit"] == 10 and d["end"] == "stopped" and d["errors"] == 0'
    fi
    BG=

    echo "== lavapipe: could not start"
    run nodev VK_DRIVER_FILES=/nonexistent.json VK_ICD_FILENAMES=/nonexistent.json -- --backend vulkan --seconds 2 --yes --json
    want 12 "no Vulkan device: exit 12 and JSON with the reason" 'd["result"] == "not-started" and d["exit"] == 12 and d["end"] == "not-started" and "Vulkan" in d["reason"] and d["devices"] == [] and d["device"] is None'
    run nopci -- --backend vulkan --pci 0000:7f:1f.0 --seconds 2 --yes --json
    want 12 "--pci with an address no device has: exit 12" 'd["result"] == "not-started" and "0000:7f:1f.0" in d["reason"] and d["pci"] == "0000:7f:1f.0"'
    run nobackend -- --backend hip --seconds 2 --yes --json
    want 12 "a back end that is not installed: exit 12" 'd["result"] == "not-started" and d["backend"] == "hip"'

    echo "== lavapipe: without --json the exit codes are the old ones"
    run old0 -- --backend vulkan --seconds 1 --yes
    [ "$RC" = 0 ] && grep -q "PASS" <<<"$OUT" && ok "PASS: exit 0 and the report on standard output" || bad "plain run: exit $RC"
    run old2 VK_DRIVER_FILES=/nonexistent.json VK_ICD_FILENAMES=/nonexistent.json -- --backend vulkan --seconds 1 --yes
    [ "$RC" = 2 ] && [ -z "$OUT" ] && ok "no Vulkan device: exit 2, no JSON" || bad "plain no-device run: exit $RC, output '$OUT'"

    echo "== command line"
    run u1 -- --json --seconds 1;                         [ "$RC" = 2 ] && grep -q -- "--yes" "$ERR" && ok "--json without --yes: exit 2, says why" || bad "--json without --yes: exit $RC"
    run u2 -- --pci 03:00.0 --yes --json;                 [ "$RC" = 2 ] && grep -q "PCI address" "$ERR" && ok "--pci with a short address: exit 2" || bad "--pci short: exit $RC"
    run u3 -- --pci 0000:03:00.0 --device 0 --yes --json; [ "$RC" = 2 ] && ok "--pci with --device: exit 2" || bad "--pci + --device: exit $RC"
    run u4 -- --json --list;                              [ "$RC" = 2 ] && ok "--json with --list: exit 2" || bad "--json --list: exit $RC"
    run u5 -- --help;                                     [ "$RC" = 0 ] && grep -q -- "--json-live" <<<"$OUT" && grep -q -- "--watch-kernel" <<<"$OUT" && grep -q "15 ended early at the temperature limit" <<<"$OUT" && ok "--help lists the new options and exit codes" || bad "--help"

    # ------------------------------------------------------------ --watch-kernel (lavapipe has no PCI address: every GPU line counts)
    FIX=$SRC/tests/kmsg_fixtures
    KW=(SRS_TEST_KWATCH_TAIL_S=0.3)
    echo "== kernel watch: a clean log"
    run kclean "${KW[@]}" SRS_TEST_KMSG=$FIX/clean.txt -- --backend vulkan --seconds 1 --yes --json --watch-kernel
    want 0 "exit 0; the watch was on and saw nothing" 'd["result"] == "pass" and d["kernel_watch"] == "on" and d["kernel_events"] == [] and d["kernel_other"] == []'
    run koff -- --backend vulkan --seconds 1 --yes --json
    want 0 "without --watch-kernel the JSON says the watch was off" 'd["kernel_watch"] == "off" and "kernel_events" not in d'

    echo "== kernel watch: an amdgpu ring timeout and GPU reset"
    run kring "${KW[@]}" SRS_TEST_KMSG=$FIX/amdgpu-ring-timeout.txt -- --backend vulkan --seconds 1 --yes --json --watch-kernel
    want 14 "exit 14 although the test's own numbers were right" 'd["exit"] == 14 and d["result"] == "fail" and d["errors"] == 0 and d["end"] == "completed" and d["reason"].startswith("kernel log: ")'
    js '[e["kind"] for e in d["kernel_events"]] == ["ring-timeout", "gpu-reset", "gpu-reset-done", "parser-error"]' && ok "events: ring timeout, one GPU reset (three lines), reset done, parser error" || bad "events: $OUT"
    js 'd["kernel_events"][0]["process"] == "kwin_wayland" and d["kernel_events"][0]["detail"] == "gfx_0.0.0" and d["kernel_events"][0]["pci"] == "0000:03:00.0"' && ok "the ring, the card and the program the driver blames (kwin_wayland)" || bad "ring timeout details: $OUT"
    js 'd["kernel_events"][1]["vram_lost"] is True and d["kernel_events"][1]["detail"] == "MODE1" and d["kernel_events"][2]["vram_lost"] is False' && ok "the reset: MODE1, video memory lost" || bad "reset details: $OUT"
    js '"ring timeout (gfx_0.0.0, in kwin_wayland)" in d["reason"] and "GPU reset (MODE1, video memory wiped)" in d["reason"]' && ok "the reason is in plain words" || bad "reason: $OUT"
    grep -q "FAIL - kernel log:" "$ERR" && ok "and it is said on standard error" || bad "nothing on standard error"
    run kring-plain "${KW[@]}" SRS_TEST_KMSG=$FIX/amdgpu-ring-timeout.txt -- --backend vulkan --seconds 1 --yes --watch-kernel
    [ "$RC" = 1 ] && grep -q "FAIL - kernel log:" "$ERR" && ok "the same without --json: exit 1" || bad "plain kernel-watch run: exit $RC"

    echo "== kernel watch: the older amdgpu wording, NVIDIA Xid, an unknown driver error"
    run kold "${KW[@]}" SRS_TEST_KMSG=$FIX/old-format.txt -- --backend vulkan --seconds 1 --yes --json --watch-kernel
    want 14 "a ring timeout line with no PCI address (older kernels, dmesg form): exit 14" '[e["kind"] for e in d["kernel_events"]] == ["ring-timeout", "gpu-reset", "gpu-reset-done"] and d["kernel_events"][0]["pci"] is None and d["kernel_events"][0]["process"] == "Xwayland"'
    run kxid "${KW[@]}" SRS_TEST_KMSG=$FIX/nvidia-xid.txt -- --backend vulkan --seconds 1 --yes --json --watch-kernel
    want 14 "NVIDIA Xid: exit 14" '[e["kind"] for e in d["kernel_events"]] == ["nvidia-xid"] and d["kernel_events"][0]["detail"] == "Xid 79" and d["kernel_events"][0]["process"] == "game.exe" and d["kernel_events"][0]["pci"] == "0000:01:00"'
    run kunk "${KW[@]}" SRS_TEST_KMSG=$FIX/unknown-error.txt -- --backend vulkan --seconds 1 --yes --json --watch-kernel
    want 0 "a driver error line SRS does not know: shown, not counted (exit 0)" 'd["kernel_events"] == [] and len(d["kernel_other"]) == 1 and "never seen" in d["kernel_other"][0]'

    echo "== kernel watch: a reset that appears while the test runs, and one just after it"
    cp "$FIX/clean.txt" "$TMP/kmsg.live"
    (sleep 1.5; cat "$FIX/amdgpu-ring-timeout.txt" >>"$TMP/kmsg.live") &
    run kmid "${KW[@]}" SRS_TEST_KMSG=$TMP/kmsg.live -- --backend vulkan --seconds 3 --yes --json --watch-kernel
    wait
    want 14 "lines written during the run are seen (exit 14), with their time" 'd["exit"] == 14 and 1.0 < d["kernel_events"][0]["t_s"] < 3.5'
    cp "$FIX/clean.txt" "$TMP/kmsg.tail"
    (sleep 3.2; cat "$FIX/amdgpu-ring-timeout.txt" >>"$TMP/kmsg.tail") &
    run ktail SRS_TEST_KWATCH_TAIL_S=4 SRS_TEST_KMSG=$TMP/kmsg.tail -- --backend vulkan --seconds 1 --yes --json --watch-kernel
    wait
    want 14 "a reset in the seconds after the load ended still fails the run" 'd["exit"] == 14 and d["kernel_events"][0]["t_s"] > d["seconds"]'

    echo "== kernel watch: a kernel log that cannot be read"
    run knolog "${KW[@]}" SRS_TEST_KMSG=$TMP/does-not-exist -- --backend vulkan --seconds 1 --yes --json --watch-kernel
    want 0 "exit 0, and the JSON says the log was not watched" 'd["kernel_watch"] == "unreadable" and "own result is checked" in d["kernel_watch_note"] and d["kernel_events"] == []'
    grep -q "Kernel log not readable" "$ERR" && ok "said plainly on standard error" || bad "unreadable log not mentioned"
else echo "  skip  no lavapipe ($LVP): the Vulkan part"; fi

# ---------------------------------------------------------------- part 2: the fake HIP runtime
echo "== build the fake GPU runtime"
STUB=$TMP/libgpustub.so
if gcc -O2 -Wall -Wextra -shared -fPIC -fvisibility=hidden -o "$STUB" "$SRC/tests/gpu_stub.c" -lm 2>"$TMP/stub.log" && [ ! -s "$TMP/stub.log" ]; then ok "gpu_stub.c built, no warnings"
else cat "$TMP/stub.log"; bad "gpu_stub.c does not build cleanly"; exit 1; fi
BASE=(-u SRS_GPU_SOFTWARE_ONLY SRS_HIP_LIB=$STUB SRS_HIP_RTC=$STUB SRS_HIP_BLAS=$STUB SRS_CUDA_LIB=/nonexistent/libcuda.so SRS_CUDA_RTC=/nonexistent/libnvrtc.so
      SRS_OPENCL_LIB=/nonexistent/libOpenCL.so SRS_CLBLAST_LIB=/nonexistent/libclblast.so VK_DRIVER_FILES=/nonexistent VK_ICD_FILENAMES=/nonexistent
      VK_LOADER_DRIVERS_DISABLE='*')
SPCI=ffff:ff:1f.7   # the address the fake runtime gives its device

echo "== fake HIP: a clean run, the GPU chosen by --pci"
run hip-pass -- --backend hip --pci FFFF:FF:1F.7 --seconds 1 --yes --json
want 0 "exit 0; --pci (upper case accepted) finds the device" 'd["result"] == "pass" and d["backend"] == "hip" and d["pci"] == "'$SPCI'" and d["errors"] == 0'

echo "== fake HIP: wrong results"
run hip-wrong STUB_CMP_WRONG_AFTER=6 -- --backend hip --seconds 1 --yes --json
want 11 "exit 11 and JSON: FAIL with the error count" 'd["result"] == "fail" and d["exit"] == 11 and d["end"] == "compute-errors" and d["errors"] > 0 and d["devices"][0]["state"] == "failed"'
run hip-wrong-plain STUB_CMP_WRONG_AFTER=6 -- --backend hip --seconds 1 --yes
[ "$RC" = 1 ] && grep -q "FAIL" <<<"$OUT" && ok "the same without --json: exit 1 and FAIL in the report" || bad "plain wrong-results run: exit $RC"

echo "== fake HIP: the runtime breaks in the middle of the run"
run hip-err STUB_LAUNCH_ERR_AFTER=40 -- --backend hip --seconds 5 --yes --json
want 13 "exit 13 and JSON: ended early, runtime error" 'd["result"] == "ended-early" and d["exit"] == 13 and d["end"] == "runtime-error" and d["reason"] != "" and d["seconds"] < 4.5'
run hip-err-plain STUB_LAUNCH_ERR_AFTER=40 -- --backend hip --seconds 5 --yes
[ "$RC" = 1 ] && ok "the same without --json: exit 1" || bad "plain runtime-error run: exit $RC"

echo "== fake HIP: the temperature limit (made-up sensors)"
HW=$TMP/sysroot/sys/bus/pci/devices/$SPCI/hwmon/hwmon0
mkdir -p "$HW"
printf 'amdgpu\n' >"$HW/name"; printf 'edge\n' >"$HW/temp1_label"; printf 'junction\n' >"$HW/temp2_label"
printf '61000\n' >"$HW/temp1_input"; printf '72000\n' >"$HW/temp2_input"; printf '250000000\n' >"$HW/power1_average"; printf '3100000000\n' >"$HW/freq1_input"
run hip-cool -- --backend hip --seconds 1 --yes --json
want 0 "72 C under the limit: exit 0, the sensors are in the JSON" 'd["result"] == "pass" and d["max"] == {"edge_c": 61, "junction_c": 72, "power_w": 250, "clock_mhz": 3100} and d["devices"][0]["temperature_limit_active"]'
printf '104000\n' >"$HW/temp2_input"
run hip-hot -- --backend hip --seconds 5 --yes --json
want 15 "104 C at a 100 C limit: exit 15, not a stability verdict" 'd["result"] == "ended-early" and d["exit"] == 15 and d["end"] == "temperature-limit" and "104" in d["reason"] and d["errors"] == 0 and d["seconds"] < 4.5'
run hip-hot-plain -- --backend hip --seconds 5 --yes
[ "$RC" = 1 ] && ok "the same without --json: exit 1" || bad "plain temperature run: exit $RC"
rm -rf "$TMP/sysroot/sys"

echo "== kernel watch with a known card (fake HIP, PCI $SPCI)"
FIX=$SRC/tests/kmsg_fixtures
KW=(SRS_TEST_KWATCH_TAIL_S=0.3)
sed "s/0000:03:00\\.0/$SPCI/g" "$FIX/amdgpu-ring-timeout.txt" >"$TMP/kmsg.mine"
run hk-other "${KW[@]}" SRS_TEST_KMSG=$FIX/amdgpu-ring-timeout.txt -- --backend hip --seconds 1 --yes --json --watch-kernel
want 0 "a reset of ANOTHER card (0000:03:00.0) does not fail this run" 'd["exit"] == 0 and d["kernel_events"] == [] and d["kernel_events_other_cards"] == 4 and d["pci"] == "'$SPCI'"'
run hk-mine "${KW[@]}" SRS_TEST_KMSG=$TMP/kmsg.mine -- --backend hip --seconds 1 --yes --json --watch-kernel
want 14 "the same lines for THIS card: exit 14" 'd["exit"] == 14 and len(d["kernel_events"]) == 4 and d["kernel_events_other_cards"] == 0'
run hk-xid "${KW[@]}" SRS_TEST_KMSG=$FIX/nvidia-xid.txt -- --backend hip --seconds 1 --yes --json --watch-kernel
want 0 "an NVIDIA Xid of another card does not fail this run" 'd["exit"] == 0 and d["kernel_events_other_cards"] == 1'
run hk-old "${KW[@]}" SRS_TEST_KMSG=$FIX/old-format.txt -- --backend hip --seconds 1 --yes --json --watch-kernel
want 14 "a ring timeout that names no card cannot be ruled out: exit 14" 'd["exit"] == 14 and d["kernel_events"][0]["kind"] == "ring-timeout" and d["kernel_events_other_cards"] == 2'
run hk-wrong "${KW[@]}" STUB_CMP_WRONG_AFTER=6 SRS_TEST_KMSG=$TMP/kmsg.mine -- --backend hip --seconds 1 --yes --json --watch-kernel
want 11 "wrong results AND a reset: exit 11 stays, the kernel events are listed" 'd["exit"] == 11 and d["errors"] > 0 and len(d["kernel_events"]) == 4'
run hk-err "${KW[@]}" STUB_LAUNCH_ERR_AFTER=40 SRS_TEST_KMSG=$TMP/kmsg.mine -- --backend hip --seconds 5 --yes --json --watch-kernel
want 14 "a runtime error AND a reset: exit 14 (the reset explains it)" 'd["exit"] == 14 and d["end"] == "runtime-error" and d["reason"].startswith("kernel log: ")'

echo "== fake HIP: --json-live with wrong results"
run hip-live STUB_CMP_WRONG_AFTER=6 -- --backend hip --seconds 2 --yes --json-live
want 11 "exit 11; the last line is the result" 'd["result"] == "fail"'
python3 -I -c 'import json,sys
L = [json.loads(l) for l in sys.argv[1].strip().splitlines()]
assert len(L) >= 3 and any(x.get("event") == "sample" and x["errors"] > 0 for x in L[:-1])' "$OUT" 2>/dev/null && ok "the live lines show the errors as they are found" || bad "live lines with errors"

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
