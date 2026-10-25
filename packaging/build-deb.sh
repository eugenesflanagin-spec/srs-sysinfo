#!/bin/bash
# Runs INSIDE a fresh Debian/Ubuntu container (started by build.sh):
#   build-deb.sh VER SUFFIX DIST      e.g.  build-deb.sh 1.1.0 '~deb13' trixie
# Mounts: /in/srs-sysinfo-VER.tar.gz (ro), /pkg = this packaging tree (ro),
#         /out = logs, /out/pkg = finished package.
set -euo pipefail
VER=$1 SUFFIX=$2 DIST=$3
export DEBIAN_FRONTEND=noninteractive
. /pkg/lib-version.sh

echo "### toolchain"
apt-get update -qq
apt-get install -y -q --no-install-recommends build-essential devscripts debhelper equivs fakeroot \
  lintian pkgconf ca-certificates file >/tmp/toolchain.log 2>&1 || { tail -30 /tmp/toolchain.log; exit 1; }

mkdir -p /work && cd /work
cp /in/srs-sysinfo-$VER.tar.gz srs-sysinfo_$VER.orig.tar.gz
tar xzf srs-sysinfo_$VER.orig.tar.gz
cd srs-sysinfo-$VER
cp -a /pkg/debian debian
mkdir -p debian/man && cp /pkg/man/*.1 debian/man/

# version + date come from the source, the distro suffix from distros.sh
DATE=$(release_date "$VER" CHANGELOG.md)
stamp_debian_changelog debian/changelog "$VER" "$DATE"
stamp_man "$VER" "$DATE" debian/man/*.1
sed -i "1s/^srs-sysinfo (\([^)]*\)) [^;]*;/srs-sysinfo (\1$SUFFIX) $DIST;/" debian/changelog
head -1 debian/changelog

echo "### build dependencies"
(cd /tmp && mk-build-deps -i -r -t 'apt-get -y -q --no-install-recommends' /work/srs-sysinfo-$VER/debian/control) >/tmp/builddeps.log 2>&1 \
  || { cat /tmp/builddeps.log; exit 1; }
echo "### dpkg-buildpackage"
dpkg-buildpackage -us -uc -b
cd /work
lintian -I --show-overrides srs-sysinfo_*.changes >/out/lintian.txt 2>&1 || true
echo "### lintian"; cat /out/lintian.txt
# GitHub turns '~' into '.' in release asset names, so name the file that way from the start
for f in srs-sysinfo_*.deb; do cp "$f" "/out/pkg/${f//\~/.}"; done
dpkg-deb -c srs-sysinfo_*.deb >/out/contents.txt
echo "### built: $(ls /out/pkg)"
