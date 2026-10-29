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
# SRS_CTK_ROUTE, SRS_CTK_PACMAN_LOCK); SRS_CTK_DRY_RUN=1 everywhere. HOME and XDG_* are a throw-away folder. The GUI part runs on
# its own Xvfb display, never clicks Install, and is skipped when Xvfb / xwininfo are missing.
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-}
GUI=${2:-$(dirname "$BIN")/srs-gui}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH [SRS-GUI]"; exit 2; }
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

# ctk ENV... -- ARGS...  : the CLI with the fakes; sets OUT and RC; checks that no real GPU library was loaded
n=0
ctk() {
    local extra=()
    while [ "$1" != -- ]; do extra+=("$1"); shift; done
    shift
    n=$((n + 1))
    OUT=$(env -u SRS_GPU_SOFTWARE_ONLY PATH="$FAKE" SRS_CTK_DRY_RUN=1 SRS_OS_RELEASE="$TMP/os-release" SRS_PCI_SYSFS="$TMP/nopci" \
        SRS_CTK_ROUTE="$TMP/route" SRS_CTK_PACMAN_LOCK="$TMP/no-db.lck" SRS_CUDA_LIB="$STUB" SRS_CUDA_RTC=/nonexistent/libnvrtc.so \
        SRS_CUDA_BLAS="$STUB" SRS_HIP_LIB=/nonexistent/libamdhip64.so SRS_OPENCL_LIB=/nonexistent/libOpenCL.so SRS_CLBLAST_LIB=/nonexistent/libclblast.so \
        VK_DRIVER_FILES=/nonexistent VK_ICD_FILENAMES=/nonexistent LD_DEBUG=libs LD_DEBUG_OUTPUT="$TMP/ld.$n" "${extra[@]}" \
        /usr/bin/timeout 120 "$BIN" "$@" 2>&1)
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
if command -v script >/dev/null; then
    out=$(printf 'no\n' | env PATH="$FAKE:/usr/bin" SRS_CTK_DRY_RUN=1 SRS_OS_RELEASE="$TMP/os-release" SRS_PCI_SYSFS="$TMP/nopci" SRS_CTK_ROUTE="$TMP/route" \
          SRS_CTK_PACMAN_LOCK="$TMP/no-db.lck" SRS_CUDA_LIB="$STUB" SRS_CUDA_RTC=/nonexistent SRS_HIP_LIB=/nonexistent SRS_OPENCL_LIB=/nonexistent \
          script -qec "$BIN cuda-toolkit --install" /dev/null 2>&1)
    grep -q "This runs, as administrator: sudo pacman -S --needed cuda" <<<"$out" && grep -q "cancelled, nothing installed" <<<"$out" && ok "terminal, answer no: the command is shown, cancelled" || bad "tty no: $out"
    out=$(printf 'yes\n' | env PATH="$FAKE:/usr/bin" SRS_CTK_DRY_RUN=1 SRS_OS_RELEASE="$TMP/os-release" SRS_PCI_SYSFS="$TMP/nopci" SRS_CTK_ROUTE="$TMP/route" \
          SRS_CTK_PACMAN_LOCK="$TMP/no-db.lck" SRS_CUDA_LIB="$TMP/zluda/libcuda.so.1" SRS_CUDA_RTC=/nonexistent SRS_HIP_LIB=/nonexistent SRS_OPENCL_LIB=/nonexistent \
          script -qec "$BIN cuda-toolkit --install" /dev/null 2>&1)
    grep -q "Type yes AGAIN" <<<"$out" && grep -q "cancelled, nothing installed (recommended with ZLUDA)" <<<"$out" && ok "ZLUDA: a second yes is needed; one yes cancels" || bad "zluda tty: $out"
    out=$(printf 'yes\nyes\n' | env PATH="$FAKE:/usr/bin" SRS_CTK_DRY_RUN=1 SRS_OS_RELEASE="$TMP/os-release" SRS_PCI_SYSFS="$TMP/nopci" SRS_CTK_ROUTE="$TMP/route" \
          SRS_CTK_PACMAN_LOCK="$TMP/no-db.lck" SRS_CUDA_LIB="$TMP/zluda/libcuda.so.1" SRS_CUDA_RTC=/nonexistent SRS_HIP_LIB=/nonexistent SRS_OPENCL_LIB=/nonexistent \
          script -qec "$BIN cuda-toolkit --install" /dev/null 2>&1)
    grep -q "dry run (SRS_CTK_DRY_RUN): would run: sudo pacman -S --needed cuda" <<<"$out" && ok "ZLUDA, yes twice, dry run: shows what it would run, runs nothing" || bad "dry run: $out"
else echo "  skip  no script(1) for a pseudo-terminal"; fi
[ ! -e "$TMP/EXECUTED" ] && ok "no package manager, sudo or pkexec was ever started" || bad "something was executed: $(cat "$TMP/EXECUTED")"

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
            SRS_OS_RELEASE="$TMP/os-release" SRS_PCI_SYSFS="$TMP/nopci" SRS_CTK_ROUTE="$TMP/route" SRS_CTK_PACMAN_LOCK="$TMP/no-db.lck" \
            SRS_CUDA_LIB="$TMP/zluda/libcuda.so.1" SRS_CUDA_RTC=/nonexistent SRS_CUDA_BLAS=/nonexistent SRS_HIP_LIB=/nonexistent SRS_OPENCL_LIB=/nonexistent \
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

exit $fail
