#!/bin/bash
# Runs INSIDE a fresh archlinux:base-devel container (started by build.sh):
#   build-arch.sh VER
# Mounts: /in/srs-sysinfo-VER.tar.gz (ro), /pkg = this packaging tree (ro),
#         /out = logs, /out/pkg = finished package.
# makepkg refuses to run as root, so the package is built by a throwaway user.
set -euo pipefail
VER=$1

echo "### toolchain + build dependencies"
pacman -Syu --noconfirm --needed gtk3 glib2 pango cairo hicolor-icon-theme hwdata systemd >/tmp/deps.log 2>&1 \
  || { tail -30 /tmp/deps.log; exit 1; }
useradd -m builder
mkdir -p /work && cp /pkg/arch/PKGBUILD /in/srs-sysinfo-$VER.tar.gz /work/
cd /work
SUM=$(sha256sum srs-sysinfo-$VER.tar.gz | cut -d' ' -f1)
sed -i -E "s/^pkgver=.*/pkgver=$VER/; s/^sha256sums=.*/sha256sums=('$SUM')/" PKGBUILD
grep -E '^(pkgver|pkgrel|sha256sums)=' PKGBUILD
chown -R builder: /work

echo "### makepkg"
su builder -c "cd /work && makepkg -f --noconfirm --config /pkg/arch/makepkg.conf"
PKGFILE=$(ls /work/srs-sysinfo-$VER-*-x86_64.pkg.tar.zst)
cp "$PKGFILE" /out/pkg/
bsdtar -xOf "$PKGFILE" .BUILDINFO >/out/BUILDINFO.txt
bsdtar -tvf "$PKGFILE" >/out/contents.txt
echo "### .BUILDINFO (flags)"; grep -E '^(builddir|startdir|packager|buildenv|options) ' /out/BUILDINFO.txt | tr '\n' ' '; echo
echo "### built: $(ls /out/pkg)"
