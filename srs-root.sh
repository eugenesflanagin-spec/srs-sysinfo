#!/bin/bash
# srs-root - collect root-only data for srs-gui (run through pkexec; one prompt).
# Writes ~/.cache/srs/root/ of the invoking user and hands the files back to them.
# Read-only on the hardware side: dmidecode, decode-dimms, smartctl/nvme queries, MSR reads,
# lspci config dumps, copies of /sys/firmware/acpi/tables and AMD memory-controller registers (SMN reads through
# ryzen_smu: an address is written to its smn file, which only selects what to read). Nothing is written to devices.
# `srs-root --user NAME` collects for NAME (used by the srs-root@NAME.service boot unit).
set -u
uid=${PKEXEC_UID:-$(id -u)}
if [ "${1:-}" = "--user" ] && [ -n "${2:-}" ]; then
  [ "$(id -u)" = 0 ] || { echo "srs-root: --user needs root" >&2; exit 2; }
  uid=$(getent passwd "$2" | cut -d: -f3)
  [ -n "$uid" ] || { echo "srs-root: no such user: $2" >&2; exit 2; }
fi
user=$(getent passwd "$uid" | cut -d: -f1)
home=$(getent passwd "$uid" | cut -d: -f6)
[ -n "$user" ] && [ -d "$home" ] || { echo "srs-root: cannot resolve the invoking user" >&2; exit 2; }
out="$home/.cache/srs/root"
mkdir -p "$out" || exit 2
# ~/.cache/srs belongs to the user (srs-gui keeps its own files there too), only root/ is ours
[ -d "$home/.cache/srs" ] && [ ! -L "$home/.cache/srs" ] && chown -h "$uid" "$home/.cache/srs" 2>/dev/null || true   # never follow a symlink as root
cd "$out" || exit 2
log() { echo "srs-root: $*"; }

