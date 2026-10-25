#!/usr/bin/env python3
"""Sync CHECKLIST.tsv with the code: for every item that has a real producer in sec_<group>.c,
copy its bucket, needs_root flag and Linux source from the srs_item table into the checklist,
so the checklist (and the stubs generated from it) never disagree with what the program does.
Usage: python3 sync_checklist.py [CHECKLIST.tsv]   (then: python3 gen_stubs.py CHECKLIST.tsv sec_stubs.c)"""
import csv
import glob
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'CHECKLIST.tsv'
STR = r'"((?:[^"\\]|\\.)*)"'
ITEM = re.compile(r'\{\s*' + STR + r',\s*' + STR + r',\s*' + STR + r',\s*(B_\w+),\s*(\d+),\s*' + STR + r',\s*(\w+),')
BUCKET = {'B_LINUX': 'LINUX', 'B_ABSENT': 'ABSENT', 'B_WINONLY': 'WINONLY'}

code = {}
for src in sorted(glob.glob('sec_*.c')):
    if src == 'sec_stubs.c':
        continue
    for m in ITEM.finditer(open(src, encoding='utf-8').read()):
        iid, _cat, _title, bucket, root, lsrc, fn = m.groups()
        if fn == 'NULL' or iid in code:
            continue
        code[iid] = (BUCKET[bucket], 'yes' if root != '0' else 'no', bytes(lsrc, 'utf-8').decode('unicode_escape'))

with open(path, encoding='utf-8') as f:
    rd = csv.DictReader(f, delimiter='\t', quoting=csv.QUOTE_NONE)
    fields, rows = rd.fieldnames, list(rd)
changed = 0
for r in rows:
    c = code.get(r['id'])
    if not c:
        continue
    new = {'bucket': c[0], 'needs_root': c[1], 'linux_source': c[2]}
    if any(r[k] != v for k, v in new.items()):
        changed += 1
        r.update(new)
with open(path, 'w', encoding='utf-8', newline='') as f:
    f.write('\t'.join(fields) + '\n')
    for r in rows:
        f.write('\t'.join(r.get(k) or '' for k in fields) + '\n')
print('%d of %d items have producers; %d checklist rows updated' % (len(code), len(rows), changed))
