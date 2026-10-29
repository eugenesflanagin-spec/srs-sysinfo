#!/usr/bin/env bash
# tests/bench-work-test.sh - correctness test for the Everyday Work Benchmark (bench_work.c).
# Tiny runs only (64 KB test data, 0.1 s per measurement, 2 threads; a few seconds in all).
#
#   tests/bench-work-test.sh [BINARY]     default: dist/native/sysreport-search (make native)
#
# The engine is checked through a small test program built here with AddressSanitizer and
# UndefinedBehaviorSanitizer (bench_work.c + the Makefile's sources, -DSRS_NO_MAIN): the
# self-test, every cell measured and checked, the four compression libraries, a run with two
# libraries turned off ("not installed" rows), Stop in the middle, the result file and the
# history lines. When BINARY knows "work" a short CLI run is checked too.
# Results go to a throw-away HOME, so ~/.cache/srs and the run history are not touched.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-$SRC/dist/native/sysreport-search}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
mkdir -p "$HOME"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

echo "== build the test program (ASan + UBSan)"
cat > "$TMP/harness.c" <<'C'
#define _GNU_SOURCE
#include "bench_work.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile int cancel;
static int starts, dones;
static void cb(const wk_result *r, int done, void *ud) { (void)r; (void)ud; if (done) dones++; else starts++; }
static void *stopper(void *p) { struct timespec d = { 0, (long)(size_t)p }; nanosleep(&d, NULL); cancel = 1; return NULL; }
static const char *st(int s) { return s == WK_ST_OK ? "ok" : s == WK_ST_FAIL ? "fail" : s == WK_ST_SKIP ? "skip" : "notrun"; }
int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "run";
    wk_cfg c;
    srs_out o;
    pthread_t t;
    const wk_result *r;
    char msg[256];
    int rc, i;
    if (!strcmp(mode, "self")) { rc = wk_selftest(msg, sizeof msg); printf("SELF|%d|%s\n", rc, msg); return 0; }
    wk_cfg_default(&c);
    c.part_kib = 64;
    c.seconds = 0.1;
    c.threads = 2;
    c.cb = cb;
    if (!strcmp(mode, "stop")) {
        c.seconds = 0.3;
        c.run_mask = 1u << WK_SORT | 1u << WK_HASH | 1u << WK_TEXT | 1u << WK_INTERP;
        pthread_create(&t, NULL, stopper, (void *)(size_t)400000000);
    }
    printf("EST|%.1f\n", wk_estimate_seconds(&c));
    so_text_init(&o, stdout);
    rc = bench_work(&c, &o, &cancel);
    if (!strcmp(mode, "stop")) pthread_join(t, NULL);
    r = wk_last();
    if (!r) { printf("RC|%d|nores\n", rc); return 0; }
    for (i = 0; i < WK_NCODEC; i++)
        printf("CODEC|%s|%s|%s|%.1f|%.1f|%.1f|%.1f|%.2f|%.2f|%.2f|%.2f|%s|%s\n", wk_codec_name[i], st(r->comp[i].state), st(r->dec[i].state),
               r->comp[i].v[0], r->comp[i].v[1], r->dec[i].v[0], r->dec[i].v[1], r->ratio[i][0], r->ratio[i][1], r->ratio[i][2], r->ratio[i][3],
               r->comp[i].note, r->dec[i].note);
    for (i = 0; i < WT_N; i++) printf("TEST|%s|%s|%.3f|%.3f|%d|%s\n", wk_test_name[i], st(r->t[i].state), r->t[i].v[0], r->t[i].v[1], r->t[i].threads, r->t[i].note);
    for (i = 0; i < WK_NLIB; i++) printf("LIB|%s|%s|%s\n", wk_lib_id[i], r->libver[i], r->libwhy[i]);
    printf("RC|%d|%d|%d|%d|%d|%d\n", rc, r->cancelled, r->failed, r->self_ok, starts, dones);
    printf("STAGE|%s|\n", wk_stage());
    return 0;
}
C
SECS=$(make -s -C "$SRC" --no-print-directory --eval 'srs-print-secs:; @echo $(SECS)' srs-print-secs)
case " $SECS " in *" bench_work.c "*) ;; *) SECS="$SECS bench_work.c" ;; esac
( cd "$SRC" && /usr/bin/gcc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DSRS_HAVE_SECTIONS -D_DEFAULT_SOURCE -pthread -DSRS_NO_MAIN -I"$SRC" -o "$TMP/harness" "$TMP/harness.c" core.c cli.c plat_posix.c $SECS -lm -ldl ) 2>"$TMP/build.log"
if [ -x "$TMP/harness" ]; then
    grep -n "bench_work" "$TMP/build.log" && bad "compiler warnings in bench_work.c" || ok "built"