# 2.4: `srs-root --acpi-eval METHOD [ARG...]` evaluates ONE ACPI method through the acpi_call module, for the user's
# Tools > ACPI method evaluation (asked twice in the GUI first). This runs BIOS code and can change hardware state, so
# the method path and every argument are checked here, as root: an ACPI name path (\_SB.PCI0.GPP0._STA style) and
# arguments that are numbers (decimal or 0x hex), "strings" (letters, digits, space . _ -) or buffers bXXXX...
# Every call is logged to acpi-calls.txt with the time, the call and the result.
if [ "${1:-}" = "--acpi-eval" ]; then
  shift
  m=${1:-}; shift || true
  [[ $m =~ ^\\?(\^)*[A-Z_][A-Z0-9_]{0,3}(\.[A-Z_][A-Z0-9_]{0,3})*$ ]] || { echo "srs-root: not an ACPI method path: $m" >&2; exit 2; }
  [ "${m:0:1}" = '\' ] || m="\\$m"
  args=""
  for a in "$@"; do
    [[ $a =~ ^(0x[0-9a-fA-F]{1,16}|[0-9]{1,20}|\"[A-Za-z0-9\ ._-]{0,64}\"|b([0-9a-fA-F]{2}){1,256})$ ]] || { echo "srs-root: argument not allowed: $a" >&2; exit 2; }
    args="$args $a"
  done
  [ $# -le 7 ] || { echo "srs-root: at most 7 arguments" >&2; exit 2; }
  modprobe acpi_call 2>/dev/null
  [ -w /proc/acpi/call ] || { echo "srs-root: the acpi_call module is not installed (Arch: acpi_call-dkms)" >&2; exit 3; }
  printf '%s%s' "$m" "$args" > /proc/acpi/call
  res=$(tr -d '\000' < /proc/acpi/call)
  printf '%s\t%s%s\t%s\n' "$(date -Is)" "$m" "$args" "$res" >> acpi-calls.txt
  chown "$uid" acpi-calls.txt 2>/dev/null
  printf '%s\n' "$res"
  exit 0
fi

command -v dmidecode >/dev/null && { dmidecode > dmidecode.txt 2>&1; log dmidecode; }
command -v decode-dimms >/dev/null && { decode-dimms > spd.txt 2>&1; log decode-dimms; }
for d in /sys/block/*; do
  n=${d##*/}
  case $n in loop*|ram*|zram*|dm-*|sr*) continue ;; esac
  # USB bridges: SMART pass-through is meaningless on sticks and can upset flaky enclosures
  if [ -z "${SRS_SMART_USB:-}" ] && readlink -f "/sys/block/$n" | grep -q '/usb[0-9]'; then
    echo "skipped: USB-attached (set SRS_SMART_USB=1 to include)" > "smart-$n.txt"; continue
  fi
  command -v smartctl >/dev/null && smartctl -x "/dev/$n" > "smart-$n.txt" 2>&1
  case $n in nvme*) command -v nvme >/dev/null && nvme smart-log "/dev/$n" > "nvme-$n.txt" 2>&1 ;; esac
done
log smart
rm -rf acpi-tables && mkdir acpi-tables && cp -r /sys/firmware/acpi/tables/. acpi-tables/ 2>/dev/null; log acpi-tables
command -v lspci >/dev/null && { lspci -vvvnnxxx > pci-all.txt 2>&1; log lspci; }
cat /proc/ioports > ioports.txt; cat /proc/iomem > iomem.txt
dmesg > dmesg.txt 2>&1
command -v smbstatus >/dev/null && smbstatus > smbstatus.txt 2>&1
# MSRs (AMD family 17h+ and architectural): read-only via /dev/cpu/N/msr
modprobe msr 2>/dev/null
{
  printf "%-12s %-34s" "MSR" "Name"
  CPUS=$(printf '%s\n' /dev/cpu/[0-9]* | sort -V)   # CPU-0, 1, 2 ... 15 (the glob gave 0, 1, 10, 11 ... 2)
  for c in $CPUS; do printf " %-18s" "CPU-${c##*/}"; done; echo
  while read -r addr name; do
    printf "%-12s %-34s" "$addr" "$name"
    for c in $CPUS; do
      v=$(dd if="$c/msr" bs=8 count=1 skip=$((addr)) iflag=skip_bytes 2>/dev/null | od -An -tx8 | tr -d ' ')
      printf " %-18s" "${v:-n/a}"
    done
    echo
  done <<'EOF'
0x0000001B APIC_BASE
0x000000E7 MPERF
0x000000E8 APERF
0xC0000080 EFER
0xC0010015 HWCR
0xC0010061 PSTATE_CURRENT_LIMIT
0xC0010062 PSTATE_CONTROL
0xC0010063 PSTATE_STATUS
0xC0010064 PSTATE_DEF_0
0xC0010065 PSTATE_DEF_1
0xC0010066 PSTATE_DEF_2
0xC0010293 HW_PSTATE_STATUS
0xC00102B0 CPPC_CAP1
0xC00102B1 CPPC_ENABLE
0xC00102B3 CPPC_REQUEST
0xC0010299 RAPL_PWR_UNIT
0xC001029A CORE_ENERGY_STAT
0xC001029B PKG_ENERGY_STAT
EOF
} > msrs.txt 2>&1
log msrs
# 2.4: AMD memory controller (UMC) registers for the Memory Timings page, read through the ryzen_smu driver.
# Writing exactly 4 bytes (an SMN address) to its smn file makes the driver READ that address; never 8 bytes (= a write).
# Only Ryzen 7000 / 9000 desktop (family 19h models 60h-7Fh, 1Ah models 40h-4Fh), whose register map was checked.
fam=$(awk -F': ' '/^cpu family/{print $2; exit}' /proc/cpuinfo); mod=$(awk -F': ' '/^model\t/{print $2; exit}' /proc/cpuinfo)
vend=$(awk -F': ' '/^vendor_id/{print $2; exit}' /proc/cpuinfo)
SMN=/sys/kernel/ryzen_smu_drv/smn
rm -f umc.txt
if [ "$vend" = AuthenticAMD ] && { { [ "${fam:-0}" = 25 ] && [ "${mod:-0}" -ge 96 ] && [ "${mod:-0}" -le 127 ]; } ||
                                   { [ "${fam:-0}" = 26 ] && [ "${mod:-0}" -ge 64 ] && [ "${mod:-0}" -le 79 ]; }; }; then
  if [ -w "$SMN" ]; then
    tmp=$(mktemp)
    smn_rd() {   # one address -> 8 hex digits; the 4-byte request goes out in one write() through dd
      local a=$1
      printf "\\x$(printf %02x $((a & 255)))\\x$(printf %02x $(((a >> 8) & 255)))\\x$(printf %02x $(((a >> 16) & 255)))\\x$(printf %02x $(((a >> 24) & 255)))" > "$tmp"
      [ "$(stat -c %s "$tmp")" = 4 ] || return 1
      dd if="$tmp" of="$SMN" bs=4 count=1 status=none 2>/dev/null || return 1
      od -An -tx4 -N4 "$SMN" | tr -d ' '
    }
    (
      flock -w 10 9 || exit 1   # one SMN user at a time (the driver keeps a single result)
      echo "# AMD UMC registers, family $fam model $mod, $(date -Is)"
      for ch in 0 1 2 3; do
        for off in 0x50000 0x50004 0x50008 0x5000C 0x50020 0x50024 0x50028 0x5002C 0x50040 0x50044 0x50048 0x5004C \
                   0x50100 0x5012C 0x50130 0x5014C 0x50198 $(seq 0x50200 4 0x502E0 | xargs printf '0x%X ') 0x50DF0 0x50DF4; do
          a=$(((ch << 20) | off)); v1=$(smn_rd $a) && v2=$(smn_rd $a) || continue
          [ "$v1" = "$v2" ] && printf 'ch%d 0x%05X %s\n' "$ch" "$off" "$v1"   # a value that changed between reads is left out
        done
      done
    ) 9>/run/lock/srs-smn.lock > umc.txt
    rm -f "$tmp"
    log umc
  else
    echo "# not read: the ryzen_smu driver is not loaded (no $SMN)" > umc.txt
  fi
fi
# 2.4: the BIOS's AMD Overclocking Data (AOD) table: memory ProcODT, RTT, drive strengths and set voltages. An SSDT names
# its place (OperationRegion AODE, SystemMemory); it lies in ACPI NVS memory, which kernels with IO_STRICT_DEVMEM keep
# from /dev/mem unless booted with iomem=relaxed. Only that region is read, and only when the kernel allows it.
rm -f aod.bin aod.txt
aodt=$(grep -l -a "AODE" /sys/firmware/acpi/tables/SSDT* 2>/dev/null | head -1)
if [ "$vend" = AuthenticAMD ] && [ -n "$aodt" ]; then
  o=$(grep -abo "AODE" "$aodt" | head -1 | cut -d: -f1)
  read -r -a b < <(od -An -tu1 -v -j $((o - 2)) -N 24 "$aodt" | tr '\n' ' ')
  aml_int() {   # AML integer at index $1 of b[]: ByteConst 0x0A, WordConst 0x0B, DWordConst 0x0C, QWordConst 0x0E -> "value next-index"
    local i=$1 n k v=0
    case ${b[$i]:-0} in 10) n=1 ;; 11) n=2 ;; 12) n=4 ;; 14) n=8 ;; *) echo "0 0"; return ;; esac
    for ((k = n; k >= 1; k--)); do v=$(( (v << 8) | ${b[$((i + k))]:-0} )); done
    echo "$v $((i + n + 1))"
  }
  if [ "${b[0]:-}" = 91 ] && [ "${b[1]:-}" = 128 ] && [ "${b[6]:-}" = 0 ]; then   # 5B 80 'AODE' 00 = OperationRegion, SystemMemory
    read -r aaddr nx < <(aml_int 7); read -r alen _ < <(aml_int "$nx")
    if [ "$aaddr" -gt 0 ] && [ "$alen" -gt 0 ] && [ "$alen" -le 65536 ]; then
      if dd if=/dev/mem of=aod.bin bs="$alen" count=1 skip="$aaddr" iflag=skip_bytes status=none 2>aod.err && [ -s aod.bin ]; then
        printf 'AOD table at 0x%X, %d bytes, from %s\n' "$aaddr" "$alen" "${aodt##*/}" > aod.txt
      else
        rm -f aod.bin
        printf 'not readable: AOD table at 0x%X (%d bytes) is in ACPI NVS memory, which this kernel keeps from /dev/mem (%s)\n' \
          "$aaddr" "$alen" "$(tr -d '\n' < aod.err | sed 's/^dd: //')" > aod.txt
      fi
    fi
  fi
  rm -f aod.err
  log aod
