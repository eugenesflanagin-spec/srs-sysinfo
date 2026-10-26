#!/usr/bin/env bash
# tests/bench-os-test.sh - correctness test for the OS latency benchmark (bench_os.c).
# Tiny runs only (about 1-2 s in all), so it is safe on a desktop.
#
#   tests/bench-os-test.sh [BINARY]     default: dist/native/sysreport-search (make native)
#
# When BINARY knows "os-latency" a short CLI run is checked too. The engine itself is always
# checked through a small test program built here (bench_os.c + the Makefile's sources, with
# -DSRS_NO_MAIN): sane statistics in every row (best <= median <= 95% <= 99% <= worst), the
# result file and history line, no child process left, no temporary folder left - also after
# a run that is stopped in the middle of the context-switch test and in the middle of the file test.
# Results go to a throw-away HOME, so ~/.cache/srs and the run history are not touched.
#
#   SANITIZE=1 tests/bench-os-test.sh   builds the test program with -fsanitize=address,undefined
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
leftovers() { find "$HOME/.cache/srs" -maxdepth 1 -name 'srs-os-*' 2>/dev/null | wc -l; }

echo "== build the test program"
cat > "$TMP/harness.c" <<'C'
#define _GNU_SOURCE
#include "bench_os.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
static volatile int cancel;
static void *stopper(void *p) { struct timespec d = { 0, (long)(size_t)p }; nanosleep(&d, NULL); cancel = 1; return NULL; }
static int live;
static void cb(const os_row *r, int i, void *ud) { (void)r; (void)i; (void)ud; live++; }
int main(int argc, char **argv)
{
    os_cfg c;
    static os_result r;
    srs_out o;
    pthread_t t;
    int rc, i;
    os_cfg_default(&c);
    c.samples = 200;
    c.jitter_secs = 0.2;
    c.cb = cb;
    if (argc > 1 && !strcmp(argv[1], "stop")) {   /* switch + files, Stop after 40 ms (in the switch test) */
        c.run_mask = 1u << OS_G_SWITCH | 1u << OS_G_FILES;
        c.samples = 1000000;
        pthread_create(&t, NULL, stopper, (void *)(size_t)40000000);
    } else if (argc > 1 && !strcmp(argv[1], "stopfiles")) {   /* files only, 100000 files, Stop after 40 ms */
        c.run_mask = 1u << OS_G_FILES;
        c.samples = 1000000;
        pthread_create(&t, NULL, stopper, (void *)(size_t)40000000);
    }
    so_text_init(&o, stdout);
    rc = bench_os(&c, &r, &o, &cancel);
    if (argc > 1 && strncmp(argv[1], "stop", 4) == 0) pthread_join(t, NULL);
    for (i = 0; i < r.nrows; i++)
        printf("ROW|%s|%d|%ld|%.1f|%.1f|%.1f|%.1f|%.1f\n", r.row[i].name, r.row[i].skipped, r.row[i].n, r.row[i].best,
               r.row[i].med, r.row[i].p95, r.row[i].p99, r.row[i].worst);
    printf("RC|%d|%d|%d\n", rc, r.cancelled, live);
    printf("CHILD|%d\n", waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD ? 0 : 1);
    printf("SCHED|%s|%s\n", r.sched, r.sched_detail);
    return 0;
}
C
SECS=$(make -s -C "$SRC" --no-print-directory --eval 'srs-print-secs:; @echo $(SECS)' srs-print-secs)
case " $SECS " in *" bench_os.c "*) ;; *) SECS="$SECS bench_os.c" ;; esac
SAN=; [ "${SANITIZE:-0}" = 1 ] && SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
( cd "$SRC" && /usr/bin/gcc -std=c11 -Wall -Wextra -Wno-format-truncation -O2 $SAN -DSRS_HAVE_SECTIONS -D_DEFAULT_SOURCE -pthread \
    -DSRS_NO_MAIN -I"$SRC" -o "$TMP/harness" "$TMP/harness.c" core.c cli.c plat_posix.c $SECS -lm ) 2>"$TMP/build.log"
# bench_os.c alone with the strict flags (format truncation warnings on)
/usr/bin/gcc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -DSRS_HAVE_SECTIONS -pthread -I"$SRC" -c -o /dev/null "$SRC/bench_os.c" 2>>"$TMP/build.log"
if [ -x "$TMP/harness" ]; then
    grep -n "bench_os.*warning" "$TMP/build.log" && bad "compiler warnings in bench_os.c" || ok "built${SAN:+ (address + undefined sanitizers)}"
