#!/usr/bin/env python3
"""tools/check-mnemonics.py - every entry of a menu (or of a window page) needs its own Alt+letter.

GTK does not complain about two entries with the same mnemonic: Alt+letter then only moves the focus between
them and activates neither. The GUI source marks each menu / page with

    /* MNEMONIC-MENU: name[, name ...] */  ...labels...  /* MNEMONIC-END */

(regions with the same name are one menu, e.g. the Options menu is built in gui_gtk.c and gui_tabs.inc; one
region can belong to several menus, e.g. a tab's button row is on every page). This script collects the string
literals with a mnemonic ("_x") in each menu and fails when a letter is used twice. The two labels of a
`cond ? "a" : "b"` are one entry. Mnemonics are case-insensitive; "__" is a literal underscore.

    python3 tools/check-mnemonics.py            exit 0 = no duplicates, 1 = duplicates (listed)
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent
FILES = ["gui_gtk.c", "gui_tabs.inc"]
LIT = r'"(?:[^"\\]|\\.)*"'
TERNARY = re.compile(r'\?\s*(' + LIT + r')\s*:\s*(' + LIT + r')')


def mnemonic(lit):
    s = lit[1:-1].replace("__", "")
    m = re.search(r'_([A-Za-z0-9])', s)
    return m.group(1).lower() if m else None


def main():
    menus = {}
    for f in FILES:
        text = (SRC / f).read_text(encoding="utf-8")
        for m in re.finditer(r'/\*\s*MNEMONIC-MENU:\s*([^*()]+?)\s*(?:\(.*?)?\*/(.*?)/\*\s*MNEMONIC-END\s*\*/', text, re.S):
            names = [n.strip() for n in m.group(1).split(",") if n.strip()]
            start_line = text.count("\n", 0, m.start()) + 1
            body = re.sub(r'/\*.*?\*/', lambda c: "\n" * c.group(0).count("\n"), m.group(2), flags=re.S)
            # the second label of "c ? a : b" is the same entry (blanked, line breaks kept)
            body = TERNARY.sub(lambda t: t.group(0)[:t.start(2) - t.start(0)] + '""' + t.group(0)[t.end(2) - t.start(0):], body)
            for k, line in enumerate(body.split("\n")):
                for lit in re.findall(LIT, line):
                    c = mnemonic(lit)
                    if c:
                        for n in names:
                            menus.setdefault(n, []).append((c, lit, f"{f}:{start_line + k}"))
    bad = 0
    for name in sorted(menus):
        seen = {}
        for c, lit, where in menus[name]:
            seen.setdefault(c, []).append((lit, where))
        dups = {c: v for c, v in seen.items() if len(v) > 1}
        print(f"{'FAIL' if dups else 'ok  '}  {name}: {len(menus[name])} entries")
        for c, v in sorted(dups.items()):
            bad = 1
            print(f"        Alt+{c.upper()}: " + "; ".join(f"{lit} ({w})" for lit, w in v))
    if not menus:
        print("no MNEMONIC-MENU regions found")
        return 1
    return bad


if __name__ == "__main__":
    sys.exit(main())
