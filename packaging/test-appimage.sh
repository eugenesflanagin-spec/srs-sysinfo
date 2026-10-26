#!/bin/bash
# Start the AppImage in FRESH containers of other distributions and check that it works:
# --version, the command-line ID lookup (with the ID files from inside the AppImage), and
# the GUI window under a virtual X server (Xvfb). Each container gets only GTK 3, Xvfb,
# fonts and the X tools for the check; the AppImage brings the rest.
#   test-appimage.sh [-s SHOTDIR] APPIMAGE VER [IMAGE...]
#     IMAGE    default: debian:12 fedora:44 (any Debian/Ubuntu, Fedora, openSUSE or Arch image)
#     SHOTDIR  keep a screenshot of the window per image (shot-<image>.xwd, convert it with
#              e.g. `magick shot.xwd shot.png`). The container's host name is MYPC, so your
#              PC's name is not in the picture.
# Containers have no FUSE, so the AppImage runs with APPIMAGE_EXTRACT_AND_RUN=1, as an
# ordinary user (not root). Used the same way by build-all.sh and the GitHub workflow.
set -euo pipefail
SHOTS=
if [ "${1:-}" = -s ]; then SHOTS=$2; shift 2; fi

if [ "${1:-}" != --inside ]; then
  PKG=$(cd "$(dirname "$0")" && pwd)
  AI=$(readlink -f "${1:?usage: test-appimage.sh [-s SHOTDIR] APPIMAGE VER [IMAGE...]}")
  VER=${2:?version}; shift 2
  IMAGES=("$@"); [ ${#IMAGES[@]} -eq 0 ] && IMAGES=(debian:12 fedora:44)
  SHOTMOUNT=()
  if [ -n "$SHOTS" ]; then mkdir -p "$SHOTS"; SHOTMOUNT=(-v "$(readlink -f "$SHOTS"):/shots"); fi
  fail=0
  for img in "${IMAGES[@]}"; do
    echo "### AppImage test in a fresh $img"
    docker pull -q "$img" >/dev/null
    tag=${img//[:\/]/-}
    if docker run --rm --name "srs-pkg-aitest-${tag//./}-$$" --hostname MYPC \
         -v "$AI:/in/srs.AppImage:ro" -v "$PKG:/pkg:ro" "${SHOTMOUNT[@]}" \
         "$img" bash /pkg/test-appimage.sh --inside "$VER" "$tag" "$(id -u):$(id -g)"; then
      echo "PASS: $img"
    else
      echo "FAIL: $img"; fail=1
    fi
  done
  exit $fail
fi

# ---- inside the container (as root: install, then test as a normal user)
VER=$2 TAG=$3 OWNER=$4
fail() { echo "FAIL: $*"; exit 1; }
echo "--- installing GTK 3, Xvfb, fonts"
. /etc/os-release
case " ${ID:-} ${ID_LIKE:-} " in
  *" debian "*|*" ubuntu "*)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq && apt-get install -y -q --no-install-recommends libgtk-3-0 xvfb xauth x11-utils x11-apps \
      fonts-dejavu-core >/tmp/install.log 2>&1 || { tail -20 /tmp/install.log; exit 1; } ;;
  *" fedora "*)
    dnf -y -q install gtk3 xorg-x11-server-Xvfb xorg-x11-xauth xwininfo xwd dejavu-sans-fonts shadow-utils \
      >/tmp/install.log 2>&1 || { tail -20 /tmp/install.log; exit 1; } ;;
  *" suse "*|*" opensuse "*)
    zypper -q -n install gtk3-tools libgtk-3-0 xvfb-run xorg-x11-server-Xvfb xwininfo xwd dejavu-fonts shadow \
      >/tmp/install.log 2>&1 || { tail -20 /tmp/install.log; exit 1; } ;;
  *" arch "*)
    pacman -Sy --noconfirm --needed gtk3 xorg-server-xvfb xorg-xwininfo xorg-xwd ttf-dejavu \
      >/tmp/install.log 2>&1 || { tail -20 /tmp/install.log; exit 1; } ;;
  *) fail "unknown distribution ${ID:-?}" ;;
esac
echo "glibc: $(ldd --version | head -1)"
useradd -m tester
cp /in/srs.AppImage /home/tester/srs.AppImage && chown tester: /home/tester/srs.AppImage && chmod 755 /home/tester/srs.AppImage

cat >/tmp/as-user.sh <<'EOF'
set -u
VER=$1
fail() { echo "FAIL: $*"; exit 1; }
export APPIMAGE_EXTRACT_AND_RUN=1
cd "$HOME"
v=$(./srs.AppImage --version 2>/dev/null | head -1)
echo "--version: $v"
[[ $v == "sysreport-search $VER "* ]] || fail "--version"
v=$(./srs.AppImage sysreport-search --version 2>/dev/null | head -1)
[[ $v == "sysreport-search $VER "* ]] || fail "sysreport-search --version"
# the container has no hwdata package: the name must come from the AppImage's own pci.ids
[ -e /usr/share/hwdata/pci.ids ] && echo "(note: this image has its own pci.ids)"
./srs.AppImage id 1002:7550 2>/dev/null | grep -q 'Navi 48' || fail "ID lookup (pci.ids)"
echo "ID lookup: OK"
ln -s srs.AppImage sysreport-search
./sysreport-search --version 2>/dev/null | head -1 | grep -q "^sysreport-search $VER " || fail "sysreport-search symlink"
echo "symlink sysreport-search: OK"

Xvfb :99 -screen 0 1600x1000x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
xpid=$!
for _ in $(seq 50); do [ -e /tmp/.X11-unix/X99 ] && break; sleep 0.2; done
export DISPLAY=:99 GDK_BACKEND=x11
unset WAYLAND_DISPLAY
./srs.AppImage >/tmp/gui.log 2>&1 &
gpid=$!
win=
for _ in $(seq 60); do
  win=$(xwininfo -root -tree 2>/dev/null | grep -o '"SRS[^"]*"' | head -1)
  [ -n "$win" ] && break
  kill -0 $gpid 2>/dev/null || { cat /tmp/gui.log; fail "the GUI exited"; }
  sleep 1
done
[ -n "$win" ] || { cat /tmp/gui.log; fail "no SRS window after 60 s"; }
echo "GUI window: $win"
sleep 5   # let the first page fill in
kill -0 $gpid 2>/dev/null || { cat /tmp/gui.log; fail "the GUI exited after showing its window"; }
[ -d /shots ] && xwd -root -silent >"/tmp/shot.xwd" && echo "screenshot taken"
grep -E 'CRITICAL|ERROR|Segmentation|cannot open shared object' /tmp/gui.log && fail "errors in the GUI log"
grep 'WARNING' /tmp/gui.log | head -5 | sed 's/^/  warning: /' || true
kill $gpid; wait $gpid 2>/dev/null
kill $xpid; wait $xpid 2>/dev/null
echo "all checks passed"
EOF
su tester -c "bash /tmp/as-user.sh $VER" || exit 1
if [ -d /shots ] && [ -f /tmp/shot.xwd ]; then
  cp /tmp/shot.xwd "/shots/shot-$TAG.xwd" && chown "$OWNER" "/shots/shot-$TAG.xwd"
fi
