#!/usr/bin/env bash
# tests/autostart-test.sh - the Startup Apps page's core (perf_autostart.c): everything that starts at login is listed with
# the reason it is hidden or will not start, and add / disable / enable / edit / remove / restore / undo / system overrides
# change only the user's own files, atomically, keeping comments and translations, with one .srs-bak of a file the user had.
#
#   tests/autostart-test.sh [SYSREPORT-SEARCH]      (the argument is accepted for make test-bench and not used)
#
# SAFETY: a throw-away HOME / XDG_CONFIG_HOME / XDG_STATE_HOME / XDG_CONFIG_DIRS (copies of tests/autostart_fixtures);
# /etc/xdg/autostart is left out (SRS_TEST_AUTOSTART_NO_ETC=1); systemctl is tests/fake-systemctl.sh, which only logs
# its arguments and keeps its "enabled" list in the throw-away folder. Nothing outside that folder is written.
set -u
cd "$(dirname "$0")/.." || exit 2
command -v pkg-config >/dev/null && pkg-config --exists glib-2.0 || { echo "  skip  no glib-2.0 development files"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
CC=/usr/bin/gcc   # full path, like the Makefile (/opt/aocc/bin shadows gcc here; $CC may be clang)
# shellcheck disable=SC2046
$CC -std=c11 -Wall -Wextra -Werror -Wno-format-truncation -O2 -D_DEFAULT_SOURCE -D_FORTIFY_SOURCE=2 $(pkg-config --cflags glib-2.0) \
    -o "$TMP/ash" tests/autostart_harness.c perf_autostart.c $(pkg-config --libs glib-2.0) || { echo "  FAIL  build"; exit 1; }
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_STATE_HOME=$TMP/home/.local/state XDG_CACHE_HOME=$TMP/home/.cache
export XDG_DATA_HOME=$TMP/home/.local/share XDG_CONFIG_DIRS=$TMP/sys XDG_CURRENT_DESKTOP=KDE SRS_TEST_AUTOSTART_NO_ETC=1
export SRS_TEST_SYSTEMCTL=$PWD/tests/fake-systemctl.sh FAKE_SYSTEMCTL_DIR=$TMP/fake FAKE_SYSTEMCTL_LOG=$TMP/fake/log
export LANG=C LC_ALL=C LANGUAGE=
mkdir -p "$HOME" "$TMP/fake" "$XDG_CONFIG_HOME"
cp -r tests/autostart_fixtures/sys "$TMP/sys"
cp -r tests/autostart_fixtures/user/. "$XDG_CONFIG_HOME/"
printf 'fake-agent.service\n' > "$TMP/fake/enabled"
A=$TMP/ash
U=$XDG_CONFIG_HOME/autostart
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
has() { grep -qF -- "$2" <<<"$1" && ok "$3" || { bad "$3"; printf '        wanted: %s\n' "$2"; }; }
hasnt() { grep -qF -- "$2" <<<"$1" && { bad "$3"; printf '        unwanted: %s\n' "$2"; } || ok "$3"; }
row() { "$A" list | grep "^$1|"; }

echo "== listing"
L=$("$A" list)
has "$L" "xdg:plain.desktop|on=1|starts=1|hidden=0|missing=0|desk=0|System|" "plain system entry starts"
has "$L" "xdg:hidden.desktop|on=0|starts=0|hidden=1" "Hidden=true in the system entry: off and hidden"
has "$L" "Turned off in the system entry (Hidden=true)" "  ... says why"
has "$L" "xdg:gnomeoff.desktop|on=0|starts=0" "X-GNOME-Autostart-enabled=false: off"
has "$L" "xdg:nodisplay.desktop|on=1|starts=1|hidden=1" "NoDisplay=true: hidden but still starts"
has "$L" "xdg:gnomeonly.desktop|on=1|starts=0|hidden=1|missing=0|desk=1" "OnlyShowIn=GNOME on KDE: not for this desktop"
has "$L" "xdg:notkde.desktop|on=1|starts=0|hidden=1|missing=0|desk=1" "NotShowIn=KDE: not for this desktop"
has "$L" "Starts only when this setting is on: kaccessrc:Keyboard:Gestures:false" "KDE autostart condition shown raw"
has "$L" "TryExec program not found: srs-no-such-program-tryexec" "TryExec missing"
has "$L" "xdg:missing.desktop|on=1|starts=0|hidden=0|missing=1" "Exec program missing"
has "$L" "Program not found: srs-no-such-program" "  ... says which program"
has "$L" "xdg:copied.desktop|on=1|starts=1|hidden=0|missing=0|desk=0|System, changed for you|delay=0|Copied By User|true --users-copy|" "a user file replaces the system entry of the same name"
has "$L" "xdg:mine.desktop|on=1|starts=1|hidden=0|missing=0|desk=0|User|delay=3|My Own App|true --mine|" "user entry; sh -c sleep wrapper read as delay 3"
has "$L" "systemd:fake-agent.service|on=1|starts=1" "enabled systemd user unit listed"
has "$L" "env:setenv.sh|on=1|starts=1|hidden=0|missing=0|desk=0|Plasma login script" "Plasma env script listed"
[ "$(grep -c '^xdg:copied.desktop|' <<<"$L")" = 1 ] && ok "overridden entry listed once" || bad "overridden entry listed once"
[ "$("$A" label xdg:plain.desktop)" = "Disable for Me" ] && ok "Remove on a system entry = Disable for Me" || bad "remove label (system)"
[ "$("$A" label xdg:mine.desktop)" = "Remove" ] && ok "Remove on a user entry = Remove" || bad "remove label (user)"

echo "== delay wrapper"
[ "$("$A" wrap 'foo --x "a b"' 5)" = 'sh -c "sleep 5 && foo --x \"a b\""' ] && ok "wrap escapes quotes" || bad "wrap: $("$A" wrap 'foo --x "a b"' 5)"
[ "$("$A" unwrap 'sh -c "sleep 5 && foo --x \"a b\""')" = '5|foo --x "a b"' ] && ok "unwrap round trip" || bad "unwrap"
[ "$("$A" unwrap 'sh -c "sleep 3 && snapper-tools --autostart; exit 0"')" = '3|snapper-tools --autostart; exit 0' ] && ok "unwrap the user's style" || bad "unwrap 2"
[ "$("$A" check 'srs-no-such-thing --x')" = 'Program not found: srs-no-such-thing' ] && ok "check: missing program" || bad "check missing"
[ "$("$A" check 'env FOO=1 true --x')" = 'ok' ] && ok "check: env VAR=x prog" || bad "check env"

echo "== disable / enable a system entry (per-user override, /etc never written)"
SUM0=$(cat "$TMP"/sys/autostart/* | md5sum)
"$A" disable xdg:plain.desktop || bad "disable plain"
[ -f "$U/plain.desktop" ] && ok "override copy written" || bad "override copy written"
grep -qx 'Hidden=true' "$U/plain.desktop" && ok "  ... Hidden=true" || bad "  ... Hidden=true"
grep -qx 'X-SRS-Override=true' "$U/plain.desktop" && ok "  ... marked X-SRS-Override" || bad "  ... X-SRS-Override"
grep -qx '# a comment that must survive' "$U/plain.desktop" && ok "  ... comment kept" || bad "  ... comment kept"
grep -qx 'Name\[de\]=Einfache Systemanwendung' "$U/plain.desktop" && ok "  ... translation kept" || bad "  ... translation kept"
[ ! -e "$U/plain.desktop.srs-bak" ] && ok "  ... no .srs-bak for a file SRS made" || bad "  ... unexpected .srs-bak"
ls "$U"/*.tmp >/dev/null 2>&1 && bad "temporary file left behind" || ok "no temporary file left"
has "$(row xdg:plain.desktop)" "on=0|starts=0" "re-read: off"
"$A" enable xdg:plain.desktop || bad "enable plain"
[ ! -e "$U/plain.desktop" ] && ok "enable removes SRS's override copy" || bad "enable removes SRS's override copy"
has "$(row xdg:plain.desktop)" "on=1|starts=1|hidden=0|missing=0|desk=0|System|" "re-read: on, plain System again"
"$A" enable xdg:gnomeoff.desktop || bad "enable gnomeoff"
grep -qx 'X-GNOME-Autostart-enabled=true' "$U/gnomeoff.desktop" && grep -qx 'Hidden=false' "$U/gnomeoff.desktop" && ok "enabling a system entry that is off: override with it on" || bad "gnomeoff override"
has "$(row xdg:gnomeoff.desktop)" "on=1|starts=1" "re-read: gnomeoff on"
[ "$(cat "$TMP"/sys/autostart/* | md5sum)" = "$SUM0" ] && ok "system entries unchanged" || bad "system entries changed!"

echo "== the user's own copy of a system entry: changed in place, one .srs-bak"
"$A" disable xdg:copied.desktop || bad "disable copied"
grep -qx 'Hidden=true' "$U/copied.desktop" && ok "Hidden=true in the user's copy" || bad "Hidden=true in the user's copy"
grep -qx 'Exec=true --users-copy' "$U/copied.desktop.srs-bak" && ok ".srs-bak keeps the original" || bad ".srs-bak keeps the original"
"$A" enable xdg:copied.desktop || bad "enable copied"
[ -f "$U/copied.desktop" ] && grep -qx 'Hidden=false' "$U/copied.desktop" && ok "the user's copy is never deleted, Hidden=false" || bad "user's copy kept"
grep -qx 'Hidden=true' "$U/copied.desktop.srs-bak" && bad ".srs-bak overwritten" || ok ".srs-bak written once only"

echo "== add, edit, remove, restore, undo"
K=$("$A" add 'Test Tool' 'true --tool %U' 4 1) || bad "add"
[ "$K" = "xdg:test-tool.desktop" ] && ok "add -> $K" || bad "add key: $K"
F=$U/test-tool.desktop
grep -qx 'Exec=sh -c "sleep 4 && true --tool"' "$F" && ok "delay wrapped, field codes dropped" || bad "add Exec: $(grep Exec= "$F")"
grep -qx 'X-GNOME-Autostart-Delay=4' "$F" && grep -qx 'OnlyShowIn=KDE;' "$F" && grep -qx 'X-SRS-Added=true' "$F" && ok "delay key, OnlyShowIn, X-SRS-Added" || bad "add keys"
has "$(row "$K")" "on=1|starts=1|hidden=0|missing=0|desk=0|User|delay=4|Test Tool|true --tool|" "re-read: added entry"
K2=$("$A" add 'Test Tool' 'true' 0 0)
[ "$K2" = "xdg:test-tool-2.desktop" ] && ok "same name -> unique file" || bad "unique file: $K2"
"$A" edit "$K" 'Renamed Tool' 'true --renamed' 0 || bad "edit"
grep -qx 'Exec=true --renamed' "$F" && ! grep -q 'X-GNOME-Autostart-Delay' "$F" && grep -qx 'Name=Renamed Tool' "$F" && ok "edit: name, command, delay 0 unwraps" || bad "edit result"
"$A" remove xdg:mine.desktop || bad "remove mine"
[ ! -e "$U/mine.desktop" ] && [ -f "$U/.srs-trash/mine.desktop" ] && ok "remove moves to .srs-trash" || bad "remove -> trash"
has "$("$A" trash)" "mine.desktop|My Own App" "trash lists it"
"$A" restore mine.desktop || bad "restore"
[ -f "$U/mine.desktop" ] && [ ! -e "$U/.srs-trash/mine.desktop" ] && ok "restore puts it back" || bad "restore result"
grep -q 'X-SRS-Removed-From' "$U/mine.desktop" && bad "restore leaves the trash marker" || ok "restore drops the trash marker"
grep -qx 'Exec=sh -c "sleep 3 && true --mine"' "$U/mine.desktop" && ok "restored content intact" || bad "restored content"
"$A" remove xdg:plain.desktop || bad "remove system entry"
grep -qx 'Hidden=true' "$U/plain.desktop" && [ -f "$TMP/sys/autostart/plain.desktop" ] && ok "remove on a system entry = disable for me" || bad "system remove"
"$A" undo >/dev/null || bad "undo 1"
[ ! -e "$U/plain.desktop" ] && ok "undo: override gone again" || bad "undo 1 result"
"$A" undo >/dev/null || bad "undo 2"
[ -f "$U/.srs-trash/mine.desktop" ] && [ ! -e "$U/mine.desktop" ] && ok "undo restore: back in the trash" || bad "undo 2 result"
"$A" undo >/dev/null || bad "undo 3"
[ -f "$U/mine.desktop" ] && [ ! -e "$U/.srs-trash/mine.desktop" ] && ok "undo remove: back in autostart" || bad "undo 3 result"
"$A" undo >/dev/null || bad "undo 4"
grep -qx 'Exec=sh -c "sleep 4 && true --tool"' "$F" && grep -qx 'Name=Test Tool' "$F" && ok "undo edit: old content back" || bad "undo 4 result"
"$A" undo >/dev/null; "$A" undo >/dev/null || bad "undo 5/6"
[ ! -e "$F" ] && ok "undo add: file gone" || bad "undo add"
[ -s "$XDG_STATE_HOME/srs/autostart-undo.log" ] && ok "undo log on disk" || bad "undo log on disk"

echo "== systemd user units and Plasma scripts"
"$A" disable systemd:fake-agent.service || bad "disable unit"
grep -q 'fake-systemctl.sh --user disable -- fake-agent.service' "$TMP/fake/log" && ok "systemctl --user disable called" || bad "systemctl disable"
has "$(row systemd:fake-agent.service)" "on=0|starts=0" "disabled unit still listed (so it can be enabled again)"
"$A" enable systemd:fake-agent.service || bad "enable unit"
grep -q 'fake-systemctl.sh --user enable -- fake-agent.service' "$TMP/fake/log" && ok "systemctl --user enable called" || bad "systemctl enable"
"$A" disable systemd:fake-agent.service; "$A" undo >/dev/null || bad "undo unit"
has "$(row systemd:fake-agent.service)" "on=1" "undo re-enables the unit"
"$A" disable env:setenv.sh || bad "disable env script"
[ -f "$XDG_CONFIG_HOME/plasma-workspace/env/disabled/setenv.sh" ] && [ ! -e "$XDG_CONFIG_HOME/plasma-workspace/env/setenv.sh" ] && ok "env script moved to env/disabled" || bad "env move"
has "$(row env:setenv.sh)" "on=0" "re-read: env script off"
"$A" enable env:setenv.sh || bad "enable env"
[ -f "$XDG_CONFIG_HOME/plasma-workspace/env/setenv.sh" ] && ok "env script back" || bad "env back"

echo "== Restore Default"
"$A" disable xdg:plain.desktop >/dev/null
grep -q "Remove SRS's copy" <<<"$("$A" default-what xdg:plain.desktop)" && ok "system entry with SRS's copy: Restore Default offered" || bad "default-what plain"
"$A" default xdg:plain.desktop || bad "default plain"
[ ! -e "$U/plain.desktop" ] && has "$(row xdg:plain.desktop)" "on=1|starts=1|hidden=0|missing=0|desk=0|System|" "Restore Default: SRS's copy gone, the system default applies" || bad "default plain result"
"$A" undo >/dev/null && grep -qx 'Hidden=true' "$U/plain.desktop" && ok "Undo brings SRS's copy back" || bad "undo default plain"
"$A" default xdg:plain.desktop >/dev/null
[ "$("$A" default-what xdg:plain.desktop)" = none ] && ok "plain system entry: nothing to restore" || bad "default-what none"
"$A" edit xdg:mine.desktop 'Renamed Mine' 'true --mine' 3 || bad "edit mine"
[ -f "$U/mine.desktop.srs-bak" ] && grep -qx 'Name=Renamed Mine' "$U/mine.desktop" && ok "user entry changed, .srs-bak kept" || bad "mine edit"
grep -q "original" <<<"$("$A" default-what xdg:mine.desktop)" && ok "user entry with an original: Restore Default offered" || bad "default-what mine"
"$A" default xdg:mine.desktop || bad "default mine"
grep -qx 'Name=My Own App' "$U/mine.desktop" && [ -f "$U/mine.desktop.srs-bak" ] && ok "Restore Default: original back, .srs-bak kept" || bad "default mine result"
"$A" default systemd:fake-agent.service || bad "default unit"
grep -q 'fake-systemctl.sh --user preset -- fake-agent.service' "$TMP/fake/log" && ok "systemd unit: systemctl --user preset" || bad "preset"

echo "== the old (2.2) Startup manager's backups"
OB=$XDG_CONFIG_HOME/srs/startup-backup
mkdir -p "$OB"
printf '[Desktop Entry]\nType=Application\nName=Old Saved App\nExec=true --old\n' > "$OB/oldapp.desktop.orig"
printf '[Desktop Entry]\nType=Application\nName=Old Override\nExec=true\nHidden=true\nX-SRS-Override=true\n' > "$OB/plain.desktop.20261020-101500"
echo notes > "$OB/notes.txt"
OL=$("$A" old-list)
[ "$(wc -l <<<"$OL")" = 2 ] && has "$OL" "oldapp.desktop.orig|Old Saved App" "old backups listed (only .desktop ones)" || bad "old-list: $OL"
[ "$("$A" old-offer)" = 2 ] && [ "$("$A" old-offer)" = 0 ] && ok "offered once" || bad "old-offer"
"$A" old-import oldapp.desktop.orig || bad "import"
grep -qx 'Exec=true --old' "$U/oldapp.desktop" && [ -f "$OB/oldapp.desktop.orig" ] && ok "brought back as a copy; the old backup stays" || bad "import result"
"$A" old-import oldapp.desktop.orig 2>/dev/null && bad "import over an existing entry" || ok "never replaces an entry that is there"
"$A" undo >/dev/null && [ ! -e "$U/oldapp.desktop" ] && [ -f "$OB/oldapp.desktop.orig" ] && ok "Undo takes the copy away, backup kept" || bad "undo import"
"$A" old-import plain.desktop.20261020-101500 && has "$(row xdg:plain.desktop)" "on=0" "an old override copy comes back as written (X-SRS-Override kept)" || bad "import override"
grep -qx 'X-SRS-Override=true' "$U/plain.desktop" && ok "X-SRS-Override read and kept" || bad "X-SRS-Override"

grep -vE ' --user (list-unit-files|show|enable|disable|preset) ' "$TMP/fake/log" | grep -q . && bad "unexpected systemctl calls" || ok "only list/show/enable/disable/preset --user calls"

[ $fail = 0 ] && echo "autostart-test: all passed" || echo "autostart-test: FAILED"
exit $fail