else
    cat "$TMP/build.log"; bad "test program does not build"; exit 1
fi
( cd "$SRC" && /usr/bin/gcc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -DSRS_HAVE_SECTIONS -pthread -c bench_work.c -o "$TMP/bw.o" ) 2>"$TMP/strict.log"
[ -s "$TMP/strict.log" ] && { cat "$TMP/strict.log"; bad "warnings with -std=c11 -Wall -Wextra -O2"; } || ok "no warnings with -std=c11 -Wall -Wextra -O2"

echo "== self-test"
out=$("$TMP/harness" self 2>&1)
grep -q '^SELF|1|' <<<"$out" && ok "$(grep '^SELF' <<<"$out" | cut -d'|' -f3)" || { echo "$out"; bad "self-test"; }

echo "== short run (64 KB per data type, 0.1 s, 2 threads)"
t0=$(date +%s.%N)
out=$("$TMP/harness" 2>&1); hrc=$?
t1=$(date +%s.%N)
[ "$hrc" = 0 ] || { echo "$out" | tail -30; bad "test program exited $hrc (sanitizer report above?)"; }
grep -q 'runtime error\|AddressSanitizer\|LeakSanitizer' <<<"$out" && { grep -m5 'runtime error\|Sanitizer' <<<"$out"; bad "sanitizer report"; } || ok "no sanitizer reports"
IFS='|' read -r _ rc cancelled failed selfok starts dones <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = 0 ] && [ "$cancelled" = 0 ] && [ "$failed" = 0 ] && [ "$selfok" = 1 ] && ok "return 0, nothing failed, in $(printf '%.1f' "$(echo "$t1 - $t0" | bc)") s (estimate $(grep '^EST|' <<<"$out" | cut -d'|' -f2) s)" \
    || bad "rc=$rc cancelled=$cancelled failed=$failed self=$selfok"
# every measured cell (codec compress / decompress, and each test that was not skipped) starts once and reports twice
ncell=$(( $(grep -c '^CODEC|[^|]*|ok|' <<<"$out") + $(grep -c '^CODEC|[^|]*|[^|]*|ok|' <<<"$out") + $(grep -c '^TEST|[^|]*|ok|' <<<"$out") ))
[ "$ncell" -gt 0 ] && [ "$starts" -ge "$ncell" ] && [ "$dones" -ge $((ncell * 2)) ] && ok "progress callback: $starts starts, $dones results for $ncell measured cells" || bad "callback: $starts starts, $dones results for $ncell cells"
# the libraries are optional at run time: zlib is on practically every system, a missing other one only skips its rows
nlib=$(grep -c '^LIB|[a-z0-9]*|[a-z]' <<<"$out")
grep -q '^LIB|zlib|[a-z]' <<<"$out" && ok "$nlib of 4 libraries loaded: $(grep '^LIB|' <<<"$out" | cut -d'|' -f3 | paste -sd';')" || bad "zlib did not load"
[ "$nlib" = 4 ] || echo "  note  not installed here: $(grep '^LIB|[a-z0-9]*||' <<<"$out" | cut -d'|' -f2 | paste -sd' ') (their rows are skipped)"
awk -F'|' '/^CODEC\|/ && $3 == "skip" && $4 == "skip" && $13 ~ /not installed/ { print "  note  skipped: " $2 " (library not installed)"; next }
           /^CODEC\|/ { if ($3 != "ok" || $4 != "ok" || !($5 > 0 && $6 > 0 && $7 > 0 && $8 > 0)) { print "  FAIL  " $0; e = 1 }
                        else if (!($11 < 1.05 && $9 > 1.5 && $10 > 1.5 && $12 > 1)) { print "  FAIL  ratio: " $0; e = 1 } }
           END { exit e }' <<<"$out" && ok "codecs: compress and decompress measured and round-trip checked; text and records shrink, random data does not" || fail=1