fi
# 2.4, opt-in (Options > Read DIMM voltages (PMIC)): the set voltages of each DDR5 module's PMIC, its registers 0x00-0x3F
# read over SMBus with i2cdump (read byte data; nothing is written to the PMIC; no -f, so a device a driver owns is skipped)
rm -f pmic-*.txt
if grep -qs '^Read DIMM PMIC=1' "$home/.config/srs/SRSDEF.ini" && command -v i2cdump >/dev/null; then
  modprobe i2c-dev 2>/dev/null
  for d in /sys/bus/i2c/drivers/spd5118/*-00[5][0-7]; do
    [ -e "$d/eeprom" ] || continue
    n=${d##*/}; bus=${n%%-*}; hub=$((16#${n#*-}))
    t=$(od -An -tu1 -j 200 -N 1 "$d/eeprom" | tr -d ' ')   # SPD byte 200: PMIC0 device type, bit 7 = installed
    [ $((t & 128)) = 128 ] && [ $((t & 127)) -le 6 ] || continue
    pm=$((0x48 | (hub & 7)))
    { printf '# PMIC of module %s (SPD hub 0x%02X) at 0x%02X, type %d\n' "$n" "$hub" "$pm" $((t & 127))
      i2cdump -y -r 0x00-0x3F "$bus" "$pm" b 2>&1; } > "pmic-${n#*-}.txt"
  done
  log pmic
fi
date > collected.txt
chown -R "$uid" "$out"
log "done -> $out"
