#!/usr/bin/env bash
# tests/ir-test.sh - IR Blaster (ir.c; CLI "ir"; the [ir-blaster] item): dongle detection, the codes file shared with
# IR Remote (kept byte for byte, nothing it wrote is lost, a copy before the first write, a broken file is never
# overwritten), learning and sending against a made-up dongle, and the USB packets SRS writes compared byte for byte
# with the real elksmart-comm (when /usr/bin/elksmart-comm and libusb.h are there).
#
#   tests/ir-test.sh SYSREPORT-SEARCH      (make test-bench passes the path)
#
# SAFETY: no infrared is ever sent and no USB device is opened. SRS_TEST_IR=fake is set for the whole script (SRS then
# opens nothing: the "dongle" is in memory and only logs the bytes); elksmart-comm runs with a stand-in libusb
# (tests/ir_fake_libusb.c) that the loader is checked to pick before it runs, inside bwrap with an empty /dev when
# bwrap works. HOME and the XDG folders are throw-away, so the real ~/.config/irremote is never read or written.
set -u
BIN=${1:-}
[ -x "$BIN" ] || { echo "usage: $0 SYSREPORT-SEARCH"; exit 2; }
BIN=$(readlink -f "$BIN")
SRC=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home XDG_CONFIG_HOME=$TMP/home/.config XDG_CACHE_HOME=$TMP/home/.cache XDG_DATA_HOME=$TMP/home/.local/share
export XDG_STATE_HOME=$TMP/home/.local/state XDG_RUNTIME_DIR=$TMP/run
unset IRREMOTE_CONFIG SRS_TEST_IR_SYSFS SRS_TEST_IR_LEARN SRS_TEST_IR_PROTO SRS_TEST_IR_IDENT SRS_TEST_IR_STREAM SRS_TEST_IR_SHORT
# the udev rule is looked for in made-up folders (this PC's own rule does not change the results); RULES_ETC stands for
# /etc/udev/rules.d (where Copy Install Command writes), RULES_USR for /usr/lib/udev/rules.d (the package's)
RULES_ETC=$TMP/rules-etc RULES_USR=$TMP/rules-usr
mkdir -p "$RULES_ETC" "$RULES_USR"
export SRS_TEST_IR_RULES=$RULES_ETC:$RULES_USR
export SRS_TEST_IR=fake SRS_TEST_IR_LOG=$TMP/usb.log
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
CODES=$XDG_CONFIG_HOME/irremote/remotes.json
fail=0
ok()   { printf '  ok    %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; fail=1; }
skip() { printf '  skip  %s\n' "$*"; }
run()  { OUT=$(timeout 60 "$BIN" ir "$@" 2>&1 </dev/null); RC=$?; }
has()  { grep -qF -- "$1" <<<"$OUT"; }

# learned codes in the format IR Remote stores (copied from a real 045c:0132 capture), an NEC code, odd frequencies
POWER=1e6b206922692222236923682368236823ffffffffffffffffffff6fbeffffffffffffffffffffffffffffffffffffffffffffffffffff77ffff378924ffffffffffffffffffffffffffffffffffffffffffffffff52ffff358b23ffffffffffffffffffffffffffff75
VOLUP=75ffff4d1cffffffffffffffffffffffffffffffffffffffffffffffff51ffff358a23ffffffffffffffffffffffffffff75
VOLDN=ffff378825ffffffffffffffffffffffffffffffffffffffffffffffff4effff368924ffffffffffffffffffffffffffff75
NEC=20df10ef
# a learned capture of NEC 20 df 10 ef (9 ms + 4.5 ms leader, 560 us marks): SRS's fake answers with it when no code is given
LEARNNEC=ffff35ff1a2323232323692323232323232323232323692369232323692369236923692369232323232323236923232323232323232369236923692323236923692369236923

echo "== detection"
SRS_TEST_IR=absent run --status
[ $RC = 1 ] && has "No ELK Smart / OCRUSTAR IR blaster found - plug one in." && has "045c:0132, 045c:0184, 045c:0195, 045c:0134" \
    && ok "no dongle: the plug-one-in line and the supported IDs" || bad "absent: rc=$RC $OUT"
SRS_TEST_IR=noperm run --status
[ $RC = 1 ] && has "don't have permission" && has "70-elksmart-ir.rules" && has 'TAG+="uaccess"' && has "unplug and replug" \
    && ok "no permission: the udev rule and replug" || bad "noperm: rc=$RC $OUT"
run --status
[ $RC = 0 ] && has "Dongle ready" && ok "fake dongle ready" || bad "fake: rc=$RC $OUT"
# sysfs detection on a made-up tree: an unrelated device, then the dongle
SYS=$TMP/sys
mkdir -p "$SYS/1-1" "$SYS/3-4" "$SYS/3-4/3-4:1.0" "$SYS/usb3"
printf '046d\n' >"$SYS/1-1/idVendor"; printf 'c52b\n' >"$SYS/1-1/idProduct"
printf '045c\n' >"$SYS/3-4/idVendor"; printf '0132\n' >"$SYS/3-4/idProduct"; printf '3\n' >"$SYS/3-4/busnum"; printf '7\n' >"$SYS/3-4/devnum"
printf 'OCRUSTAR\n' >"$SYS/3-4/manufacturer"; printf 'Smart IR Blaster\n' >"$SYS/3-4/product"
OUT=$(SRS_TEST_IR= SRS_TEST_IR_SYSFS=$SYS timeout 20 "$BIN" ir --status 2>&1); RC=$?
has "OCRUSTAR Smart IR Blaster" && has "045c:0132" && has "/dev/bus/usb/003/007" && ok "sysfs: finds 045c:0132 among other devices" \
    || bad "sysfs detection: $OUT"
printf '0133\n' >"$SYS/3-4/idProduct"
OUT=$(SRS_TEST_IR= SRS_TEST_IR_SYSFS=$SYS timeout 20 "$BIN" ir --status 2>&1)
has "plug one in" && ok "sysfs: 045c:0133 is not one of ours" || bad "sysfs other product: $OUT"
printf '0132\n' >"$SYS/3-4/idProduct"
OUT=$(SRS_TEST_IR= SRS_TEST_IR_SYSFS=$SYS timeout 20 "$BIN" ir --status 2>&1)
has "device file /dev/bus/usb/003/007 is missing" && ! has "udev rule (" && ok "no device file at all: says so (not the permission advice)" \
    || bad "missing node: $OUT"

echo "== the udev rule: the package's or the user's copy in /etc"
SRS_TEST_IR=noperm run --status
has "Install the udev rule" && has "/etc/udev/rules.d" && has "Rule     not installed" && ok "no rule anywhere: install it (package or /etc copy)" \
    || bad "no rule: $OUT"
printf '%s\n' 'SUBSYSTEM=="usb", ATTRS{idVendor}=="045c", TAG+="uaccess"' >"$RULES_ETC/70-elksmart-ir.rules"
SRS_TEST_IR=noperm run --status
has "The udev rule is installed ($RULES_ETC/70-elksmart-ir.rules)" && has "unplug and replug" && has "Rule     $RULES_ETC/70-elksmart-ir.rules" \
    && ok "rule in /etc (Copy Install Command): found, says replug" || bad "rule in etc: $OUT"
OUT=$(SRS_TEST_IR=noperm timeout 20 "$BIN" '-save=[ir-blaster]' 2>&1)
grep -qE "Permission rule +$RULES_ETC/70-elksmart-ir.rules" <<<"$OUT" && ok "report: Permission rule shows the /etc copy" || bad "report rule: $OUT"
rm "$RULES_ETC/70-elksmart-ir.rules"; printf 'x\n' >"$RULES_USR/70-elksmart-ir.rules"
OUT=$(timeout 20 "$BIN" '-save=[ir-blaster]' 2>&1)
grep -qE "Permission rule +$RULES_USR/70-elksmart-ir.rules" <<<"$OUT" && ok "report: the package's rule" || bad "report usr rule: $OUT"
printf 'x\n' >"$RULES_ETC/70-elksmart-ir.rules"
OUT=$(timeout 20 "$BIN" '-save=[ir-blaster]' 2>&1)
grep -qE "Permission rule +$RULES_ETC/" <<<"$OUT" && ok "both: the /etc copy is the one in effect" || bad "report both: $OUT"
rm -f "$RULES_ETC/70-elksmart-ir.rules" "$RULES_USR/70-elksmart-ir.rules"

echo "== the [ir-blaster] item (read-only)"
: >"$SRS_TEST_IR_LOG"
OUT=$(SRS_TEST_IR=absent timeout 20 "$BIN" '-save=[ir-blaster]' 2>&1)
grep -qF "Not connected" <<<"$OUT" && grep -qF "plug one in" <<<"$OUT" && ok "absent: Status Not connected" || bad "item absent: $OUT"
OUT=$(timeout 20 "$BIN" '-save=[ir-blaster]' 2>&1)
grep -qF "Ready" <<<"$OUT" && [ ! -s "$SRS_TEST_IR_LOG" ] && ok "fake: Ready, and the item wrote nothing to the dongle" || bad "item fake: $OUT"
[ ! -e "$CODES" ] && ok "the item does not create the codes file" || bad "the item created $CODES"
timeout 20 "$BIN" --list | grep -qE '^ir-blaster +hardware +linux +bus +IR Blaster' && ok "listed: ir-blaster in group bus" || bad "--list"

echo "== the codes file, shared with IR Remote"
mkdir -p "$(dirname "$CODES")"
# what IR Remote writes (json.dump indent=2, ensure_ascii=False + newline), with an own-photo layout (a dict), a layout id,
# a shortcut, non-ASCII names and a field SRS does not know
python3 -I - "$CODES" "$POWER" "$VOLUP" "$VOLDN" <<'EOF'
import json, sys
p, pw, vu, vd = sys.argv[1:5]
b = lambda n, c, **k: dict({"name": n, "type": "learned", "code": c, "frequency": 38000, "shortcut": "", "app_shortcut": ""}, **k)
d = {"version": 2, "remotes": [
    {"name": "Visio", "buttons": [b("Power", pw, shortcut="Meta+Ctrl+P"), b("Volume Up", vu), b("Volume Down", vd)], "layout": "vizio-onn-tv"},
    {"name": "Wohnzimmer ü \"Licht\"", "buttons": [b("An/Aus →", "", future_field={"x": [1, 2.5, None, True]})],
     "layout": {"image": "photo.jpg", "buttons": [{"name": "An/Aus →", "x": 0.25, "y": 0.5}], "note": "tab\there"}}]}
with open(p, "w", encoding="utf-8") as f:
    json.dump(d, f, indent=2, ensure_ascii=False); f.write("\n")
EOF
cp "$CODES" "$TMP/orig.json"
run --list
[ $RC = 0 ] && has "Visio" && has "Volume Down" && has "Meta+Ctrl+P" && has "Wohnzimmer" && ok "lists IR Remote's file" || bad "--list: $OUT"
run --export "$TMP/export.json"
cmp -s "$TMP/export.json" "$TMP/orig.json" && ok "export = IR Remote's file byte for byte (layout dict, unknown field, UTF-8, escapes)" \
    || { bad "export differs from IR Remote's file"; diff "$TMP/orig.json" "$TMP/export.json" | head -20; }
[ ! -e "$CODES.before-srs" ] && ok "no copy made before the first write" || bad "copy made without a write"
SRS_TEST_IR_LEARN=$NEC run --learn --yes --save "visio" "Input"
[ $RC = 0 ] && ok "learn + save into an existing remote (name found ignoring case)" || bad "learn save: rc=$RC $OUT"
cmp -s "$CODES.before-srs" "$TMP/orig.json" && ok "first write: the old file kept as remotes.json.before-srs" || bad "no .before-srs copy"
python3 -I - "$TMP/orig.json" "$CODES" <<'EOF' && ok "nothing IR Remote wrote was changed or lost; the new button is IR Remote's shape" || bad "data changed"
import json, sys
a, b = (json.load(open(p, encoding="utf-8")) for p in sys.argv[1:3])
v = b["remotes"][0]["buttons"].pop()
assert v == {"name": "Input", "type": "learned", "code": "20df10ef", "frequency": 38000, "shortcut": "", "app_shortcut": ""}, v
assert a == b, "differs"
EOF
cp "$CODES" "$TMP/second.json"
SRS_TEST_IR_LEARN=$NEC run --learn --yes --save "Visio" "Input 2"
cmp -s "$CODES.before-srs" "$TMP/orig.json" && ok "the copy is made once (not replaced by later writes)" || bad ".before-srs replaced"
printf '{"version": 2, "remotes": [ {"name": "Broken"' >"$CODES"
cp "$CODES" "$TMP/broken.json"
run --list
[ $RC = 1 ] && has "not valid JSON" && ok "a broken file: says so" || bad "broken list: rc=$RC $OUT"
SRS_TEST_IR_LEARN=$NEC run --learn --yes --save TV Power
cmp -s "$CODES" "$TMP/broken.json" && ok "a broken file is never overwritten" || bad "broken file overwritten"
printf '{"version": 1, "remotes": [{"buttons": []}]}\n' >"$CODES"
run --list
has "every remote needs a name" && ok "IR Remote's checks: every remote needs a name" || bad "nameless remote: $OUT"
cp "$TMP/second.json" "$CODES"
run --import "$TMP/orig.json"
[ $RC = 0 ] && python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); assert [b["name"] for b in d["remotes"][0]["buttons"]]==["Power","Volume Up","Volume Down","Input"]' "$CODES" \
    && ok "import merges (same-named buttons replaced, others kept)" || bad "import merge: $OUT"
