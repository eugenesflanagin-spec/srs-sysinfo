#!/bin/bash
# Build the AppImage srs-sysinfo-VER-x86_64.AppImage (+ .zsync for AppImageUpdate)
# in a fresh Ubuntu 22.04 container, so it runs on distributions with glibc 2.35
# or newer (the self-check prints the exact minimum).
#   build-appimage.sh SOURCE_TARBALL OUTDIR
#     SOURCE_TARBALL  srs-sysinfo-VER.tar.gz from `git archive --prefix=srs-sysinfo-VER/`
#     OUTDIR          gets the .AppImage, the .zsync, build.log-style output on stdout,
#                     and contents.txt (every file in the AppImage)
# Used the same way by build-all.sh (your PC) and .github/workflows/release.yml.
# Needs docker only. The tools come from their official GitHub releases, pinned to
# a version and checked against the sha256 below (GitHub shows the same digest on
# each release asset). The container is removed afterwards (--rm).
set -euo pipefail
IMAGE=ubuntu:22.04

# ---- pinned tools: name|url|sha256
TOOLS=(
  "linuxdeploy-x86_64.AppImage|https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage|c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
  "linuxdeploy-plugin-gtk.sh|https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/7a3fbc31a9e5075073ff8790f26effbac5f84453/linuxdeploy-plugin-gtk.sh|b0f4cbc684a0103a9651f0955b635eaea0096b3a66c0f5a2c2aa337960375171"
  "appimagetool-x86_64.AppImage|https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage|ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
  "runtime-x86_64|https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64|2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"
)
# ID databases for systems without the hwdata package, from hwdata v0.412 (pinned to the
# tag's commit). srs-gui prefers the system's own /usr/share/hwdata when it exists.
HWDATA=https://raw.githubusercontent.com/vcrhonek/hwdata/816a9490ffcdad1929e0e53f79c500e1aeadd492
IDS=(
  "pci.ids|$HWDATA/pci.ids|4f020f031d1f2d011b2a656a191519a056cb9744229551aed2cf7f90dfc2e18a"
  "usb.ids|$HWDATA/usb.ids|34b2b2289508a866cf38b96e54b37ac52c0ab7ce8361a56116af24e3fd094421"
  "pnp.ids|$HWDATA/pnp.ids|92c667650db4b8762032d9092d8b61420883d42c1d1b6e5f715f5da84c02359e"
)
UPDATE_INFO="gh-releases-zsync|eugenesflanagin-spec|srs-sysinfo|latest|srs-sysinfo-*x86_64.AppImage.zsync"

