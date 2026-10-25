# shellcheck shell=bash
# shared helpers for test.sh (sourced inside a fresh distro container; KIND=arch|deb|rpm)
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS+1)); }
install_pkgs() {
  . /etc/os-release; echo "### $PRETTY_NAME"
  if [ "$KIND" = arch ]; then
    # the archlinux image has NoExtract for usr/share/doc/*, which would drop the guide
    sed -i -E '/^NoExtract/ s#[[:space:]]usr/share/doc/\*##' /etc/pacman.conf
    grep -q '^NoExtract.*usr/share/doc/\*' /etc/pacman.conf && { fail "could not remove NoExtract usr/share/doc"; finish; }
    echo "note: removed usr/share/doc/* from NoExtract in /etc/pacman.conf (image drops docs)"
    pacman -Syu --noconfirm >/tmp/upgrade.log 2>&1 || { tail -30 /tmp/upgrade.log; fail "pacman -Syu"; finish; }
    pacman -U --noconfirm /pkgs/*.pkg.tar.zst >/tmp/install.log 2>&1 || { tail -30 /tmp/install.log; fail "pacman -U"; finish; }
    pacman -Q srs-sysinfo
  elif [ "$KIND" = deb ]; then
    export DEBIAN_FRONTEND=noninteractive
    # minimized images (ubuntu:24.04) drop /usr/share/doc/* via dpkg path-exclude; the guide lives there
    for x in /etc/dpkg/dpkg.cfg.d/excludes; do [ -f $x ] && grep -q '^path-exclude=/usr/share/doc' $x && { echo "note: removed $x (minimized image drops /usr/share/doc)"; rm -f $x; }; done
    apt-get update -qq && apt-get install -y -q --no-install-recommends /pkgs/*.deb >/tmp/install.log 2>&1 \
      || { tail -30 /tmp/install.log; fail "apt install"; finish; }
    grep -E '^Setting up ' /tmp/install.log | grep -c . | sed 's/^/packages set up incl. deps: /'
    for p in /pkgs/*.deb; do dpkg-deb -f "$p" Package Version Depends; done
  elif command -v dnf >/dev/null; then
    # container images set tsflags=nodocs, which would drop the guide (it is %doc)
    dnf -y -q --setopt=tsflags= install /pkgs/*.rpm >/tmp/install.log 2>&1 || { tail -30 /tmp/install.log; fail "dnf install"; finish; }
    rpm -q $(for p in /pkgs/*.rpm; do rpm -qp --qf '%{NAME} ' "$p"; done)
  else
    # the Tumbleweed image sets rpm.install.excludedocs, which would drop the guide (it is %doc)
    for x in /usr/etc/zypp/zypp.conf.d/excludedocs.conf /etc/zypp/zypp.conf.d/excludedocs.conf; do [ -f $x ] && { echo "note: removed $x (image excludes docs)"; rm -f $x; }; done
    zypper -n -q --no-gpg-checks install --allow-unsigned-rpm --no-recommends /pkgs/*.rpm >/tmp/install.log 2>&1 \
      || { tail -30 /tmp/install.log; fail "zypper install"; finish; }
    rpm -q $(for p in /pkgs/*.rpm; do rpm -qp --qf '%{NAME} ' "$p"; done)
  fi
  echo "OK: installed with the distro package manager"
}
check_ldd() {
  command -v ldd >/dev/null || { echo "(no ldd in image, checking with ld.so)"; }
  for f in "$@"; do
    if command -v ldd >/dev/null; then out=$(ldd "$f" 2>&1); else out=$(/lib64/ld-linux-x86-64.so.2 --list "$f" 2>&1); fi
    if echo "$out" | grep -q 'not found'; then fail "ldd $f:"; echo "$out" | grep 'not found'; else echo "OK ldd: $f ($(echo "$out" | wc -l) libs, none missing)"; fi
  done
}
run_cmd() {  # run_cmd LABEL CMD...   (expects exit 0)
  local label=$1; shift
  out=$("$@" 2>&1); rc=$?
  echo "--- $label (exit $rc)"; echo "$out" | head -${HEADN:-6}
  [ $rc -eq 0 ] || fail "$label exit $rc"
}
run_expect_fail() {  # run_expect_fail LABEL PATTERN CMD...  (non-zero exit with a message matching PATTERN)
  local label=$1 pat=$2; shift 2
  out=$("$@" 2>&1 </dev/null); rc=$?
  echo "--- $label (exit $rc)"; echo "$out" | head -${HEADN:-8}
  echo "$out" | grep -qiE "$pat" || fail "$label: message not matching /$pat/"
}
finish() { if [ $FAILS -eq 0 ]; then echo "RESULT: PASS"; exit 0; else echo "RESULT: FAIL ($FAILS)"; exit 1; fi; }
