#!/usr/bin/env bash
# tests/mem-timings-test.sh - the Memory Timings page (2.4, mem_timings.c and [mem-tweak]) on made-up data:
#   1. tests/mem_timings_harness.c, linked with mem_timings.c: the UMC register decode, DDR5 SPD images (JEDEC, EXPO,
#      XMP 3.0), the AMD AOD table and the Intel memory controller dumps, each checked against hand-worked values.
#   2. The CLI page render from a fake root cache (umc.txt, aod.bin, pmic-0051.txt), a fake ryzen_smu PM table and fake
#      SPD EEPROMs, in a throw-away HOME.
#
#   tests/mem-timings-test.sh SYSREPORT-SEARCH      (make test-bench passes the path)
#
# SAFETY: no root, no /dev/mem, no SMN access, no SPD or PMIC read. The page reads the PM table from SRS_RSMU_DIR and the SPDs from
# SRS_SPD_DIR, both pointed at files made here, so the real ryzen_smu driver and the real modules are never read. The
# only real things the page still looks at are the CPU vendor (/proc/cpuinfo), the SMBIOS slot list and which spd5118
# devices exist (to know where to put the fake SPDs) and their temperature sensors, the same as any page view.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH"; exit 2; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
ROOT=$HOME/.cache/srs/root
mkdir -p "$ROOT" "$TMP/gen" "$TMP/rsmu" "$TMP/spd"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
skip() { printf '  skip  %s\n' "$*"; }

echo "== build the test harness (mem_timings.c + tests/mem_timings_harness.c)"
H=$TMP/mem_timings_harness
if /usr/bin/gcc -std=c11 -Wall -Wextra -Wno-format-truncation -D_DEFAULT_SOURCE -I"$SRC" -o "$H" "$SRC/tests/mem_timings_harness.c" "$SRC/mem_timings.c" 2>"$TMP/cc.log" && [ ! -s "$TMP/cc.log" ]; then
    ok "built, no warnings"
else cat "$TMP/cc.log"; bad "the harness does not build cleanly"; exit 1; fi

"$H"; [ $? = 0 ] || fail=1

