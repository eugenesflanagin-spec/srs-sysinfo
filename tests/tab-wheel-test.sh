#!/usr/bin/env bash
# tests/tab-wheel-test.sh - the mouse wheel over the side (vertical) tab list: down / up = the next / previous tab, one notch per
# tab, no wrap-around; over the page itself and with tabs on top the wheel does not change the tab.
#
#   tests/tab-wheel-test.sh SRS-GUI
#
# Read-only: a page of device lists (USB & PCI devices), no test or benchmark runs. Own Xvfb, throw-away HOME, xdotool clicks 4 / 5.
set -u
GUI=${1:-}
[ -x "$GUI" ] || { echo "usage: $0 SRS-GUI"; exit 2; }
command -v Xvfb >/dev/null && command -v xdotool >/dev/null || { echo "  skip  no Xvfb / xdotool"; exit 0; }
TMP=$(mktemp -d)
cleanup() { [ -n "${G:-}" ] && kill "$G" 2>/dev/null; [ -n "${XPID:-}" ] && kill "$XPID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share XDG_STATE_HOME=$TMP/home/.local/state
mkdir -p "$XDG_CONFIG_HOME/srs"
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
d=:$((50 + RANDOM % 9))
Xvfb "$d" -screen 0 1400x1000x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
sleep 1
start() {   # $1 = layout (side / row), $2 = the item to open (default pci-bus)
    printf '[SRSDEF]\nTheme=Light\n[Tabs]\nLineup=Expert\nLayout=%s\n' "$1" >"$XDG_CONFIG_HOME/srs/SRSDEF.ini"
    env -u WAYLAND_DISPLAY GDK_BACKEND=x11 DISPLAY=$d SRS_TEST_TAB_LOG=1 SRS_OPEN=${2:-pci-bus} "$GUI" >"$TMP/log" 2>&1 & G=$!
    sleep 4
    W=$(DISPLAY=$d xdotool search --name "SRS System Information" | tail -1)
    DISPLAY=$d xdotool windowsize "$W" 1180 820 windowmove "$W" 0 0; sleep 1
}
last() { grep "SRS-TEST: tab" "$TMP/log" | tail -1 | awk '{print $3}'; }
settle() { local a b; for i in $(seq 1 60); do a=$(wc -l <"$TMP/log"); sleep 6; b=$(wc -l <"$TMP/log"); [ "$a" = "$b" ] && return; done; }   # every page drawn
wheel() { DISPLAY=$d xdotool mousemove "$1" "$2"; sleep 0.2; for i in $(seq 1 "$4"); do DISPLAY=$d xdotool mousemove 1100 700 mousemove "$1" "$2" click "$3"; sleep 0.15; done; settle; }
# Note: on Xvfb a tab's tooltip opens under the pointer and then takes the wheel, so long runs of notches are not tested here
# (the no-wrap clamp is in on_nb_scroll); check those on a real desktop.   # away and back: no tab tooltip (on Xvfb it opens under the pointer and takes the wheel)

echo "== side tabs: the wheel over the tab list steps through the tabs"
start side
a=$(last)
wheel 300 420 5 3; b=$(last)
[ -n "$a" ] && [ -n "$b" ] && [ "$b" -eq $((a + 3)) ] && ok "3 notches down: tab $a -> $b" || bad "down: tab $a -> $b (want +3)"
wheel 300 420 4 1; c=$(last)
[ "$c" -eq $((b - 1)) ] && ok "1 notch up: tab $b -> $c" || bad "up: tab $b -> $c (want -1)"
n=$(grep -c "SRS-TEST: tab" "$TMP/log")
wheel 900 500 5 3
[ "$(grep -c 'SRS-TEST: tab' "$TMP/log")" = "$n" ] && ok "over the page itself the wheel does not change the tab" || bad "the wheel over the page changed the tab"
kill $G; wait $G 2>/dev/null; G=

# keyboard in the tab list itself (arrows, Home, End) is GTK's own notebook navigation: SRS adds no key handling there
# (grep GDK_KEY_Up / Down / Home / End finds none), and Xvfb without a window manager does not give a tab the focus
# reliably, so it is checked on a real desktop.
echo "== keyboard: arrows in a spin button change the value, not the tab (Stress & Burnin tab)"
printf '[SRSDEF]\nTheme=Light\n[Tabs]\nLineup=Expert\nLayout=side\n' >"$XDG_CONFIG_HOME/srs/SRSDEF.ini"
env -u WAYLAND_DISPLAY GDK_BACKEND=x11 DISPLAY=$d SRS_TEST_TAB_LOG=1 SRS_OPEN=stressburn "$GUI" >"$TMP/log" 2>&1 & G=$!
sleep 5
W=$(DISPLAY=$d xdotool search --name "SRS System Information" | tail -1)
DISPLAY=$d xdotool windowsize "$W" 1180 820 windowmove "$W" 0 0; sleep 2
DISPLAY=$d xdotool windowfocus --sync "$W"
n=$(grep -c "SRS-TEST: tab" "$TMP/log")
for k in Up Up Down; do DISPLAY=$d xdotool key "$k"; sleep 0.5; done   # focus starts in the tool's first control
sleep 1
[ "$(grep -c 'SRS-TEST: tab' "$TMP/log")" = "$n" ] && ok "arrows inside the tool's controls did not switch tabs" || bad "arrows in the tool switched tabs"
kill $G; wait $G 2>/dev/null; G=

echo "== tabs on top: unchanged"
start row
n=$(grep -c "SRS-TEST: tab" "$TMP/log")
wheel 500 210 5 3
[ "$(grep -c 'SRS-TEST: tab' "$TMP/log")" = "$n" ] && ok "the wheel over top tabs does not change the tab" || bad "top tabs changed by the wheel"
kill $G; wait $G 2>/dev/null; G=

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