run --import "$TMP/orig.json" --replace
cmp -s "$CODES" "$TMP/orig.json" && ok "import --replace" || bad "import replace"
rm -f "$CODES" "$CODES.before-srs"
SRS_TEST_IR_LEARN=$NEC run --learn --yes --save "New TV" "Power"
python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d=={"version":2,"remotes":[{"name":"New TV","buttons":[{"name":"Power","type":"learned","code":"20df10ef","frequency":38000,"shortcut":"","app_shortcut":""}]}]}, d' "$CODES" \
    && [ ! -e "$CODES.before-srs" ] && ok "no file yet: created in IR Remote's format, nothing to copy" || bad "fresh file: $(cat "$CODES")"
ls "$(dirname "$CODES")" | grep -q 'json\.srs-' && bad "temporary file left behind" || ok "no temporary file left"
cp "$TMP/orig.json" "$CODES"

echo "== learn and send (fake dongle)"
: >"$SRS_TEST_IR_LOG"
run --learn
[ $RC = 1 ] && has "not started" && [ ! -s "$SRS_TEST_IR_LOG" ] && ok "--learn without a terminal or --yes: asks, does nothing" || bad "learn ask: rc=$RC $OUT"
run --send Visio Power
[ $RC = 1 ] && has "not sent" && [ ! -s "$SRS_TEST_IR_LOG" ] && ok "--send without a terminal or --yes: asks, sends nothing" || bad "send ask: rc=$RC $OUT"
OUT=$(SRS_TEST_IR=absent timeout 20 "$BIN" ir --send Visio Power --yes 2>&1 </dev/null); RC=$?
[ $RC = 1 ] && has "plug one in" && ok "--send with no dongle: the plug-one-in line" || bad "send absent: $OUT"
OUT=$(SRS_TEST_IR=noperm timeout 20 "$BIN" ir --learn --yes 2>&1 </dev/null); RC=$?
[ $RC = 1 ] && has "udev rule" && has 'TAG+="uaccess"' && ok "--learn without permission: the fix" || bad "learn noperm: $OUT"
SRS_TEST_IR_LEARN=$POWER run --learn --yes
[ $RC = 0 ] && has "$POWER" && grep -qx 'OUT fdfdfdfd' "$SRS_TEST_IR_LOG" && ok "learn: 0132 ack skipped, pauses waited out, code in pieces joined" \
    || bad "learn: rc=$RC $OUT"
