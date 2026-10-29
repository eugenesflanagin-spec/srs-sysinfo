#!/usr/bin/env python3
"""tests/columns-test.py DUMP - checks a SRS_TEST_ALIGN_DUMP file: in every block of tab stops, each column's cells must
start at the same x on every row (2 px tolerance). Prints one line per item: OK, or MISALIGNED with the column and rows."""
import sys, collections
rows = collections.defaultdict(list)
for ln in open(sys.argv[1], encoding="utf-8", errors="replace"):
    p = ln.rstrip("\n").split("\t")
    if len(p) != 4 or p[2] == "-":
        continue
    rows[(p[0], p[2])].append((int(p[1]), [int(x) for x in p[3].split(",") if x]))
items = collections.OrderedDict()
for (iid, tag), lines in rows.items():
    bad = items.setdefault(iid, [])
    ncol = max(len(x) for _, x in lines)
    for c in range(ncol):
        xs = collections.Counter(x[c] for _, x in lines if len(x) > c)
        if len(xs) > 1 and max(xs) - min(xs) > 2:
            common = xs.most_common(1)[0][0]
            off = [l for l, x in lines if len(x) > c and abs(x[c] - common) > 2]
            bad.append("%s col %d: x %s (rows %s)" % (tag, c + 1, sorted(xs), off[:5]))
nbad = 0
for iid, bad in items.items():
    if bad:
        nbad += 1
        print("MISALIGNED  %s: %s" % (iid, "; ".join(bad[:3])))
    else:
        print("OK          %s" % iid)
print("%d items with tables checked, %d misaligned" % (len(items), nbad))
sys.exit(1 if nbad else 0)