echo "== the page, from a fake root cache"
"$H" gen "$TMP/gen" || { bad "the harness could not write its files"; exit 1; }
"$H" umc > "$ROOT/umc.txt"
cp "$TMP/gen/aod.bin" "$ROOT/aod.bin"
cp "$TMP/gen/pm_table" "$TMP/gen/pm_table_version" "$TMP/rsmu/"
VENDOR=$(sed -n 's/^vendor_id[[:space:]]*: //p' /proc/cpuinfo | head -1)
for d in /sys/bus/i2c/drivers/spd5118/*-00*; do   # a fake SPD for each spd5118 device the page will look for
    [ -e "$d" ] || continue
    mkdir -p "$TMP/spd/${d##*/}" && cp "$TMP/gen/spd.bin" "$TMP/spd/${d##*/}/eeprom"
done
# an i2cdump of the PMIC: R0x21 SWA = 0xDC (VDD 1.350 V), R0x25 SWB = 0xDC (VDDQ 1.350 V), R0x27 SWC = 0x78 (VPP 1.800 V)
{ echo "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f    0123456789abcdef"
  echo "00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................"
  echo "10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................"
  echo "20: 00 dc 00 00 00 dc 00 78 00 00 00 00 00 00 00 00    .?...?.x........"
  echo "30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................"; } > "$ROOT/pmic-0051.txt"
page() { SRS_RSMU_DIR=${RSMU:-$TMP/rsmu} SRS_SPD_DIR=$TMP/spd timeout 60 "$BIN" '-save=[mem-tweak]=STDOUT' 2>&1; }
has() { grep -qE "$1" <<<"$OUT"; }
OUT=$(page)
has '^Memory speed +6000 MT/s \(MEMCLK 3000 MHz\)' && ok "Memory speed 6000 MT/s (MEMCLK 3000 MHz)" || bad "Memory speed line: $(grep 'Memory speed' <<<"$OUT")"
has '^Channels with a DIMM +2 \(A, B\)' && ok "2 channels with a DIMM (A, B)" || bad "channels: $(grep 'Channels' <<<"$OUT")"
has '^Gear Down Mode \(GDM\) +Enabled' && has '^Command rate +1T' && has '^Bank groups +Enabled' && ok "GDM Enabled, 1T, bank groups Enabled" || bad "controller lines"
row() { grep -E "^$1 " <<<"$OUT" | head -1 | tr -s ' '; }
for w in "tCL 30 30 10.00 ns" "tRCDRD 38 38 12.67 ns" "tRCDWR 38 38 12.67 ns" "tRP 38 38 12.67 ns" "tRAS 96 96 32.00 ns" "tRC 146 146 48.67 ns" \
         "tRRDS 8 8" "tRRDL 8 8" "tFAW 32 32" "tWTRS 8 8" "tWTRL 16 16" "tWR 90 90 30.00 ns" "tRTP 12 12" "tCWL 28 28" "tREFI 15000 15000 5.00 us" \
         "tRFC 884 884 294.67 ns" "tRFC2 480 480 160.00 ns" "tRFCsb 390 390 130.00 ns"; do
    [[ "$(row "${w%% *}")" == "$w "* ]] && ok "timing row: $w" || bad "timing row ${w%% *}: [$(row "${w%% *}")], want [$w ...]"
done
# tRFC, tRFC2 and tRFCsb follow tREFI
[ "$(grep -oE '^(tREFI|tRFC|tRFC2|tRFCsb|tRDRDSCL) ' <<<"$OUT" | tr -d ' ' | tr '\n' ,)" = "tREFI,tRFC,tRFC2,tRFCsb,tRDRDSCL," ] && ok "tRFC / tRFC2 / tRFCsb sit right after tREFI" || bad "refresh rows out of order"
has '^tRDRDSCL +- +- ' && ok "a register not in umc.txt shows -" || bad "unread timing not shown as -"
echo "== PM table (fake ryzen_smu files)"
has '^MEMCLK +3000 MHz' && has '^UCLK +1500 MHz \(UCLK:MEMCLK 1:2\)' && has '^FCLK +2000 MHz' && ok "MEMCLK 3000, UCLK 1500 (1:2), FCLK 2000 MHz" || bad "clocks: $(grep -E '^(MEMCLK|UCLK|FCLK)' <<<"$OUT" | tr '\n' '|')"
has '^VSOC +1\.2500 V' && has '^VDDP +1\.1250 V' && has '^VDD MISC +1\.0000 V' && ok "VSOC 1.2500, VDDP 1.1250, VDD MISC 1.0000 V" || bad "voltages"
has '^VDDG IOD / CCD +not in this PM table version' && ok "Ryzen 7000 table: VDDG not in it, said so" || bad "VDDG line"
echo "== BIOS memory settings (fake AOD table)"
has '^ProcODT +48\.0 Ω' && ok "ProcODT 48.0 Ω" || bad "ProcODT: $(grep ProcODT <<<"$OUT")"
has '^RttNomWr / RttNomRd +RZQ/5 \(48 Ω\) / RZQ/5 \(48 Ω\)' && ok "RttNomWr / RttNomRd RZQ/5 (48 Ω)" || bad "RttNom line"
has '^RttWr +Off' && has '^RttPark / RttParkDqs +RZQ/4 \(60 Ω\) / RZQ/5 \(48 Ω\)' && ok "RttWr Off, RttPark RZQ/4 (60 Ω)" || bad "RttWr / RttPark"
has '^DIMM VDD / VDDQ / VPP \(set\) +1\.435 / 1\.400 / 1\.800 V' && has '^CPU VDDIO \(memory\) +1\.400 V' && ok "VDD 1.435 / 1.400 / 1.800 V, VDDIO 1.400 V" || bad "AOD voltages"
echo "== modules (fake SPDs)"
if ls -d "$TMP"/spd/*/ >/dev/null 2>&1; then
    if has '^Modules \(SPD\)'; then
        has '^Size +16 GB, 1 rank, 16 Gb x8 chips' && ok "Size 16 GB, 1 rank, 16 Gb x8" || bad "Size: $(grep '^Size' <<<"$OUT" | head -1)"
        has '^Module +Samsung SRS-TEST-16G' && has '^DRAM chips +Micron' && has '^Made +2026, week 44' && ok "maker, part, DRAM maker, date" || bad "module lines"
        has '^EXPO 1 +6000 MT/s +30-38-38-96-134 +295 / 160 / 130 ns +1\.350 / 1\.350 / 1\.800 V' && ok "EXPO 1 row" || bad "EXPO row: $(grep '^EXPO' <<<"$OUT" | head -1)"
        has '^XMP 1 +6000 MT/s +36-36-36-96-132 .*Lv1-6000C36$' && ok "XMP 1 row with its name" || bad "XMP row: $(grep '^XMP' <<<"$OUT" | head -1)"
        has '^JEDEC +4800 MT/s +40-39-39-77-116 .*1\.100 / 1\.100 / 1\.800 V' && ok "JEDEC row" || bad "JEDEC row: $(grep '^JEDEC' <<<"$OUT" | head -1)"
    else skip "the SMBIOS slot list did not pair a slot with an spd5118 device: no Modules section"; fi
else skip "no spd5118 devices on this PC: the Modules section is not checked"; fi
if ls -d /sys/bus/i2c/drivers/spd5118/*-0051 >/dev/null 2>&1 && has '^Modules \(SPD\)'; then
    has '^PMIC set VDD / VDDQ / VPP +1\.350 / 1\.350 / 1\.800 V$' && ok "PMIC set voltages 1.350 / 1.350 / 1.800 V (pmic-0051.txt)" || bad "PMIC: $(grep PMIC <<<"$OUT")"
    has '^PMIC mode +secure \(locked\)' && ok "PMIC mode secure (locked)" || bad "PMIC mode"
    [ "$(grep -c '^PMIC set' <<<"$OUT")" = 1 ] && ok "only the module with a pmic file shows PMIC lines" || bad "PMIC lines: $(grep -c '^PMIC set' <<<"$OUT")"
else skip "no spd5118 device at 0x51: the PMIC line is not checked"; fi

echo "== what the page says when things are missing"
RSMU=$TMP/no-such-dir; OUT=$(page)
if [ "$VENDOR" = AuthenticAMD ]; then
    has '^Clocks / SoC voltages +not shown: the ryzen_smu driver is not loaded' && ok "AMD, no PM table: says ryzen_smu is not loaded" || bad "no PM table: $(grep -i 'clocks' <<<"$OUT")"
else skip "the 'ryzen_smu not loaded' line is AMD only (this CPU: $VENDOR)"; fi
has '^Memory speed +6000 MT/s' && ok "the speed still comes from umc.txt" || bad "no speed without the PM table"
mkdir -p "$TMP/rsmu2"; printf '\x99\x99\x99\x00' > "$TMP/rsmu2/pm_table_version"; cp "$TMP/gen/pm_table" "$TMP/rsmu2/"
if [ "$VENDOR" = AuthenticAMD ]; then
    RSMU=$TMP/rsmu2; OUT=$(page)
    has 'not shown: PM table version 0x999999 is not known to SRS yet' && ok "an unknown PM table version is named, not decoded" || bad "unknown PM table: $(grep -i 'clocks' <<<"$OUT")"
fi
unset RSMU
printf 'ch0 0x50204 2626601c\n' >> "$ROOT/umc.txt"   # tCL 28 now: the AOD table (tCL 30) no longer matches
OUT=$(page)
has '^Status +the AOD table was read, but its layout is not known' && ! has '^ProcODT' && ok "an AOD table that does not match the live timings is not used" || bad "AOD mismatch: $(grep -A1 'AOD table' <<<"$OUT" | tail -1)"
"$H" umc > "$ROOT/umc.txt"
rm -f "$ROOT/aod.bin"; echo "not readable: /dev/mem (IO_STRICT_DEVMEM)" > "$ROOT/aod.txt"
OUT=$(page)
has '^Status +not readable on this kernel' && ok "aod.txt 'not readable': explained (iomem=relaxed)" || bad "aod.txt not readable: $(grep -A1 'AOD table' <<<"$OUT" | tail -1)"
rm -f "$ROOT/aod.txt"
OUT=$(page)
has '^Status +not read yet \(Menu > Run as root\)' && ok "no AOD file at all: 'not read yet'" || bad "no AOD: $(grep -A1 'AOD table' <<<"$OUT" | tail -1)"
echo "not read: the ryzen_smu driver is not loaded" > "$ROOT/umc.txt"
cat > "$ROOT/intel-imc.txt" <<'EOF'
gen adl
model 0xB7
base 0xFEDC0000
reg 0x0D800 0x00000001
reg 0x0D80C 0x00100010
reg 0x1D800 0x00000001
reg 0x1D80C 0x00100010
reg 0x05F60 0x000186A0
reg 0x05918 0x00000078
reg 0x05E04 0x00001100
reg 0x0E000 0x00000026
reg 0x0E004 0x01318000
reg 0x0E070 0x001E0000
EOF
OUT=$(page)
if [ "$VENDOR" = AuthenticAMD ]; then
    has '^Status +the ryzen_smu driver is not loaded: install ryzen_smu' && ok "AMD, umc.txt 'not read': says to install ryzen_smu" || bad "umc not read: $(grep '^Status' <<<"$OUT")"
    ! has 'tCL-tRCD-tRP-tRAS' && ok "AMD: an intel-imc.txt is ignored" || bad "AMD showed Intel timings"
elif [ "$VENDOR" = GenuineIntel ]; then
    has '^Memory speed +6000 MT/s \(DDR5\)' && has '^tCL-tRCD-tRP-tRAS +30-38-38-96' && ok "Intel: DDR5 6000 MT/s 30-38-38-96 from intel-imc.txt" || bad "Intel page: $(grep -E 'speed|tCL' <<<"$OUT")"
    has 'experimental' && ok "Intel: marked experimental" || bad "Intel: not marked experimental"
else skip "CPU vendor $VENDOR: the vendor branches are not checked"; fi
rm -f "$ROOT/umc.txt" "$ROOT/intel-imc.txt"
OUT=$(page)
if [ "$VENDOR" = AuthenticAMD ]; then
    has '^Status +needs root once: Menu > Run as root' && ok "AMD, no umc.txt: 'needs root once'" || bad "no umc: $(grep '^Status' <<<"$OUT")"
fi
! grep -qiE 'segmentation|core dumped|abort' <<<"$OUT" && ok "no crash" || bad "crash"

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
