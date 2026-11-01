#!/usr/bin/env bash
# tests/perf-view-test.sh - the Performance tab of the GPU Top window: the sampler (perf_data.c) on two /proc + /sys
# snapshots taken one second apart (every rate and percentage checked against hand-worked numbers), the GPU part on the
# RX 9070 XT fixture, then each of the six pages for 10 s under xvfb (no crash, no GTK criticals).
#
#   tests/perf-view-test.sh [SYSREPORT-SEARCH]      (the argument is accepted for make test-bench and not used)
#   SRS_PERF_SHOTS=/abs/dir  also saves a screenshot of every page (light and dark theme) there, and of the process
#                            actions menu and End Task confirmation
#
# Process actions (End Task, Force Kill, tree, Suspend / Resume, priority, affinity, zombies) are tested ONLY on processes
# this script starts itself (sleep copies, a zombie maker); pkexec is never run (SRS_TEST_PKEXEC = a logging fake).
#
# SAFETY: the sampler reads fixtures (SRS_TEST_PROC_ROOT, SRS_TEST_GPU_ROOT; GRBM performance counters are never read by
# this view). The GUI runs in xvfb with a throw-away HOME / XDG_* folders; systemctl and loginctl are
# tests/fake-systemctl.sh (logs its arguments, changes nothing); no button is pressed, so nothing is started, stopped
# or disconnected. Its process list comes from the real /proc, read only.
set -u
cd "$(dirname "$0")/.." || exit 2
command -v pkg-config >/dev/null && pkg-config --exists glib-2.0 || { echo "  skip  no glib-2.0 development files"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
CC=/usr/bin/gcc   # full path, like the Makefile (/opt/aocc/bin shadows gcc here; $CC may be clang)
FLAGS="-std=c11 -Wall -Wextra -Werror -Wno-format-truncation -O2 -DSRS_HAVE_SECTIONS -D_DEFAULT_SOURCE -pthread -D_FORTIFY_SOURCE=2"
DATA="perf_data.c power_src.c power_model.c gpu_top.c gpu_top_amd.c gpu_top_nvml.c lists.c plat_posix.c"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
has() { grep -qxF -- "$2" <<<"$1" && ok "$3" || { bad "$3"; printf '        wanted line: %s\n' "$2"; }; }

echo "== sampler on fixtures (t0 -> t1, 1 s)"
# shellcheck disable=SC2046,SC2086
$CC $FLAGS $(pkg-config --cflags glib-2.0) -o "$TMP/pdh" tests/perf_data_harness.c $DATA $(pkg-config --libs glib-2.0) -lm -ldl \
    || { echo "  FAIL  build (sampler)"; exit 1; }
F=$PWD/tests/perf_fixtures
HZ=$(getconf CLK_TCK)
O=$(XDG_DATA_DIRS=$F XDG_DATA_HOME=/nonexistent "$TMP/pdh" "$F/t0" "$F/t1")
has "$O" "ncpu: 2" "two logical CPUs"
has "$O" "model: Fixture CPU 2-Thread" "CPU model"
has "$O" "cpu: 25.0" "total CPU 25 % (50 of 200 ticks)"
has "$O" "user: 20.0" "user time 20 %"
has "$O" "kernel: 5.0" "kernel time 5 %"
has "$O" "cpu0: 30.0 10.0" "CPU 0 user / kernel"
has "$O" "cpu1: 10.0 0.0" "CPU 1 user / kernel"
has "$O" "mhz: 3500" "average clock"
has "$O" "max_mhz: 5000" "maximum clock"
has "$O" "governor: schedutil" "governor"
has "$O" "cpu_temp: 61.2 (k10temp Tctl)" "k10temp Tctl"
has "$O" "mem_used: 8192000000" "memory in use = total - available"
has "$O" "mem_cached: 3328000000" "cached = Cached + SReclaimable"
has "$O" "swap_used: 1024000000" "swap in use"
has "$O" "psi: 1 cpu=12.50 mem=1.25/0.50 io=3.00/2.00" "pressure stall avg10"
has "$O" "disk nvme0n1: rd=1048576 wr=2097152 busy=25.0 resp=5.00 size=1024000000 model=Fixture NVMe 1TB" "disk rates, active time, response"
grep -q '^disk loop0' <<<"$O" && bad "loop device listed" || ok "loop devices left out"
has "$O" "net eth0: rx=125000 tx=25000 kind=Ethernet speed=1000" "adapter rates (lo left out)"
if [ "$HZ" = 100 ]; then
    has "$O" "proc 4242: name=fixture app ppid=1 user=$(id -un) state=R cpu=40.0 rss=8192000 rd=1048576 wr=524288 thr=3 nice=5 gpu=-1 app=1 kthread=0 session=3 unit=session-3.scope cmd=/usr/bin/fixture-app --flag" \
        "process: CPU 40 %, disk rates, application (desktop entry + graphical session), session from the cgroup"
else
    ok "process CPU check skipped (CLK_TCK=$HZ, fixture assumes 100)"
fi
grep -q '^proc 1: .* rd=-1 wr=-1 ' <<<"$O" && ok "unreadable /proc/<pid>/io = not readable (shown as a dash)" || bad "unreadable io"
grep -q '^proc 77: .*kthread=1 .*cmd=\[kworker/0:1\]' <<<"$O" && ok "kernel thread" || bad "kernel thread"

echo "== GPU through gpu_top.h (RX 9070 XT fixture, counters off)"
G=$(SRS_TEST_GPU_ROOT=$PWD/tests/gpu_top_fixtures/rx9070xt XDG_DATA_DIRS=$F "$TMP/pdh" --gpu "$F/t0" "$F/t1")
grep -q '^gpu 0: open=1 .*vram=[0-9]*/17095983104 .*name=AMD Radeon RX 9070 XT' <<<"$G" && ok "GPU sampled: VRAM total, name" || { bad "GPU sampled"; grep '^gpu' <<<"$G"; }
grep -q '^temp GPU Hotspot: [0-9]' <<<"$G" && ok "GPU hotspot in Thermals" || bad "GPU hotspot"
N=$(SRS_TEST_GPU_ROOT=$PWD/tests/gpu_top_fixtures/rx9070xt XDG_DATA_DIRS=$F "$TMP/pdh" --gpu --no-procs "$F/t0" "$F/t1")
grep -qx 'procs: 0' <<<"$N" && grep -q '^gpu 0: open=1 .*vram=[0-9]*/17095983104' <<<"$N" && ok "pd_set_procs(0): no process walk, GPU still sampled (gt no_fdinfo)" \
    || bad "pd_set_procs(0)"

echo "== a process that exec()s keeps its PID and start time: the name is read again"
cp "$(command -v sleep)" "$TMP/srs-exec-after"
sh -c "sleep 1.2; exec \"$TMP/srs-exec-after\" 300" & EX=$!
EO=$("$TMP/pdh" --live 3 2>/dev/null | grep "^proc $EX: ")
kill $EX 2>/dev/null; wait $EX 2>/dev/null
grep -q "name=srs-exec-after " <<<"$EO" && ok "after exec() the process shows its new name" || bad "exec name: $EO"

echo "== process actions (only on processes this test started)"
# shellcheck disable=SC2046,SC2086
$CC $FLAGS -Wno-deprecated-declarations $(pkg-config --cflags gtk+-3.0) -o "$TMP/pph" tests/perf_procs_harness.c perf_procs.c $DATA \
    $(pkg-config --libs gtk+-3.0) -lm -ldl || { echo "  FAIL  build (process actions)"; exit 1; }
P=$TMP/pph
sleep 300 & A=$!
sleep 0.3
[ "$("$P" signal $A STOP)" = "0 " ] && sleep 0.2 && [ "$("$P" stat $A | cut -d' ' -f2)" = T ] && ok "Suspend: SIGSTOP, state T" || bad "Suspend"
[ "$("$P" signal $A CONT)" = "0 " ] && sleep 0.2 && [ "$("$P" stat $A | cut -d' ' -f2)" = S ] && ok "Resume: SIGCONT, sleeping again" || bad "Resume"
[ "$("$P" renice $A 10)" = "0 " ] && [ "$(ps -o ni= -p $A | tr -d ' ')" = 10 ] && ok "Set Priority: nice 10 (every thread)" || bad "Set Priority"
"$P" aff-set $A 0 >/dev/null && [ "$("$P" aff-get $A)" = 0 ] && ok "Set CPU Affinity: CPU 0 only" || bad "Set CPU Affinity"
[ "$("$P" signal $A TERM 12345 | cut -c1)" = 2 ] && kill -0 $A 2>/dev/null && ok "wrong start time (PID reuse): nothing is signalled" || bad "PID reuse guard"
[ "$("$P" signal $A TERM)" = "0 " ] && wait $A 2>/dev/null; [ "$("$P" stat $A)" = gone ] && ok "End Task: SIGTERM ends it" || bad "End Task"
sleep 300 & A=$!
sleep 0.2
"$P" signal $A KILL >/dev/null; wait $A 2>/dev/null; [ "$("$P" stat $A)" = gone ] && ok "Force Kill: SIGKILL" || bad "Force Kill"
bash -c 'sleep 0.1 & exec sleep 300' & Z=$!      # the child exits, exec'd sleep never reaps it: a zombie
sleep 0.6
ZC=$(ps -o pid= --ppid $Z | tr -d ' ' | head -1)
[ -n "$ZC" ] && [ "$("$P" stat "$ZC" | cut -d' ' -f2)" = Z ] && ok "zombie maker: child $ZC is a zombie of $Z" || bad "zombie maker"
kill $Z; wait $Z 2>/dev/null; sleep 0.2
[ -n "$ZC" ] && [ "$("$P" stat "$ZC")" = gone ] && ok "ending the zombie's parent removes the zombie" || bad "zombie parent"
bash -c 'sleep 300 & sleep 300 & wait' & T=$!
sleep 0.4
TK=$("$P" tree $T)
[ "$(wc -w <<<"$TK")" = 2 ] && ok "End Process Tree: the two children found (deepest first)" || bad "tree: '$TK'"
for k in $TK; do "$P" signal "$k" TERM >/dev/null; done; "$P" signal $T TERM >/dev/null; wait $T 2>/dev/null
[ "$("$P" signal 1 TERM | cut -c1-2)" = -1 ] && ok "PID 1 refused" || bad "PID 1"
grep -q 'kernel thread' <<<"$("$P" untouchable 77 2 1)" && ok "kernel threads untouchable" || bad "kernel thread"
grep -q 'Plasma desktop' <<<"$("$P" critical 999999 plasmashell)" && ok "critical: plasmashell warned" || bad "critical"
[ "$(SRS_TEST_PKEXEC=/fake/pkexec "$P" elevated kill TERM 4242)" = "/fake/pkexec $(type -P kill) -s TERM 4242" ] \
    && ok "other users: pkexec kill -s TERM <pid> (fake, not run)" || bad "pkexec argv"
if command -v python3 >/dev/null; then   # a 3-thread process of this test: the elevated renice names every thread
    python3 -c 'import threading,time
for _ in range(2): threading.Thread(target=time.sleep, args=(300,), daemon=True).start()
time.sleep(300)' & PY=$!
    sleep 0.5
    RA=$(SRS_TEST_PKEXEC=/fake/pkexec "$P" elevated renice 5 $PY)
    kill $PY; wait $PY 2>/dev/null
    [ "$(wc -w <<<"${RA#*-p }")" = 3 ] && [ "${RA##* }" = "$PY" ] && ok "other users: pkexec renice -n 5 -p <every thread> (fake, not run)" \
        || bad "renice argv: $RA"
fi

echo "== the six pages under xvfb, 10 s each"
if ! command -v xvfb-run >/dev/null || ! pkg-config --exists gtk+-3.0; then
    echo "  skip  no xvfb-run or GTK 3 development files"
else
    # shellcheck disable=SC2046,SC2086
    # no -Werror here: this link also builds other agents' files (perf_tray.c, perf_sni.c)
    $CC ${FLAGS/-Werror /} -Wno-deprecated-declarations $(pkg-config --cflags gtk+-3.0) -o "$TMP/demo" tests/perf_view_demo.c perf_view.c \
        perf_admin.c perf_autostart.c perf_tray.c perf_sni.c perf_procs.c $DATA $(pkg-config --libs gtk+-3.0) -lm -ldl || { echo "  FAIL  build (view)"; exit 1; }
    H=$TMP/home
    mkdir -p "$H/.config" "$H/.cache" "$H/.local/share" "$H/.local/state"
    cp -r tests/autostart_fixtures/user/. "$H/.config/"
    cp -r tests/perf_fixtures/fake "$TMP/fake"
    themes="Adwaita"
    [ -n "${SRS_PERF_SHOTS:-}" ] && { mkdir -p "$SRS_PERF_SHOTS"; themes="Adwaita Adwaita:dark"; }
    for th in $themes; do
        for pg in summary performance processes startup users services; do
            res=cpu; [ "$pg" = performance ] && [ "$th" != Adwaita ] && res=gpu
            shot=""; [ -n "${SRS_PERF_SHOTS:-}" ] && shot="$SRS_PERF_SHOTS/$pg-${th/:/-}.png"
            out=$(env -u WAYLAND_DISPLAY -u DISPLAY GDK_BACKEND=x11 NO_AT_BRIDGE=1 GTK_THEME="$th" HOME="$H" XDG_CONFIG_HOME="$H/.config" \
                XDG_CACHE_HOME="$H/.cache" XDG_DATA_HOME="$H/.local/share" XDG_STATE_HOME="$H/.local/state" \
                XDG_CONFIG_DIRS="$PWD/tests/autostart_fixtures/sys" SRS_TEST_AUTOSTART_NO_ETC=1 XDG_CURRENT_DESKTOP=KDE \
                SRS_TEST_SYSTEMCTL="$PWD/tests/fake-systemctl.sh" SRS_TEST_LOGINCTL="$PWD/tests/fake-systemctl.sh" \
                FAKE_SYSTEMCTL_DIR="$TMP/fake" FAKE_SYSTEMCTL_LOG="$TMP/fake/log" \
                SRS_TEST_GPU_ROOT="$PWD/tests/gpu_top_fixtures/rx9070xt" SRS_PERF_PAGE=$pg SRS_PERF_RES=$res SRS_PERF_DEMO_SECONDS=11 \
                xvfb-run -a -s "-screen 0 1400x900x24" bash -c "\"$TMP/demo\" & P=\$!; sleep 9; [ -n \"$shot\" ] && import -window root \"$shot\"; wait \$P; echo exit=\$?" 2>&1)
            if grep -q '^exit=0$' <<<"$out" && ! grep -qE 'CRITICAL|Segmentation|Aborted|AddressSanitizer' <<<"$out"; then ok "$pg ($th)"
            else bad "$pg ($th)"; printf '%s\n' "$out" | tail -5 | sed 's/^/        /'; fi
        done
    done
    grep -E ' (start|stop|restart|enable|disable|terminate-session) ' "$TMP/fake/log" && bad "a change was asked of systemctl / loginctl" \
        || ok "systemctl / loginctl only read (list / show)"
    # End Task through the window: a copy of sleep named srs-test-victim, selected by SRS_PERF_SELECT_PID, Delete, the
    # confirmation's End Task (Alt+E); only that process is ever touched
    if command -v xdotool >/dev/null; then
        cp "$(command -v sleep)" "$TMP/srs-test-victim"
        printf '#!/bin/sh\necho "pkexec $*" >> "%s"\nexit 1\n' "$TMP/pkexec.log" > "$TMP/fake-pkexec"; chmod +x "$TMP/fake-pkexec"
        for th in $themes; do
            "$TMP/srs-test-victim" 300 & V=$!
            sh1=""; sh2=""; sh3=""
            [ -n "${SRS_PERF_SHOTS:-}" ] && { sh1="$SRS_PERF_SHOTS/procs-menu-${th/:/-}.png"; sh2="$SRS_PERF_SHOTS/procs-confirm-${th/:/-}.png"
                                             sh3="$SRS_PERF_SHOTS/procs-result-${th/:/-}.png"; }
            env -u WAYLAND_DISPLAY -u DISPLAY GDK_BACKEND=x11 NO_AT_BRIDGE=1 GTK_THEME="$th" HOME="$H" XDG_CONFIG_HOME="$H/.config" \
                XDG_CACHE_HOME="$H/.cache" XDG_DATA_HOME="$H/.local/share" XDG_STATE_HOME="$H/.local/state" \
                SRS_TEST_SYSTEMCTL="$PWD/tests/fake-systemctl.sh" SRS_TEST_LOGINCTL="$PWD/tests/fake-systemctl.sh" \
                FAKE_SYSTEMCTL_DIR="$TMP/fake" FAKE_SYSTEMCTL_LOG="$TMP/fake/log" SRS_TEST_PKEXEC="$TMP/fake-pkexec" \
                SRS_TEST_GPU_ROOT="$PWD/tests/gpu_top_fixtures/rx9070xt" SRS_PERF_PAGE=processes SRS_PERF_SELECT_PID=$V \
                SRS_PERF_DEMO_SECONDS=20 xvfb-run -a -s "-screen 0 1400x900x24" bash -c "\"$TMP/demo\" & P=\$!; sleep 6
                    xdotool key shift+F10; sleep 1; [ -n \"$sh1\" ] && import -window root \"$sh1\"; xdotool key Escape; sleep 0.5
                    xdotool key Delete; sleep 1.5; [ -n \"$sh2\" ] && import -window root \"$sh2\"
                    xdotool key alt+e; sleep 1.5; [ -n \"$sh3\" ] && import -window root \"$sh3\"; sleep 1.5; kill \$P; wait \$P" >/dev/null 2>&1
            sleep 0.3
            if kill -0 $V 2>/dev/null; then bad "End Task in the window ($th): srs-test-victim ($V) still runs"; kill $V; wait $V 2>/dev/null
            else ok "End Task in the window ($th): Delete, confirm, srs-test-victim ended"; wait $V 2>/dev/null; fi
        done
        [ -s "$TMP/pkexec.log" ] && bad "pkexec was asked for" || ok "no pkexec for the user's own process"
        # Users > Disconnect with the question left open while the session list is read again (every 5 s): the fake
        # loginctl must get the id that was asked about (3), after "--"; nothing else is ever terminated
        cp -r "$TMP/fake" "$TMP/fake3"; : >"$TMP/fake3/log"
        printf '      3 1000 user -     4242  user    pts/9 no   -\n' >>"$TMP/fake3/sessions.txt"
        printf '\nId=3\nName=user\nUser=1000\nState=online\nActive=no\nType=tty\nClass=user\nSeat=\nTTY=pts/9\nRemote=yes\nRemoteHost=test\nService=sshd\nDesktop=\nLeader=4242\n' >>"$TMP/fake3/show-sessions.txt"
        env -u WAYLAND_DISPLAY -u DISPLAY GDK_BACKEND=x11 NO_AT_BRIDGE=1 GTK_THEME=Adwaita HOME="$H" XDG_CONFIG_HOME="$H/.config" \
            XDG_CACHE_HOME="$H/.cache" XDG_DATA_HOME="$H/.local/share" XDG_STATE_HOME="$H/.local/state" XDG_SESSION_ID=1 \
            SRS_TEST_SYSTEMCTL="$PWD/tests/fake-systemctl.sh" SRS_TEST_LOGINCTL="$PWD/tests/fake-systemctl.sh" \
            FAKE_SYSTEMCTL_DIR="$TMP/fake3" FAKE_SYSTEMCTL_LOG="$TMP/fake3/log" SRS_TEST_PKEXEC="$TMP/fake-pkexec" \
            SRS_TEST_GPU_ROOT="$PWD/tests/gpu_top_fixtures/rx9070xt" SRS_PERF_PAGE=users SRS_PERF_DEMO_SECONDS=30 \
            xvfb-run -a -s "-screen 0 1400x900x24" bash -c "\"$TMP/demo\" & P=\$!; sleep 7
                xdotool mousemove 700 165 click 1; sleep 0.3; xdotool key End; sleep 0.5; xdotool key alt+o; sleep 12
                xdotool key alt+d; sleep 1.5; kill \$P; wait \$P" >/dev/null 2>&1
        T3=$(grep -c 'terminate-session' "$TMP/fake3/log")
        grep -q 'terminate-session -- 3$' "$TMP/fake3/log" && [ "$T3" = 1 ] \
            && ok "Disconnect after a 12 s question (the list was re-read twice): loginctl terminate-session -- 3 (fake)" \
            || bad "Disconnect: $(grep terminate "$TMP/fake3/log" || echo 'not called')"
    else
        echo "  skip  End Task through the window (no xdotool)"
    fi
fi

[ $fail = 0 ] && echo "perf-view-test: all passed" || echo "perf-view-test: FAILED"
exit $fail
