#!/usr/bin/env bash
. "$(dirname "$0")/private-bus.sh"   # a private D-Bus session bus, never the desktop's (the GUI part)
# tests/gpu-tune-test.sh - GPU Tuning, the read-only part (2.6.0): "gpu-tune status [--json]", the [gpu-tuning] item and
# the GPU Tuning tab, on made-up data only.
#
#   tests/gpu-tune-test.sh SYSREPORT-SEARCH [SRS-GUI]      (make test-bench passes the first path)
#
# SAFETY: no GPU, no real service, no LACT is touched.
#   SRS_TEST_SYSFS_ROOT  = a made-up /sys, /proc, /run, /etc and /usr built here (an AMD card with an OverDrive table, an
#                          NVIDIA card, an older AMD card, a sleeping card, a fake LACT settings file);
#   SRS_TUX_BURNER_SOCKET = the socket of tests/fake-tux-burnerd.py (or a path that does not exist). The real
#                          /run/tux-burnerd.sock is never used: the first check below proves SRS refuses it in a test.
# The fake service logs every request; the test FAILS if SRS ever sends anything but "status" and "gpu_capabilities".
# The made-up tree is fingerprinted before and after: the test FAILS if SRS changed a single file in it.
# Helper processes are ended by their own PID only. Throw-away HOME; the GUI part runs on its own Xvfb display.
set -u
BIN=${1:-}
GUI=${2:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH [SRS-GUI]"; exit 2; }
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
[ -z "$GUI" ] && [ -x "$(dirname "$BIN")/srs-gui" ] && GUI=$(dirname "$BIN")/srs-gui   # make test-bench passes one path: the GUI beside it
[ -n "$GUI" ] && GUI=$(cd "$(dirname "$GUI")" && pwd)/$(basename "$GUI")
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
DPID= XPID=
cleanup() {
    [ -n "$DPID" ] && kill "$DPID" 2>/dev/null
    [ -n "$XPID" ] && kill "$XPID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME" "$TMP/bin" "$TMP/emptybin"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
has() { grep -qF -- "$2" "$1" && ok "$3" || { bad "$3 (no '$2')"; sed 's/^/        /' "$1" | head -60; }; }
hasnt() { grep -qF -- "$2" "$1" && bad "$3 (found '$2')" || ok "$3"; }
# js FILE EXPR   a Python expression over d (the file as JSON); true = ok. The expressions are this test's own text.
js() { python3 -I -c 'import json,sys
d = json.load(open(sys.argv[1]))
sys.exit(0 if eval(sys.argv[2]) else 1)' "$1" "$2" 2>/dev/null; }
jsok() { js "$1" "$2" && ok "$3" || { bad "$3 ($2)"; head -c 1500 "$1"; echo; }; }

# ---------------------------------------------------------------- the made-up machine
R=$TMP/root
DRM=$R/sys/class/drm
mkcard() {   # mkcard N VENDOR DEVICE DRIVER PCI
    local d=$DRM/card$1/device
    mkdir -p "$d/power" "$DRM/card$1-DP-1"
    printf '%s\n' "$2" >"$d/vendor"; printf '%s\n' "$3" >"$d/device"; printf '0x1eae\n' >"$d/subsystem_vendor"; printf '0x8811\n' >"$d/subsystem_device"
    printf '0xc0\n' >"$d/revision"; printf 'DRIVER=%s\nPCI_SLOT_NAME=%s\n' "$4" "$5" >"$d/uevent"; printf 'active\n' >"$d/power/runtime_status"
}
OD='OD_SCLK_OFFSET:\n300Mhz\nOD_MCLK:\n0: 676Mhz\n1: 1375MHz\nOD_RANGE:\nSCLK_OFFSET:    -500Mhz       1000Mhz\nMCLK:      97Mhz       1500Mhz\nVDDGFX_OFFSET:    -200mv          0mv\nOD_VDDGFX_OFFSET:\n-110mV\n'
mkcard 0 0x1002 0x7550 amdgpu 0000:03:00.0
printf "$OD" >"$DRM/card0/device/pp_od_clk_voltage"
H=$DRM/card0/device/hwmon/hwmon4
mkdir -p "$H"
printf 'amdgpu\n' >"$H/name"
printf '374000000\n' >"$H/power1_cap"; printf '212000000\n' >"$H/power1_cap_min"; printf '374000000\n' >"$H/power1_cap_max"; printf '304000000\n' >"$H/power1_cap_default"
printf '33000000\n' >"$H/power1_average"; printf 'edge\n' >"$H/temp1_label"; printf '27000\n' >"$H/temp1_input"; printf 'junction\n' >"$H/temp2_label"; printf '31000\n' >"$H/temp2_input"
printf '700\n' >"$H/in0_input"; printf '63000000\n' >"$H/freq1_input"; printf '589\n' >"$H/fan1_input"
mkcard 1 0x10de 0x2684 nvidia 0000:01:00.0
mkcard 2 0x1002 0x73bf amdgpu 0000:0a:00.0      # an older card: clock and voltage points, not offsets
printf 'OD_SCLK:\n0: 500Mhz\n1: 2575Mhz\nOD_MCLK:\n0: 97Mhz\n1: 1000Mhz\nOD_VDDC_CURVE:\n0: 700Mhz 800mV\nOD_RANGE:\nSCLK:     500Mhz       2800Mhz\n' >"$DRM/card2/device/pp_od_clk_voltage"
mkcard 3 0x8086 0x7d55 i915 0000:00:02.0
mkcard 4 0x1002 0x744c amdgpu 0000:0b:00.0      # RDNA 3: a voltage offset, and lowest / highest core clock instead of a clock offset
printf 'OD_SCLK:\n0: 500Mhz\n1: 2900Mhz\nOD_MCLK:\n0: 97Mhz\n1: 1249MHz\nOD_VDDGFX_OFFSET:\n0mV\nOD_RANGE:\nSCLK:     500Mhz       5000Mhz\nMCLK:      97Mhz       1500Mhz\nVDDGFX_OFFSET:    -450mv          0mv\n' >"$DRM/card4/device/pp_od_clk_voltage"
mkdir -p "$DRM/card9/device"                    # a firmware framebuffer: no PCI device, must be skipped
mkdir -p "$R/usr/share/libdrm"
printf '# List of AMDGPU IDs\n1.0.0\n7550,\tC0,\tMade-Up Radeon RX Test\n' >"$R/usr/share/libdrm/amdgpu.ids"
mkdir -p "$R/proc/4242" "$R/proc/77" "$R/run" "$R/etc/lact"
printf 'bash\n' >"$R/proc/77/comm"
lact_on()  { printf 'lact\n' >"$R/proc/4242/comm"; : >"$R/run/lactd.sock"; }
lact_off() { rm -f "$R/proc/4242/comm" "$R/run/lactd.sock"; }
lact_cfg() {   # lact_cfg CURRENT_PROFILE_LINE
    cat >"$R/etc/lact/config.yaml" <<EOF
version: 5
daemon:
  log_level: info
  admin_group: wheel
apply_settings_timer: 5
gpus:
  1002:7550-1EAE:8811-0000:03:00.0:
    fan_control_enabled: true
    power_cap: 304.0
    performance_level: manual
    max_memory_clock: 1258
    voltage_offset: -125
profiles:
  quiet:
    gpus:
      1002:7550-1EAE:8811-0000:03:00.0:
        power_cap: 250.0
        voltage_offset: -60
  '123':
    gpus:
      1002:7550-1EAE:8811-0000:03:00.0:
        fan_control_enabled: true
        power_cap: 374.0
        performance_level: manual
        max_memory_clock: 1375
        voltage_offset: -110
        gpu_clock_offsets:
          0: 300
  empty:
    gpus: {}
$1
EOF
}
cat >"$TMP/bin/tburn-cli" <<'EOF'
#!/bin/sh
echo "fake tburn-cli was RUN: $*" >>"$(dirname "$0")/tburn-cli.log"
exit 0
EOF
chmod +x "$TMP/bin/tburn-cli"
SOCK=$TMP/tb.sock
LOG=$TMP/requests.log
export SRS_TEST_SYSFS_ROOT=$R
NOTB=$TMP/emptybin:/usr/bin:/bin          # a PATH with no tburn-cli (unless the system has one in /usr/bin: checked below)
WITHTB=$TMP/bin:/usr/bin:/bin
T() { "$BIN" gpu-tune "$@"; }             # the caller sets SRS_TUX_BURNER_SOCKET and PATH
daemon() {   # daemon MODE [--caps ...]: start the fake service, wait until it listens
    daemon_stop
    : >"$LOG"
    python3 -I "$HERE/fake-tux-burnerd.py" "$SOCK" "$LOG" "$@" >"$TMP/daemon.out" 2>&1 &
    DPID=$!
    for i in $(seq 1 50); do grep -q ready "$TMP/daemon.out" 2>/dev/null && return 0; sleep 0.1; done
    bad "the fake service did not start"; cat "$TMP/daemon.out"; return 1
}
daemon_stop() { [ -n "$DPID" ] && { kill "$DPID" 2>/dev/null; wait "$DPID" 2>/dev/null; }; DPID=; rm -f "$SOCK"; }
fingerprint() { (cd "$R" && find . -type f -print0 | sort -z | xargs -0 sha256sum; find . | sort) | sha256sum; }
FP0=$(fingerprint)

echo "== guard: in a test with no socket named, the real service is not asked"
env -u SRS_TUX_BURNER_SOCKET PATH="$NOTB" "$BIN" gpu-tune status --json >"$TMP/g.json" 2>"$TMP/g.err"; rc=$?
[ $rc = 0 ] && ok "exit 0" || { bad "exit $rc"; cat "$TMP/g.err"; }
jsok "$TMP/g.json" 'd["tux_burner"]["daemon"] == "not-checked" and "real service is not asked" in d["tux_burner"]["message"]' "a made-up /sys: the service state is not-checked, and it says why"
env -u SRS_TEST_SYSFS_ROOT -u SRS_TUX_BURNER_SOCKET SRS_TEST_PRIVATE_BUS=1 PATH="$NOTB" "$BIN" gpu-tune status >"$TMP/g2" 2>&1; rc=$?
[ $rc = 1 ] && grep -q "test mode without a made-up /sys" "$TMP/g2" && ok "a GUI-test environment with no made-up /sys: nothing real is read (card, service, LACT)" || { bad "test mode without a root: exit $rc"; head -3 "$TMP/g2"; }
# a comma-decimal locale must not change how numbers are read (the GUI runs in the user's locale)
if locale -a 2>/dev/null | grep -qi '^de_DE\.utf-\?8$'; then
    lact_on; lact_cfg "current_profile: null"
    LC_ALL=de_DE.UTF-8 PATH="$NOTB" "$BIN" gpu-tune status --json >"$TMP/loc.json" 2>&1
    jsok "$TMP/loc.json" 'd["lact"]["active_values"]["power_cap_w"] == 304 and d["now"]["voltage_offset_mv"] == -110' "numbers read the same under a comma-decimal locale"
    lact_off; rm -f "$R/etc/lact/config.yaml"
fi
export SRS_TUX_BURNER_SOCKET=$SOCK
if PATH="$NOTB" command -v tburn-cli >/dev/null 2>&1 || PATH="$NOTB" command -v tux-burner-cli >/dev/null 2>&1; then
    # this PC has Tux Burner installed: for the "not installed" checks, a copy of /usr/bin (links) without its two commands
    mkdir -p "$TMP/sysbin"
    find /usr/bin -maxdepth 1 -mindepth 1 ! -name tburn-cli ! -name tux-burner-cli -exec ln -s -t "$TMP/sysbin" {} + 2>/dev/null
    NOTB=$TMP/emptybin:$TMP/sysbin
    echo "  note  this PC has tburn-cli in /usr/bin: the 'not installed' checks use a PATH without it"
fi
PATH="$NOTB" command -v tburn-cli >/dev/null 2>&1 && { bad "cannot hide the system's tburn-cli"; exit 1; }

echo "== no Tux Burner: the card's own values"
lact_off; rm -f "$R/etc/lact/config.yaml"
PATH="$NOTB" T status >"$TMP/s1" 2>&1; rc=$?
[ $rc = 0 ] && ok "status exits 0" || bad "status exit $rc"
has "$TMP/s1" "Made-Up Radeon RX Test" "the card's name from amdgpu.ids"
has "$TMP/s1" "card0, PCI 0000:03:00.0, driver amdgpu" "card, PCI address, driver"
grep -Eq "Voltage Offset +-110 mV" "$TMP/s1" && ok "voltage offset -110 mV" || bad "voltage offset"
grep -Eq "Core Clock Offset +\+300 MHz" "$TMP/s1" && ok "core clock offset +300 MHz" || bad "core clock offset"
grep -Eq "Memory Clock \(Highest\) +1375 MHz" "$TMP/s1" && ok "highest memory clock 1375 MHz" || bad "memory clock"
grep -Eq "Power Limit +374 W" "$TMP/s1" && ok "power limit 374 W" || bad "power limit"
grep -Eq "Driver Range, Voltage Offset +-200 \.\. 0 mV" "$TMP/s1" && ok "driver range, voltage" || bad "driver range voltage"
grep -Eq "Driver Range, Core Clock Offset +-500 \.\. \+1000 MHz" "$TMP/s1" && ok "driver range, core clock" || bad "driver range clock"
grep -Eq "Driver Range, Memory Clock +97 \.\. 1500 MHz" "$TMP/s1" && ok "driver range, memory clock" || bad "driver range memory"
grep -Eq "Driver Range, Power Limit +212 \.\. 374 W \(factory setting 304 W\)" "$TMP/s1" && ok "driver range, power limit and factory setting" || bad "driver range power"
grep -Eq "Temperature +27 C edge, 31 C hotspot" "$TMP/s1" && ok "live temperature" || bad "live temperature"
grep -Eq "Tux Burner +not installed" "$TMP/s1" && ok "Tux Burner: not installed" || bad "Tux Burner line"
has "$TMP/s1" "Changing GPU settings needs Tux Burner, a separate free program that holds the safety limits. Reading works without it." "says what Tux Burner is for"
has "$TMP/s1" "the package tux-burner from the AUR" "how to install it (Arch)"
has "$TMP/s1" "https://github.com/eugenesflanagin-spec/tux-burner" "where to get it (other systems)"
has "$TMP/s1" "kept by Tux Burner (not known without its service)" "the voltage floor is not guessed"
has "$TMP/s1" "4 more graphics cards" "says there are more cards"
hasnt "$TMP/s1" "card9" "a framebuffer with no PCI device is not a card"
PATH="$NOTB" T status --json >"$TMP/s1.json" 2>&1
jsok "$TMP/s1.json" 'd["gpu"]["pci"] == "0000:03:00.0" and d["gpu"]["driver"] == "amdgpu" and d["gpu"]["vendor"] == "amd" and d["gpu"]["overdrive"] == "offsets" and d["gpu"]["backend"] is None' "JSON: the card"
jsok "$TMP/s1.json" 'd["now"] == {"voltage_offset_mv": -110, "sclk_offset_mhz": 300, "sclk_max_mhz": None, "mclk_max_mhz": 1375, "mclk_offset_mhz": None, "power_cap_w": 374}' "JSON: now"
jsok "$TMP/s1.json" 'd["ranges"]["voltage_offset_mv"] == [-200, 0] and d["ranges"]["sclk_offset_mhz"] == [-500, 1000] and d["ranges"]["power_cap_w"] == [212, 374] and d["ranges"]["mclk_mhz"] == [97, 1500]' "JSON: ranges"
jsok "$TMP/s1.json" 'd["limits"] == {"voltage_floor_mv": None, "voltage_floor_source": None, "suggest_floor_mv": None, "sclk_ceiling_mhz": None}' "JSON: limits Tux Burner does not report are null, not guessed"
jsok "$TMP/s1.json" 'd["tux_burner"]["installed"] is False and d["tux_burner"]["daemon"] == "not-running" and d["tux_burner"]["version"] is None' "JSON: Tux Burner absent"
jsok "$TMP/s1.json" 'd["lact"] == {"running": False, "active_profile": None, "active_values": None, "matches_card": None, "config_sha256": None}' "JSON: no LACT"
jsok "$TMP/s1.json" 'd["can_write"] is False and "not installed" in d["why_not"]' "JSON: can_write false, and why"
jsok "$TMP/s1.json" 'd["live"] == {"edge_c": 27, "junction_c": 31, "power_w": 33, "clock_mhz": 63, "vddgfx_mv": 700, "fan_rpm": 589}' "JSON: live readings"

echo "== the other cards"
PATH="$NOTB" T status --all >"$TMP/all" 2>&1
has "$TMP/all" "NVIDIA cards have no settings SRS can read by itself" "NVIDIA without Tux Burner: says so"
has "$TMP/all" "older OverDrive table" "an older AMD card: not supported yet"
has "$TMP/all" "Tuning this kind of card is not supported yet" "another vendor: not supported yet"
PATH="$NOTB" T status --all --json >"$TMP/all.json" 2>&1
jsok "$TMP/all.json" '[x["gpu"]["card"] for x in d] == ["card0", "card1", "card2", "card3", "card4"] and [x["gpu"]["overdrive"] for x in d] == ["offsets", "none", "older-table", "none", "offsets"]' "JSON --all: five cards in order"
PATH="$NOTB" T status --pci 0000:0b:00.0 --json >"$TMP/r3.json" 2>&1
jsok "$TMP/r3.json" 'd["now"]["voltage_offset_mv"] == 0 and d["now"]["sclk_max_mhz"] == 2900 and d["now"]["sclk_offset_mhz"] is None and d["now"]["mclk_max_mhz"] == 1249 and d["ranges"]["voltage_offset_mv"] == [-450, 0] and d["ranges"]["sclk_mhz"] == [500, 5000] and d["ranges"]["sclk_offset_mhz"] is None' "an RDNA 3 table: voltage offset, highest core clock and their ranges"
PATH="$NOTB" T status --pci 0000:0b:00.0 >"$TMP/r3" 2>&1
grep -Eq "Core Clock \(Highest\) +2900 MHz" "$TMP/r3" && grep -Eq "Driver Range, Core Clock +500 \.\. 5000 MHz" "$TMP/r3" && ok "RDNA 3 text: highest core clock and its range" || bad "RDNA 3 text"
PATH="$NOTB" T status --pci 0000:0A:00.0 --json >"$TMP/p.json" 2>&1
jsok "$TMP/p.json" 'd["gpu"]["card"] == "card2" and d["now"]["voltage_offset_mv"] is None' "--pci picks a card (upper case accepted)"
printf 'suspended\n' >"$DRM/card3/device/power/runtime_status"
: >"$DRM/card3/device/pp_od_clk_voltage"
PATH="$NOTB" T status --pci 0000:00:02.0 >"$TMP/sl" 2>&1
has "$TMP/sl" "This card is asleep" "a sleeping card is not read"
PATH="$NOTB" T status --pci 0000:00:02.0 --json >"$TMP/sl.json" 2>&1
jsok "$TMP/sl.json" 'd["gpu"]["asleep"] is True and "asleep" in d["why_not"]' "JSON: asleep"
PATH="$NOTB" T status --pci 0000:00:02.0 --wake --json >"$TMP/sl2.json" 2>&1
jsok "$TMP/sl2.json" 'd["gpu"]["asleep"] is False' "--wake reads it"
# a running service is not asked about the cards while one of them sleeps (the question could wake it)
daemon both || exit 1
PATH="$WITHTB" T status --json >"$TMP/sl3.json" 2>&1
jsok "$TMP/sl3.json" 'd["tux_burner"]["daemon"] == "running" and d["tux_burner"]["serves_this_card"] is False and "asleep" in d["why_not"]' "with a sleeping card the service is asked for its status only"
[ "$(sort -u "$LOG")" = '{"method":"status"}' ] && ok "no gpu_capabilities request was sent" || bad "requests sent: $(sort -u "$LOG" | tr '\n' ' ')"
PATH="$WITHTB" T status --wake --json >"$TMP/sl4.json" 2>&1
jsok "$TMP/sl4.json" 'd["tux_burner"]["serves_this_card"] is True' "--wake asks about the cards"
daemon_stop
printf 'active\n' >"$DRM/card3/device/power/runtime_status"; rm -f "$DRM/card3/device/pp_od_clk_voltage"

echo "== Tux Burner installed, its service not running"
PATH="$WITHTB" T status >"$TMP/s2" 2>&1
grep -Eq "Tux Burner +installed; its service is not running" "$TMP/s2" && ok "says the service is not running" || bad "service line"
has "$TMP/s2" "sudo tux-burner-cli --install-systemd-service" "the command that installs the service"
has "$TMP/s2" "SRS does not install the service itself." "SRS does not install it"
PATH="$WITHTB" T status --json >"$TMP/s2.json" 2>&1
jsok "$TMP/s2.json" 'd["tux_burner"]["installed"] is True and d["tux_burner"]["cli"].endswith("/bin/tburn-cli") and d["tux_burner"]["daemon"] == "not-running" and "not running" in d["why_not"]' "JSON: installed, service not running"
: >"$SOCK.plain"
SRS_TUX_BURNER_SOCKET=$SOCK.plain PATH="$WITHTB" T status --json >"$TMP/s2b.json" 2>&1
jsok "$TMP/s2b.json" 'd["tux_burner"]["daemon"] == "not-running"' "a plain file where the socket should be is not a service"

echo "== the service runs (fake service, today's abilities)"
daemon amd || exit 1
PATH="$WITHTB" T status >"$TMP/s3" 2>&1
grep -Eq "Tux Burner +0\.99\.0 running \(idle\)" "$TMP/s3" && ok "version and state" || bad "Tux Burner line: $(grep 'Tux Burner' "$TMP/s3" | head -1)"
grep -Eq "Settings Back End +Tux Burner AMD \(OverDrive\)" "$TMP/s3" && ok "back end: Tux Burner AMD" || bad "back end line"
grep -Eq "Lowest Allowed Voltage Offset +-150 mV \(Tux Burner's floor, its default; change it in Tux Burner\)" "$TMP/s3" && ok "the voltage floor from the service" || bad "floor line"
has "$TMP/s3" "Searches from SRS need a newer Tux Burner for: the LACT check inside the service, a clock ceiling, the reset watch, tuning sessions with automatic return." "says what a newer Tux Burner must bring"
PATH="$WITHTB" T status --json >"$TMP/s3.json" 2>&1
jsok "$TMP/s3.json" 'd["tux_burner"]["daemon"] == "running" and d["tux_burner"]["version"] == "0.99.0" and d["tux_burner"]["protocol"] == "2.0" and d["tux_burner"]["state"] == "idle" and d["tux_burner"]["serves_this_card"] is True' "JSON: running, version, protocol"
jsok "$TMP/s3.json" 'd["tux_burner"]["missing_capabilities"] == ["amd-lact-gate-v1", "amd-sclk-ceiling-v1", "gpu-reset-events-v1", "tuning-session-v1"]' "JSON: missing abilities"
jsok "$TMP/s3.json" 'd["limits"]["voltage_floor_mv"] == -150 and d["limits"]["voltage_floor_source"] == "default" and d["gpu"]["backend"] == "tux-burner-amd"' "JSON: floor, back end"
jsok "$TMP/s3.json" 'd["can_write"] is False and "newer Tux Burner" in d["why_not"]' "JSON: can_write false until the service has the new abilities"
PATH="$WITHTB" T status --all --json >/dev/null 2>&1
if [ -s "$LOG" ] && ! grep -v -x -e '{"method":"status"}' -e '{"method":"gpu_capabilities","gpu_index":[0-9]*}' "$LOG" | grep -q .; then ok "SRS sent only status and gpu_capabilities ($(wc -l <"$LOG") requests)"
else bad "SRS sent something else to the service:"; grep -v -x -e '{"method":"status"}' -e '{"method":"gpu_capabilities","gpu_index":[0-9]*}' "$LOG" | head -5; fi

echo "== the service has every ability a search needs"
daemon amd --caps amd-lact-gate-v1,amd-sclk-ceiling-v1,gpu-reset-events-v1,tuning-session-v1 || exit 1
PATH="$WITHTB" T status --json >"$TMP/s4.json" 2>&1
jsok "$TMP/s4.json" 'd["tux_burner"]["missing_capabilities"] == [] and d["can_write"] is True and d["why_not"] == ""' "JSON: nothing missing, can_write true"
PATH="$NOTB" T status --json >"$TMP/s4b.json" 2>&1
jsok "$TMP/s4b.json" 'd["can_write"] is False and "tburn-cli" in d["why_not"]' "service there, tburn-cli missing: can_write false"

echo "== NVIDIA through Tux Burner's NVIDIA back end"
daemon both || exit 1
PATH="$WITHTB" T status --pci 0000:01:00.0 >"$TMP/nv" 2>&1
grep -Eq "Settings Back End +Tux Burner NVIDIA" "$TMP/nv" && ok "back end: Tux Burner NVIDIA" || bad "NVIDIA back end line"
grep -Eq "Core Clock Offset +\+125 MHz" "$TMP/nv" && grep -Eq "Memory Clock Offset +\+500 MHz" "$TMP/nv" && grep -Eq "Power Limit +320 W" "$TMP/nv" && ok "clock offsets and power limit from the service" || bad "NVIDIA values"
hasnt "$TMP/nv" "Lowest Allowed Voltage Offset" "no AMD voltage floor on an NVIDIA card"
PATH="$WITHTB" T status --pci 0000:01:00.0 --json >"$TMP/nv.json" 2>&1
jsok "$TMP/nv.json" 'd["tux_burner"]["missing_capabilities"] == ["gpu-reset-events-v1", "tuning-session-v1"]' "an NVIDIA card does not need the AMD-only abilities"
jsok "$TMP/nv.json" 'd["gpu"]["backend"] == "tux-burner-nvidia" and d["now"]["sclk_offset_mhz"] == 125 and d["now"]["mclk_offset_mhz"] == 500 and d["now"]["power_cap_w"] == 320 and d["ranges"]["power_cap_w"] == [150, 600] and d["limits"]["voltage_floor_mv"] is None' "JSON: NVIDIA card"
PATH="$WITHTB" T status --pci 0000:03:00.0 --json >"$TMP/nv2.json" 2>&1
jsok "$TMP/nv2.json" 'd["gpu"]["backend"] == "tux-burner-amd"' "the AMD card is matched to the service's other index"
daemon nocard || exit 1
PATH="$WITHTB" T status --json >"$TMP/nc.json" 2>&1
jsok "$TMP/nc.json" 'd["tux_burner"]["daemon"] == "running" and d["tux_burner"]["serves_this_card"] is False and "no controls for this card" in d["why_not"]' "a service with no card: said plainly"

echo "== the service refuses, is old, is silent, or talks nonsense"
daemon denied || exit 1
PATH="$WITHTB" T status >"$TMP/dn" 2>&1
has "$TMP/dn" 'Tux Burner'"'"'s service answered: "daemon client uid is not allowed".' "the service's own message is shown"
has "$TMP/dn" "install the service again" "with the fix"
PATH="$WITHTB" T status --json >"$TMP/dn.json" 2>&1
jsok "$TMP/dn.json" 'd["tux_burner"]["daemon"] == "not-allowed" and d["can_write"] is False' "JSON: not-allowed"
daemon old || exit 1
PATH="$WITHTB" T status >"$TMP/old" 2>&1
has "$TMP/old" "the service speaks protocol 1.0; this SRS knows protocol 2" "another protocol version: said plainly"
PATH="$WITHTB" T status --json >"$TMP/old.json" 2>&1
jsok "$TMP/old.json" 'd["tux_burner"]["daemon"] == "unusable-version" and d["tux_burner"]["protocol"] == "1.0"' "JSON: unusable-version"
daemon silent || exit 1
t0=$(date +%s.%N)
PATH="$WITHTB" T status --json >"$TMP/si.json" 2>&1
t1=$(date +%s.%N)
jsok "$TMP/si.json" 'd["tux_burner"]["daemon"] == "no-answer" and "did not answer" in d["tux_burner"]["message"] and d["now"]["voltage_offset_mv"] == -110' "a service that never answers: no-answer, the card's values still shown"
python3 -I -c "import sys; sys.exit(0 if $t1 - $t0 < 3 else 1)" && ok "and SRS does not wait long ($(python3 -I -c "print(round($t1 - $t0, 1))") s)" || bad "waited too long for a silent service"
daemon garbage || exit 1
PATH="$WITHTB" T status --json >"$TMP/ga.json" 2>&1
jsok "$TMP/ga.json" 'd["tux_burner"]["daemon"] == "no-answer"' "an answer that is not JSON: no usable answer"
daemon_stop
[ ! -s "$TMP/bin/tburn-cli.log" ] && ok "tburn-cli was never run (status only looks for it)" || bad "tburn-cli was run: $(cat "$TMP/bin/tburn-cli.log")"

echo "== LACT"
lact_on; lact_cfg "current_profile: '123'"
PATH="$NOTB" T status >"$TMP/l1" 2>&1
grep -Eq 'LACT +running, active profile "123"' "$TMP/l1" && ok "LACT running, active profile named" || bad "LACT line: $(grep LACT "$TMP/l1" | head -1)"
grep -Eq 'Set By +LACT profile "123" \(the card is at LACT'"'"'s saved values\)' "$TMP/l1" && ok "set by LACT profile 123" || bad "Set By line"
has "$TMP/l1" "LACT is running and owns this card's settings. SRS will not change LACT's saved settings." "the LACT line"
has "$TMP/l1" 'LACT'"'"'s Saved Values (profile "123")' "LACT's saved values are shown"
PATH="$NOTB" T status --json >"$TMP/l1.json" 2>&1
jsok "$TMP/l1.json" 'd["lact"]["running"] is True and d["lact"]["active_profile"] == "123" and d["lact"]["active_values"] == {"voltage_offset_mv": -110, "sclk_offset_mhz": 300, "mclk_max_mhz": 1375, "power_cap_w": 374} and d["lact"]["matches_card"] is True' "JSON: active profile values, matches the card"
jsok "$TMP/l1.json" 'd["lact"]["config_sha256"] == "'"$(sha256sum "$R/etc/lact/config.yaml" | cut -d' ' -f1)"'"' "JSON: the settings file's SHA-256"
sed -i 's/^-110mV$/-90mV/' "$DRM/card0/device/pp_od_clk_voltage"
PATH="$NOTB" T status >"$TMP/l2" 2>&1
has "$TMP/l2" "The card is not at these values right now" "the card differs from LACT's saved values: said in orange"
PATH="$NOTB" T status --json >"$TMP/l2.json" 2>&1
jsok "$TMP/l2.json" 'd["now"]["voltage_offset_mv"] == -90 and d["lact"]["matches_card"] is False' "JSON: matches_card false"
printf "$OD" >"$DRM/card0/device/pp_od_clk_voltage"
lact_cfg "current_profile: quiet"
PATH="$NOTB" T status --json >"$TMP/l3.json" 2>&1
jsok "$TMP/l3.json" 'd["lact"]["active_profile"] == "quiet" and d["lact"]["active_values"] == {"voltage_offset_mv": -60, "sclk_offset_mhz": None, "mclk_max_mhz": None, "power_cap_w": 250} and d["lact"]["matches_card"] is False' "an unquoted profile name; only its own values"
lact_cfg "current_profile: null"
PATH="$NOTB" T status --json >"$TMP/l4.json" 2>&1
jsok "$TMP/l4.json" 'd["lact"]["active_profile"] is None and d["lact"]["active_values"]["voltage_offset_mv"] == -125 and d["lact"]["active_values"]["power_cap_w"] == 304' "no profile selected: the base settings"
lact_cfg "current_profile: empty"
PATH="$NOTB" T status --json >"$TMP/l5.json" 2>&1
jsok "$TMP/l5.json" 'd["lact"]["active_profile"] == "empty" and d["lact"]["active_values"] is None and d["lact"]["matches_card"] is None' "a profile with nothing for this card: the base values are not shown as if in force"
lact_off
PATH="$NOTB" T status >"$TMP/l6" 2>&1
grep -Eq "LACT +not running \(settings file found\)" "$TMP/l6" && ok "LACT not running, its file still read" || bad "LACT off line"
hasnt "$TMP/l6" "LACT is running and owns" "no LACT line when it is not running"
rm -f "$R/etc/lact/config.yaml"

echo "== the [gpu-tuning] item (what the tab shows)"
PATH="$NOTB" "$BIN" '-save=[gpu-tuning]=STDOUT' >"$TMP/item" 2>&1
has "$TMP/item" "GPU Tuning (read-only" "the item renders"
has "$TMP/item" "Made-Up Radeon RX Test" "with the AMD card"
has "$TMP/item" "NVIDIA cards have no settings SRS can read by itself" "and every other card"
"$BIN" --list 2>/dev/null | grep -q "^gpu-tuning" && ok "listed by --list" || bad "not in --list"

echo "== command line"
PATH="$NOTB" T >"$TMP/u0" 2>&1;                       [ $? = 2 ] && grep -q "usage:" "$TMP/u0" && ok "no sub-command: usage, exit 2" || bad "no sub-command"
PATH="$NOTB" T status --bogus >"$TMP/u1" 2>&1;        [ $? = 2 ] && ok "unknown option: exit 2" || bad "unknown option"
PATH="$NOTB" T status --pci 0000:7f:00.0 >"$TMP/u2" 2>&1; [ $? = 2 ] && grep -q "no graphics card at" "$TMP/u2" && ok "--pci with no such card: exit 2" || bad "--pci no such card"
PATH="$NOTB" T status --pci nonsense >"$TMP/u3" 2>&1;  [ $? = 2 ] && ok "--pci with a wrong address: exit 2" || bad "--pci wrong address"
PATH="$NOTB" T status --all --pci 0000:03:00.0 >"$TMP/u4" 2>&1; [ $? = 2 ] && ok "--all with --pci: exit 2" || bad "--all + --pci"
for sub in apply auto-uv auto-oc efficiency restore plan profiles export-lact; do
    PATH="$WITHTB" T $sub --yes >"$TMP/u5" 2>&1; rc=$?
    [ $rc = 2 ] && grep -q "not in this version" "$TMP/u5" || { bad "gpu-tune $sub: exit $rc"; continue; }
done
ok "apply, auto-uv, auto-oc, efficiency, restore, plan, profiles, export-lact: not in this version, exit 2, nothing run"
PATH="$NOTB" T --help >"$TMP/u6" 2>&1;                [ $? = 0 ] && grep -q "never changes" "$TMP/u6" && ok "--help" || bad "--help"
E=$TMP/empty; mkdir -p "$E/sys/class/drm"
SRS_TEST_SYSFS_ROOT=$E PATH="$NOTB" T status >"$TMP/u7" 2>&1; [ $? = 1 ] && grep -q "no graphics card found" "$TMP/u7" && ok "no graphics card: exit 1, says so" || bad "no graphics card"

echo "== a PC with no AMD card and the service running (2.6.0: SRS read the service's answer after freeing it)"
# One NVIDIA card, no amdgpu.ids: nothing large is read before the service's answer, which is the case that crashed.
# GLIBC_TUNABLES makes glibc hand every larger block back to the system on free(), so a read after free() is a crash
# on any machine and with any earlier allocations, not only on this layout.
NV=$TMP/nvroot
mkdir -p "$NV/sys/class/drm/card0/device/power" "$NV/proc" "$NV/run" "$NV/etc"
printf '0x10de\n' >"$NV/sys/class/drm/card0/device/vendor"; printf '0x2684\n' >"$NV/sys/class/drm/card0/device/device"
printf '0x10de\n' >"$NV/sys/class/drm/card0/device/subsystem_vendor"; printf '0x0001\n' >"$NV/sys/class/drm/card0/device/subsystem_device"
printf '0xa1\n' >"$NV/sys/class/drm/card0/device/revision"; printf 'DRIVER=nvidia\nPCI_SLOT_NAME=0000:01:00.0\n' >"$NV/sys/class/drm/card0/device/uevent"
printf 'active\n' >"$NV/sys/class/drm/card0/device/power/runtime_status"
daemon nvidia || exit 1
for tun in "" glibc.malloc.mmap_threshold=65536; do
    SRS_TEST_SYSFS_ROOT=$NV PATH="$WITHTB" env ${tun:+GLIBC_TUNABLES=$tun} "$BIN" gpu-tune status --json >"$TMP/nvonly.json" 2>"$TMP/nvonly.err"; rc=$?
    [ $rc = 0 ] && ok "NVIDIA-only PC, service running${tun:+, $tun}: exit 0" || { bad "NVIDIA-only PC${tun:+, $tun}: exit $rc"; head -5 "$TMP/nvonly.err"; }
    jsok "$TMP/nvonly.json" 'd["gpu"]["vendor"] == "nvidia" and d["tux_burner"]["daemon"] == "running" and d["gpu"]["backend"] == "tux-burner-nvidia" and d["now"]["power_cap_w"] == 320' "and the JSON is whole: the NVIDIA card through the service"
    SRS_TEST_SYSFS_ROOT=$NV PATH="$WITHTB" env ${tun:+GLIBC_TUNABLES=$tun} "$BIN" gpu-tune status >"$TMP/nvonly.txt" 2>&1; rc=$?
    [ $rc = 0 ] && grep -Eq "Settings Back End +Tux Burner NVIDIA" "$TMP/nvonly.txt" && ok "the text form too" || bad "NVIDIA-only text: exit $rc"
    SRS_TEST_SYSFS_ROOT=$NV PATH="$WITHTB" env ${tun:+GLIBC_TUNABLES=$tun} "$BIN" '-save=[gpu-tuning]=STDOUT' >"$TMP/nvonly.item" 2>&1; rc=$?
    [ $rc = 0 ] && grep -q "Tux Burner NVIDIA" "$TMP/nvonly.item" && ok "the [gpu-tuning] item too" || bad "NVIDIA-only item: exit $rc"
done
daemon old || exit 1
SRS_TEST_SYSFS_ROOT=$NV PATH="$WITHTB" env GLIBC_TUNABLES=glibc.malloc.mmap_threshold=65536 "$BIN" gpu-tune status --json >"$TMP/nvold.json" 2>&1; rc=$?
[ $rc = 0 ] && js "$TMP/nvold.json" 'd["tux_burner"]["daemon"] == "unusable-version"' && ok "an old service on the NVIDIA-only PC: exit 0, unusable-version" || bad "old service, NVIDIA-only: exit $rc"
daemon_stop

echo "== the JSON stays valid whatever the service and LACT hand over"
# strict: the bytes must be UTF-8 and the text must parse (python's json.load(open()) could be lenient about bytes)
jstrict() { python3 -I -c 'import json,sys
d = json.loads(open(sys.argv[1], "rb").read().decode("utf-8", "strict"))
sys.exit(0 if eval(sys.argv[2]) else 1)' "$1" "$2" 2>"$TMP/jstrict.err"; }
daemon hostile || exit 1
PATH="$WITHTB" T status --json >"$TMP/h.json" 2>&1; rc=$?
[ $rc = 0 ] && ok "a service with quotes, backslashes, control characters and non-UTF-8 bytes in its words: exit 0" || bad "hostile service: exit $rc"
jstrict "$TMP/h.json" 'd["gpu"]["backend"] == "tux-burner-a\"b\\c d\ufffde\ufffd"' && ok "the vendor word is escaped (the tab was already made a space), bad bytes become U+FFFD" || { bad "hostile vendor word"; cat "$TMP/jstrict.err"; head -c 600 "$TMP/h.json"; echo; }
jstrict "$TMP/h.json" 'd["tux_burner"]["version"] == "0.99.0x�"' && ok "a letter cut in the middle becomes U+FFFD" || { bad "cut letter"; cat "$TMP/jstrict.err"; }
PATH="$WITHTB" T status --all --json >"$TMP/h2.json" 2>&1
jstrict "$TMP/h2.json" 'len(d) == 5' && ok "--all --json is valid too" || { bad "hostile --all"; cat "$TMP/jstrict.err"; }
daemon_stop
# a LACT profile name of 40 two-byte letters: SRS keeps 63 bytes, which cuts the 32nd letter in half
lact_on; lact_cfg "current_profile: $(printf 'é%.0s' $(seq 1 40))"
PATH="$NOTB" T status --json >"$TMP/h3.json" 2>&1
jstrict "$TMP/h3.json" 'd["lact"]["active_profile"].startswith("ééé") and "�" in d["lact"]["active_profile"]' && ok "a LACT profile name cut in mid-letter: valid JSON, the half letter is U+FFFD" || { bad "cut LACT profile name"; cat "$TMP/jstrict.err"; head -c 400 "$TMP/h3.json"; echo; }
printf 'current_profile: "a\\"b\xff"\n' >"$TMP/lact-tail"; lact_cfg "$(cat "$TMP/lact-tail")"
PATH="$NOTB" T status --json >"$TMP/h4.json" 2>&1
jstrict "$TMP/h4.json" 'isinstance(d["lact"], dict)' && ok "a LACT profile name with a quote and a non-UTF-8 byte: valid JSON" || { bad "hostile LACT profile name"; cat "$TMP/jstrict.err"; head -c 400 "$TMP/h4.json"; echo; }
lact_off; rm -f "$R/etc/lact/config.yaml"

echo "== nothing was written"
[ "$(fingerprint)" = "$FP0" ] && ok "the made-up /sys, /proc, /run and /etc are byte for byte as before" || bad "SRS changed the made-up tree"
[ ! -s "$TMP/bin/tburn-cli.log" ] && ok "tburn-cli was never run" || bad "tburn-cli was run"

# ---------------------------------------------------------------- the tab, under Xvfb
if [ -n "$GUI" ] && [ -x "$GUI" ] && command -v Xvfb >/dev/null; then
    echo "== GUI: the GPU Tuning tab (Xvfb, private bus)"
    case "${DBUS_SESSION_BUS_ADDRESS:-}" in *"$TMP"*|*nonexistent*|unix:path=/tmp/dbus-*|unix:abstract=*) ok "the session bus is a private one" ;; *) echo "  note  bus address: ${DBUS_SESSION_BUS_ADDRESS:-unset}" ;; esac
    mkdir -p "$XDG_CONFIG_HOME/srs"
    lact_on; lact_cfg "current_profile: '123'"
    daemon amd || exit 1
    d=:$((70 + RANDOM % 9))
    Xvfb "$d" -screen 0 1500x1100x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 & XPID=$!
    sleep 1
    gui() {   # gui TAG THEME-DARK(0/1) -> the tab's text in $TMP/gui-TAG.txt
        local tag=$1 dark=$2
        rm -f "$TMP/gui-$tag.txt"
        printf '[SRSDEF]\nTheme=%s\n[Tabs]\nLineup=Expert\n' "$([ "$dark" = 1 ] && echo Dark || echo Light)" >"$XDG_CONFIG_HOME/srs/SRSDEF.ini"   # the theme; no first-start question
        env -u WAYLAND_DISPLAY -u GTK_THEME GDK_BACKEND=x11 DISPLAY=$d PATH="$WITHTB" \
            ${SRS_SHOT_DIR:+SRS_TEST_TAB_HOLD=1} SRS_TEST_TAB_DUMP="$TMP/gui-$tag.txt" "$GUI" --gui-new --gui-open=gputune >"$TMP/gui-$tag.log" 2>&1 &
        local G=$!
        for i in $(seq 1 80); do [ -s "$TMP/gui-$tag.txt" ] && break; sleep 0.25; done
        sleep 0.3
        [ -n "${SRS_SHOT_DIR:-}" ] && command -v import >/dev/null && import -display "$d" -window root "$SRS_SHOT_DIR/gpu-tuning-$tag.png" 2>/dev/null
        kill $G 2>/dev/null; wait $G 2>/dev/null
    }
    for th in light dark; do
        gui $th $([ $th = dark ] && echo 1 || echo 0)
        if [ ! -s "$TMP/gui-$th.txt" ]; then bad "$th: the tab did not open"; tail -5 "$TMP/gui-$th.log"; continue; fi
        has "$TMP/gui-$th.txt" "page=Benchmarks & Stress" "$th: it is on the Benchmarks & Stress page"
        has "$TMP/gui-$th.txt" "tab=GPU Tuning" "$th: the tab is called GPU Tuning"
        has "$TMP/gui-$th.txt" "after=Stress & Burnin" "$th: placed right after Stress & Burnin"
        has "$TMP/gui-$th.txt" "Made-Up Radeon RX Test" "$th: shows the card"
        has "$TMP/gui-$th.txt" "0.99.0 running (idle)" "$th: shows Tux Burner"
        has "$TMP/gui-$th.txt" "LACT is running and owns this card's settings" "$th: the LACT line"
        has "$TMP/gui-$th.txt" "buttons=!resetview,!refresh,!copy,!guide,!pop" "$th: the only buttons are Reset view, Refresh, Copy, Guide, Open in window (none changes anything)"
    done
    daemon_stop
    printf '[SRSDEF]\nTheme=Light\n[Tabs]\nLineup=Basic\n' >"$XDG_CONFIG_HOME/srs/SRSDEF.ini"   # the smallest lineup: the tab is there too
    env -u WAYLAND_DISPLAY -u GTK_THEME GDK_BACKEND=x11 DISPLAY=$d PATH="$NOTB" ${SRS_SHOT_DIR:+SRS_TEST_TAB_HOLD=1} SRS_TEST_TAB_DUMP="$TMP/gui-absent.txt" "$GUI" --gui-new --gui-open=gputune >"$TMP/gui-absent.log" 2>&1 &
    G=$!
    for i in $(seq 1 80); do [ -s "$TMP/gui-absent.txt" ] && break; sleep 0.25; done
    sleep 0.3
    [ -n "${SRS_SHOT_DIR:-}" ] && command -v import >/dev/null && import -display "$d" -window root "$SRS_SHOT_DIR/gpu-tuning-absent.png" 2>/dev/null
    kill $G 2>/dev/null; wait $G 2>/dev/null
    [ -s "$TMP/gui-absent.txt" ] || { echo "        (the tab did not open; the GUI's last lines:)"; tail -5 "$TMP/gui-absent.log" | sed 's/^/        /'; }
    has "$TMP/gui-absent.txt" "Changing GPU settings needs Tux Burner" "Tux Burner absent: the tab says what to install"
    has "$TMP/gui-absent.txt" "after=Stress & Burnin" "the Basic lineup has the tab too, right after Stress & Burnin"
    # the columns line up (tests/columns-test.py reads the x position of every cell)
    lact_on; lact_cfg "current_profile: '123'"
    daemon both || exit 1
    env -u WAYLAND_DISPLAY -u GTK_THEME GDK_BACKEND=x11 DISPLAY=$d PATH="$WITHTB" SRS_TEST_ALIGN_DUMP="$TMP/align.tsv" SRS_TEST_ALIGN_IDS=gpu-tuning \
        timeout 60 "$GUI" --gui-new >"$TMP/gui-align.log" 2>&1
    if python3 -I "$HERE/columns-test.py" "$TMP/align.tsv" >"$TMP/align.out" 2>&1 && grep -q "^OK  *gpu-tuning" "$TMP/align.out"; then ok "every column of the page lines up ($(tail -1 "$TMP/align.out"))"
    else bad "columns"; cat "$TMP/align.out" | head -5; fi
    daemon_stop
    if ! grep -v -x -e '{"method":"status"}' -e '{"method":"gpu_capabilities","gpu_index":[0-9]*}' "$LOG" | grep -q .; then ok "the GUI sent only status and gpu_capabilities ($(wc -l <"$LOG") requests)"; else bad "the GUI sent something else to the service"; fi
    [ "$(lact_off; rm -f "$R/etc/lact/config.yaml"; fingerprint)" = "$FP0" ] && ok "the GUI wrote nothing into the made-up tree" || bad "the GUI changed the made-up tree"
    kill "$XPID" 2>/dev/null; XPID=
else echo "  skip  GUI part (pass SRS-GUI as the second argument; needs Xvfb)"; fi

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
