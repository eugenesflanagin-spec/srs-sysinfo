#!/usr/bin/env bash
# tests/perf-tray-test.sh - the live telemetry tray meters (perf_tray.c, perf_sni.c): registration with the tray,
# IconPixmap sizes and byte order, NewIcon only on change, tooltips, left click, the dbusmenu check list (add and
# remove a meter), Quit, the settings file, and the Task Manager settings page (light and dark, under xvfb).
#
#   tests/perf-tray-test.sh            (builds its own small driver, tests/perf_tray_test.c, with the project flags)
#
# SAFETY: everything runs on a PRIVATE session bus (dbus-run-session) with a fake tray (tests/fake-sniwatcher.py);
# DBUS_SESSION_BUS_ADDRESS, DISPLAY and WAYLAND_DISPLAY are unset first, so no icon ever reaches the real desktop.
# The machine is synthetic (SRS_TEST_TRAY_FEED=sine: no /proc sampling, no GPU opened). Throw-away HOME and XDG dirs.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
unset DBUS_SESSION_BUS_ADDRESS DISPLAY WAYLAND_DISPLAY
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
has() { grep -qF -- "$2" "$1" && ok "$3" || { bad "$3 (no '$2')"; sed 's/^/        /' "$1" | head -40; }; }

command -v dbus-run-session >/dev/null || { echo "SKIP: no dbus-run-session"; exit 0; }
python3 -I -c 'import gi; gi.require_version("Gio", "2.0"); from gi.repository import Gio' 2>/dev/null || { echo "SKIP: no python3-gi"; exit 0; }

CC=/usr/bin/gcc
[ -x "$CC" ] || CC=cc
DRV=$TMP/perf_tray_test
echo "== build the driver (warnings are errors)"
(cd "$SRC" && "$CC" -std=c11 -Wall -Wextra -Wno-format-truncation -Werror -O2 -DSRS_HAVE_SECTIONS -D_DEFAULT_SOURCE -D_FORTIFY_SOURCE=2 \
    -pthread -Wno-deprecated-declarations $(pkg-config --cflags gtk+-3.0) -o "$DRV" tests/perf_tray_test.c perf_tray.c perf_sni.c \
    perf_data.c power_src.c power_model.c gpu_top.c gpu_top_amd.c gpu_top_nvml.c lists.c plat_posix.c $(pkg-config --libs gtk+-3.0) -lm -ldl) \
    > "$TMP/build.log" 2>&1 && ok "driver builds warning-free" || { bad "driver build"; cat "$TMP/build.log"; exit 1; }

# one run: $1 = name, $2 = seconds, $3 = [Task Manager] lines; the fake tray starts 1 s after the meters (re-register path)
run() {
    local R=$TMP/$1
    mkdir -p "$R/home" "$R/cfg/srs" "$R/cache" "$R/data" "$R/state" "$R/png"
    printf '[SRSDEF]\nTheme=Dark\nClose To Tray=0\n\n[Task Manager]\n%b' "$3" > "$R/cfg/srs/SRSDEF.ini"
    env HOME="$R/home" XDG_CONFIG_HOME="$R/cfg" XDG_CACHE_HOME="$R/cache" XDG_DATA_HOME="$R/data" XDG_STATE_HOME="$R/state" \
        SRS_TEST_TRAY_FEED=sine SRS_TEST_TRAY_LOG=1 SRS_TEST_TRAY_PNG_DIR="$R/png" \
        timeout 90 dbus-run-session -- sh -c "'$DRV' sni $2 ${4:-} 2>'$R/driver.log' & sleep 1; python3 -I '$HERE/fake-sniwatcher.py' '$R' $2 >'$R/watcher.log' 2>&1; wait" \
        > "$R/session.log" 2>&1
}
J() { python3 -I -c "import json,sys; d=json.load(open(sys.argv[1])); $2" "$1"; }

