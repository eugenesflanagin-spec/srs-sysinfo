#!/usr/bin/env bash
# tests/bench-net-test.sh - correctness test for the network benchmark (bench_net.c).
# Loopback only, tiny runs (about 5 s in all). The internet part is NEVER run here: it contacts a
# server on the internet and moves real data, so it only runs after the user said yes in the app.
#
#   tests/bench-net-test.sh [BINARY]     default: dist/native/sysreport-search (make native)
#   SANITIZE=1 tests/bench-net-test.sh   build the test program with the address + undefined sanitizers
#
# The engine is checked through a small test program built here (bench_net.c + the Makefile's
# sources, with -DSRS_NO_MAIN): sane numbers in every loopback row, the result file and history
# line, no socket or thread left, Stop works, the ask-first text names the server and the data
# use, a bad server address is refused before anything is sent, and nothing private (host name,
# IP or MAC address) is in the output. With strace installed, every connect / sendto is checked to
# go to 127.0.0.1. When BINARY knows "net" a short CLI run (--local only) is checked too.
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

echo "== build the test program"
cat > "$TMP/harness.c" <<'C'
#define _GNU_SOURCE
#include "bench_net.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile int cancel;
static void *stopper(void *p) { struct timespec d = { 0, (long)(size_t)p }; nanosleep(&d, NULL); cancel = 1; return NULL; }
static int count(const char *dir) { DIR *d = opendir(dir); struct dirent *e; int n = 0; if (!d) return -1; while ((e = readdir(d))) n += e->d_name[0] != '.'; closedir(d); return n; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char **argv)
{
    net_cfg c;
    net_where w;
    srs_out o;
    pthread_t t;
    char why[256] = "", ask[4096];
    const char *mode = argc > 1 ? argv[1] : "run";
    const net_result *r = net_last();
    int rc, fd0, fd1, th1;
    double t0;
    net_cfg_default(&c);
    so_text_init(&o, stdout);
    if (!strcmp(mode, "probe")) {   /* reads /proc and /sys only */
        c.internet = 0;
        printf("PROBE|%d|%s|%s|%s|%s\n", net_probe(&c, &w), w.server_host, w.curl, w.ifc.kind, w.problem);
        net_ask_text(&c, &w, ask, sizeof ask);
        printf("ASK|%s\n", ask);
        c.server = "http://speed.example";
        printf("BADURL|%d|%s\n", net_probe(&c, &w), w.problem);
        c.internet = 1;   /* refused before anything is sent */
        rc = bench_net(&c, NULL, &cancel, why, sizeof why);
        printf("REFUSE|%d|%s\n", rc, why);
        c.internet = 0;
        c.local = 0;
        rc = bench_net(&c, NULL, &cancel, why, sizeof why);
        printf("NOTHING|%d|%s\n", rc, why);
        return 0;
    }
    c.local = 1;
    c.internet = 0;
    c.streams = 2;
    c.secs = 0.3;
    if (!strcmp(mode, "stop")) { c.secs = 5; pthread_create(&t, NULL, stopper, (void *)(size_t)400000000); }
    fd0 = count("/proc/self/fd");
    t0 = now();
    rc = bench_net(&c, &o, &cancel, why, sizeof why);
    if (!strcmp(mode, "stop")) pthread_join(t, NULL);
    fd1 = count("/proc/self/fd");
    th1 = count("/proc/self/task");
    printf("XFER|tcp1|%d|%.1f|%.1f|%.2f\n", r->tcp1.done, r->tcp1.avg, r->tcp1.peak, r->tcp1.mb);
    printf("XFER|tcpn|%d|%.1f|%.1f|%.2f\n", r->tcpn.done, r->tcpn.avg, r->tcpn.peak, r->tcpn.mb);
    printf("XFER|udp|%d|%.1f|%.1f|%.2f\n", r->udp.done, r->udp.avg, r->udp.peak, r->udp.mb);
    printf("UDP|%.3f|%ld\n", r->udp_loss, r->udp_sent);
    printf("LAT|tcp|%d|%.4f|%.4f|%.4f|%ld|%ld\n", r->tcp_rtt.done, r->tcp_rtt.best, r->tcp_rtt.med, r->tcp_rtt.p99, r->tcp_rtt.n, r->tcp_rtt.lost);
    printf("LAT|udp|%d|%.4f|%.4f|%.4f|%ld|%ld\n", r->udp_rtt.done, r->udp_rtt.best, r->udp_rtt.med, r->udp_rtt.p99, r->udp_rtt.n, r->udp_rtt.lost);
    printf("NET|%.0f|%.0f\n", r->dns_first, r->down1.avg);
    printf("RC|%d|%s|%.2f|%d|%d|%d\n", rc, why, now() - t0, fd0, fd1, th1);
    return 0;
}
C
SECS=$(make -s -C "$SRC" --no-print-directory --eval 'srs-print-secs:; @echo $(SECS)' srs-print-secs)
case " $SECS " in *" bench_net.c "*) ;; *) SECS="$SECS bench_net.c" ;; esac
SAN=; [ "${SANITIZE:-0}" = 1 ] && SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
( cd "$SRC" && /usr/bin/gcc -std=c11 -Wall -Wextra -Wno-format-truncation -O2 $SAN -DSRS_HAVE_SECTIONS -D_DEFAULT_SOURCE -pthread \
    -DSRS_NO_MAIN -I"$SRC" -o "$TMP/harness" "$TMP/harness.c" core.c cli.c plat_posix.c $SECS -lm -ldl ) 2>"$TMP/build.log"
