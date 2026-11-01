#!/usr/bin/env bash
# tests/gpu-top-test.sh - GPU Top (gpu_top*.c, CLI gpu-top): device list, Device Info, gpu_metrics decode (v1.3,
# v2.2, v3.0), GRBM sampling, per-process fdinfo percentages, NVIDIA through NVML, pp_table, JSON shapes and the
# command-line checks, all on made-up data.
#
#   tests/gpu-top-test.sh SYSREPORT-SEARCH      (make test-bench passes the path)
#
# SAFETY: no GPU is touched. SRS_TEST_GPU_ROOT points every sysfs / proc read at tests/gpu_top_fixtures/multi (one
# real RX 9070 XT capture + made-up GPUs, see make-multi.py); the AMDGPU ioctl answers and the GRBM samples come from
# files there, nothing opens /dev/dri. NVIDIA data comes from tests/nvml_stub.c built here (SRS_TEST_NVML_LIB); with a
# test root SRS never loads the system's libnvidia-ml (on this kind of PC it may be ZLUDA's). Throw-away HOME.
set -u
BIN=${1:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH [SRS-GUI]"; exit 2; }
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
has() { grep -qF -- "$2" "$1" && ok "$3" || { bad "$3 (no '$2')"; sed 's/^/        /' "$1" | head -40; }; }
json_ok() { python3 -I -c 'import json,sys; [json.loads(l) for l in open(sys.argv[1]) if l.strip()]' "$1" 2>/dev/null; }

CC=/usr/bin/gcc
[ -x "$CC" ] || CC=cc
"$CC" -shared -fPIC -O1 -o "$TMP/libnvml_stub.so" "$HERE/nvml_stub.c" || { bad "build the NVML stub"; exit 1; }
FIX=$TMP/multi
cp -a "$HERE/gpu_top_fixtures/multi" "$FIX"
export SRS_TEST_GPU_ROOT=$FIX SRS_TEST_NVML_LIB=$TMP/libnvml_stub.so
G() { "$BIN" gpu-top "$@"; }

echo "== device list"
G --list > "$TMP/list" 2>&1; rc=$?
[ $rc = 0 ] && ok "--list exits 0" || bad "--list exit $rc"
[ "$(grep -c '^#[0-9]' "$TMP/list")" = 5 ] && ok "five GPUs, sorted by PCI address" || bad "five GPUs: $(grep '^#' "$TMP/list")"
has "$TMP/list" "#1: AMD Radeon RX 9070 XT (0000:03:00.0)" "AMD name from amdgpu.ids"
has "$TMP/list" "#2: AMD Radeon RX 6800 (0000:0a:00.0)" "second AMD card"
has "$TMP/list" "vendor NVIDIA (0x10de)" "NVIDIA card found by its PCI vendor"
G --list -J > "$TMP/list.json" && json_ok "$TMP/list.json" && ok "--list -J is JSON" || bad "--list -J is not JSON"

echo "== Device Info (--dump)"
G --dump --device 1 > "$TMP/dump" 2>&1 || bad "--dump exit $?"
has "$TMP/dump" "ASIC Name : GFX1201" "ASIC name from family + external revision"
has "$TMP/dump" "Chip Class: GFX12" "chip class"
has "$TMP/dump" "RenderBackendPlus (RB+)    :  16 (128 ROPs)" "RB+ and ROPs"
has "$TMP/dump" "Peak FP32: 39321 GFLOPS" "peak FP32 (CU x 128 x 2 x MHz)"
has "$TMP/dump" "Peak Memory BW       : 644 GB/s" "peak memory bandwidth (GDDR6)"
has "$TMP/dump" "L3 Cache             :   64 MiB" "Infinity Cache from the firmware (RDNA 4)"
has "$TMP/dump" "L2 Cache             : 8192 KiB (32 Banks)" "L2"
has "$TMP/dump" "PCIe Link Speed (DPM, Min-Max): Gen1x16 - Gen5x16" "PCIe DPM range"
has "$TMP/dump" "PCIe Link Speed (System, Max) : Gen5x16" "system PCIe port found above AMD's switch"
has "$TMP/dump" "Supported Power Profiles: [\"BOOTUP_DEFAULT\", \"3D_FULL_SCREEN\", \"POWER_SAVING\", \"VIDEO\", \"VR\", \"COMPUTE\", \"CUSTOM\", \"WINDOW_3D\"]" "power profiles (no sub-lines)"
has "$TMP/dump" "GFX_ME   feature:  29, ver:      0xC26" "firmware line as amdgpu_top prints it"
has "$TMP/dump" "HEVC      :    8192x4352 (Decode),    8192x4352 (Encode)" "video caps"
has "$TMP/dump" "Unknown(273) num: 1, ver:   1.0.3" "IP discovery, unknown IP"
has "$TMP/dump" "name   : [Navi48 XTX G29201 16GB]" "VBIOS"
has "$TMP/dump" "GPU Metrics Version: v1.3" "gpu_metrics version"
G --dump --device 2 > "$TMP/dump2" 2>&1
has "$TMP/dump2" "ASIC Name : Sienna Cichlid/Navi21" "Navi 21 ASIC"
has "$TMP/dump2" "L3 Cache             :  128 MiB" "Infinity Cache from the ASIC table (16 channels x 8 MiB)"
has "$TMP/dump2" "GPU Clock (OD Max): 2800 MHz" "overdrive limit from pp_od_clk_voltage"
G --dump --device 3 > "$TMP/dump3" 2>&1
has "$TMP/dump3" "GPU Type  : APU" "APU flag"
G --dump -J > "$TMP/dump.json" && json_ok "$TMP/dump.json" && ok "--dump -J is JSON" || bad "--dump -J is not JSON"
python3 -I - "$TMP/dump.json" <<'EOF' && ok "--dump -J has amdgpu_top's keys" || bad "--dump -J keys"
import json, sys
d = json.load(open(sys.argv[1]))
amd = [x for x in d if x.get("ASIC Name") == "GFX1201"][0]
for k in ("DeviceName", "PCI", "GPU Type", "Chip Class", "Total Compute Unit", "VRAM Type", "L3 Cache", "PCIe Link",
          "Hardware IP info", "Firmware info", "IP Discovery table", "Video Caps", "VBIOS", "Sensors", "gpu_metrics", "gpu_activity"):
    assert k in amd, k
assert amd["L3 Cache"] == 64 << 20 and amd["Total ROP"] == 128
assert amd["Video Caps"]["AV1"]["Decode"] == {"width": 8192, "height": 4352}
EOF

echo "== gpu_metrics (v1.3 real, v2.2 and v3.0 made up)"
G --gpu-metrics --device 1 > "$TMP/gm1"
has "$TMP/gm1" "GPU Metrics v1.3 (120 bytes)" "v1.3 header"
has "$TMP/gm1" "voltage_gfx" "v1.3 voltages"
G --gpu-metrics --device 2 > "$TMP/gm2"
has "$TMP/gm2" "Throttle Status             : [PPT0, TDC_GFX]" "indep_throttle_status decoded, TEMP_HOTSPOT below 90 C dropped"
G --gpu-metrics --device 3 > "$TMP/gm3"
has "$TMP/gm3" "GPU Metrics v2.2" "v2.2 header"
has "$TMP/gm3" "temperature_core            : [6100, 6200, 6000, 5900, 6300, 6400, 6000, 6100]" "v2.2 core temperatures"
has "$TMP/gm3" "average_gfx_activity        : 42.00 %" "v2.2 activity in 0.01 %"
has "$TMP/gm3" "Throttle Status             : [FPPT]" "v2.2 throttle"
G --gpu-metrics --device 4 > "$TMP/gm4"
has "$TMP/gm4" "GPU Metrics v3.0 (264 bytes)" "v3.0 header"
has "$TMP/gm4" "average_socket_power        : 21000 mW" "v3.0 power in mW"
has "$TMP/gm4" "average_ipu_activity        : [10, 20, 30, 40, 0, 0, 0, 0]" "v3.0 NPU columns"
G --gpu-metrics -J > "$TMP/gm.json" && json_ok "$TMP/gm.json" && ok "--gpu-metrics -J is JSON" || bad "--gpu-metrics -J is not JSON"
python3 -I - "$TMP/gm.json" <<'EOF' && ok "--gpu-metrics -J: null for missing fields, header, throttle list" || bad "--gpu-metrics -J content"
import json, sys
d = json.load(open(sys.argv[1]))
assert len(d) == 4, len(d)   # the four AMD cards; NVIDIA has no gpu_metrics
m = {x["device_path"]["pci"]: x["gpu_metrics"] for x in d}
assert m["0000:03:00.0"]["header"] == {"structure_size": 120, "format_revision": 1, "content_revision": 3}
assert m["0000:03:00.0"]["average_cpu_power"] is None
assert m["0000:0a:00.0"]["Throttle Status"] == ["PPT0", "TDC_GFX"]
assert m["0000:0e:00.0"]["average_ipu_activity"] == [10, 20, 30, 40, 0, 0, 0, 0]
assert m["0000:0c:00.0"]["current_l3clk"] == [4000, 4000]
EOF

echo "== activity, GRBM sampling and fdinfo (two samples, 1 s apart)"
G -J -n 2 --interval 1000 --pc > "$TMP/stream.json" 2>"$TMP/stream.err"; rc=$?
[ $rc = 0 ] && [ "$(wc -l < "$TMP/stream.json")" = 2 ] && ok "-J -n 2: two samples" || bad "-J -n 2 exit $rc, $(wc -l < "$TMP/stream.json") lines"
json_ok "$TMP/stream.json" && ok "each sample is JSON" || bad "the samples are not JSON"
python3 -I - "$TMP/stream.json" <<'EOF' && ok "GRBM from the samples, fdinfo %, NVML fields, activity scaling" || bad "sample content"
import json, sys
lines = [json.loads(l) for l in open(sys.argv[1])]
first, last = lines[0]["devices"], lines[-1]["devices"]
dev = lambda s, pci: [d for d in s if d["Info"]["PCI"] == pci][0]
navi = dev(first, "0000:0a:00.0")
p = navi["fdinfo"]["1004"]["usage"]
assert p["GFX"]["value"] == 30 and p["Compute"]["value"] == 10, p      # (5.3 - 5.0) s of 1 s, (1.1 - 1.0) s
assert p["VRAM"]["value"] == 1024
assert navi["gpu_activity"]["GFX"]["value"] == 88
# GRBM: the first sample had an idle GPU (no reads, as amdgpu_top); the second one reads srs-grbm.txt
g = dev(last, "0000:0a:00.0")["GRBM"]
assert g["Graphics Pipe"]["value"] == 50 and g["Texture Pipe"]["value"] == 50 and g["Color Block"]["value"] == 0, g
g2 = dev(last, "0000:0a:00.0")["GRBM2"]
assert g2["Command Processor - Graphics"]["value"] == 25, g2
rx = dev(first, "0000:03:00.0")
assert rx["fdinfo"]["1001"]["usage"]["GFX"]["value"] == 25, rx["fdinfo"]["1001"]
assert rx["fdinfo"]["1001"]["usage"]["VCN_Unified"] is not None and rx["fdinfo"]["1001"]["usage"]["Decode"] is None  # VCN 5: unified
apu = dev(first, "0000:0c:00.0")
assert apu["gpu_activity"]["GFX"]["value"] == 42      # v2.x: 0.01 % -> %
strix = dev(first, "0000:0e:00.0")
assert strix["gpu_activity"]["GFX"]["value"] == 15 and strix["npu_metrics"]["npu_power"]["value"] == 900
nv = dev(first, "0000:01:00.0")
assert nv["Info"]["DeviceName"] == "NVIDIA GeForce RTX 4090 (stub)"
assert nv["GRBM"] is None and nv["GRBM2"] is None      # no NVML equivalent
assert nv["gpu_activity"]["GFX"]["value"] == 87 and nv["Sensors"]["Edge Temperature"]["value"] == 66
assert sorted(nv["fdinfo"]) == ["1001", "1002"] and nv["fdinfo"]["1001"]["usage"]["VRAM"]["value"] == 2048
EOF

echo "== --no-pc, --smi, --process"
G -J -n 2 --interval 1000 --no-pc --device 2 > "$TMP/nopc.json"
python3 -I -c 'import json,sys; d=[json.loads(l) for l in open(sys.argv[1])][-1]["devices"][0]; assert d["GRBM"] is None and d["GRBM2"] is None' "$TMP/nopc.json" \
    && ok "--no-pc: no GRBM" || bad "--no-pc still has GRBM"
G --smi --interval 100 > "$TMP/smi"
has "$TMP/smi" "#2  [AMD Radeon RX 6800       ](gfx1030)| 0000:0a:00.0   |  3072/ 16384 MiB |" "--smi line 1"
has "$TMP/smi" "[SwPowerCap, SwThermalSlowdown]" "--smi NVIDIA throttle reasons"
G --smi -J --interval 200 --pc > "$TMP/smi.json"
python3 -I - "$TMP/smi.json" <<'EOF2' && ok "--smi samples every GPU over the same interval; GRBM off where it cannot be read" || bad "--smi -J GRBM"
import json, sys
d = {x["Info"]["PCI"]: x for x in json.load(open(sys.argv[1]))}
assert d["0000:0a:00.0"]["GRBM"]["Graphics Pipe"]["value"] == 50, d["0000:0a:00.0"]["GRBM"]
assert d["0000:03:00.0"]["GRBM"] is None      # no GRBM samples in this card's fixture = registers not readable
assert d["0000:01:00.0"]["GRBM"] is None      # NVIDIA
EOF2
G --process --device 2 > "$TMP/proc"
has "$TMP/proc" "glxgears-test   (   1004), VRAM  1024 MiB (  6%), GTT     2 MiB (  0%)" "--process line"

echo "== GRBM off by default (2.5, as in the window), --pc turns it on, --no-pc still accepted"
G -J -n 2 --interval 100 --device 2 > "$TMP/def.json"
G -J -n 2 --interval 100 --device 2 --pc > "$TMP/pc.json"
G -J -n 2 --interval 100 --device 2 --no-pc > "$TMP/nopc1.json"
python3 -I - "$TMP/def.json" "$TMP/pc.json" "$TMP/nopc1.json" <<'EOF2' && ok "default: no GRBM; --pc: GRBM; --no-pc: no GRBM" || bad "GRBM default / --pc / --no-pc"
import json, sys
dev = lambda f: json.loads(open(f).readlines()[-1])["devices"][0]
assert dev(sys.argv[1])["GRBM"] is None and dev(sys.argv[3])["GRBM"] is None
assert dev(sys.argv[2])["GRBM"]["Graphics Pipe"]["value"] == 50
EOF2

echo "== --interval is kept exactly (10 ms steps: 250 ms is 250, not 200)"
for iv in 250 1000 105; do
    G -J -n 1 --interval $iv --device 2 > "$TMP/iv$iv.json"
    python3 -I -c 'import json,sys; d=json.loads(open(sys.argv[1]).readline()); assert d["period"]["duration"] == int(sys.argv[2]), d["period"]' \
        "$TMP/iv$iv.json" $iv && ok "--interval $iv: period $iv ms" || bad "--interval $iv: $(grep -o '"period":{[^}]*}' "$TMP/iv$iv.json")"
done

echo "== a runtime-suspended card is not opened (it would wake up)"
R=$TMP/asleep
cp -a "$FIX" "$R"
D=$(cd "$R/sys/bus/pci/devices/0000:01:00.0" && pwd -P)
echo suspended > "$D/power/runtime_status"
SRS_TEST_GPU_ROOT=$R G --dump > "$TMP/asleep.out" 2> "$TMP/asleep.err"; rc=$?
has "$TMP/asleep.err" "gpu-top: #0 0000:01:00.0: asleep (runtime suspended) - not read" "the sleeping card is named on stderr"
grep -q "0000:01:00.0" "$TMP/asleep.out" && bad "the sleeping card was read" || ok "the sleeping card was not read (the others were: exit $rc)"
SRS_TEST_GPU_ROOT=$R G --dump --device 0 > /dev/null 2> "$TMP/asleep0.err"; rc=$?
[ $rc = 2 ] && grep -q "add --wake" "$TMP/asleep0.err" && ok "--device 0 on a sleeping card: exit 2, says --wake" || bad "--device 0 asleep: exit $rc"
SRS_TEST_GPU_ROOT=$R G --dump --device 0 --wake > "$TMP/wake.out" 2>&1; rc=$?
[ $rc = 0 ] && grep -q "0000:01:00.0" "$TMP/wake.out" && ok "--device 0 --wake reads it" || bad "--wake: exit $rc"
SRS_TEST_GPU_ROOT=$R G --list > "$TMP/asleep.list"
has "$TMP/asleep.list" "asleep (runtime suspended)" "--list says asleep"
if [ -n "${2:-}" ] && [ -x "${2:-}" ] && command -v Xvfb >/dev/null; then   # The Big Top window: starts on the first awake card
    d=:$((50 + RANDOM % 9))
    Xvfb "$d" -screen 0 1400x1000x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
    sleep 1
    mkdir -p "$XDG_CONFIG_HOME/srs"
    env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS GDK_BACKEND=x11 DISPLAY=$d SRS_TEST_GPU_ROOT=$R SRS_TEST_BIGTOP_LOG=1 \
        SRS_OPEN=gputop "$2" > "$TMP/gui.log" 2>&1 & GP=$!
    sleep 7; kill $GP 2>/dev/null; wait $GP 2>/dev/null; kill $XPID 2>/dev/null
    grep -q "SRS-TEST: bigtop open #1 " "$TMP/gui.log" && ! grep -q "SRS-TEST: bigtop open #0 " "$TMP/gui.log" \
        && ok "window: the awake cards opened, the sleeping #0 not" || bad "window opened: $(grep 'bigtop open' "$TMP/gui.log" | tr '\n' ' ')"
fi

echo "== pp_table, DRM info, XDNA"
G --pp-table --device 2 > "$TMP/ppt"
has "$TMP/ppt" "SMU (MP1) version: 11.0.7" "SMU version from the IP discovery"
has "$TMP/ppt" "from sysfs: smu_11_0_7_powerplay_table (Navi 21-24)" "table chosen by SMU version"
has "$TMP/ppt" "boost_power_limit       : 255 W" "packed header fields"
has "$TMP/ppt" "UCLKFMAX                            : min      674, max     1150" "overdrive limits"
G --pp-table --device 1 > "$TMP/ppt1"
has "$TMP/ppt1" "not available on this card" "RDNA 4: pp_table not available"
G --drm-info --device 1 > "$TMP/drm"
has "$TMP/drm" "not available" "DRM info in a test root: says so"
G --xdna > "$TMP/xdna"
has "$TMP/xdna" "no XDNA NPU" "no NPU in the test root"

echo "== idle desktop card (2.5.0): no D3 drop while it drives a monitor"
for case in connected none; do
    R=$TMP/idle-$case
    cp -a "$HERE/gpu_top_fixtures/rx9070xt" "$R"
    D=$(cd "$R/sys/bus/pci/devices/0000:03:00.0" && pwd -P)
    find "$R/proc" -name '*.next' -delete
    printf '\0\0\0\0\0\0' | dd of="$D/gpu_metrics" bs=1 seek=16 conv=notrunc 2>/dev/null   # GFX / UMC / media activity 0 %
    echo auto > "$D/power/control"
    if [ $case = connected ]; then mkdir -p "$D/drm/card0/card0-DP-2"; echo connected > "$D/drm/card0/card0-DP-2/status"; fi
    SRS_TEST_GPU_ROOT=$R G -J -n 4 --no-pc --interval 100 > "$TMP/idle-$case.json"
done
python3 -I - "$TMP/idle-connected.json" "$TMP/idle-none.json" <<'EOF2' && ok "idle card with a monitor: gpu_metrics in every sample; without one it may sleep (D3, amdgpu_top)" || bad "idle D3 rule"
import json, sys
con = [json.loads(l)["devices"][0] for l in open(sys.argv[1])]
non = [json.loads(l)["devices"][0] for l in open(sys.argv[2])]
assert len(con) == 4 and all(d["gpu_metrics"] is not None and d["gpu_activity"]["GFX"]["value"] == 0 for d in con), [d["gpu_metrics"] is None for d in con]
assert non[-1]["gpu_metrics"] is None, "a sleepable idle card should drop to D3"
EOF2

echo "== command line"
G --bogus >/dev/null 2>&1; [ $? = 2 ] && ok "unknown option: exit 2" || bad "unknown option exit"
G --device 9 --dump >/dev/null 2>&1; [ $? = 2 ] && ok "no such GPU: exit 2" || bad "--device 9 exit"
G --interval 5 --smi >/dev/null 2>&1; [ $? = 2 ] && ok "--interval out of range: exit 2" || bad "--interval 5 exit"
G --dump --smi >/dev/null 2>&1; [ $? = 2 ] && ok "two modes: exit 2" || bad "two modes exit"
G --tui -J >/dev/null 2>&1; [ $? = 2 ] && ok "--tui with -J: exit 2" || bad "--tui -J exit"
G --help | grep -q "amdgpu_top" && ok "--help" || bad "--help"
SRS_TEST_NVML_LIB= G --list >/dev/null 2>&1 && SRS_TEST_NVML_LIB= G --dump --device 0 > "$TMP/nonv" 2>&1
has "$TMP/nonv" "NVIDIA GPU 10de:2684" "test root without the stub: NVML not loaded, sysfs data only"
empty=$TMP/empty; mkdir -p "$empty/sys/bus/pci/devices"
SRS_TEST_GPU_ROOT=$empty G --list >/dev/null 2>&1; [ $? = 1 ] && ok "no GPU: exit 1" || bad "no GPU exit"

[ $fail = 0 ] && echo "gpu-top-test: all passed" || echo "gpu-top-test: FAILED"
exit $fail
