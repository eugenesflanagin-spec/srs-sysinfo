#!/usr/bin/env bash
# tests/bench-compute-test.sh - correctness test for the CPU Compute Benchmark (bench_compute.c).
# Tiny runs only (0.1 s per measurement, at most 2 threads), so it is safe on a desktop.
#
#   tests/bench-compute-test.sh [BINARY]     default: dist/native/sysreport-search (make native)
#   EXTRA_CFLAGS=-m32 tests/bench-compute-test.sh    the same checks on a 32-bit build (no sanitizers)
#
# The engine is checked through a small test program built here that includes bench_compute.c
# directly (so it can reach the kernels and the checks) and links the Makefile's other sources:
#  - the self-test passes on every code path this CPU has
#  - a corrupted result is reported as wrong by every test's check (wrong-but-fast)
#  - a short run on 2 threads: every row measured and right, result file and history line written
#  - each code path forced in turn gives a result and is the one reported
#  - Stop in the middle returns "stopped" with the rest of the rows marked not measured
# The same program is built once more with -fsanitize=address,undefined for the self-test and
# the corruption test. When BINARY knows "compute", a short CLI run is checked too.
# Results go to a throw-away HOME, so ~/.cache/srs and the run history are not touched.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-$SRC/dist/native/sysreport-search}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

cat > "$TMP/harness.c" <<'C'
#include "bench_compute.c"
#include <pthread.h>
static volatile int cancel;
static void *stopper(void *p) { struct timespec d = { 0, (long)(size_t)p }; nanosleep(&d, NULL); cancel = 1; return NULL; }
static int cb_start, cb_done;
static void cb(const cp_result *r, int done, void *ud) { (void)r; (void)ud; if (done) cb_done++; else cb_start++; }

/* spoil the output of a correct round the way a wrong-but-fast kernel would */
static void corrupt(cp_work *w)
{
    size_t i, n = work_out_bytes(w);
    switch (w->test) {
    case CP_SGEMM: case CP_FMA_SP: case CP_FFT_SP: { float *f = w->out[0]; i = n / 4 / 3; f[i] = f[i] * 1.5f + 1; break; }
    case CP_DGEMM: case CP_FMA_DP: case CP_FFT_DP: case CP_NBODY: { double *d = w->out[0]; i = n / 8 / 3; d[i] = d[i] * 1.5 + 1; break; }
    case CP_MANDEL: for (i = 0; i < n; i += 37) ((unsigned char *)w->out[0])[i] ^= 1; break;
    case CP_RAY: for (i = 0; i < n; i += 37) ((unsigned char *)w->out[0])[i] ^= 0x40; break;
    }
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "self";
    char msg[300];
    int t, isa, rc;
    if (!strcmp(mode, "self")) {
        rc = cp_selftest(msg, sizeof msg);
        printf("SELF|%d|%s\nBEST|%s\n", rc, msg, cp_isa_id(cp_isa_best()));
        return 0;
    }
    if (!strcmp(mode, "fault")) {   /* every check must catch a spoilt result, on every path */
        for (t = 0; t < CP_NTEST; t++) {
            cp_ref r;
            if (ref_make(&r, t, 1)) { printf("FAULT|%s|oom\n", cp_test_id[t]); continue; }
            for (isa = CP_ISA_C; isa < CP_NISA; isa++) {
                cp_work w;
                char why[128] = "";
                int good, caught;
                if (!cp_isa_ok(isa) || (!has_vector(t) && isa != CP_ISA_C)) continue;
                if (work_make(&w, &r, isa)) { printf("FAULT|%s|oom\n", cp_test_id[t]); continue; }
                work_run(&w);
                good = work_check(&w, why, sizeof why);
                corrupt(&w);
                caught = !work_check(&w, why, sizeof why);
                printf("FAULT|%s|%s|%d|%d|%s\n", cp_test_id[t], cp_isa_id(isa), good, caught, why);
                work_free(&w);
            }
            ref_free(&r);
        }
        return 0;
    }
    {
        cp_cfg c;
        srs_out o;
        pthread_t th;
        const cp_result *r;
        cp_cfg_default(&c);
        c.threads = 2;
        c.seconds = 0.1;
        c.cb = cb;
        if (!strcmp(mode, "stop")) {
            pthread_create(&th, NULL, stopper, (void *)(size_t)300000000);
            c.seconds = 0.4;
        } else if (!strncmp(mode, "isa=", 4)) {
            for (isa = 0; isa < CP_NISA; isa++) if (!strcmp(mode + 4, cp_isa_id(isa))) c.isa = isa;
            c.single_only = 1;
            c.run_mask = 1u << CP_SGEMM | 1u << CP_DGEMM | 1u << CP_FMA_SP | 1u << CP_FMA_DP | 1u << CP_NBODY | 1u << CP_MANDEL;
        } else if (!strcmp(mode, "small")) c.run_mask = 1u << CP_DGEMM | 1u << CP_MANDEL;
        so_text_init(&o, stdout);
        rc = bench_compute(&c, &o, &cancel);
        if (!strcmp(mode, "stop")) pthread_join(th, NULL);
        r = cp_last();
        for (t = 0; t < CP_NTEST; t++)
            printf("ROW|%s|%d|%.3f|%.3f|%s|%s\n", cp_test_id[t], r->t[t].state, r->t[t].v[0], r->t[t].v[1], cp_isa_id(r->t[t].isa), r->t[t].note);
        printf("RC|%d|%d|%d|%d|%d\n", rc, r->cancelled, r->failed, cb_start, cb_done);
        return 0;
    }
}
C