run --learn --yes
has "$LEARNNEC" && has "Attempted pulse decode: 20df10ef" && ok "learn: the code and the attempted pulse decode (20df10ef)" || bad "learn hint: $OUT"
: >"$SRS_TEST_IR_LOG"
t0=$(date +%s)
SRS_TEST_IR_LEARN=never run --learn --yes --timeout 2
t1=$(date +%s)
[ $RC = 1 ] && has "Nothing was received" && grep -qx RESET "$SRS_TEST_IR_LOG" && [ $((t1 - t0)) -lt 15 ] \
    && ok "learn times out, then resets the dongle (left in learn mode)" || bad "learn timeout: rc=$RC $OUT $(cat "$SRS_TEST_IR_LOG")"
SRS_TEST_IR_SHORT=1 SRS_TEST_IR_LEARN=$LEARNNEC run --learn --yes --timeout 2
[ $RC = 1 ] && has "incomplete (received 70 of the 90 bytes" && ok "a code shorter than announced: received N of M bytes" || bad "short learn: rc=$RC $OUT"
t0=$(date +%s)
SRS_TEST_IR_STREAM=1 run --probe
t1=$(date +%s)
[ $RC = 0 ] && has "answering" && [ $((t1 - t0)) -lt 10 ] && ok "a dongle that never stops talking: the flush before identify is bounded" \
    || bad "streaming dongle: rc=$RC $((t1 - t0)) s $OUT"
