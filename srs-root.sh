#!/bin/bash
# srs-root - collect root-only data for srs-gui (run through pkexec; one prompt).
# Writes ~/.cache/srs/root/ of the invoking user and hands the files back to them.
# Read-only on the hardware side: dmidecode, decode-dimms, smartctl/nvme queries, MSR reads,
# lspci config dumps and copies of /sys/firmware/acpi/tables. Nothing is written to devices.
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
  for c in /dev/cpu/[0-9]*; do printf " %-18s" "CPU-${c##*/}"; done; echo
  while read -r addr name; do
    printf "%-12s %-34s" "$addr" "$name"
    for c in /dev/cpu/[0-9]*; do
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
date > collected.txt
chown -R "$uid" "$out"
log "done -> $out"