# the Makefile's sources, without bench_compute.c (the harness includes it)
SECS=$(make -s -C "$SRC" --no-print-directory --eval 'srs-print-secs:; @echo $(SECS)' srs-print-secs)
SECS=$(tr ' ' '\n' <<<"$SECS" | grep -v '^bench_compute\.c$' | tr '\n' ' ')
FLAGS="-std=c11 -Wall -Wextra -Wno-format-truncation -ffp-contract=off -DSRS_HAVE_SECTIONS -D_DEFAULT_SOURCE -pthread -DSRS_NO_MAIN -I$SRC ${EXTRA_CFLAGS:-}"

echo "== build the test programs"
( cd "$SRC" && /usr/bin/gcc $FLAGS -O2 -o "$TMP/harness" "$TMP/harness.c" core.c cli.c plat_posix.c $SECS -lm ) 2>"$TMP/build.log"
if [ -x "$TMP/harness" ]; then
    grep -n "bench_compute" "$TMP/build.log" | grep -q warning && { grep -n "bench_compute" "$TMP/build.log"; bad "compiler warnings in bench_compute.c"; } || ok "built (-O2)"
else
    cat "$TMP/build.log"; bad "test program does not build"; exit 1
fi
[ -z "${EXTRA_CFLAGS:-}" ] && ( cd "$SRC" && /usr/bin/gcc $FLAGS -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$TMP/harness-san" \
    "$TMP/harness.c" core.c cli.c plat_posix.c $SECS -lm ) 2>"$TMP/build-san.log" && ok "built (address + undefined sanitizers)" || \
    { [ -n "${EXTRA_CFLAGS:-}" ] && echo "  note  no sanitizer build with EXTRA_CFLAGS" || { tail -5 "$TMP/build-san.log"; bad "sanitizer build"; }; }

for h in harness harness-san; do
    [ -x "$TMP/$h" ] || continue
    tag=$([ "$h" = harness ] && echo "" || echo " [sanitizers]")
    echo "== self-test$tag"
    out=$("$TMP/$h" self 2>&1); rc=$?
    [ "$rc" = 0 ] && grep -q '^SELF|1|' <<<"$out" && ok "$(grep '^SELF|' <<<"$out" | cut -d'|' -f3)" || { echo "$out" | tail -5; bad "self-test"; }

    echo "== a spoilt result is reported as wrong$tag"
    out=$("$TMP/$h" fault 2>&1); rc=$?
    n=$(grep -c '^FAULT|' <<<"$out")
    [ "$rc" = 0 ] && [ "$n" -ge 9 ] || { echo "$out" | tail -5; bad "fault test did not finish ($n results)"; }
    awk -F'|' '/^FAULT\|/ { if ($4 != 1) { print "  FAIL  " $2 " " $3 ": right result not accepted"; e = 1 }
                            else if ($5 != 1) { print "  FAIL  " $2 " " $3 ": spoilt result not caught"; e = 1 } }
               END { exit e }' <<<"$out" && ok "$n kernel / code-path combinations: right result accepted, spoilt one caught" || fail=1
done