if command -v flock >/dev/null; then
    : >"$SRS_TEST_IR_LOG"
    flock -o "$XDG_RUNTIME_DIR/irremote-$(id -u).lock" sleep 30 & LKPID=$!   # -o: only flock holds it, so killing flock frees it
    sleep 0.5
    run --probe
    kill $LKPID 2>/dev/null; wait $LKPID 2>/dev/null
    [ $RC = 1 ] && has "busy" && has "not asked" && [ ! -s "$SRS_TEST_IR_LOG" ] && ok "--probe while IR Remote holds the dongle: busy, exit 1, not asked" \
        || bad "probe busy: rc=$RC $OUT"
else skip "no flock(1): the busy-lock check"
fi
: >"$SRS_TEST_IR_LOG"
run --send visio "power" --yes --repeat 3
n=$(grep -c '^OUT ff' "$SRS_TEST_IR_LOG")
[ $RC = 0 ] && has "Sent 3 times" && [ "$(head -1 "$SRS_TEST_IR_LOG")" = "OUT fcfcfcfc" ] && [ "$n" -ge 3 ] && ok "send x3: identify, then the code 3 times" \
    || bad "send: rc=$RC n=$n $OUT"
OUT=$(SRS_TEST_IR_IDENT=none timeout 60 "$BIN" ir --send Visio Power --yes 2>&1 </dev/null); RC=$?
[ $RC = 1 ] && has "isn't answering" && grep -qx RESET "$SRS_TEST_IR_LOG" && ok "a dongle that does not answer: reset tried, then a clear message" \
    || bad "wedged: rc=$RC $OUT"
