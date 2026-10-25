#!/bin/bash
# Runs INSIDE a fresh Fedora/openSUSE container (started by build.sh):
#   build-rpm.sh VER [DIST]           e.g.  build-rpm.sh 1.1.0 .tw   ("" = distro default, .fc44)
# Mounts: /in/srs-sysinfo-VER.tar.gz (ro), /pkg = this packaging tree (ro),
#         /out = logs, /out/pkg = finished package.
set -euo pipefail
VER=$1 DIST=${2:-}
. /pkg/lib-version.sh

echo "### toolchain"
if command -v dnf >/dev/null; then
  dnf -y -q install rpm-build rpmlint rpmdevtools dnf-plugins-core gcc make file tar gzip >/tmp/toolchain.log 2>&1 \
    || { tail -30 /tmp/toolchain.log; exit 1; }
else
  zypper -n -q --gpg-auto-import-keys refresh >/dev/null
  zypper -n -q install --no-recommends rpm-build rpmlint rpmdevtools gcc make file tar gzip >/tmp/toolchain.log 2>&1 \
    || { tail -30 /tmp/toolchain.log; exit 1; }
fi

T=/root/rpmbuild
mkdir -p $T/SOURCES $T/SPECS
cp /in/srs-sysinfo-$VER.tar.gz /pkg/man/*.1 $T/SOURCES/
cp /pkg/rpm/srs-sysinfo.spec $T/SPECS/
SPEC=$T/SPECS/srs-sysinfo.spec
# version + date come from the source (CHANGELOG.md heading)
DATE=$(release_date "$VER" <(tar xzOf /in/srs-sysinfo-$VER.tar.gz srs-sysinfo-$VER/CHANGELOG.md))
stamp_spec "$SPEC" "$VER" "$DATE"
stamp_man "$VER" "$DATE" $T/SOURCES/*.1
DEF=(--define "_topdir $T"); [ -n "$DIST" ] && DEF+=(--define "dist $DIST")
echo "Version: $(rpmspec -q --srpm --qf '%{version}-%{release}' "${DEF[@]}" $SPEC)  _defaultdocdir = $(rpm --eval %_defaultdocdir)"

echo "### build dependencies"
if command -v dnf >/dev/null; then
  dnf -y -q builddep $SPEC >/tmp/builddeps.log 2>&1 || { cat /tmp/builddeps.log; exit 1; }
else
  rpmspec -q --buildrequires "${DEF[@]}" $SPEC | xargs -d '\n' zypper -n -q install --no-recommends >/tmp/builddeps.log 2>&1 \
    || { cat /tmp/builddeps.log; exit 1; }
fi
echo "### rpmbuild"
rpmbuild -bb "${DEF[@]}" $SPEC
RPM=$(ls $T/RPMS/x86_64/srs-sysinfo-$VER-*.x86_64.rpm | grep -v -- '-debug')
rpmlint $RPM $SPEC >/out/rpmlint.txt 2>&1 || true
echo "### rpmlint"; cat /out/rpmlint.txt
cp $RPM /out/pkg/
rpm -qlvp $RPM >/out/contents.txt
echo "### built: $(ls /out/pkg)"