# bench_net.c alone with the strict flags (format truncation warnings on), and its libcurl numbers against curl.h
/usr/bin/gcc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -DSRS_HAVE_SECTIONS -pthread -I"$SRC" -c -o /dev/null "$SRC/bench_net.c" 2>>"$TMP/build.log"
if [ -x "$TMP/harness" ]; then
    grep -n "bench_net.*warning" "$TMP/build.log" && bad "compiler warnings in bench_net.c" || ok "built${SAN:+ (address + undefined sanitizers)}"
else
    cat "$TMP/build.log"; bad "test program does not build"; exit 1
fi
if [ -f /usr/include/curl/curl.h ]; then
    /usr/bin/gcc -std=c11 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -DSRS_HAVE_SECTIONS -DNET_CHECK_CURL_ABI -pthread -I"$SRC" \
        -c -o /dev/null "$SRC/bench_net.c" 2>"$TMP/abi.log" && ! grep -q warning "$TMP/abi.log" \
        && ok "libcurl option numbers match curl.h" || { cat "$TMP/abi.log"; bad "libcurl option numbers differ from curl.h"; }
else
    echo "  note  curl.h not installed: option numbers not checked"
fi

echo "== short loopback run (2 streams, 0.3 s per test)"
out=$("$TMP/harness")
awk -F'|' '/^XFER\|/ { if (!($3 == 1 && $4 > 0 && $5 >= $4 && $6 > 0)) { print "  FAIL  bad throughput row: " $0; e = 1 } else printf "  ok    %-5s %8.0f Mbit/s average, %8.0f peak\n", $2, $4, $5 }
           /^LAT\|/  { if (!($3 == 1 && $4 > 0 && $4 <= $5 && $5 <= $6 && $6 < 100 && $7 > 10)) { print "  FAIL  bad round trip row: " $0; e = 1 } else printf "  ok    %s round trip median %.4f ms over %d\n", $2, $5, $7 }
           /^UDP\|/  { if (!($3 > 0 && $2 >= 0 && $2 < 100)) { print "  FAIL  bad UDP loss: " $0; e = 1 } else printf "  ok    UDP %.2f%% of %d packets lost\n", $2, $3 }
           END { exit e }' <<<"$out" || fail=1
IFS='|' read -r _ rc why secs fd0 fd1 th <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = 0 ] && ok "return 0 in $secs s" || bad "rc=$rc ($why)"
[ "$fd0" = "$fd1" ] && ok "no socket left open ($fd1 descriptors before and after)" || bad "descriptors $fd0 -> $fd1"
[ "$th" = 1 ] && ok "no thread left" || bad "$th threads after the run"
grep -q '^NET|0|0' <<<"$out" && ok "internet part not run" || bad "internet results present"
f=$HOME/.cache/srs/bench-net.txt
[ -s "$f" ] && grep -q "TCP round trip" "$f" && grep -q "Mbit/s" "$f" && grep -q "Internet.*not run" "$f" && ok "result file written" || bad "no or incomplete bench-net.txt"
grep -q $'\tnet\tloopback streams=2' "$HOME/.cache/srs/bench-history.tsv" 2>/dev/null && ok "history line added" || bad "no history line"

echo "== nothing private in the results"
host=$(cat /proc/sys/kernel/hostname 2>/dev/null || hostname)
all="$out"$'\n'"$(cat "$f" "$HOME/.cache/srs/bench-history.tsv" 2>/dev/null)"
[ -n "$host" ] && grep -qiF -- "$host" <<<"$all" && bad "host name in the output" || ok "no host name"
grep -qE '([0-9]{1,3}\.){3}[0-9]{1,3}' <<<"$all" && bad "an IPv4 address in the output: $(grep -oE '([0-9]{1,3}\.){3}[0-9]{1,3}' <<<"$all" | head -1)" || ok "no IPv4 address"
grep -qiE '([0-9a-f]{2}:){5}[0-9a-f]{2}|([0-9a-f]{1,4}:){3,7}[0-9a-f]{1,4}' <<<"$all" && bad "a MAC or IPv6 address in the output" || ok "no MAC or IPv6 address"

echo "== Stop in the middle (5 s tests, Stop after 0.4 s)"
out=$("$TMP/harness" stop)
IFS='|' read -r _ rc why secs fd0 fd1 th <<<"$(grep '^RC|' <<<"$out")"
[ "$rc" = 1 ] && ok "return 1 (stopped) after $secs s" || bad "stop: rc=$rc ($why)"
awk -v s="$secs" 'BEGIN { exit !(s < 3) }' && ok "stopped quickly" || bad "Stop took $secs s"
grep -q "Stopped before the end" <<<"$out" && ok "result says it was stopped" || bad "no Stopped line"
[ "$fd0" = "$fd1" ] && [ "$th" = 1 ] && ok "no socket or thread left" || bad "after Stop: descriptors $fd0 -> $fd1, $th threads"