run --send Visio Nope --yes
has 'has no button "Nope"' && has '"Volume Up"' && ok "unknown button: lists the ones there are" || bad "unknown button: $OUT"
run --send "Wohnzimmer ü \"Licht\"" "An/Aus →" --yes
has "isn't learned yet" && ok "a button with no code: not sent" || bad "unlearned: $OUT"
run --send-code 12345 --yes
has "even number" && ok "bad code: odd number of digits" || bad "odd code: $OUT"
run --send-code 20df1 --nec --yes
has "even number" && ok "bad NEC code" || bad "nec odd: $OUT"
run --send-code 20df10 --nec --yes
has "4 hex digits per press" && ok "NEC needs address + command pairs" || bad "nec pairs: $OUT"
: >"$SRS_TEST_IR_LOG"
run --send Visio Power --dry-run
[ $RC = 0 ] && has "nothing sent" && [ ! -s "$SRS_TEST_IR_LOG" ] && ok "--dry-run: packets shown, nothing written" || bad "dry run: $OUT"

echo "== the packets compared with elksmart-comm"
EC=/usr/bin/elksmart-comm
LIB=$TMP/fakeusb
if [ ! -x "$EC" ]; then skip "no $EC (package elksmart-comm-git)"
elif [ ! -r /usr/include/libusb-1.0/libusb.h ]; then skip "no libusb.h to build the stand-in libusb"
elif ! mkdir -p "$LIB" || ! /usr/bin/gcc -shared -fPIC -O1 -I/usr/include/libusb-1.0 -o "$LIB/libusb-1.0.so.0" "$SRC/tests/ir_fake_libusb.c" 2>"$TMP/cc.log"; then
    bad "building the stand-in libusb: $(cat "$TMP/cc.log")"
