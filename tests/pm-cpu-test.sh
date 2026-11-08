#!/usr/bin/env bash
# tests/pm-cpu-test.sh - 2.6.0 CPU telemetry from the SMU PM table (CPU Voltages, AMD Temperatures, CPU Speeds) on a
# fake ryzen_smu folder (SRS_RSMU_DIR): known values for table version 0x540104, then an unknown version and an
# out-of-range table must give "not known" / "out of range" and no numbers.
#   tests/pm-cpu-test.sh SYSREPORT-SEARCH
# SAFETY: reads only the fake folder; no root, no SMU or SMN access.
set -u
BIN=${1:-}; [ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH"; exit 2; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
has() { grep -qF -- "$2" <<<"$1" && ok "$3" || { bad "$3"; printf '        wanted: %s\n' "$2"; }; }
hasnt() { grep -qF -- "$2" <<<"$1" && { bad "$3"; printf '        did not want: %s\n' "$2"; } || ok "$3"; }
mk() {   # $1 dir, $2 version (hex), $3 python expression filling t (a list of 1024 floats)
    mkdir -p "$1"
    python3 -I -c "import struct,sys
t=[0.0]*1024
$3
open(sys.argv[1]+'/pm_table','wb').write(struct.pack('<1024f',*t))
open(sys.argv[1]+'/pm_table_version','wb').write(struct.pack('<I',$2))" "$1"
}
NC=$(lscpu -p=CORE,SOCKET 2>/dev/null | grep -v '^#' | awk -F, '$2==0' | sort -u | wc -l)
echo "== known table 0x540104 ($NC cores here)"
mk "$TMP/a" 0x540104 't[2]=140;t[3]=55.5;t[11]=61.2;t[271]=1.25;t[275]=1.2
for i in range(8): t[301+i]=1.0+i/100; t[309+i]=40+i; t[317+i]=4.5+i/10
t[317+7]=0.0'
O=$(SRS_RSMU_DIR=$TMP/a timeout 60 "$BIN" '-save=[cpu-volts][amd-thm-0][cpu-speeds]=STDOUT' 2>&1)
if [ "$NC" -ge 1 ] && [ "$NC" -le 8 ]; then
    has "$O" "VCORE (SMU telemetry)       1.250 V" "VCORE from index 271"
    has "$O" "VID (voltage requested)     1.200 V" "VID from index 275"
    has "$O" "55.5 W of 140 W limit" "PPT now (3) and its limit (2)"
    has "$O" "Hotspot               61.2" "hotspot from index 11"
    has "$O" "1.000 V" "core 0 voltage from index 301"
    has "$O" "40.0°C" "core 0 temperature from index 309"
    has "$O" "4500 MHz" "core 0 effective clock from index 317 (GHz x 1000)"
    [ "$NC" = 8 ] && has "$O" "resting (0)" "a core that slept the whole interval (clock 0) is shown as resting"
else
    has "$O" "not known for $NC cores" "more than 8 cores: not known (the table's per-core lists have 8 places)"
fi
echo "== unknown version 0x540108, and values out of range"
mk "$TMP/b" 0x540108 't[271]=1.25'
O=$(SRS_RSMU_DIR=$TMP/b timeout 60 "$BIN" '-save=[cpu-volts]=STDOUT' 2>&1)
has "$O" "not known for PM table version 0x540108" "an unknown table version says so"
hasnt "$O" "VCORE (SMU telemetry)" "and shows no SMU numbers"
mk "$TMP/c" 0x540104 't[3]=-5;t[11]=900;t[271]=9'
O=$(SRS_RSMU_DIR=$TMP/c timeout 60 "$BIN" '-save=[cpu-volts]=STDOUT' 2>&1)
if [ "$NC" -ge 1 ] && [ "$NC" -le 8 ]; then
    has "$O" "PM table values out of range" "a table whose values make no sense is refused"
fi
echo "== no ryzen_smu"
O=$(SRS_RSMU_DIR=$TMP/none timeout 60 "$BIN" '-save=[cpu-volts]=STDOUT' 2>&1)
has "$O" "needs the ryzen_smu driver" "without the driver: says what is needed"
[ $fail = 0 ] && echo "ALL OK" || echo "pm-cpu-test: FAILED"
exit $fail