echo "== short run: every test, 1 and 2 threads, 0.1 s each"
t0=$(date +%s.%N)
out=$("$TMP/harness" run)
t1=$(date +%s.%N)
IFS='|' read -r _ rc cancelled failed cbs cbd <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = 0 ] && [ "$cancelled" = 0 ] && [ "$failed" = 0 ] && ok "return 0 in $(printf '%.1f' "$(echo "$t1 - $t0" | bc)") s" || bad "rc=$rc cancelled=$cancelled failed=$failed"
awk -F'|' '/^ROW\|/ { if ($3 != 1 || !($4 > 0) || !($5 > 0)) { print "  FAIL  row " $2 ": state " $3 " values " $4 " / " $5 " " $7; e = 1 }
                      else printf "  ok    %-11s %10.1f %10.1f  %s\n", $2, $4, $5, $6 }
           END { exit e }' <<<"$out" || fail=1
[ "$cbs" = 9 ] && [ "$cbd" = 9 ] && ok "progress callback for every row" || bad "callbacks: $cbs started, $cbd done"
grep -q "WRONG" <<<"$out" && bad "a row says WRONG"
f=$HOME/.cache/srs/bench-compute.txt
[ -s "$f" ] && grep -q "Matrix multiply, double precision" "$f" && grep -q "GFLOPS" "$f" && grep -q "Mpixels/s" "$f" && ok "result file written" || bad "no bench-compute.txt"
grep -q $'\tcompute\t' "$HOME/.cache/srs/bench-history.tsv" 2>/dev/null && ok "history line added" || bad "no history line"
# the two banned words, assembled here so that this file itself never spells them
BANNED="$(printf 'clea%s-room|ai%sa' n d)"
grep -qiE "$BANNED" "$SRC/bench_compute.c" "$SRC/bench_compute.h" "$f" && bad "wording rule: banned word found" || ok "wording"

echo "== each code path forced (1 thread)"
best=$("$TMP/harness" self | grep '^BEST|' | cut -d'|' -f2)
for isa in c sse2 avx2 avx512; do
    out=$("$TMP/harness" "isa=$isa")
    IFS='|' read -r _ rc _ failed _ _ <<<"$(grep '^RC|' <<<"$out")"
    used=$(grep '^ROW|sgemm|' <<<"$out" | cut -d'|' -f6)
    [ "$rc" = 0 ] && [ "$failed" = 0 ] || { bad "$isa: rc=$rc failed=$failed"; continue; }
    if [ "$used" = "$isa" ]; then ok "$isa: $(grep '^ROW|sgemm|' <<<"$out" | cut -d'|' -f4) GFLOPS SGEMM, $(grep '^ROW|mandelbrot|' <<<"$out" | cut -d'|' -f4) Mpixels/s Mandelbrot"
    elif [ "$used" = "$best" ]; then ok "$isa: not on this CPU, fell back to $best"
    else bad "$isa: ran $used"; fi
done

echo "== Stop in the middle"
out=$("$TMP/harness" stop)
IFS='|' read -r _ rc cancelled failed _ _ <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = -2 ] && [ "$cancelled" = 1 ] && ok "return -2 (stopped)" || bad "stop: rc=$rc cancelled=$cancelled"
grep -q "Stopped before the end" <<<"$out" && ok "result says it was stopped" || bad "no Stopped line"
grep -q '^ROW|raytrace|0|' <<<"$out" && ok "rows after the stop are not measured" || bad "last row was measured after Stop"

echo "== short run with the sanitizers (2 tests)"
if [ -x "$TMP/harness-san" ]; then
    out=$("$TMP/harness-san" small 2>&1); rc=$?
    [ "$rc" = 0 ] && grep -q '^RC|0|0|0|' <<<"$out" && ! grep -q "runtime error\|AddressSanitizer" <<<"$out" && ok "clean" || { echo "$out" | grep -m5 "runtime error\|ERROR\|RC|"; bad "sanitizer run"; }
fi

if [ -x "$BIN" ] && "$BIN" compute --help >/dev/null 2>&1; then
    echo "== CLI: $BIN compute"
    out=$("$BIN" compute --threads 2 --seconds 0.1 --tests dgemm,mandelbrot --yes 2>&1); rc=$?
    [ "$rc" = 0 ] && grep -q "GFLOPS" <<<"$out" && ok "CLI run" || bad "CLI run (exit $rc)"
    "$BIN" compute --threads 2 --seconds 0.1 --tests dgemm </dev/null >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "asks first (no --yes, no terminal: not started)" || bad "compute started without --yes (exit $rc)"
    "$BIN" compute --no-such-option --yes >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "unknown option: exit 2, nothing run" || bad "unknown option gave exit $rc"
    "$BIN" compute --tests nosuch --yes >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "unknown --tests value: exit 2" || bad "unknown --tests value gave exit $rc"
else
    echo "== CLI: skipped ($BIN has no compute command yet)"
fi
exit $fail