echo "== probe, ask-first text and refusals (nothing is sent)"
out=$("$TMP/harness" probe)
IFS='|' read -r _ can shost curl kind problem <<<"$(grep '^PROBE|' <<<"$out")"
[ "$shost" = speed.cloudflare.com ] && ok "server: $shost; ${curl:-libcurl not installed}; ${kind:-no default route}${problem:+ ($problem)}" || bad "probe: $shost"
ask=$(sed -n '/^ASK|/,/^BADURL|/p' <<<"$out")
if [ "$can" = 1 ] && [ -n "$curl" ]; then
    grep -q "speed.cloudflare.com" <<<"$ask" && grep -q "Data use: up to about 1000 MB" <<<"$ask" && grep -q "metered" <<<"$ask" \
        && ok "ask text names the server, the data use and metered lines" || bad "ask text: $ask"
else
    grep -q "speed.cloudflare.com\|cannot run" <<<"$ask" && ok "ask text: ${ask:4:80}..." || bad "ask text: $ask"
fi
grep -q '^BADURL|0|.*https://' <<<"$out" && ok "an http:// server is refused" || bad "bad server accepted"
grep -q '^REFUSE|-1|' <<<"$out" && ok "internet run with a bad server: not started" || bad "$(grep '^REFUSE|' <<<"$out")"
grep -q '^NOTHING|-1|nothing to run' <<<"$out" && ok "nothing chosen: not started" || bad "$(grep '^NOTHING|' <<<"$out")"

if command -v strace >/dev/null && [ "${SANITIZE:-0}" != 1 ]; then
    echo "== strace: loopback run and probe talk to 127.0.0.1 only"
    strace -f -qq -e trace=connect,sendto,sendmsg -o "$TMP/strace.log" "$TMP/harness" >/dev/null 2>&1
    strace -f -qq -e trace=connect,sendto,sendmsg -o "$TMP/strace2.log" "$TMP/harness" probe >/dev/null 2>&1
    other=$(cat "$TMP/strace.log" "$TMP/strace2.log" | grep -oE 'sin6?_addr[^}]*' | grep -v '127\.0\.0\.1' | sort -u)
    [ -z "$other" ] && ok "$(grep -c 'connect(' "$TMP/strace.log") connects, all to 127.0.0.1" || bad "traffic not on loopback: $other"
else
    echo "== strace: skipped"
fi

if [ -x "$BIN" ] && "$BIN" net --help >/dev/null 2>&1; then
    echo "== CLI: $BIN net --local"
    out=$("$BIN" net --local --seconds 0.3 --streams 2 --yes 2>&1); rc=$?
    [ "$rc" = 0 ] && grep -q "TCP round trip" <<<"$out" && ok "CLI run" || bad "CLI run (exit $rc)"
    "$BIN" net --local --seconds 0.3 --streams 2 </dev/null >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "asks first (no --yes, no terminal: not started)" || bad "net started without --yes (exit $rc)"
    "$BIN" net --no-such-option --yes >/dev/null 2>&1; rc=$?
    [ "$rc" = 2 ] && ok "unknown option: exit 2, nothing run" || bad "unknown option gave exit $rc"
    # the internet part needs its own yes: --yes alone (no terminal) must not start it or contact anything
    if command -v strace >/dev/null 2>&1; then
        strace -f -qq -e trace=connect,sendto,sendmsg -o "$TMP/strace3.log" "$BIN" net --internet --yes </dev/null >/dev/null 2>&1; rc=$?
        n=$(grep -cE 'connect\(|sendto\(|sendmsg\(' "$TMP/strace3.log" | head -1)
        [ "$rc" = 2 ] && [ "$n" = 0 ] && ok "internet test not started by --yes alone, nothing sent" || bad "internet: exit $rc, $n network calls"
        strace -f -qq -e trace=connect,sendto,sendmsg -o "$TMP/strace4.log" "$BIN" net --local --internet --yes --seconds 0.3 --streams 1 </dev/null >/dev/null 2>&1; rc=$?
        other=$(grep -oE 'sin6?_(addr|port)[^}]*' "$TMP/strace4.log" | grep -v '127\.0\.0\.1' | grep addr | sort -u)
        [ "$rc" = 2 ] && [ -z "$other" ] && ok "--local --internet --yes without a terminal: not started, nothing sent outside" || bad "--local --internet --yes: exit $rc, other: $other"
    else
        "$BIN" net --internet --yes </dev/null >/dev/null 2>&1; rc=$?
        [ "$rc" = 2 ] && ok "internet test not started by --yes alone" || bad "internet test with --yes alone: exit $rc"
    fi
else
    echo "== CLI: skipped ($BIN has no net command yet)"
fi
exit $fail