if [ "${1:-}" != --inside ]; then
  PKG=$(cd "$(dirname "$0")" && pwd)
  TARBALL=$(readlink -f "${1:?usage: build-appimage.sh SOURCE_TARBALL OUTDIR}")
  OUT=${2:?output dir}
  VER=$(basename "$TARBALL" .tar.gz); VER=${VER#srs-sysinfo-}
  [[ $VER =~ ^[0-9]+(\.[0-9]+)+$ ]] || { echo "cannot get the version from $TARBALL" >&2; exit 1; }
  rm -rf "$OUT"; mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")
  echo "### appimage: building srs-sysinfo $VER in $IMAGE"
  docker pull -q "$IMAGE" >/dev/null
  exec docker run --rm --name "srs-pkg-appimage-$$" \
    -v "$TARBALL:/in/srs-sysinfo-$VER.tar.gz:ro" -v "$PKG:/pkg:ro" -v "$OUT:/out" \
    "$IMAGE" bash -c "bash /pkg/build-appimage.sh --inside $VER; rc=\$?; chown -R $(id -u):$(id -g) /out; exit \$rc"
fi

# ---- inside the container
VER=$2
export DEBIAN_FRONTEND=noninteractive
. /pkg/lib-version.sh

echo "### toolchain"
apt-get update -qq
# libgtk-3-bin / libgdk-pixbuf2.0-bin / librsvg2-common: what linuxdeploy-plugin-gtk bundles
# (IM modules, pixbuf loaders incl. SVG for icons);
# libayatana-appindicator3-1: the optional tray icon (srs-gui dlopen()s it); libsqlite3-dev: Power & cost history.
# Not bundled: zlib / zstd / lz4 / liblzma (Everyday Work benchmark), libcurl (Network benchmark's internet test) and
# libvulkan (GPU benchmarks; must match the host's GPU driver),
# loaded from the host with dlopen when present.
apt-get install -y -q --no-install-recommends build-essential pkgconf libgtk-3-dev libgtk-3-bin libsqlite3-dev \
  libgdk-pixbuf2.0-bin librsvg2-common shared-mime-info \
  libayatana-appindicator3-1 \
  curl ca-certificates file binutils desktop-file-utils appstream >/tmp/toolchain.log 2>&1 \
  || { tail -30 /tmp/toolchain.log; exit 1; }

echo "### tools (pinned, sha256 checked)"
mkdir -p /tools && cd /tools
for t in "${TOOLS[@]}" "${IDS[@]}"; do
  IFS='|' read -r name url sum <<<"$t"
  curl -fsSL --retry 3 -o "$name" "$url"
  echo "$sum  $name" | sha256sum -c -
  chmod +x "$name"
done
export PATH=/tools:$PATH
# containers have no FUSE: every AppImage tool unpacks itself and runs from there
export APPIMAGE_EXTRACT_AND_RUN=1

echo "### build srs-sysinfo $VER"
mkdir -p /work && cd /work
tar xzf /in/srs-sysinfo-$VER.tar.gz
SRC=/work/srs-sysinfo-$VER
cd "$SRC"
grep -q "SRS_VERSION \"$VER\"" srs.h || { echo "srs.h does not say $VER" >&2; exit 1; }
# generic x86-64 with the usual hardening, like the distro packages
export CFLAGS="-O2 -g0 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fcf-protection" LDFLAGS="-Wl,-z,relro,-z,now"
make CC=gcc native
A=/work/AppDir
make CC=gcc DESTDIR=$A SYSPREFIX=/usr install-system >/dev/null
# not usable from an AppImage: the boot-time unit (needs the distro package) and the
# menu entries of the separate launchers (the AppImage has one entry, with actions)
rm -rf $A/usr/lib/systemd
rm -f $A/usr/share/applications/srs-{burnin,benchmark,sysreport,user-guide}.desktop
# ID databases (see IDS above)
install -Dm644 -t $A/usr/share/hwdata /tools/pci.ids /tools/usb.ids /tools/pnp.ids
# man pages (version and date stamped as in the packages)
DATE=$(release_date "$VER" CHANGELOG.md)
mkdir -p $A/usr/share/man/man1 && cp /pkg/man/*.1 $A/usr/share/man/man1/
stamp_man "$VER" "$DATE" $A/usr/share/man/man1/*.1
# AppStream metadata
install -Dm644 /pkg/appimage/io.github.eugenesflanagin_spec.srs_sysinfo.metainfo.xml \
  $A/usr/share/metainfo/io.github.eugenesflanagin_spec.srs_sysinfo.metainfo.xml
appstreamcli validate --no-net $A/usr/share/metainfo/*.xml || { echo "metainfo is not valid" >&2; exit 1; }
desktop-file-validate $A/usr/share/applications/srs-gui.desktop

echo "### linuxdeploy (+ gtk plugin)"
cd /work
export DEPLOY_GTK_VERSION=3 ARCH=x86_64
TRAYLIB=$(ls /usr/lib/x86_64-linux-gnu/libayatana-appindicator3.so.1)
linuxdeploy-x86_64.AppImage --appdir $A \
  -e $A/usr/bin/srs-gui -e $A/usr/bin/sysreport-search \
  -d $A/usr/share/applications/srs-gui.desktop -i $A/usr/share/icons/hicolor/256x256/apps/srs.png \
  --library "$TRAYLIB" --plugin gtk >/tmp/linuxdeploy.log 2>&1 \
  || { tail -40 /tmp/linuxdeploy.log; exit 1; }
grep -E '^ERROR' /tmp/linuxdeploy.log || true
# our own AppRun replaces the generated one: it starts the command-line programs without
# the GTK settings and sources the hooks (apprun-hooks/*.sh) itself for the GUI
rm -f $A/AppRun $A/AppRun.wrapped
install -m755 /pkg/appimage/AppRun $A/AppRun
# The gtk plugin's hook forces X11 (XWayland on a Wayland desktop) because a bundled GTK can
# crash with the Wayland backend. Keep X11 as the default, but let GDK_BACKEND from the
# environment win (GDK_BACKEND=wayland ./srs-sysinfo-...AppImage).
sed -i 's/^export GDK_BACKEND=x11\b/export GDK_BACKEND="${GDK_BACKEND:-x11}"/' $A/apprun-hooks/linuxdeploy-plugin-gtk.sh
grep -q '^export GDK_BACKEND="${GDK_BACKEND:-x11}"' $A/apprun-hooks/linuxdeploy-plugin-gtk.sh \
  || { echo "FAIL: the gtk hook's GDK_BACKEND line has changed, check it" >&2; exit 1; }

echo "### licences of the bundled libraries"
# Each bundled library comes from an Ubuntu 22.04 package; ship that package's copyright file
# (licence texts) and a list of which library came from which package and version.
LIST=$A/usr/share/doc/srs-sysinfo/BUNDLED-LIBRARIES.txt
{
  echo "Libraries bundled in this AppImage, the Ubuntu 22.04 package each came from, and its version."
  echo "Each package's copyright and licence file is in usr/share/doc/<package>/copyright."
  echo "Sources: https://packages.ubuntu.com/jammy/<package> (Ubuntu 22.04 source packages)."
  echo
} >"$LIST"
for so in $A/usr/lib/*.so*; do
  b=$(basename "$so")
  pkg=$(dpkg -S "*/$b" 2>/dev/null | head -1 | cut -d: -f1) || true
  [ -n "$pkg" ] || { echo "$b  (package not found)" >>"$LIST"; continue; }
  ver=$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null || echo "?")
  printf '%-45s %-35s %s\n' "$b" "$pkg" "$ver" >>"$LIST"
  if [ -f "/usr/share/doc/$pkg/copyright" ] && [ ! -f "$A/usr/share/doc/$pkg/copyright" ]; then
    install -Dm644 "/usr/share/doc/$pkg/copyright" "$A/usr/share/doc/$pkg/copyright"
  fi