awk -F'|' '/^TEST\|/ { if ($3 == "ok") { if (!($4 > 0 && $5 > 0 && $6 == 2)) { print "  FAIL  " $0; e = 1 } }
                       else if ($3 == "skip") print "  note  skipped: " $2 " (" $7 ")"; else { print "  FAIL  " $0; e = 1 } }
           END { exit e }' <<<"$out" && ok "$(grep -c '^TEST|[^|]*|ok' <<<"$out") of 16 other tests measured on 1 and 2 threads and checked" || fail=1
grep -q '^STAGE||' <<<"$out" && ok "stage cleared at the end" || bad "stage not cleared"
F=$HOME/.cache/srs/bench-work.txt
[ -s "$F" ] && grep -q "zlib -9 decompress" "$F" && grep -q "How much smaller" "$F" && grep -q "every result was checked" "$F" && ok "result file written" || bad "bench-work.txt missing or incomplete"
# the two banned words, assembled here so that this file itself never spells them
BANNED="$(printf 'clea%s-room|ai%sa' n d)"
grep -qiE "$BANNED" "$F" && bad "forbidden wording in the result" || ok "wording"
nh=$(grep -c $'\twork-' "$HOME/.cache/srs/bench-history.tsv" 2>/dev/null)
[ "$nh" = 5 ] && ok "5 history lines (work-compress ... work-interp)" || bad "$nh history lines, want 5"

echo "== xz and lz4 turned off (as if not installed)"
out=$(SRS_WORK_NOLIB=xz,lz4 "$TMP/harness" 2>&1)
IFS='|' read -r _ rc cancelled failed _ <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = 0 ] && [ "$failed" = 0 ] && ok "return 0" || bad "rc=$rc failed=$failed"
grep -q '^CODEC|xz -6|skip|skip|.*not installed' <<<"$out" && grep -q '^CODEC|lz4|skip|skip|' <<<"$out" && grep -q '^CODEC|zlib -6|ok|ok|' <<<"$out" \
    && ok "xz and lz4 rows skipped, the others measured" || bad "skipped rows wrong"
grep -q "lz4 compress.*not installed" "$F" && grep -q "Library: xz.*not installed" "$F" && ok "result says not installed" || bad "no 'not installed' in the result"

echo "== Stop in the middle"
out=$("$TMP/harness" stop 2>&1)
IFS='|' read -r _ rc cancelled failed _ <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = -2 ] && [ "$cancelled" = 1 ] && [ "$failed" = 0 ] && ok "return -2 (stopped)" || bad "stop: rc=$rc cancelled=$cancelled failed=$failed"
grep -q "Stopped before the end" <<<"$out" && ok "result says it was stopped" || bad "no Stopped line"
grep -q 'runtime error\|Sanitizer' <<<"$out" && bad "sanitizer report after Stop" || ok "no sanitizer reports"

if [ -x "$BIN" ] && "$BIN" work --help >/dev/null 2>&1; then
    echo "== CLI: $BIN work"
    out=$("$BIN" work --part-kib 64 --seconds 0.1 --threads 2 --yes 2>&1); rc=$?
    [ "$rc" = 0 ] && grep -q "every result was checked" <<<"$out" && ok "CLI run" || bad "CLI run (exit $rc)"
    "$BIN" work --part-kib 64 --seconds 0.1 --threads 2 </dev/null >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "asks first (no --yes, no terminal: not started)" || bad "work started without --yes (exit $rc)"
    "$BIN" work --no-such-option --yes >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "unknown option: exit 2, nothing run" || bad "unknown option gave exit $rc"
    "$BIN" work --libs nosuch --yes >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "unknown --libs value: exit 2" || bad "unknown --libs value gave exit $rc"
else
    echo "== CLI: skipped ($BIN has no work command yet)"
fi
exit $fail
