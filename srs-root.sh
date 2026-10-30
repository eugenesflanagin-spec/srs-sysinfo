#!/bin/bash
# srs-root - collect root-only data for srs-gui (run through pkexec; one prompt).
# Writes ~/.cache/srs/root/ of the invoking user and hands the files back to them.
# Read-only on the hardware side: dmidecode, decode-dimms, smartctl/nvme queries, MSR reads,
# lspci config dumps, copies of /sys/firmware/acpi/tables and AMD memory-controller registers (SMN reads through
# ryzen_smu: an address is written to its smn file, which only selects what to read), the EBDA (/dev/mem below 1 MB),
# and copies of the kernel's debugfs GPIO table and amdgpu VBIOS copy. Nothing is written to devices.
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
# 2.4: RAPL power at the moment of this run: the energy counters read twice, one second apart (AMD; read-only)
rm -f rapl.txt
if [ -r /dev/cpu/0/msr ] && grep -q AuthenticAMD /proc/cpuinfo; then
  rmsr() { dd if="/dev/cpu/$1/msr" bs=8 count=1 skip=$(($2)) iflag=skip_bytes 2>/dev/null | od -An -tu8 | tr -d ' '; }
  CPUS=$(printf '%s\n' /dev/cpu/[0-9]* | sed 's|.*/||' | sort -n)
  t1=$(date +%s.%N); p1=$(rmsr 0 0xC001029B)
  declare -A c1; for c in $CPUS; do c1[$c]=$(rmsr "$c" 0xC001029A); done
  sleep 1
  t2=$(date +%s.%N); p2=$(rmsr 0 0xC001029B)
  { echo "seconds $(awk -v a="$t1" -v b="$t2" 'BEGIN { printf "%.6f", b - a }')"
    echo "package ${p1:-0} ${p2:-0}"
    for c in $CPUS; do echo "cpu$c ${c1[$c]:-0} $(rmsr "$c" 0xC001029A)"; done; } > rapl.txt
  log rapl
fi
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
# 2.4, opt-in (Options > Read the embedded controller): the EC's 256 registers through ec_sys, loaded read-only
# (write_support stays 0); unloaded again if this run loaded it
rm -f ec.bin
if grep -qs '^Read EC=1' "$home/.config/srs/SRSDEF.ini"; then
  had=0; [ -d /sys/module/ec_sys ] && had=1
  modprobe ec_sys write_support=0 2>/dev/null
  [ -r /sys/kernel/debug/ec/ec0/io ] && dd if=/sys/kernel/debug/ec/ec0/io of=ec.bin bs=256 count=1 status=none 2>/dev/null
  [ "$had" = 0 ] && modprobe -r ec_sys 2>/dev/null
  log ec
fi
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
# 2.4: the Extended BIOS Data Area. The BIOS Data Area word at 0x40E holds its segment (0 = none, normal on UEFI);
# the first EBDA byte is its size in KiB. Only memory below 1 MB is read (kernels with STRICT_DEVMEM allow that range).
rm -f ebda.txt ebda.bin ebda.err
if [ -e /dev/mem ]; then
  seg=$(dd if=/dev/mem bs=2 count=1 skip=$((0x40E)) iflag=skip_bytes status=none 2>ebda.err | od -An -tu2 | tr -d ' ')
  if [ -z "$seg" ]; then
    printf 'error %s\n' "$(tr -d '\n' < ebda.err | sed 's/^dd: //')" > ebda.txt
  else
    ea=$((seg * 16))
    { printf 'segment 0x%04X\n' "$seg"; printf 'address 0x%X\n' "$ea"; } > ebda.txt
    if [ "$seg" -gt 0 ] && [ "$ea" -lt $((0xA0000)) ]; then
      kib=$(dd if=/dev/mem bs=1 count=1 skip="$ea" iflag=skip_bytes status=none 2>>ebda.err | od -An -tu1 | tr -d ' ')
      if [ -n "$kib" ]; then
        echo "size_kib $kib" >> ebda.txt
        len=$((kib * 1024)); [ "$len" -gt 65536 ] && len=65536; [ "$len" -gt $((0xA0000 - ea)) ] && len=$((0xA0000 - ea))
        if [ "$len" -gt 0 ]; then
          if dd if=/dev/mem of=ebda.bin bs="$len" count=1 skip="$ea" iflag=skip_bytes status=none 2>>ebda.err && [ -s ebda.bin ]; then
            echo "bytes $(stat -c %s ebda.bin)" >> ebda.txt
          else
            rm -f ebda.bin
            printf 'error %s\n' "$(tr -d '\n' < ebda.err | sed 's/^dd: //')" >> ebda.txt
          fi
        fi
      else
        printf 'error %s\n' "$(tr -d '\n' < ebda.err | sed 's/^dd: //')" >> ebda.txt
      fi
    elif [ "$seg" -gt 0 ]; then
      echo "invalid pointer (at or above 0xA0000), not read" >> ebda.txt
    fi
  fi
  rm -f ebda.err
  log ebda
else
  echo "error /dev/mem is not present on this kernel" > ebda.txt
fi
# 2.4: the GPU driver's own cached copy of each AMD VBIOS (debugfs, root only; a memory copy, the GPU is not accessed).
# The PCI ROM BAR (sysfs .../rom) is never used: reading it needs a write to enable it first.
rm -f vbios-*.bin
if [ -d /sys/kernel/debug/dri ]; then
  for v in /sys/kernel/debug/dri/*/amdgpu_vbios; do
    [ -r "$v" ] || continue
    d=${v%/amdgpu_vbios}
    pci=$(sed -n 's/.* dev=\([^ ]*\).*/\1/p' "$d/name" 2>/dev/null)
    [ -z "$pci" ] && case ${d##*/} in ????:??:??.?) pci=${d##*/} ;; esac
    [ -z "$pci" ] && pci=$(basename "$(readlink -f "/sys/class/drm/card${d##*/}/device" 2>/dev/null)")
    case $pci in ????:??:??.?) ;; *) continue ;; esac
    [ -e "vbios-$pci.bin" ] && continue   # the same GPU under its minor number and its PCI name
    cat "$v" > "vbios-$pci.bin" 2>/dev/null && [ -s "vbios-$pci.bin" ] || rm -f "vbios-$pci.bin"
  done
  log vbios
fi
# 2.4: the kernel's GPIO table (chips, lines, names, direction and level of lines in use); debugfs, root only
rm -f gpio.txt
[ -r /sys/kernel/debug/gpio ] && { cat /sys/kernel/debug/gpio > gpio.txt 2>&1; log gpio; }
date > collected.txt
chown -R "$uid" "$out"
log "done -> $out"
