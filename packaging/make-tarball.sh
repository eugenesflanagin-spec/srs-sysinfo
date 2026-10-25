#!/bin/bash
# Make the generic Linux tarball srs-sysinfo-VER-linux-x86_64.tar.gz from a
# built package (normally the Arch one) plus INSTALL.txt, LICENSE, README.md.
#   make-tarball.sh PACKAGE SOURCE_TARBALL OUTDIR
#     PACKAGE  srs-sysinfo-VER-1-x86_64.pkg.tar.zst  (or a .deb: then the tarball
#              is made from that package instead; it then runs on older glibc)
# Runs its work in a fresh archlinux:base-devel container (same on your PC and
# on GitHub), so the host only needs docker. The result is reproducible: sorted
# file names, owner root, all dates = the source's commit date.
set -euo pipefail
if [ "${1:-}" != --inside ]; then
  PKG=$(cd "$(dirname "$0")" && pwd)
  P=$(readlink -f "${1:?usage: make-tarball.sh PACKAGE SOURCE_TARBALL OUTDIR}")
  S=$(readlink -f "${2:?source tarball}")
  mkdir -p "${3:?output dir}"; OUT=$(readlink -f "$3")
  docker pull -q archlinux:base-devel >/dev/null
  exec docker run --rm --name "srs-pkg-tarball-$$" \
    -v "$P:/in/$(basename "$P"):ro" -v "$S:/in/$(basename "$S"):ro" -v "$PKG:/pkg:ro" -v "$OUT:/out" \
    archlinux:base-devel bash /pkg/make-tarball.sh --inside "/in/$(basename "$P")" "/in/$(basename "$S")" "$(id -u):$(id -g)"
fi

# ---- inside the container
P=$2 S=$3 OWNER=$4
VER=$(basename "$S" .tar.gz); VER=${VER#srs-sysinfo-}
TOP=srs-sysinfo-$VER-linux-x86_64
W=/work; mkdir -p $W/src $W/$TOP && cd $W
tar xzf "$S" -C src
SRC=src/srs-sysinfo-$VER
case $P in
  *.pkg.tar.zst) bsdtar -xf "$P" -C $TOP usr; FROM="the Arch Linux package" ;;
  *.deb)         bsdtar -xOf "$P" 'data.tar.*' | bsdtar -xf - -C $TOP ./usr
                 FROM="the $(bsdtar -xOf "$P" 'control.tar.*' | bsdtar -xOf - ./control | sed -n 's/^Version: //p') .deb package" ;;
  *) echo "unknown package type: $P" >&2; exit 1 ;;
esac
# lowest glibc the programs need = the highest GLIBC_x.y symbol version they use
GLIBC=$(objdump -T $TOP/usr/bin/srs-gui $TOP/usr/bin/sysreport-search | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
title="SRS (srs-sysinfo) $VER - generic Linux x86_64 build"
sed -e "s/@VERSION@/$VER/g" -e "s/@GLIBC@/$GLIBC/g" -e "s/@BUILT_FROM@/$FROM/g" \
    -e "s/@UNDERLINE@/$(printf '%*s' ${#title} '' | tr ' ' '=')/" /pkg/INSTALL.txt.in >$TOP/INSTALL.txt
grep -q '@[A-Z_]*@' $TOP/INSTALL.txt && { echo "INSTALL.txt still has a placeholder" >&2; exit 1; }
cp $SRC/LICENSE $SRC/README.md $TOP/
EPOCH=$(stat -c %Y $SRC/CHANGELOG.md)   # git archive stamps files with the commit date
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@"$EPOCH" -cf - $TOP | gzip -9n >/out/$TOP.tar.gz

# quick self-check of the result
mkdir $W/check && tar xzf /out/$TOP.tar.gz -C $W/check
for f in INSTALL.txt LICENSE README.md usr/bin/srs-gui usr/bin/sysreport-search usr/bin/srs-root usr/bin/srs-sysreport \
         usr/share/doc/srs-sysinfo/SRS-User-Guide.pdf usr/share/doc/srs-sysinfo/html/SRS-User-Guide.html; do
  [ -e "$W/check/$TOP/$f" ] || { echo "FAIL: $f missing from the tarball" >&2; exit 1; }
done
"$W/check/$TOP/usr/bin/sysreport-search" --version | head -1 | grep -q "^sysreport-search $VER " \
  || { echo "FAIL: sysreport-search --version from the tarball is not $VER" >&2; exit 1; }
chown "$OWNER" /out/$TOP.tar.gz
echo "made $TOP.tar.gz from $(basename "$P") (needs glibc $GLIBC+), $(tar tzf /out/$TOP.tar.gz | wc -l) entries"