echo "== eleven meters, Normal speed (1 s), fake tray"
run normal 14 'Telemetry Icons=1\nTelemetry Meters=cpu,cpuhist,cputemp,mem,gpu0,gpuhist0,gputemp0,vram0,disk,space,net\n'
R=$TMP/normal
has "$R/driver.log" "SRS-TEST: tray meters 11" "eleven meters from the settings file"
has "$R/driver.log" "SRS-TEST: active at start 0" "tray not on the bus yet at start: not active"
n=$(grep -c "registered srs-" "$R/watcher.log")
[ "$n" -ge 11 ] && ok "all eleven registered with the tray (by bus name) after it started late" || { bad "registered: $n"; cat "$R/watcher.log"; }
grep -q '^FAIL' "$R/watcher.log" && { bad "fake tray checks"; grep '^FAIL' "$R/watcher.log"; } || ok "fake tray found no protocol errors"
[ "$(J "$R/watcher.json" 'print(all(i.get("sizes")==[22,32,64] for i in d["items"]))')" = True ] && ok "IconPixmap 22, 32 and 64 px on every item" || bad "pixmap sizes"
c=$(J "$R/watcher.json" 'print([len(i["newicon"]) for i in d["items"] if i["id"]=="srs-meter-cpuhist"][0])')
[ "$c" -ge 7 ] && [ "$c" -le 15 ] && ok "CPU History: $c NewIcon signals in about 12 s at 1 s" || bad "CPU History NewIcon count $c"
c=$(J "$R/watcher.json" 'print([len(i["newicon"]) for i in d["items"] if i["id"]=="srs-meter-space"][0])')
[ "$c" -le 1 ] && ok "Disk Space: $c NewIcon (not redrawn while the value stays)" || bad "Disk Space NewIcon count $c"
J "$R/watcher.json" 'print("\n".join("%s|%s" % (i["id"], i.get("tooltip")) for i in d["items"]))' > "$R/tips"
has "$R/tips" "srs-meter-cpu|['CPU Meter: " "CPU Meter tooltip title"
has "$R/tips" "of 100 % (User" "CPU % of the whole CPU by default"
has "$R/tips" "srs-meter-gpu0|['GPU Meter: " "GPU Meter tooltip"
has "$R/tips" "Edge " "GPU temperatures in the GPU tooltip"
has "$R/tips" "srs-meter-mem|['Memory Used: " "Memory tooltip"
has "$R/tips" "Network: Receive " "Network tooltip"
has "$R/driver.log" "srs-meter-cpu ProvideXdgActivationToken" "activation token taken before the click"
has "$R/driver.log" "SRS-TEST: hook open_performance" "left click opens the Performance tab"
has "$R/driver.log" "SRS-TEST: hook show_main" "menu Open SRS shows the main window"
has "$R/watcher.log" "ok menu: _Open SRS | _Settings... | Notification Area Items | CPU Meter | CPU Cores" "menu: Open SRS, Settings, meter check list"
has "$R/watcher.log" "_Quit SRS" "menu has Quit"
has "$R/watcher.log" "ok srs-meter-cores registered after the menu click" "ticking CPU Cores adds its icon"
has "$R/watcher.log" "ok srs-meter-cores removed after unticking it" "unticking removes the icon at once"
# Plasma sorts tray items by Category, then Title: all meters "Hardware", Titles "SRS <rank> <name>" in the Settings order
python3 -I - "$R/watcher.json" > "$R/order" 2>&1 <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
order = ["cpu", "cores", "cpuhist", "cputemp", "mem", "gpu0", "gpuhist0", "gputemp0", "vram0", "disk", "space", "net"]
for key in ("titles_start", "titles_with_cores", "titles_after"):
    t = sorted(d.get(key) or [], key=lambda x: x[1].casefold())
    cats = sorted(set(c for c, _, _ in t))
    got = [i[len("srs-meter-"):] for _, _, i in t]
    want = [m for m in order if m in got]
    ranks = all(title.startswith("SRS %02d " % (n + 1)) for n, (_, title, _) in enumerate(t))
    print(key, len(t), cats, "order-ok" if got == want and ranks else "order-BAD %r" % [x[1] for x in t])
