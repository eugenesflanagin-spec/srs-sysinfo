#!/bin/bash
# srs-root - collect root-only data for srs-gui (run through pkexec; one prompt).
# Fills ~/.cache/srs/root/ of the invoking user (PKEXEC_UID, or SUDO_UID under sudo).
# Read-only on the hardware side: dmidecode, decode-dimms, smartctl/nvme queries, MSR reads,
# lspci config dumps, copies of /sys/firmware/acpi/tables and AMD memory-controller registers (SMN reads through
# ryzen_smu: an address is written to its smn file, which only selects what to read), the EBDA (/dev/mem below 1 MB),
# and copies of the kernel's debugfs GPIO table and amdgpu VBIOS copy. Nothing is written to devices.
# `srs-root --user NAME` collects for NAME (used by the srs-root@NAME.service boot unit).
#
# 2.6.0, how the data gets to the user (root writes nothing inside the user's home):
#   1. the arguments are checked and anyone but root is refused BEFORE anything is created or changed;
#   2. everything is collected in a new folder only root can enter (/run/srs-root.XXXXXX), removed on every exit;
#   3. when the collection is complete the folder is given to the user and put in place of ~/.cache/srs/root BY A
#      PROCESS RUNNING AS THAT USER (install_cache below), so a link planted in the user's cache can never make root
#      write somewhere else, and a failed or interrupted run leaves the earlier data as it was;
#   4. one srs-root at a time (a lock in /run), so the boot unit and the window's Run as root cannot mix their files.
# Exit codes: 0 done; 2 wrong arguments, not root, or no such user (nothing was changed); 3 --acpi-eval without the
# acpi_call module; 4 another srs-root did not finish in time; 5 collected, but the cache could not be replaced (the
# earlier data is unchanged).
set -u
usage() {
  echo "usage: pkexec srs-root                              collect root-only data for the user who asks"
  echo "       srs-root --user NAME                         the same for NAME (as root; the boot unit)"
  echo "       pkexec srs-root --acpi-eval METHOD [ARG...]  evaluate one ACPI method"
  echo "       srs-root -h | --help                         this text"
}
# 1. the arguments, then root. Nothing above this line and nothing in this block creates, changes or deletes a file.
case "${1:-}" in
  "")          [ $# -eq 0 ] || { echo "srs-root: an empty argument" >&2; usage >&2; exit 2; } ;;
  --user)      [ $# -eq 2 ] && [ -n "$2" ] || { echo "srs-root: --user needs one name" >&2; usage >&2; exit 2; } ;;
  --acpi-eval) ;;
  -h|--help)   usage; exit 0 ;;
  *)           echo "srs-root: unknown option: $1" >&2; usage >&2; exit 2 ;;
esac
me=$(id -u)
# Test seams (tests/srs-root-args-test.sh), looked at ONLY when not root, where srs-root can do nothing a user could
# not do anyway: SRS_ROOT_TEST_HOME stands in for the user's home, and with SRS_ROOT_TEST_DRYRUN=1 the checks run and
# srs-root stops with "dry run" where the work would begin. Without the dry run, a caller who is not root is refused.
test_home="" dry=""
if [ "$me" != 0 ]; then
  test_home=${SRS_ROOT_TEST_HOME:-}
  [ -n "$test_home" ] && [ -d "$test_home" ] && [ -n "${SRS_ROOT_TEST_DRYRUN:-}" ] && dry=1
  [ -n "$dry" ] || { echo "srs-root: needs root (use: pkexec srs-root). Nothing was changed." >&2; exit 2; }
  uid=$me
else
  uid=${PKEXEC_UID:-${SUDO_UID:-0}}   # the user who asked: through pkexec, through sudo, or root itself
fi
if [ "${1:-}" = "--user" ]; then
  uid=$(getent passwd "$2" | cut -d: -f3)
  [ -n "$uid" ] || { echo "srs-root: no such user: $2" >&2; exit 2; }
fi
case $uid in ""|*[!0-9]*) echo "srs-root: cannot resolve the invoking user" >&2; exit 2 ;; esac
user=$(getent passwd "$uid" | cut -d: -f1)
gid=$(getent passwd "$uid" | cut -d: -f4)
home=$(getent passwd "$uid" | cut -d: -f6)
[ -n "$test_home" ] && home=$test_home
[ -n "$user" ] && [ -n "$gid" ] && [ -d "$home" ] || { echo "srs-root: cannot resolve the invoking user" >&2; exit 2; }
out="$home/.cache/srs/root"
log() { echo "srs-root: $*"; }

# Everything inside the user's home is done by a process running AS THAT USER (never as root): runuser, or setpriv
# where runuser cannot be used. Root itself (uid 0) and the tests (not root) need no switch.
runas=""
if [ "$me" = 0 ] && [ "$uid" != 0 ]; then
  if command -v runuser >/dev/null && runuser -u "$user" -- true 2>/dev/null; then runas=runuser
  elif command -v setpriv >/dev/null && setpriv --reuid "$uid" --regid "$gid" --init-groups -- true 2>/dev/null; then runas=setpriv
  else echo "srs-root: cannot switch to $user (needs runuser or setpriv from util-linux). Nothing was changed." >&2; exit 2; fi