else
    # the loader must take the stand-in (and nothing else called libusb), or elksmart-comm does not run at all
    got=$(LD_LIBRARY_PATH=$LIB LD_TRACE_LOADED_OBJECTS=1 "$EC" 2>&1 | grep libusb)
    if [ "$(grep -c . <<<"$got")" != 1 ] || ! grep -qF "$LIB/libusb-1.0.so.0" <<<"$got"; then
        bad "the loader does not pick the stand-in libusb for $EC: $got"
    else
        ok "elksmart-comm loads the stand-in libusb ($LIB)"
        SAND=()
        if command -v bwrap >/dev/null && bwrap --ro-bind / / --dev /dev --bind "$TMP" "$TMP" true 2>/dev/null; then
            SAND=(bwrap --ro-bind / / --dev /dev --bind "$TMP" "$TMP" --die-with-parent --)
            ok "elksmart-comm runs in bwrap with an empty /dev (no /dev/bus/usb)"
        fi
        ec() { LD_LIBRARY_PATH=$LIB SRS_TEST_IR_LOG=$TMP/ec.log timeout 30 "${SAND[@]}" "$EC" "$@" >"$TMP/ec.out" 2>"$TMP/ec.err" </dev/null; }
        cmp_send() {   # what  proto  elksmart-comm args...  --  srs args...
            local what=$1 proto=$2; shift 2
            local ea=() sa=()
            while [ "$1" != -- ]; do ea+=("$1"); shift; done; shift; sa=("$@")
            : >"$TMP/ec.log"; : >"$TMP/srs.log"
            SRS_TEST_IR_PROTO=$proto ec "${ea[@]}"
            SRS_TEST_IR_PROTO=$proto SRS_TEST_IR_LOG=$TMP/srs.log timeout 30 "$BIN" ir --send-code "${sa[@]}" --yes >/dev/null 2>&1 </dev/null
            if [ -s "$TMP/ec.log" ] && cmp -s "$TMP/ec.log" "$TMP/srs.log" && ! grep -q '^error' "$TMP/ec.err"; then
                ok "$what: $(grep -c . "$TMP/srs.log") packets identical"
            else
                bad "$what: packets differ"; diff "$TMP/ec.log" "$TMP/srs.log" | head -6; head -3 "$TMP/ec.err"
            fi
        }
        cmp_send "learned Power, plain"        plain   "$POWER" -- "$POWER"
        cmp_send "learned Volume Up, plain"    plain   "$VOLUP" -- "$VOLUP"
        cmp_send "learned Volume Down, plain"  plain   "$VOLDN" -- "$VOLDN"
        cmp_send "learned Power, Huffman"      huffman "$POWER" -- "$POWER"
        cmp_send "learned Volume Up, Huffman"  huffman "$VOLUP" -- "$VOLUP"
        cmp_send "NEC 20df10ef, plain"         plain   --nec "$NEC" -- "$NEC" --nec
        cmp_send "NEC 20df10ef40bf, Huffman"   huffman --nec 20df10ef40bf -- 20df10ef40bf --nec
        cmp_send "learned Power at 36 kHz"     plain   -f 36000 "$POWER" -- "$POWER" --frequency 36000
        cmp_send "NEC at 56 kHz, Huffman"      huffman -f 56000 --nec "$NEC" -- "$NEC" --nec --frequency 56000
        # dry run (no dongle) prints the same packets as the fake send wrote
        OUT=$(SRS_TEST_IR=absent "$BIN" ir --send-code "$NEC" --nec --frequency 56000 --dry-run --huffman 2>&1)
        diff <(grep '^OUT ' <<<"$OUT") <(grep -v fcfcfcfc "$TMP/srs.log") >/dev/null && ok "--dry-run shows exactly the packets a send writes" || bad "dry run differs"
        # learning: the same code out of the same dongle answers
        : >"$TMP/ec.log"
        SRS_TEST_IR_LEARN=$POWER ec
        ecode=$(awk 'f { print; exit } /^Full command:/ { f = 1 }' "$TMP/ec.out")
        SRS_TEST_IR_LEARN=$POWER run --learn --yes
        [ -n "$ecode" ] && [ "$ecode" = "$POWER" ] && has "$ecode" && cmp -s "$TMP/ec.log" <(grep -v RESET "$SRS_TEST_IR_LOG" | tail -3) \
            && ok "learn: elksmart-comm and SRS read the same code (0132 ack skipped by both)" || bad "learn differs: ec=$ecode $(head -2 "$TMP/ec.err")"
        : >"$SRS_TEST_IR_LOG"
        SRS_TEST_IR_LEARN=$LEARNNEC ec
        hint=$(awk 'f { print; exit } /^Attempted pulse decode:/ { f = 1 }' "$TMP/ec.out")
        SRS_TEST_IR_LEARN=$LEARNNEC run --learn --yes
        [ "$hint" = 20df10ef ] && has "Attempted pulse decode: $hint" && ok "learn: the same pulse decode hint ($hint)" || bad "hint: ec=$hint $OUT"
    fi
fi

[ $fail = 0 ] && echo "ir-test: all passed" || echo "ir-test: FAILED"
exit $fail