print("cores-title", [x[1] for x in d.get("titles_with_cores", []) if x[2] == "srs-meter-cores"])
EOF
has "$R/order" "titles_start 11 ['Hardware'] order-ok" "11 meters: Category Hardware, Titles SRS 01..11 in the Settings order"
has "$R/order" "titles_with_cores 12 ['Hardware'] order-ok" "CPU Cores ticked: renumbered, still in order"
has "$R/order" "cores-title ['SRS 02 CPU Cores']" "CPU Cores takes place 02 (after CPU Meter)"
has "$R/order" "titles_after 11 ['Hardware'] order-ok" "CPU Cores unticked: the numbers close up again"
has "$R/driver.log" "SRS-TEST: hook quit" "Quit runs the quit hook"
has "$R/driver.log" "SRS-TEST: hook quit, active 1" "active once the tray took the icons"
n=$(grep -c "^unregistered srs-" "$R/watcher.log")
[ "$n" -ge 12 ] && ok "every icon left the tray on quit" || bad "unregistered: $n"
has "$R/cfg/srs/SRSDEF.ini" "Telemetry Meters=cpu,cpuhist,cputemp,mem,gpu0,gpuhist0,gputemp0,vram0,disk,space,net" "meter choice saved after the clicks"
has "$R/cfg/srs/SRSDEF.ini" "Theme=Dark" "SRS's own [SRSDEF] keys kept"
# byte order: what the tray decoded (big-endian ARGB) equals what Cairo drew (native, premultiplied)
python3 -I - "$R" > "$R/px" 2>&1 <<'EOF'
import sys
from PIL import Image
R = sys.argv[1]
worst = 0
for m in ("space", "mem"):
    a = Image.open("%s/png/%s-32.png" % (R, m)).convert("RGBA").tobytes()
    b = Image.open("%s/srs-meter-%s-32-recv.png" % (R, m)).convert("RGBA").tobytes()
    if m == "space":
        worst = max(abs(x - y) for x, y in zip(a, b))
print("maxdiff", worst)
EOF
grep -q "maxdiff [0-2]$" "$R/px" && ok "received pixels match the drawn ones ($(cat "$R/px"))" || { bad "pixel compare"; cat "$R/px"; }

echo "== High speed (2/s), per-core units"
run high 9 'Update Speed=2\nPer Core Units=1\nTelemetry Icons=1\nTelemetry Meters=cpu,cpuhist\n'
R=$TMP/high
c=$(J "$R/watcher.json" 'print([len(i["newicon"]) for i in d["items"] if i["id"]=="srs-meter-cpuhist"][0])')
[ "$c" -ge 9 ] && ok "CPU History: $c NewIcon in about 7 s at High (twice a second)" || bad "High speed NewIcon count $c"
J "$R/watcher.json" 'print("\n".join("%s|%s" % (i["id"], i.get("tooltip")) for i in d["items"]))' > "$R/tips"
has "$R/tips" "of 1600 % (User" "Per Core Units: 16 logical CPUs = 1600 %"

echo "== Tray Position: First from the start, and Normal -> First while running"
run pos1 12 'Tray Position=1\nTelemetry Icons=1\nTelemetry Meters=cpu,mem,net\n'
R=$TMP/pos1
J "$R/watcher.json" 'print("first", sorted(set(c for c,_,_ in d["titles_start"])), [t for _,t,_ in sorted(d["titles_start"], key=lambda x: x[1])])' > "$R/pos"
has "$R/pos" "first ['Other'] [' SRS 01 CPU Meter', ' SRS 02 Memory Used', ' SRS 03 Network']" "First: Category Other (Plasma's first group), Titles ' SRS 01..' in order"
has "$R/cfg/srs/SRSDEF.ini" "Tray Position=1" "Tray Position kept in the settings file"
run flip 12 'Telemetry Icons=1\nTelemetry Meters=cpu,mem\n' 3
R=$TMP/flip
has "$R/driver.log" "SRS-TEST: tray position first" "switched to First while running"
J "$R/watcher.json" 'print("flip", sorted(set(c for c,_,_ in d["titles_start"])), [t for _,t,_ in sorted(d["titles_start"], key=lambda x: x[1])], len(d["registered"]), len(d["unregistered"]))' > "$R/pos"
has "$R/pos" "flip ['Other'] [' SRS 01 CPU Meter', ' SRS 02 Memory Used'] " "after the switch: Category Other and ' SRS' titles"
J "$R/watcher.json" 'print("reg", [i for i,_ in d["registered"]][:4], [i for i,_ in d["unregistered"]][:2])' > "$R/reg"
has "$R/reg" "reg ['srs-meter-cpu', 'srs-meter-mem', 'srs-meter-cpu', 'srs-meter-mem'] ['srs-meter-cpu', 'srs-meter-mem']" "the icons registered anew (a tray reads Category only at registration), same Ids"