else
    cat "$TMP/build.log"; bad "test program does not build"; exit 1
fi

echo "== short full run (200 samples, 0.2 s tick)"
t0=$(date +%s.%N)
out=$("$TMP/harness")
t1=$(date +%s.%N)
rows=$(grep -c '^ROW|' <<<"$out")
[ "$rows" -ge 20 ] && ok "$rows result rows in $(printf '%.1f' "$(echo "$t1 - $t0" | bc)") s" || bad "only $rows rows"
awk -F'|' '/^ROW\|/ && $3 == 0 { if (!($4 > 0 && $5 <= $6 && $6 <= $7 && $7 <= $8 && $8 <= $9 && $5 >= 0)) { print "  FAIL  bad statistics: " $0; e = 1 } }
           /^ROW\|/ && $3 == 1 { print "  note  skipped: " $2 }
           END { exit e }' <<<"$out" && ok "statistics ordered in every measured row" || fail=1
IFS='|' read -r _ rc cancelled live <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = 0 ] && [ "$cancelled" = 0 ] && [ "$live" = "$rows" ] && ok "return 0, live callback for every row" || bad "rc=$rc cancelled=$cancelled live=$live rows=$rows"
grep -q '^CHILD|0' <<<"$out" && ok "no child process left" || bad "a child process was left behind"
ok "scheduler: $(grep '^SCHED|' <<<"$out" | cut -d'|' -f2)"
[ -s "$HOME/.cache/srs/bench-os-latency.txt" ] && grep -q "Median" "$HOME/.cache/srs/bench-os-latency.txt" && ok "result file written" || bad "no bench-os-latency.txt"
grep -q $'\tos-latency\t' "$HOME/.cache/srs/bench-history.tsv" 2>/dev/null && ok "history line added" || bad "no history line"
[ "$(leftovers)" = 0 ] && ok "no temporary folder left" || bad "temporary folder left in ~/.cache/srs"

echo "== Stop in the middle"
out=$("$TMP/harness" stop)
IFS='|' read -r _ rc cancelled live <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = 1 ] && [ "$cancelled" = 1 ] && ok "return 1 (stopped) after $live finished rows" || bad "stop: rc=$rc cancelled=$cancelled"
grep -q "Stopped before the end" <<<"$out" && ok "result says it was stopped" || bad "no Stopped line"
grep -q '^CHILD|0' <<<"$out" && ok "no child process left" || bad "a child process was left behind"
[ "$(leftovers)" = 0 ] && ok "no temporary folder left" || bad "temporary folder left after Stop"

echo "== Stop in the middle of the file test"
out=$("$TMP/harness" stopfiles)
IFS='|' read -r _ rc cancelled live <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = 1 ] && [ "$cancelled" = 1 ] && ok "return 1 (stopped)" || bad "stop in files: rc=$rc cancelled=$cancelled"
[ "$(leftovers)" = 0 ] && ok "no temporary folder or file left" || bad "temporary folder left after Stop in the file test"

echo "== folder left by a killed run is removed next time"
mkdir -p "$HOME/.cache/srs/srs-os-999999-AbC123" && : > "$HOME/.cache/srs/srs-os-999999-AbC123/f000001"
mkdir -p "$HOME/.cache/srs/srs-os-999999-KEEPME" && : > "$HOME/.cache/srs/srs-os-999999-KEEPME/other.txt"
"$TMP/harness" stop >/dev/null
[ ! -e "$HOME/.cache/srs/srs-os-999999-AbC123" ] && ok "stale test folder removed" || bad "stale test folder still there"
[ -e "$HOME/.cache/srs/srs-os-999999-KEEPME/other.txt" ] && ok "a folder holding other files is left alone" || bad "removed a file that was not ours"

if [ -x "$BIN" ] && "$BIN" os-latency --help >/dev/null 2>&1; then
    echo "== CLI: $BIN os-latency"
    out=$("$BIN" os-latency --samples 200 --tick-seconds 0.2 --yes 2>&1); rc=$?
    [ "$rc" = 0 ] && grep -q "Median" <<<"$out" && ok "CLI run" || bad "CLI run (exit $rc)"
    "$BIN" os-latency --samples 200 --tick-seconds 0.2 </dev/null >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "asks first (no --yes, no terminal: not started)" || bad "os-latency started without --yes (exit $rc)"
    "$BIN" os-latency --no-such-option --yes >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "unknown option: exit 2, nothing run" || bad "unknown option gave exit $rc"
else
    echo "== CLI: skipped ($BIN has no os-latency command yet)"
fi
exit $fail