fi
as_user() {
  case $runas in
    runuser) runuser -u "$user" -- "$@" ;;
    setpriv) setpriv --reuid "$uid" --regid "$gid" --init-groups -- "$@" ;;
    *)       "$@" ;;
  esac
}
# one srs-root at a time. The lock file is in /run, where only root can create files (not in /run/lock, which
# everyone may write to on some systems). lock SECONDS: 0 = got it.
lock() {
  ( umask 077; : >> /run/srs-root.lock ) 2>/dev/null
  exec 8< /run/srs-root.lock || return 1
  flock -w "$1" 8
}
# install_cache OUT NEW - runs AS THE USER. The finished collection NEW becomes the cache OUT. OUT is replaced only
# when the copy is whole; if anything fails before that, OUT is as it was. The ACPI call log is carried over.
install_cache() {
  local out=$1 new=$2 name=${1##*/} tmp old="" d
  mkdir -p "${out%/*}" || return 1
  for d in "$out".old.*; do   # an earlier run that was cut off in the middle of the swap: put its old cache back
    [ -d "$d" ] || continue
    if [ ! -e "$out" ] && [ ! -L "$out" ] && [ -d "$d/$name" ]; then mv "$d/$name" "$out"; fi
    rm -rf "$d"
  done
  rm -rf "$out".new.*
  tmp=$(mktemp -d "$out.new.XXXXXX") || return 1
  cp -a "$new/." "$tmp/" && chmod 700 "$tmp" || { rm -rf "$tmp"; return 1; }   # serial numbers inside: the user's alone
  [ -s "$tmp/collected.txt" ] || { rm -rf "$tmp"; return 1; }   # written last: without it the collection is not whole
  if [ -f "$out/acpi-calls.txt" ] && [ ! -L "$out/acpi-calls.txt" ]; then
    cp -p "$out/acpi-calls.txt" "$tmp/" || { rm -rf "$tmp"; return 1; }
  fi
  if [ -e "$out" ] || [ -L "$out" ]; then
    old=$(mktemp -d "$out.old.XXXXXX") || { rm -rf "$tmp"; return 1; }
    mv "$out" "$old/$name" || { rm -rf "$tmp"; rmdir "$old"; return 1; }
  fi
  if mv -T "$tmp" "$out"; then
    [ -n "$old" ] && rm -rf "$old"
    return 0
  fi
  [ -n "$old" ] && mv "$old/$name" "$out" && rmdir "$old"   # could not put the new one in place: the old one back
  rm -rf "$tmp"
  return 1
}

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
  [ -z "$dry" ] || { echo "srs-root: dry run (test): would evaluate $m$args for $user"; exit 0; }
  modprobe acpi_call 2>/dev/null
  [ -w /proc/acpi/call ] || { echo "srs-root: the acpi_call module is not installed (Arch: acpi_call-dkms)" >&2; exit 3; }
  printf '%s%s' "$m" "$args" > /proc/acpi/call
  res=$(tr -d '\000' < /proc/acpi/call)
  # the log line is appended by a process running as the user, and not while a collection swaps the cache
  lock 30 || true
  printf '%s\t%s%s\t%s\n' "$(date -Is)" "$m" "$args" "$res" |
    as_user sh -c 'mkdir -p "$1" && cat >> "$1/acpi-calls.txt"' sh "$out" ||
    echo "srs-root: the call could not be logged to $out/acpi-calls.txt" >&2
  printf '%s\n' "$res"
  exit 0
fi

[ -z "$dry" ] || { echo "srs-root: dry run (test): would collect for $user into $out"; exit 0; }
# 4. one at a time; 2. a new folder only root can enter, removed on every exit
lock 180 || { echo "srs-root: another srs-root is still running; nothing was collected this time. Nothing was changed." >&2; exit 4; }
new=$(mktemp -d /run/srs-root.XXXXXX 2>/dev/null) || new=$(mktemp -d "${TMPDIR:-/tmp}/srs-root.XXXXXX") ||
  { echo "srs-root: cannot create a folder to collect into. Nothing was changed." >&2; exit 2; }
trap 'cd /; rm -rf "$new"' EXIT
trap 'exit 130' HUP INT TERM
cd "$new" || exit 2

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
# 2.4: UEFI System Table pointers and the firmware-update table (ESRT): root-only files, copied as text
rm -f efi-systab.txt esrt.txt
[ -r /sys/firmware/efi/systab ] && cp /sys/firmware/efi/systab efi-systab.txt
for d in /sys/firmware/efi/esrt/entries/entry*; do
  [ -d "$d" ] || continue
  for f in fw_class fw_type fw_version lowest_supported_fw_version capsule_flags last_attempt_version last_attempt_status; do
    [ -r "$d/$f" ] && printf '%s %s %s\n' "${d##*/}" "$f" "$(tr -d '\n' < "$d/$f")"
  done
done > esrt.txt
[ -s esrt.txt ] || rm -f esrt.txt
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
# 2.4 (experimental): Intel memory timings from the memory controller's register window (MCHBAR), read with aligned
# 32-bit loads by SRS's own reader; the window is never switched on. Core 6th gen to Core Ultra.
rm -f intel-imc.txt
if [ "$vend" = GenuineIntel ]; then
  rd=$(dirname "$(readlink -f "$0")")/sysreport-search
  [ -x "$rd" ] || rd=$(command -v sysreport-search)
  if [ -n "$rd" ]; then "$rd" intel-imc-dump > intel-imc.txt 2>&1
  else echo "error SRS's own reader (sysreport-search) is not installed next to srs-root, so the Intel timings could not be read" > intel-imc.txt; fi
  log intel-imc
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
# 3. the collection is complete: give the folder to the user (root writes nothing into it after this line) and let a
# process running as the user put it in place of the cache
chown -R "$uid:$gid" "$new"
if as_user bash -c "$(declare -f install_cache)"'; install_cache "$1" "$2"' bash "$out" "$new"; then
  log "done -> $out"
else
  echo "srs-root: the data was collected but could not be put into $out; the earlier data there is unchanged" >&2
  exit 5
fi
