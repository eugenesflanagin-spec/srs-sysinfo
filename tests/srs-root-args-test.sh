#!/usr/bin/env bash
# tests/srs-root-args-test.sh - the input checks of `srs-root --acpi-eval METHOD [ARG...]` (2.4). srs-root runs as root
# and hands the method and arguments to the acpi_call module, so these two checks are what stands between a caller and
# root: only an ACPI name path, and only numbers, "simple strings" and bXXXX buffers as arguments.
#
#   tests/srs-root-args-test.sh [ANY]      (the argument is ignored; make test-bench passes the CLI path)
#
# SAFETY: srs-root.sh is NOT run (it needs root and writes the invoking user's cache). Its two [[ =~ ]] patterns are
# copied out of the script text and applied here with the same bash quoting rules (eval of the very same text), so a
# change to either pattern in srs-root.sh is what gets tested. Also checks that the script parses (bash -n).
set -u
SRC=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT=$SRC/srs-root.sh
fail=0
ok()  { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

echo "== srs-root.sh parses"
bash -n "$SCRIPT" 2>"${TMPDIR:-/tmp}/srs-root-n.$$" && ok "bash -n srs-root.sh" || bad "bash -n: $(cat "${TMPDIR:-/tmp}/srs-root-n.$$")"
rm -f "${TMPDIR:-/tmp}/srs-root-n.$$"

echo "== the two patterns, taken from srs-root.sh"
MRE=$(sed -n 's/^ *\[\[ \$m =~ \(.*\) \]\] || { echo "srs-root: not an ACPI method path.*/\1/p' "$SCRIPT")
ARE=$(sed -n 's/^ *\[\[ \$a =~ \(.*\) \]\] || { echo "srs-root: argument not allowed.*/\1/p' "$SCRIPT")
[ "$(wc -l <<<"$MRE")" = 1 ] && [ -n "$MRE" ] && ok "method pattern: $MRE" || { bad "the method pattern was not found once in srs-root.sh"; exit 1; }
[ "$(wc -l <<<"$ARE")" = 1 ] && [ -n "$ARE" ] && ok "argument pattern: $ARE" || { bad "the argument pattern was not found once in srs-root.sh"; exit 1; }
grep -q '\[ \$# -le 7 \]' "$SCRIPT" && ok "at most 7 arguments (the check is in the script)" || bad "the 7-argument limit is gone"
# the call is built only from the checked values, and written with printf '%s' (no shell expansion of them)
grep -q "printf '%s%s' \"\$m\" \"\$args\" > /proc/acpi/call" "$SCRIPT" && ok "the call is written with printf '%s', from the checked values" || bad "the /proc/acpi/call write changed"

m_ok() { local s=$1; eval "[[ \$s =~ $MRE ]]"; }
a_ok() { local s=$1; eval "[[ \$s =~ $ARE ]]"; }

echo "== method paths"
for s in '\_SB.PCI0.GPP0._STA' '_TZ.TZ01._TMP' '\_SB' '^^_SB.PCI0' 'SB.AMW0.WMAX' '\_SB_.PCI0.SBRG.EC0_._Q11'; do
    m_ok "$s" && ok "allowed: $s" || bad "refused but valid: $s"
done
for s in '\_SB.PCI0; rm -rf /' '$(id)' 'a;b' '1 2' '' '\_sb.pci0' '\_SB.TOOLONG' '\_SB..X' '\\_SB' '\_SB.PCI0.' '_SB/PCI0' \
         $'\\_SB.PCI0\n_STA' '`id`' '\_SB.PCI0 0x1' '0X1F'; do
    m_ok "$s" && bad "allowed but must be refused: [$s]" || ok "refused: [${s//$'\n'/\\n}]"
done

echo "== arguments"
for s in 0x1F 42 0 0xFFFFFFFFFFFFFFFF '"abc def"' '""' '"A-1_b.c"' b0102 b0102ff b00; do
    a_ok "$s" && ok "allowed: $s" || bad "refused but valid: $s"
done
# b0102f is an odd number of hex digits (b0102ff is three whole bytes, so it is allowed above)
for s in b0102f '\_SB.PCI0; rm -rf /' '$(id)' 'a;b' '1 2' '' 0x 0x12345678901234567 123456789012345678901 '"abc"def"' '"a;b"' \
         '"$(id)"' "'single'" b b0 -1 0xG1 $'1\n2' '"a\"b"' '`id`' '"tab	here"' "\"$(printf 'x%.0s' $(seq 1 65))\"" B0102 b01xy; do
    a_ok "$s" && bad "allowed but must be refused: [$s]" || ok "refused: [${s//$'\n'/\\n}]"
done
echo "== nothing dangerous passes either pattern"
for s in '\_SB.PCI0; rm -rf /' '$(id)' 'a;b' '1 2' '`id`' '$HOME' '|cat' '>/etc/x' '&&' $'x\ny'; do
    { m_ok "$s" || a_ok "$s"; } && bad "a pattern lets [$s] through" || ok "neither pattern lets [${s//$'\n'/\\n}] through"
done

[ $fail = 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $fail
