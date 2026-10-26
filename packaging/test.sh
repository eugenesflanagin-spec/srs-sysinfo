#!/bin/bash
# Fresh-container install + read-only checks for srs-sysinfo
# (run by test-in-container.sh as: docker run -e KIND=arch|deb|rpm -e VER=x.y.z -v pkgs:/pkgs -v packaging:/pkg IMAGE bash /pkg/test.sh)
set -u
. /pkg/test-common.sh
install_pkgs
for b in srs-gui sysreport-search srs-root srs-sysreport; do
  command -v $b >/dev/null && echo "OK installed: $(command -v $b)" || fail "missing $b"
done
check_ldd /usr/bin/srs-gui /usr/bin/sysreport-search
run_cmd "sysreport-search --help" sysreport-search --help
run_cmd "sysreport-search --version" sysreport-search --version
sysreport-search --version 2>&1 | head -1 | grep -q "^sysreport-search ${VER} " && echo "OK version ${VER}" || fail "--version is not ${VER}"
run_cmd "srs-gui --help" srs-gui --help
run_cmd "srs-gui --version" srs-gui --version
ls -lL /usr/share/hwdata/pci.ids /usr/share/hwdata/usb.ids 2>&1
[ -s /usr/share/srs-sysinfo/power-zip-rates.tsv ] && echo "OK power-zip-rates.tsv" || fail "missing /usr/share/srs-sysinfo/power-zip-rates.tsv"

echo "--- user guide"
D=/usr/share/doc/srs-sysinfo
pdf=$D/SRS-User-Guide.pdf html=$D/html/SRS-User-Guide.html
[ -f "$pdf" ] && [ "$(head -c5 "$pdf")" = "%PDF-" ] && echo "OK real PDF: $pdf ($(stat -c %s "$pdf") bytes)" || fail "$pdf missing or not a PDF"
[ -e "$pdf.gz" ] && fail "$pdf.gz exists (compressed)"
[ -f "$html" ] && grep -qi '<html' "$html" && echo "OK html: $html ($(stat -c %s "$html") bytes)" || fail "$html missing or not HTML"
[ -e "$html.gz" ] && fail "$html.gz exists (compressed)"
for f in USER-GUIDE.md CLI-REFERENCE.md GLOSSARY.md README.md CHANGELOG.md EVIDENCE.md; do
  [ -f "$D/$f" ] && echo "OK $D/$f" || fail "missing $D/$f"
done
ls $D/CHECKLIST.tsv* >/dev/null 2>&1 && echo "OK $(ls $D/CHECKLIST.tsv*)" || fail "missing CHECKLIST.tsv"
n=$(ls $D/SECTIONS/*.md 2>/dev/null | wc -l); [ "$n" -gt 0 ] && echo "OK SECTIONS/*.md: $n" || fail "no SECTIONS/*.md"
n=$(ls $D/images/*.png 2>/dev/null | wc -l); [ "$n" -gt 0 ] && echo "OK images/*.png: $n" || fail "no images/*.png"
z=$(find $D -name '*.gz' | tr '\n' ' '); echo "compressed files in $D: ${z:-none}"
# every image the HTML guide references must exist
for img in $(grep -o 'src="\.\./images/[^"]*"' "$html" | sed 's/^src="\.\.\///; s/"$//' | sort -u); do
  [ -f "$D/$img" ] || fail "html references missing $D/$img"
done
echo "OK html image references resolve"
run_cmd "sysreport-search guide sensors --print" sysreport-search guide sensors --print
sysreport-search guide sensors --print 2>&1 | grep -E "^Command .*($D/SRS-User-Guide\.pdf|$D/html/SRS-User-Guide\.html)#" >/dev/null \
  && echo "OK guide command points at the installed guide" || fail "guide --print does not point at $D"
[ -f /usr/share/applications/srs-user-guide.desktop ] && echo "OK srs-user-guide.desktop" || fail "missing srs-user-guide.desktop"

echo "--- desktop-file-validate"
if [ "$KIND" = arch ]; then pacman -S --noconfirm --needed desktop-file-utils >/dev/null 2>&1
elif [ "$KIND" = deb ]; then apt-get install -y -q --no-install-recommends desktop-file-utils >/dev/null 2>&1
elif command -v dnf >/dev/null; then dnf -y -q install desktop-file-utils >/dev/null 2>&1
else zypper -n -q install --no-recommends desktop-file-utils >/dev/null 2>&1; fi
if command -v desktop-file-validate >/dev/null; then
  for f in /usr/share/applications/srs-*.desktop; do
    out=$(desktop-file-validate "$f" 2>&1); rc=$?
    [ $rc -eq 0 ] && echo "OK desktop-file-validate $(basename $f)${out:+ ($out)}" || { fail "desktop-file-validate $f"; echo "$out"; }
  done
else echo "(desktop-file-validate not available)"; fi
finish
