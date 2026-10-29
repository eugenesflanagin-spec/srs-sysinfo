#!/usr/bin/env bash
# tests/cuda-toolkit-test.sh - the optional NVIDIA CUDA toolkit question (cuda_toolkit.c, the cuda-toolkit command and the
# GUI's ask dialog / Options entry), WITHOUT a GPU and WITHOUT running any package manager.
#
#   tests/cuda-toolkit-test.sh SYSREPORT-SEARCH [SRS-GUI]
#
# SAFETY: libcuda is the fake runtime (tests/gpu_stub.c via SRS_CUDA_LIB), libnvrtc is the stub or a file that does not exist,
# HIP / OpenCL / Vulkan point at files that do not exist, and the run is traced with LD_DEBUG=libs: the test FAILS if a real GPU
# library is loaded. PATH is a folder of FAKE pacman / apt-get / apt-cache / sudo / pkexec scripts that only write down that they
# were called; /etc/os-release, the PCI tree, the route table and pacman's lock are fakes (SRS_OS_RELEASE, SRS_PCI_SYSFS,
# SRS_CTK_ROUTE, SRS_CTK_PACMAN_LOCK); SRS_CTK_DRY_RUN=1 everywhere. SRS only takes the package manager, sudo and pkexec from
# /usr/bin, /usr/sbin, /bin and /sbin; SRS_CTK_TOOL_DIRS (read only in a dry run) points it at the fake ones instead. The cases
# that check that rule (a fake pacman / sudo planted in ~/.local/bin or the current folder must never be chosen) only look at
# the plan, in a dry run. SRS_CUDA_BLAS / SRS_CLBLAST_LIB / VK_DRIVER_FILES point at the stub or files that do not exist in
# every run, and at the end every LD_DEBUG trace is searched once more for the real libcuda / libcublas (this PC has ZLUDA's).
# HOME and XDG_* are a throw-away folder. The GUI part runs on its own Xvfb display, never clicks Install, and is skipped
# when Xvfb / xwininfo are missing.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-}
GUI=${2:-$(dirname "$BIN")/srs-gui}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH [SRS-GUI]"; exit 2; }
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")   # absolute: some cases run from another folder
case $GUI in /*) ;; *) GUI=$PWD/$GUI ;; esac
TMP=$(mktemp -d)
cleanup() { [ -n "${XPID:-}" ] && kill "$XPID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$HOME"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

echo "== build the fake GPU runtime and the fake tools"
STUB=$TMP/libgpustub.so
gcc -O2 -Wall -Wextra -shared -fPIC -fvisibility=hidden -o "$STUB" "$SRC/tests/gpu_stub.c" -lm || { bad "gpu_stub.c"; exit 1; }
mkdir -p "$TMP/zluda" && cp "$STUB" "$TMP/zluda/libcuda.so.1"   # a path with "zluda" in it = ZLUDA to SRS
FAKE=$TMP/fakebin
mkdir -p "$FAKE"
for t in pacman apt-get dnf zypper sudo pkexec; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/EXECUTED"\nexit 0\n' "$t" "$TMP" > "$FAKE/$t"
done
# apt-cache knows the packages listed in $TMP/apt-knows
printf '#!/bin/sh\nfor a; do p=$a; done\n/usr/bin/grep -qx "$p" "%s/apt-knows" 2>/dev/null\n' "$TMP" > "$FAKE/apt-cache"
chmod +x "$FAKE"/*
: > "$TMP/apt-knows"
printf 'Iface\tDestination\tGateway\nenp1s0\t00000000\t0101A8C0\n' > "$TMP/route"
: > "$TMP/noroute"
mkdir -p "$TMP/pci/0000:01:00.0" "$TMP/nopci"
echo 0x10de > "$TMP/pci/0000:01:00.0/vendor"; echo 0x030000 > "$TMP/pci/0000:01:00.0/class"
osr() { printf 'NAME="%s"\nID=%s\nID_LIKE="%s"\nPRETTY_NAME="%s"\n' "$2" "$1" "$3" "$2" > "$TMP/os-release"; }
osr arch "Arch Linux" ""

# ctk [--tty INPUT] ENV... -- ARGS...  : the CLI with the fakes; sets OUT and RC; checks that no real GPU library was loaded.
# --tty runs it on a pseudo-terminal (script(1)) with INPUT (printf %b) as the typed answers
n=0
ctk() {
    local extra=() tty=
    [ "$1" = --tty ] && { tty=$2; shift 2; }
    while [ "$1" != -- ]; do extra+=("$1"); shift; done
    shift
    n=$((n + 1))
    local run=(env -u SRS_GPU_SOFTWARE_ONLY PATH="$FAKE" SHELL=/bin/sh SRS_CTK_DRY_RUN=1 SRS_CTK_TOOL_DIRS="$FAKE" SRS_OS_RELEASE="$TMP/os-release" \
        SRS_PCI_SYSFS="$TMP/nopci" SRS_CTK_ROUTE="$TMP/route" SRS_CTK_PACMAN_LOCK="$TMP/no-db.lck" SRS_CUDA_LIB="$STUB" SRS_CUDA_RTC=/nonexistent/libnvrtc.so \
        SRS_CUDA_BLAS="$STUB" SRS_HIP_LIB=/nonexistent/libamdhip64.so SRS_HIP_RTC=/nonexistent/libhiprtc.so SRS_HIP_BLAS=/nonexistent/libhipblas.so \
        SRS_OPENCL_LIB=/nonexistent/libOpenCL.so SRS_CLBLAST_LIB=/nonexistent/libclblast.so \
        VK_DRIVER_FILES=/nonexistent VK_ICD_FILENAMES=/nonexistent LD_DEBUG=libs LD_DEBUG_OUTPUT="$TMP/ld.$n" "${extra[@]}")
    if [ -n "$tty" ]; then
        OUT=$(printf '%b' "$tty" | "${run[@]}" /usr/bin/timeout 120 /usr/bin/script -qec "$(printf '%q ' "$BIN" "$@")" /dev/null 2>&1)
    else
        OUT=$("${run[@]}" /usr/bin/timeout 120 "$BIN" "$@" 2>&1)
    fi
    RC=$?
    if cat "$TMP/ld.$n".* 2>/dev/null | grep "calling init" | grep -iE 'libcuda|libnvidia|libamdhip|libhsa|librocm|libhiprtc|libnvrtc|libcublas|libhipblas|libOpenCL|libclblast|/usr/lib.*zluda' | grep -v "$STUB\|$TMP/zluda" | grep -q .; then
        bad "a real GPU library was loaded"
    fi
}

echo "== detection"
ctk -- cuda-toolkit --check
grep -q "CUDA driver library (libcuda): found" <<<"$OUT" && grep -q "Runtime compiler (NVRTC): not installed - SRS uses its built-in CUDA tests" <<<"$OUT" && ok "libcuda without NVRTC: found / not installed" || bad "status: $OUT"
grep -q "NVIDIA graphics card: none found" <<<"$OUT" && ! grep -q WARNING <<<"$OUT" && ok "no NVIDIA card in the fake PCI tree, no ZLUDA warning" || bad "pci / zluda: $OUT"
grep -q "The NVIDIA CUDA toolkit is NVIDIA's free set of developer tools" <<<"$OUT" && grep -q "runtime compiler (NVRTC)" <<<"$OUT" && ok "the plain-English explanation is shown" || bad "explanation missing"
grep -q "You can install it later from Options > NVIDIA CUDA toolkit for GPU tests..." <<<"$OUT" && ok "the 'install later' hint names the Options entry" || bad "later hint missing"
ctk SRS_CUDA_RTC="$STUB" -- cuda-toolkit --check
grep -q "Runtime compiler (NVRTC): installed" <<<"$OUT" && ! grep -q "install it later" <<<"$OUT" && ok "NVRTC present: installed, no hint" || bad "nvrtc present: $OUT"
ctk SRS_CUDA_LIB="$TMP/zluda/libcuda.so.1" -- cuda-toolkit --check
grep -q "libcuda): ZLUDA" <<<"$OUT" && grep -q "WARNING: This computer uses ZLUDA. Installing the NVIDIA CUDA toolkit is NOT recommended" <<<"$OUT" && ok "ZLUDA: detected and warned about" || bad "zluda: $OUT"
ctk SRS_CUDA_LIB=/nonexistent/libcuda.so SRS_PCI_SYSFS="$TMP/pci" -- cuda-toolkit --check
grep -q "NVIDIA graphics card: found" <<<"$OUT" && grep -q "libcuda): not installed" <<<"$OUT" && ok "NVIDIA card without a driver library: found / not installed" || bad "pci nvidia: $OUT"

echo "== the install command per distribution (built, never run)"
ctk -- cuda-toolkit --check
grep -q "Install on Arch Linux: sudo pacman -S --needed cuda" <<<"$OUT" && ok "Arch: sudo pacman -S --needed cuda" || bad "arch: $(grep Install <<<"$OUT")"
osr cachyos "CachyOS" "arch"; ctk -- cuda-toolkit --check
grep -q "Install on CachyOS: sudo pacman -S --needed cuda" <<<"$OUT" && ok "Arch-based (ID_LIKE=arch): pacman" || bad "cachyos: $(grep Install <<<"$OUT")"
osr debian "Debian GNU/Linux 13 (trixie)" ""; echo libnvrtc12 > "$TMP/apt-knows"; ctk -- cuda-toolkit --check
grep -q "Install on Debian GNU/Linux 13 (trixie): sudo apt-get install libnvrtc12" <<<"$OUT" && ok "Debian 13: apt-get install libnvrtc12" || bad "debian: $(grep -A1 Install <<<"$OUT")"
osr ubuntu "Ubuntu 22.04 LTS" "debian"; echo libnvrtc11.2 > "$TMP/apt-knows"; ctk -- cuda-toolkit --check
grep -q "sudo apt-get install libnvrtc11.2" <<<"$OUT" && ok "Ubuntu 22.04: apt-get install libnvrtc11.2" || bad "ubuntu: $(grep Install <<<"$OUT")"
echo nvidia-cuda-toolkit > "$TMP/apt-knows"; ctk -- cuda-toolkit --check
grep -q "sudo apt-get install nvidia-cuda-toolkit" <<<"$OUT" && ok "apt without libnvrtc*: nvidia-cuda-toolkit" || bad "apt fallback: $(grep Install <<<"$OUT")"
: > "$TMP/apt-knows"; ctk -- cuda-toolkit --check
grep -q "Install on Ubuntu 22.04 LTS: by hand" <<<"$OUT" && grep -q "multiverse" <<<"$OUT" && ok "apt knows no package: instructions (non-free / multiverse)" || bad "apt none: $OUT"
osr fedora "Fedora Linux 43" ""; ctk -- cuda-toolkit --check
grep -q "Install on Fedora Linux 43: by hand" <<<"$OUT" && grep -q "developer.nvidia.com/cuda-downloads" <<<"$OUT" && grep -q "dnf install" <<<"$OUT" && ok "Fedora: instructions + NVIDIA link" || bad "fedora: $OUT"
osr opensuse-tumbleweed "openSUSE Tumbleweed" "opensuse suse"; ctk -- cuda-toolkit --check
grep -q "by hand" <<<"$OUT" && grep -q "zypper install" <<<"$OUT" && ok "openSUSE: instructions" || bad "opensuse: $OUT"
osr arch "Arch Linux" ""

echo "== --install refuses safely (no terminal, locked database, no network) and runs nothing"
ctk -- cuda-toolkit --install </dev/null
[ $RC = 2 ] && grep -q "runs only in a terminal" <<<"$OUT" && ok "no terminal: not started, says why" || bad "no tty: rc=$RC $OUT"
: > "$TMP/db.lck"; ctk SRS_CTK_PACMAN_LOCK="$TMP/db.lck" -- cuda-toolkit --install </dev/null
[ $RC = 1 ] && grep -q "package database is locked" <<<"$OUT" && ok "pacman db.lck: not started, says why" || bad "lock: rc=$RC $OUT"
ctk SRS_CTK_ROUTE="$TMP/noroute" -- cuda-toolkit --install </dev/null
[ $RC = 1 ] && grep -q "no network connection" <<<"$OUT" && ok "no default route: not started, says why" || bad "no network: rc=$RC $OUT"
osr fedora "Fedora Linux 43" ""; ctk -- cuda-toolkit --install </dev/null
[ $RC = 1 ] && grep -q "no install command for Fedora" <<<"$OUT" && ok "Fedora: --install points to the instructions" || bad "fedora install: rc=$RC"
osr arch "Arch Linux" ""
if [ -x /usr/bin/script ]; then
    ctk --tty 'no\n' -- cuda-toolkit --install
    grep -q "This runs, as administrator: sudo pacman -S --needed cuda" <<<"$OUT" && grep -q "Programs used: $FAKE/sudo and $FAKE/pacman" <<<"$OUT" && grep -q "cancelled, nothing installed" <<<"$OUT" \
        && ok "terminal, answer no: the command and the full paths are shown, cancelled" || bad "tty no: $OUT"
    ctk --tty 'yes\n' SRS_CUDA_LIB="$TMP/zluda/libcuda.so.1" -- cuda-toolkit --install
    grep -q "Type yes AGAIN" <<<"$OUT" && grep -q "cancelled, nothing installed (recommended with ZLUDA)" <<<"$OUT" && ok "ZLUDA: a second yes is needed; one yes cancels" || bad "zluda tty: $OUT"
    ctk --tty 'yes\nyes\n' SRS_CUDA_LIB="$TMP/zluda/libcuda.so.1" -- cuda-toolkit --install
    grep -q "dry run (SRS_CTK_DRY_RUN): would run: sudo pacman -S --needed cuda (as: $FAKE/sudo $FAKE/pacman -S --needed cuda)" <<<"$OUT" \
        && ok "ZLUDA, yes twice, dry run: shows what it would run (full paths), runs nothing" || bad "dry run: $OUT"
else echo "  skip  no /usr/bin/script for a pseudo-terminal"; fi
[ ! -e "$TMP/EXECUTED" ] && ok "no package manager, sudo or pkexec was ever started" || bad "something was executed: $(cat "$TMP/EXECUTED")"

echo "== only system folders: a planted pacman / sudo in ~/.local/bin, in PATH or in the current folder is never chosen (plan only, dry run)"
PLANT=$HOME/.local/bin
mkdir -p "$PLANT" "$TMP/cwd"
for t in pacman sudo pkexec apt-get apt-cache; do
    printf '#!/bin/sh\necho "planted %s $*" >> "%s/PLANTED"\nexit 0\n' "$t" "$TMP" > "$PLANT/$t"
    cp "$PLANT/$t" "$TMP/cwd/$t"
done
chmod +x "$PLANT"/* "$TMP/cwd"/*
# what a correct SRS picks on this PC: the real one in a system folder, or nothing (then it refuses)
sys_tool() { local d; for d in /usr/bin /usr/sbin /bin /sbin; do [ -f "$d/$1" ] && [ -x "$d/$1" ] && { echo "$d/$1"; return; }; done; }
SYS_PACMAN=$(sys_tool pacman); SYS_ROOT=$(sys_tool sudo); [ -n "$SYS_ROOT" ] || SYS_ROOT=$(sys_tool pkexec)
check_plan() {   # check_plan WHAT: the plan uses the system pacman (or refuses), never a planted one
    if grep -q "$PLANT\|$TMP/cwd\|$FAKE" <<<"$OUT"; then bad "$1: a planted tool was chosen: $(grep -m2 "Programs used\|$PLANT\|$TMP/cwd" <<<"$OUT")"
    elif [ -n "$SYS_PACMAN" ] && [ -n "$SYS_ROOT" ]; then
        grep -q "Programs used: $SYS_ROOT, $SYS_PACMAN" <<<"$OUT" && ok "$1: $SYS_ROOT and $SYS_PACMAN (system folders), not the planted ones" || bad "$1: $(grep -m2 "Install on\|Programs" <<<"$OUT")"
    elif [ -z "$SYS_PACMAN" ]; then
        grep -q "pacman was not found in /usr/bin, /usr/sbin, /bin or /sbin, so SRS does not run it" <<<"$OUT" && ok "$1: no system pacman here: refused with instructions" || bad "$1: $OUT"
    else grep -q "Programs used: (no sudo or pkexec" <<<"$OUT" && ok "$1: no system sudo / pkexec here: none chosen" || bad "$1: $OUT"; fi
}
ctk PATH="$PLANT:$FAKE" SRS_CTK_TOOL_DIRS= -- cuda-toolkit --check
check_plan "fake pacman / sudo first in PATH (~/.local/bin)"
cd "$TMP/cwd" && ctk PATH=":$FAKE:" SRS_CTK_TOOL_DIRS= -- cuda-toolkit --check; cd - >/dev/null
check_plan "empty PATH entries + a pacman in the current folder"
cd "$TMP/cwd" && ctk PATH="." SRS_CTK_TOOL_DIRS= -- cuda-toolkit --check; cd - >/dev/null
check_plan "PATH=. (a relative entry)"
ctk SRS_CTK_DRY_RUN=0 -- cuda-toolkit --check
check_plan "SRS_CTK_TOOL_DIRS without a dry run is ignored"
if [ -n "$SYS_PACMAN" ] && [ -n "$SYS_ROOT" ] && [ -x /usr/bin/script ]; then
    ctk --tty 'yes\n' PATH="$PLANT:$FAKE" SRS_CTK_TOOL_DIRS= -- cuda-toolkit --install
    grep -q "would run: sudo pacman -S --needed cuda (as: $SYS_ROOT $SYS_PACMAN -S --needed cuda)" <<<"$OUT" && ok "--install (dry run): would run $SYS_ROOT $SYS_PACMAN" || bad "planted install: $(tail -3 <<<"$OUT")"
fi
[ ! -e "$TMP/PLANTED" ] && [ ! -e "$TMP/EXECUTED" ] && ok "no planted or fake tool was ever started" || bad "executed: $(cat "$TMP/PLANTED" "$TMP/EXECUTED" 2>/dev/null)"

echo "== the terminal script: every path quoted (a ' in XDG_CACHE_HOME), full paths for sudo and pacman"
SAFE=$TMP/safebin; mkdir -p "$SAFE" "$TMP/it's cache"
for t in sudo pacman; do printf '#!/bin/sh\n[ "$(basename "$0")" = sudo ] && exec "$@"\necho "%s $*" > "%s/safe.ran"\nexit 0\n' "$t" "$TMP" > "$SAFE/$t"; done
chmod +x "$SAFE"/*
cat > "$TMP/script_h.c" <<'EOF2'
#include <stdio.h>
#include <string.h>
#include "cuda_toolkit.h"
int gb_cuda_probe(gb_cuda_state *st, int recheck) { (void)recheck; memset(st, 0, sizeof *st); return 0; }
int main(int argc, char **argv) { ctk_plan p; char b[4000]; (void)argc; ctk_plan_install(&p); ctk_terminal_script(&p, argv[1], b, sizeof b); puts(b); return !p.exe[0]; }
EOF2
if gcc -O1 -Wall -Wextra -I"$SRC" -o "$TMP/script_h" "$TMP/script_h.c" "$SRC/cuda_toolkit.c" -pthread 2>"$TMP/script_h.err"; then
    sf="$TMP/it's cache/cuda-install-1-2.status"
    scr=$(env PATH=/nonexistent SRS_CTK_DRY_RUN=1 SRS_CTK_TOOL_DIRS="$SAFE" SRS_OS_RELEASE="$TMP/os-release" "$TMP/script_h" "$sf")
    grep -qF "'$SAFE/sudo' '$SAFE/pacman' '-S' '--needed' 'cuda'" <<<"$scr" && ok "the script runs sudo and pacman by full path" || bad "script paths: $scr"
    /bin/sh -c "$scr" </dev/null >/dev/null 2>&1
    [ "$(cat "$sf" 2>/dev/null)" = 0 ] && grep -q "pacman -S --needed cuda" "$TMP/safe.ran" 2>/dev/null && ok "a ' in the status path: the script still runs and writes the status" || bad "quoted script did not run: $scr"
else bad "script harness: $(head -3 "$TMP/script_h.err")"; fi

echo "== don't ask again: kept in SRSDEF.ini, other settings untouched"
mkdir -p "$XDG_CONFIG_HOME/srs"
printf '[SRSDEF]\nTheme=Dark\nUpdate Interval=2000\n\n[Other]\nX=1\n' > "$XDG_CONFIG_HOME/srs/SRSDEF.ini"
ctk -- cuda-toolkit --check
grep -q "The GUI asks about it" <<<"$OUT" && ok "default: the GUI asks" || bad "default ask: $OUT"
ctk -- cuda-toolkit --never-ask
ini=$(cat "$XDG_CONFIG_HOME/srs/SRSDEF.ini")
grep -qx "Ask CUDA Toolkit=0" <<<"$ini" && grep -qx "Theme=Dark" <<<"$ini" && grep -qx "Update Interval=2000" <<<"$ini" && grep -qx "X=1" <<<"$ini" \
    && [ "$(awk '/^\[/{s=$0} /^Ask CUDA Toolkit=/{print s}' <<<"$ini")" = "[SRSDEF]" ] && ok "--never-ask: Ask CUDA Toolkit=0 in [SRSDEF], other keys kept" || bad "ini: $ini"
ctk -- cuda-toolkit --check
grep -q "does not ask (\"Don't ask again\"" <<<"$OUT" && ok "the setting persists (read back by a new process)" || bad "persist: $OUT"
ctk -- cuda-toolkit --ask-again
grep -qx "Ask CUDA Toolkit=1" "$XDG_CONFIG_HOME/srs/SRSDEF.ini" && [ "$(grep -c 'Ask CUDA Toolkit' "$XDG_CONFIG_HOME/srs/SRSDEF.ini")" = 1 ] && ok "--ask-again: back to 1, one key only" || bad "ask-again: $(cat "$XDG_CONFIG_HOME/srs/SRSDEF.ini")"
printf '[SRSDEF]\nAsk CUDA Toolkit2=5\nAsk CUDA Toolkit = 1\n' > "$XDG_CONFIG_HOME/srs/SRSDEF.ini"; ctk -- cuda-toolkit --never-ask
grep -qx "Ask CUDA Toolkit2=5" "$XDG_CONFIG_HOME/srs/SRSDEF.ini" && grep -qx "Ask CUDA Toolkit=0" "$XDG_CONFIG_HOME/srs/SRSDEF.ini" && [ "$(grep -c 'Ask CUDA Toolkit' "$XDG_CONFIG_HOME/srs/SRSDEF.ini")" = 2 ] \
    && ok "a longer key that starts the same (Ask CUDA Toolkit2) is kept, 'Ask CUDA Toolkit = 1' is replaced" || bad "key prefix: $(cat "$XDG_CONFIG_HOME/srs/SRSDEF.ini")"
rm -f "$XDG_CONFIG_HOME/srs/SRSDEF.ini"; ctk -- cuda-toolkit --never-ask
grep -qx "\[SRSDEF\]" "$XDG_CONFIG_HOME/srs/SRSDEF.ini" && grep -qx "Ask CUDA Toolkit=0" "$XDG_CONFIG_HOME/srs/SRSDEF.ini" && ok "no ini yet: created with [SRSDEF]" || bad "new ini"

echo "== the hint after a CUDA run without NVRTC (CLI never prompts)"
ctk -- cuda-toolkit --ask-again
ctk STUB_LOG=/dev/null -- gpu-compare --backends cuda --seconds 0.1 --yes
[ $RC = 0 ] && grep -q "note: the NVIDIA CUDA runtime compiler (NVRTC) is not installed. SRS used its built-in CUDA tests" <<<"$OUT" && grep -q "Command: sudo pacman -S --needed cuda" <<<"$OUT" \
    && ok "gpu-compare: one note with the Options path and the command" || bad "hint: rc=$RC $(tail -5 <<<"$OUT")"
ctk SRS_CUDA_RTC="$STUB" STUB_LOG=/dev/null -- gpu-compare --backends cuda --seconds 0.1 --yes
! grep -q "note: the NVIDIA CUDA runtime compiler" <<<"$OUT" && ok "with NVRTC: no note" || bad "note with nvrtc"
ctk STUB_LOG=/dev/null -- gpu-burn --backend cuda --seconds 1 --yes
grep -q "note: the NVIDIA CUDA runtime compiler" <<<"$OUT" && grep -q PASS <<<"$OUT" && ok "gpu-burn: PASS and the note" || bad "burn hint: $(tail -4 <<<"$OUT")"
ctk -- cuda-toolkit --never-ask
ctk STUB_LOG=/dev/null -- gpu-burn --backend cuda --seconds 1 --yes
! grep -q "note: the NVIDIA CUDA runtime compiler" <<<"$OUT" && grep -q PASS <<<"$OUT" && ok "after Don't ask again: no note either" || bad "note after never-ask"
ctk -- cuda-toolkit --ask-again

echo "== GUI: the Options entry and the question, on a private Xvfb display (Install is never clicked)"
if [ -x "$GUI" ] && command -v Xvfb >/dev/null && command -v xwininfo >/dev/null; then
    d=:$((90 + RANDOM % 9))
    Xvfb "$d" -screen 0 1400x1000x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 & XPID=$!
    for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X${d#:}" ] && break; sleep 0.2; done
    gui() {   # gui OPEN-LIST ENV...: start the GUI, wait for a window title, leave GPID / GLOG
        local open=$1; shift
        GLOG=$TMP/gui.$open.log
        env -u WAYLAND_DISPLAY DISPLAY="$d" GDK_BACKEND=x11 G_DEBUG=fatal-criticals NO_AT_BRIDGE=1 PATH="$FAKE:/usr/bin" SRS_CTK_DRY_RUN=1 \
            SRS_CTK_TOOL_DIRS="$FAKE" SRS_OS_RELEASE="$TMP/os-release" SRS_PCI_SYSFS="$TMP/nopci" SRS_CTK_ROUTE="$TMP/route" SRS_CTK_PACMAN_LOCK="$TMP/no-db.lck" \
            SRS_CUDA_LIB="$TMP/zluda/libcuda.so.1" SRS_CUDA_RTC=/nonexistent SRS_CUDA_BLAS=/nonexistent SRS_HIP_LIB=/nonexistent SRS_HIP_RTC=/nonexistent \
            SRS_HIP_BLAS=/nonexistent SRS_OPENCL_LIB=/nonexistent \
            SRS_CLBLAST_LIB=/nonexistent VK_DRIVER_FILES=/nonexistent VK_ICD_FILENAMES=/nonexistent SRS_GPU_SOFTWARE_ONLY=1 LD_DEBUG=libs LD_DEBUG_OUTPUT="$TMP/ldg.$open" "$@" \
            "$GUI" --gui-new --gui-open="$open" >"$GLOG" 2>&1 & GPID=$!
    }
    seen() { local i; for i in $(seq "$2"); do xwininfo -display "$d" -root -tree 2>/dev/null | grep -q "\"$1\"" && return 0; kill -0 "$GPID" 2>/dev/null || return 1; sleep 0.5; done; return 1; }
    stop() { kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null; grep -v "appindicator is deprecated" "$GLOG" | grep -q "CRITICAL\|-WARNING" && bad "GTK criticals / warnings: $(grep -v 'appindicator is deprecated' "$GLOG" | grep -m3 'CRITICAL\|-WARNING')"
             cat "$TMP/ldg.$1".* 2>/dev/null | grep "calling init" | grep -iE 'libnvidia|libamdhip|libhsa|librocm|libnvrtc|libcublas|libhipblas|libOpenCL|libclblast|/usr/lib/libcuda' | grep -q . && bad "the GUI loaded a real GPU library"; }
    rm -f "$XDG_CONFIG_HOME/srs/SRSDEF.ini"
    gui cudatoolkit
    if seen "NVIDIA CUDA toolkit - SRS" 60; then ok "Options > NVIDIA CUDA toolkit... opens its window"
        [ -n "${SRS_SHOT_DIR:-}" ] && command -v xwd >/dev/null && { sleep 1; xwd -display "$d" -name "NVIDIA CUDA toolkit - SRS" -out "$SRS_SHOT_DIR/cuda-toolkit.xwd" 2>/dev/null; }
    else bad "the toolkit window did not open: $(tail -3 "$GLOG")"; fi
    stop cudatoolkit
    gui cudaask
    seen "NVIDIA CUDA toolkit - SRS" 60 && ok "the question before a CUDA run opens (ZLUDA, no NVRTC, asking on)" || bad "the question did not open: $(tail -3 "$GLOG")"
    [ -n "${SRS_SHOT_DIR:-}" ] && command -v xwd >/dev/null && { sleep 1; xwd -display "$d" -name "NVIDIA CUDA toolkit - SRS" -out "$SRS_SHOT_DIR/cuda-toolkit-ask.xwd" 2>/dev/null; }
    stop cudaask
    printf '[SRSDEF]\nAsk CUDA Toolkit=0\n' > "$XDG_CONFIG_HOME/srs/SRSDEF.ini"
    gui cudaask
    if seen "SRS[^\"]*" 60 && ! seen "NVIDIA CUDA toolkit - SRS" 8; then ok "after Don't ask again (ini = 0): no question"; else bad "asked although Don't ask again"; fi
    stop cudaask
    [ ! -e "$TMP/EXECUTED" ] && ok "the GUI started no package manager, sudo or pkexec" || bad "the GUI executed: $(cat "$TMP/EXECUTED")"
else echo "  skip  no srs-gui / Xvfb / xwininfo"; fi

echo "== every run of this test: the real libcuda / libcublas (ZLUDA's on this PC) was never loaded"
if cat "$TMP"/ld.* "$TMP"/ldg.* 2>/dev/null | grep "calling init" | grep -E 'libcublas|libcuda|libnvrtc|libamdhip|libOpenCL|libclblast' | grep -v "$STUB\|$TMP/zluda" | grep -q .; then
    bad "a real GPU library was loaded: $(cat "$TMP"/ld.* "$TMP"/ldg.* | grep 'calling init' | grep -E 'libcublas|libcuda' | grep -v "$STUB\|$TMP/zluda" | head -3)"
elif ! cat "$TMP"/ld.* 2>/dev/null | grep "calling init" | grep -q libgpustub; then bad "the LD_DEBUG traces are empty: the check above proves nothing"
else ok "no real libcuda / libcublas / libnvrtc / HIP / OpenCL library in any LD_DEBUG trace ($(ls "$TMP"/ld.* "$TMP"/ldg.* 2>/dev/null | wc -l) traces; the stub is in them)"; fi

exit $fail