done
n=$(find $A/usr/share/doc -name copyright | wc -l)
[ "$n" -gt 10 ] || { echo "FAIL: only $n licence files collected" >&2; exit 1; }
echo "$n licence files, list in usr/share/doc/srs-sysinfo/BUNDLED-LIBRARIES.txt"

echo "### appimagetool"
OUTNAME=srs-sysinfo-$VER-x86_64.AppImage
EPOCH=$(stat -c %Y "$SRC/CHANGELOG.md")   # git archive stamps files with the commit date
find $A -exec touch -h -d @"$EPOCH" {} +
cd /out
SOURCE_DATE_EPOCH=$EPOCH VERSION=$VER appimagetool-x86_64.AppImage --no-appstream \
  --runtime-file /tools/runtime-x86_64 -u "$UPDATE_INFO" $A "/out/$OUTNAME" >/tmp/appimagetool.log 2>&1 \
  || { tail -40 /tmp/appimagetool.log; exit 1; }
[ -f "/out/$OUTNAME.zsync" ] || { echo "FAIL: no $OUTNAME.zsync" >&2; tail -20 /tmp/appimagetool.log; exit 1; }

echo "### self-check"
cd /tmp
rm -rf squashfs-root && "/out/$OUTNAME" --appimage-extract >/dev/null
(cd squashfs-root && find . | sort) >/out/contents.txt
for f in AppRun srs-gui.desktop srs.png usr/bin/srs-gui usr/bin/sysreport-search usr/bin/srs-root usr/bin/srs-sysreport \
         usr/share/doc/srs-sysinfo/SRS-User-Guide.pdf usr/share/doc/srs-sysinfo/html/SRS-User-Guide.html \
         usr/share/hwdata/pci.ids usr/share/hwdata/usb.ids usr/share/man/man1/srs-gui.1 \
         usr/share/doc/srs-sysinfo/BUNDLED-LIBRARIES.txt \
         usr/share/metainfo/io.github.eugenesflanagin_spec.srs_sysinfo.metainfo.xml; do
  [ -e "squashfs-root/$f" ] || { echo "FAIL: $f missing from the AppImage" >&2; exit 1; }
done
# read the update information from the file itself (an AppImage launcher registered on the
# host for AppImages - binfmt_misc is shared with containers - may not pass --appimage-* on)
objcopy --dump-section .upd_info=/tmp/upd_info "/out/$OUTNAME" /tmp/objcopy.out
got=$(tr -d '\0' </tmp/upd_info)
[ "$got" = "$UPDATE_INFO" ] || { echo "FAIL: update information is '$got'" >&2; exit 1; }
grep -q '^Filename: '"$OUTNAME"'$' "/out/$OUTNAME.zsync" || { echo "FAIL: .zsync names another file" >&2; exit 1; }
grep -q "^sysreport-search $VER " <<<"$("/out/$OUTNAME" --appimage-extract-and-run --version)" \
  || { echo "FAIL: --version is not $VER" >&2; exit 1; }
grep -q "^sysreport-search $VER " <<<"$("/out/$OUTNAME" --appimage-extract-and-run sysreport-search --version)" \
  || { echo "FAIL: 'sysreport-search --version' is not $VER" >&2; exit 1; }
# lowest glibc it needs = the highest GLIBC_x.y symbol version of any program or library inside
GLIBC=$(find squashfs-root -type f \( -name '*.so*' -o -perm -u+x \) -exec sh -c 'file -b "$1" | grep -q ELF && objdump -T "$1"' _ {} \; 2>/dev/null \
        | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
echo "$GLIBC" >/out/glibc.txt
rm -rf squashfs-root
echo "made $OUTNAME ($(du -h "/out/$OUTNAME" | cut -f1), needs glibc $GLIBC+) and $OUTNAME.zsync"
