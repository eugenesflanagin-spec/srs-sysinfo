#!/usr/bin/env bash
# tests/root-pages-test.sh - the 2.4 pages that show srs-root's cached root-only data, rendered by the CLI from FAKE cache
# files in a throw-away HOME: ACPI EC register dump (and its off / not-yet messages), ACPI method evaluation log, Firmware
# (UEFI System Table pointers, ESRT details, the AMI-style BIOS version), MSR Setup RAPL watts, System CMOS with --redact,
# the GPIO pin table's tabs, and the GPU VBIOS image chain ("no, more follow" when the driver's copy ends early).
#
#   tests/root-pages-test.sh SYSREPORT-SEARCH      (make test-bench passes the path)
#
# SAFETY: never run as root (it refuses), nothing is written outside the temp folder, no /dev/mem, no MSR, EC, CMOS or ROM
# access: the CMOS image comes from SRS_TEST_CMOS_FILE, everything else from the fake cache. The pages still read the
# usual world-readable sysfs files (which ESRT entries and GPUs exist, the BIOS version) like any page view; checks that
# depend on this PC are skipped when it lacks the thing (no UEFI, no ESRT entry, no DRM GPU).
set -u
BIN=${1:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH"; exit 2; }
[ "$(id -u)" = 0 ] && { echo "do not run this test as root"; exit 2; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
export LC_ALL=C.UTF-8
ROOT=$HOME/.cache/srs/root
mkdir -p "$ROOT" "$XDG_CONFIG_HOME/srs"
INI=$XDG_CONFIG_HOME/srs/SRSDEF.ini
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
skip() { printf '  skip  %s\n' "$*"; }
page() { OUT=$(timeout 60 "$BIN" "-save=[$1]=STDOUT" "${@:2}" 2>&1); }
has() { grep -qE "$1" <<<"$OUT"; }
# write bytes given as hex pairs: hexbytes "55 AA 01" > file
hexbytes() { local h; for h in "$@"; do printf "\\x$h"; done; }

echo "== ACPI embedded controller (acpi-ec)"
printf '[SRSDEF]\nRedact Reports=0\n' > "$INI"
page acpi-ec
has '^EC register dump +off: Menu > Options > Read the embedded controller, then Run as root' && ok "no ec.bin, option off: says how to switch it on" || bad "off message: $(grep 'EC register' <<<"$OUT")"
printf '[SRSDEF]\nRead EC=1\n' > "$INI"
page acpi-ec
has '^EC register dump +not in the srs-root cache yet: Menu > Run as root' && ok "option on, no ec.bin yet: 'not in the srs-root cache yet'" || bad "on, no cache: $(grep 'EC register' <<<"$OUT")"
printf 'Read EC=1\n' > "$INI"
page acpi-ec
has 'off: Menu > Options' && ok "'Read EC=1' only counts as a line of its own after another line (as written by SRS)" || bad "first-line key: $(grep 'EC register' <<<"$OUT")"
for i in $(seq 0 255); do printf "\\x$(printf %02x "$i")"; done > "$ROOT/ec.bin"
page acpi-ec
has '^EC registers \(read once by srs-root' && ok "ec.bin: the register dump is shown" || bad "no EC dump heading"
has '^0000  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F$' && has '^00F0  F0 F1 F2 F3 F4 F5 F6 F7 F8 F9 FA FB FC FD FE FF$' \
    && ok "rows 0000 and 00F0, 16 bytes each" || bad "EC rows: $(grep -E '^00(00|F0) ' <<<"$OUT")"
[ "$(grep -cE '^00[0-9A-F]0  ' <<<"$OUT")" = 16 ] && ok "16 rows for 256 bytes" || bad "EC row count $(grep -cE '^00[0-9A-F]0  ' <<<"$OUT")"
! has 'EC register dump' && ok "no 'off' line when the dump is there" || bad "both the dump and a status line"
head -c 300 /dev/zero > "$ROOT/ec.bin"
page acpi-ec
[ "$(grep -cE '^00[0-9A-F]0  ' <<<"$OUT")" = 16 ] && ok "a 300-byte ec.bin is cut at 256 bytes" || bad "300-byte ec.bin: $(grep -cE '^0[0-9A-F]{3}  ' <<<"$OUT") rows"
rm -f "$ROOT/ec.bin"

echo "== ACPI method evaluation log (acpi-eval)"
printf '2026-10-30T10:00:00+01:00\t\\_SB.PCI0.GPP0._STA\t0xf\n' > "$ROOT/acpi-calls.txt"
page acpi-eval
has '^Your evaluations \(newest last\)' && ok "one logged call: 'Your evaluations'" || bad "no log heading"
has '^When +Method and arguments +Result' && ok "column headings" || bad "no column headings"
has '^2026-10-30T10:00:00\+01:00 +\\_SB\.PCI0\.GPP0\._STA +0xf$' && ok "the call: time, method, result" || bad "log row: $(grep '^2026' <<<"$OUT")"
{ echo "a broken line"; for i in $(seq 1 35); do printf '2026-10-30T10:%02d:00+01:00\t\\_TZ.TZ01._TMP 0x%X\t%d\n' "$i" "$i" "$((2980 + i))"; done; } > "$ROOT/acpi-calls.txt"
page acpi-eval
has '^Your last 30 evaluations \(newest last\)' && ok "35 calls: 'Your last 30 evaluations'" || bad "no 'last 30' heading"
[ "$(grep -c '^2026-10-30T' <<<"$OUT")" = 30 ] && ! has '^2026-10-30T10:05:00' && has '^2026-10-30T10:06:00.*_TMP 0x6 +2986$' && has '^2026-10-30T10:35:00.*0x23 +3015$' \
    && ok "the newest 30 (calls 6 to 35)" || bad "rows: $(grep -c '^2026-10-30T' <<<"$OUT")"
printf '2026-10-30T11:00:00+01:00\t\\_SB.AMW0.WMAX 0 0x13 b0102\t0x0\nno tabs here\n2026-10-30T11:01:00+01:00\t\\_SB.PCI0.LPCB.EC0._Q11\tError: AE_NOT_FOUND\n' > "$ROOT/acpi-calls.txt"
page acpi-eval
[ "$(grep -c '^2026-10-30T' <<<"$OUT")" = 2 ] && ! has 'no tabs here' && has '^2026-10-30T11:00:00\+01:00 +\\_SB\.AMW0\.WMAX 0 0x13 b0102 +0x0$' \
    && has 'EC0\._Q11 +Error: AE_NOT_FOUND$' && ok "arguments and an error result shown; a line without tabs skipped" || bad "rows: $(grep '^2026' <<<"$OUT")"

echo "== Firmware (efi-systab.txt, esrt.txt)"
if [ ! -d /sys/firmware/efi ]; then skip "not booted with UEFI: the Firmware checks are skipped"
else
    printf 'ACPI20=0x7a3fe014\nSMBIOS3=0x7b1c4000\n' > "$ROOT/efi-systab.txt"
    page firmware
    if [ -r /sys/firmware/efi/systab ]; then skip "/sys/firmware/efi/systab is readable here: the page reads it, not the cache"
    else
        has '^System Table +ACPI20=0x7a3fe014$' && has '^ +SMBIOS3=0x7b1c4000$' && ok "System Table pointers from the cache, one per line" || bad "systab: $(grep -A1 'System Table' <<<"$OUT")"
    fi
    E=$(ls /sys/firmware/efi/esrt/entries 2>/dev/null | head -1)
    if [ -z "$E" ]; then skip "no ESRT entry on this PC: the ESRT checks are skipped"
    else
        page firmware
        has "^ESRT Entries +[0-9]+ \(details are root only: Menu > Run as root\)" && ok "no esrt.txt: details are root only" || bad "no esrt.txt: $(grep ESRT <<<"$OUT")"
        { echo "$E fw_class 11111111-2222-3333-4444-555555555555"; echo "$E fw_type 1"; echo "$E fw_version 6149"
          echo "$E lowest_supported_fw_version 4660"; echo "$E capsule_flags 0x0"; echo "$E last_attempt_version 6149"; echo "$E last_attempt_status 0"
          echo "entry99 fw_type 2"; } > "$ROOT/esrt.txt"
        page firmware
        has "^$E +system firmware \(the BIOS\)$" && ok "$E: system firmware (the BIOS)" || bad "type: $(grep "^$E" <<<"$OUT")"
        has '^  Firmware class +11111111-2222-3333-4444-555555555555$' && ok "firmware class GUID" || bad "class: $(grep 'Firmware class' <<<"$OUT")"
        BV=$(cat /sys/class/dmi/id/bios_version 2>/dev/null)
        if [[ $BV =~ ^1805($|[^[:alnum:]]) ]]; then
            has '^  Version +BIOS 1805 \(stored as 0x1805 = 6149\)$' && ok "BIOS 1805 here: 6149 is shown as BIOS 1805" || bad "version: $(grep '  Version' <<<"$OUT")"
            has '^  Last update +BIOS 1805 \(stored as 0x1805 = 6149\): success$' && ok "last update: BIOS 1805, success" || bad "last update: $(grep 'Last update' <<<"$OUT")"
        else
            has '^  Version +6149 \(0x1805\)$' && ok "the BIOS here is not 1805 ('$BV'): 6149 shown as a plain number" || bad "version: $(grep '  Version' <<<"$OUT")"
            has '^  Last update +6149 \(0x1805\): success$' && ok "last update: 6149, success" || bad "last update: $(grep 'Last update' <<<"$OUT")"
        fi
        if [[ $BV =~ ^1234($|[^[:alnum:]]) ]]; then skip "BIOS version 1234: the plain-number case is not checked"
        else has '^  Lowest allowed +4660 \(0x1234\)$' && ok "lowest allowed 4660 (0x1234): not the BIOS version, plain number" || bad "lowest: $(grep 'Lowest' <<<"$OUT")"; fi
        ! has '^entry99' && ok "an esrt.txt line for an entry this PC does not have is ignored" || bad "entry99 shown"
        page firmware --redact
        has '^  Firmware class +XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX$' && ok "--redact masks the firmware class GUID" || bad "redact class: $(grep 'Firmware class' <<<"$OUT")"
        sed -i "s/^$E last_attempt_version 6149/$E last_attempt_version 0/" "$ROOT/esrt.txt"
        page firmware
        has '^  Last update +none recorded by the firmware$' && ok "last attempt 0: none recorded" || bad "no last update: $(grep 'Last update' <<<"$OUT")"
    fi
fi

echo "== MSR Setup RAPL watts (msrs.txt, rapl.txt)"
{ printf '%-12s %-34s %-18s %-18s\n' MSR Name CPU-0 CPU-1
  printf '%-12s %-34s %-18s %-18s\n' 0xC0010299 RAPL_PWR_UNIT 00000000000a1003 00000000000a1003; } > "$ROOT/msrs.txt"
page msr-setup
has '^RAPL energy unit +1/2\^16 J = 15\.26 uJ, time unit 1/2\^10 s' && ok "RAPL unit 1/2^16 J" || bad "unit: $(grep 'RAPL energy' <<<"$OUT")"
has '^Energy counters +a counter alone means nothing' && ok "no rapl.txt: says the next Run as root gives watts" || bad "no rapl: $(grep -i 'energy counters\|Package power' <<<"$OUT")"
# 65536 units = 1 J; 6553600 in 1 s = 100 W. CPU 1 wraps around 2^32 (0xFFFFFFF0 -> 327664 + 16 = 327680 units = 5 W)
printf 'seconds 1.000000\npackage 0 6553600\ncpu0 0 655360\ncpu1 4294967280 327664\n' > "$ROOT/rapl.txt"
page msr-setup
has '^Package power +100\.0 W while srs-root ran \(energy counter read twice, 1\.00 s apart\)' && ok "package 100.0 W" || bad "package: $(grep 'Package power' <<<"$OUT")"
has '^Core power +15\.00 W in all; per CPU \(W\): CPU-0 10\.00, CPU-1 5\.00$' && ok "cores 10.00 + 5.00 W (a 32-bit counter wrap handled)" || bad "cores: $(grep 'Core power' <<<"$OUT")"
printf 'seconds 2.000000\npackage 0 6553600\n' > "$ROOT/rapl.txt"
page msr-setup
has '^Package power +50\.0 W .* 2\.00 s apart' && ! has '^Core power' && ok "2 s apart: 50.0 W; no CPU lines, no core line" || bad "2 s: $(grep -E 'Package|Core' <<<"$OUT")"
printf 'package 0 6553600\nseconds 1.0\n' > "$ROOT/rapl.txt"
page msr-setup
has '^Energy counters ' && ok "counters before the 'seconds' line are not used" || bad "seconds order: $(grep 'Package power' <<<"$OUT")"

echo "== System CMOS with --redact (SRS_TEST_CMOS_FILE: a made-up image, the real CMOS is not read)"
# 114 bytes from address 0x0E: 0x0E status 00, 0x32 century 0x20, "PASSWORD" at 0x40
CM=$TMP/cmos.bin
{ head -c $((0x32 - 0x0E)) /dev/zero; printf '\x20'; head -c $((0x40 - 0x33)) /dev/zero; printf 'PASSWORD'; head -c $((0x80 - 0x48)) /dev/zero; } > "$CM"
[ "$(stat -c %s "$CM")" = 114 ] || bad "the made-up CMOS image is not 114 bytes"
export SRS_TEST_CMOS_FILE=$CM
page system-cmos
has '^CMOS RAM +114 bytes, addresses 0x0E - 0x7F' && ok "114 bytes, 0x0E - 0x7F" || bad "size: $(grep '^CMOS RAM ' <<<"$OUT")"
has '^0x32 Century +0x20  20 \(years 2000 - 2099\)' && ok "century byte 0x20" || bad "century: $(grep Century <<<"$OUT")"
has '^0x0E Diagnostic status +0x00  no errors flagged' && ok "diagnostic status 0x00" || bad "status: $(grep Diagnostic <<<"$OUT")"
has '^003E  00 00 50 41 53 53 57 4F 52 44 00 00 00 00 00 00  \.\.PASSWORD\.\.\.\.\.\.$' && ok "without --redact the bytes are shown (row 003E)" || bad "row 003E: $(grep '^003E' <<<"$OUT")"
page system-cmos --redact
has '^003E  (XX ){16} \.{16}$' && ok "--redact: row 003E is XX, no text" || bad "redacted row: $(grep '^003E' <<<"$OUT")"
[ "$(grep -cE '^00[0-7]E  (XX )+ +\.+$' <<<"$OUT")" = 8 ] && ok "--redact: all 8 rows masked" || bad "masked rows: $(grep -cE '^00[0-7]E  (XX )+ +\.+$' <<<"$OUT")"
! grep -q 'PASSWORD\|50 41 53 53' <<<"$OUT" && ok "--redact: the bytes appear nowhere" || bad "the CMOS bytes leak with --redact"
has 'Bytes masked \(Redact Reports is on\)' && ok "--redact: says the bytes were masked" || bad "no masked note"
unset SRS_TEST_CMOS_FILE

echo "== I/O GPIO pin table (gpio.txt): tabs expanded so the rows line up"
printf 'gpiochip0: GPIOs 512-767, parent: platform/AMDI0030:00, AMD GPIO:\n#0\t    ∅|   input  high\n#10\t    ∅|   input  low\n#100\t↑ ∅|\n' > "$ROOT/gpio.txt"
page io-gpio
col() { local l; l=$(grep -E "^$1[[:space:]]" <<<"$OUT" | head -1); l=${l%%"$2"*}; echo ${#l}; }
c0=$(col '#0' '∅'); c10=$(col '#10' '∅')
[ -n "$c0" ] && [ "$c0" = "$c10" ] && [ "$c0" -gt 3 ] && ok "#0 and #10: ∅ at the same column ($c0)" || bad "∅ columns: #0 $c0, #10 $c10"
! grep -qP '\t' <<<"$(grep -E '^#' <<<"$OUT")" && ok "no tab characters left in the rows" || bad "tabs left in the rows"
a100=$(col '#100' '↑'); [ "$a100" = 8 ] && ok "#100: the text after the tab starts at column 8" || bad "#100 column $a100"
: > "$ROOT/gpio.txt"
page io-gpio
has "The kernel's GPIO table is empty" && ok "an empty gpio.txt: 'empty'" || bad "empty gpio.txt: $(grep -A1 'Pin states' <<<"$OUT" | tail -1)"
rm -f "$ROOT/gpio.txt"

echo "== GPU VBIOS image (vbios-<pci>.bin)"
CARD=""
for c in /sys/class/drm/card[0-9]*; do
    [ -e "$c/device/driver" ] && [[ ${c##*/} != *-* ]] && { CARD=${c##*/}; break; }
done
if [ -z "$CARD" ]; then skip "no DRM GPU: the VBIOS checks are skipped"
else
    SLOT=$(basename "$(readlink -f "/sys/class/drm/$CARD/device")")
    # a 512-byte image: 55 AA, length 1 x 512, PCIR at 0x1C (vendor 1002, device 7550, length 1, code type 0 = x86),
    # an AMD-style part number, and the last-image flag ($1 = 00 clear, 80 set); the last byte makes the sum 0 mod 256
    mkimg() {
        local f=$TMP/img.bin s
        { hexbytes 55 AA 01; head -c 21 /dev/zero; hexbytes 1C 00 00 00; printf 'PCIR'; hexbytes 02 10 50 75 00 00 18 00 00 00 00 03 01 00 00 00 00 "$1" 00 00
          head -c $((0x80 - 0x34)) /dev/zero; printf '113-SRSTEST-001'; head -c $((511 - 0x80 - 15)) /dev/zero; } > "$f"
        s=$(od -An -tu1 -v "$f" | tr -s ' ' '\n' | awk 'NF { s += $1 } END { print s % 256 }')
        { cat "$f"; hexbytes "$(printf %02x $(( (256 - s) % 256 )))"; } > "$ROOT/vbios-$SLOT.bin"
    }
    mkimg 00
    [ "$(stat -c %s "$ROOT/vbios-$SLOT.bin")" = 512 ] || bad "the made-up image is not 512 bytes"
    page "gpu-bios:$CARD"
    has '^Signature +55 AA \(PCI expansion ROM\)' && ok "$CARD ($SLOT): signature 55 AA" || bad "signature: $(grep Signature <<<"$OUT")"
    has '^Checksum +valid \(first image, 512 bytes' && ok "checksum valid" || bad "checksum: $(grep Checksum <<<"$OUT")"
    has '^Part number +113-SRSTEST-001$' && ok "part number 113-SRSTEST-001" || bad "part: $(grep 'Part number' <<<"$OUT")"
    has '^1 +0x00000 .* 1002:7550 +x86 \(legacy\) +no, more follow$' && ok "image 1: 1002:7550, x86, 'no, more follow'" || bad "image row: $(grep -E '^1 ' <<<"$OUT")"
    has "The driver's copy ends after this image" && ok "says the driver's copy ends before the last image" || bad "no 'copy ends' note"
    if [ -e "/sys/class/drm/$CARD/device/rom" ]; then
        has '^Option ROM +present: the driver.s copy of its image is shown below' && ok "Option ROM line points at the image below" || bad "Option ROM: $(grep 'Option ROM' <<<"$OUT")"
    else skip "$CARD has no ROM BAR: the Option ROM line is not checked"; fi
    mkimg 80
    page "gpu-bios:$CARD"
    has '^1 +0x00000 .* x86 \(legacy\) +yes$' && ! has "The driver's copy ends" && ok "last-image flag set: 'yes', no 'copy ends' note" || bad "last image: $(grep -E '^1 ' <<<"$OUT")"
    printf 'MZ not a ROM' > "$ROOT/vbios-$SLOT.bin"
    page "gpu-bios:$CARD"
    has '^Signature +4D 5A, not 55 AA' && ok "not 55 AA: said so" || bad "bad signature: $(grep Signature <<<"$OUT")"
fi

! grep -qiE 'segmentation|core dumped' <<<"$OUT" && ok "no crash" || bad "crash"
[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
