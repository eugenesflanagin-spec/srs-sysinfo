#!/bin/bash
# Full native system report — the thorough-SIV-style dump, from Linux's own data sources.
# Writes a dated folder with an index.html linking each section, then opens it.
set -u
SELF_DIR=$(cd "$(dirname "$0")" && pwd)  # before the cd below
OUT="$HOME/sysreports/sysreport-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT" || exit 1
cd "$OUT" || exit 1

run() { # run <file> <cmd...>
  local f="$1"; shift
  { echo "### $* ###"; "$@"; } >"$f" 2>&1 || echo "(exit $? — may need root, see note in index)" >>"$f"
}

# No-root sections
run inxi.txt            inxi -c 0 -Fxxxzart
run lscpu.txt           lscpu
run cpuid.txt           sh -c 'command -v cpuid >/dev/null && cpuid -1 || echo "cpuid not installed"'
run sensors.txt         sensors -A
run lspci.txt           lspci -vvnn
run lsusb.txt           lsusb -tv
run drm.txt             sh -c 'for d in /sys/class/drm/card*/device; do echo "== $d =="; cat "$d/uevent" 2>/dev/null; done'
run meminfo.txt         cat /proc/meminfo
run lsblk.txt           lsblk -O
run mounts.txt          findmnt -A
run kernel.txt          sh -c 'uname -a; echo; cat /proc/cmdline'
run devices.txt         "$SELF_DIR/sysreport-search" devices  # SIV-style ID resolution

# Root sections via pkexec (one prompt); fall back to non-root note
if command -v pkexec >/dev/null; then
  pkexec sh -c '
    dmidecode            > "'"$OUT"'/dmidecode.txt"      2>&1
    command -v decode-dimms >/dev/null && decode-dimms > "'"$OUT"'/spd-dimms.txt" 2>&1
    hwinfo --all         > "'"$OUT"'/hwinfo-all.txt"     2>&1
    lshw -html           > "'"$OUT"'/lshw.html"          2>&1
    for d in /dev/nvme?n1 /dev/sd?; do
      [ -e "$d" ] && { echo "===== $d ====="; smartctl -x "$d"; } >> "'"$OUT"'/smart.txt" 2>&1
    done
    chown -R '"$(id -u)":"$(id -g)"' "'"$OUT"'"
  ' || echo "pkexec declined — root sections (dmidecode, SPD, hwinfo, lshw, SMART) skipped" > "$OUT/root-sections-skipped.txt"
fi

# Build index.html
{
  echo "<!doctype html><meta charset=utf-8><title>System Report $(date)</title>"
  echo "<style>body{font:14px system-ui;margin:2rem;max-width:60rem}a{display:block;padding:.2rem 0}h1{font-size:1.3rem}</style>"
  echo "<h1>System Report — $(hostname) — $(date)</h1>"
  for f in lshw.html *.txt; do [ -e "$f" ] && echo "<a href=\"$f\">$f</a>"; done
} > index.html

command -v xdg-open >/dev/null && xdg-open "$OUT/index.html" >/dev/null 2>&1 &
echo "Report written to: $OUT"
echo "Search it with: sysreport-search find PATTERN   (or: sysreport-search id VVVV:DDDD)"