echo "== Telemetry Icons off"
run off 4 'Telemetry Icons=0\nTelemetry Meters=cpu,mem\n'
R=$TMP/off
has "$R/driver.log" "SRS-TEST: tray meters 0 (off)" "no icons when the option is off"
grep -q "^registered" "$R/watcher.log" && bad "an icon registered while off" || ok "nothing registered"

echo "== no tray on the bus"
R=$TMP/notray
mkdir -p "$R/cfg/srs" "$R/home"
printf '[Task Manager]\nTelemetry Icons=1\nTelemetry Meters=cpu\n' > "$R/cfg/srs/SRSDEF.ini"
env HOME="$R/home" XDG_CONFIG_HOME="$R/cfg" SRS_TEST_TRAY_FEED=sine SRS_TEST_TRAY_LOG=1 \
    timeout 30 dbus-run-session -- "$DRV" sni 3 > "$R/driver.log" 2>&1
rc=$?
[ $rc = 0 ] && ok "runs and exits cleanly with no tray (exit 0)" || bad "exit $rc without a tray"
grep -q "registered=1" "$R/driver.log" && bad "claims to be registered without a tray" || ok "not registered without a tray"
has "$R/driver.log" "SRS-TEST: active at start 0" "no tray: SRS would not hide (no way back)"
has "$R/driver.log" "SRS-TEST: time up, active 0" "still not active at the end"

echo "== tray already on the bus (SRS started with --gui-tray at login)"
R=$TMP/first
mkdir -p "$R/cfg/srs" "$R/home"
printf '[Task Manager]\nTelemetry Icons=1\nTelemetry Meters=cpu\n' > "$R/cfg/srs/SRSDEF.ini"
env HOME="$R/home" XDG_CONFIG_HOME="$R/cfg" SRS_TEST_TRAY_FEED=sine SRS_TEST_TRAY_LOG=1 \
    timeout 60 dbus-run-session -- sh -c "python3 -I '$HERE/fake-sniwatcher.py' '$R' 12 >'$R/watcher.log' 2>&1 & sleep 1; '$DRV' sni 3 2>'$R/driver.log'; wait" \
    > "$R/session.log" 2>&1
has "$R/driver.log" "SRS-TEST: active at start 1" "tray on the bus: active at once, so SRS may start hidden"
has "$R/driver.log" "SRS-TEST: time up, active 1" "registered by the end"

echo "== settings page (xvfb, light and dark)"
if command -v xvfb-run >/dev/null; then
    for th in light dark; do
        R=$TMP/set-$th
        mkdir -p "$R/cfg/srs" "$R/home"
        printf '[Task Manager]\nTelemetry Icons=1\nTelemetry Meters=cpu,cores,mem,gpu0\n' > "$R/cfg/srs/SRSDEF.ini"
        env -u WAYLAND_DISPLAY GDK_BACKEND=x11 GTK_THEME= HOME="$R/home" XDG_CONFIG_HOME="$R/cfg" XDG_CACHE_HOME="$R/home/.cache" \
            XDG_DATA_HOME="$R/home/.local/share" XDG_STATE_HOME="$R/home/.local/state" SRS_TEST_TRAY_FEED=sine \
            timeout 60 xvfb-run -a -s "-screen 0 1280x1024x24" dbus-run-session -- "$DRV" settings "$R/page.png" $th > "$R/out" 2>&1
        grep -q "^settings [0-9]*x[0-9]*" "$R/out" && [ -s "$R/page.png" ] && ok "settings page drawn ($th, $(grep -o '^settings [0-9]*x[0-9]*' "$R/out" | cut -d' ' -f2))" \
            || { bad "settings page ($th)"; cat "$R/out"; }
        [ -n "${SRS_TEST_KEEP:-}" ] && cp "$R/page.png" "$SRS_TEST_KEEP/perf-tray-settings-$th.png"
    done
else
    echo "  skip  no xvfb-run"
fi
[ -n "${SRS_TEST_KEEP:-}" ] && cp -r "$TMP/normal" "$SRS_TEST_KEEP/perf-tray-normal" 2>/dev/null

[ $fail = 0 ] && echo "perf-tray-test: all passed" || echo "perf-tray-test: FAILED"
exit $fail
