#!/usr/bin/env python3
"""Adds the Wheel edition's translations (wheel_<lang>.tsv: ID<TAB>text)
to Rockbox's apps/lang/<lang>.lang. Existing phrases are replaced, so it
can be run again after editing a .tsv. Description and target come from
english.lang. usage: apply.py  (from anywhere)"""
import os, re
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = {"fr": "francais", "zh": "chinese-simp", "ja": "japanese", "ko": "korean"}

eng = open(os.path.join(ROOT, "apps/lang/english.lang"), encoding="utf-8").read()
meta = {m.group(1): (m.group(2), m.group(3)) for m in
        re.finditer(r'<phrase>\n  id: (\S+)\n  desc: ([^\n]*)\n  user: (\S+)\n', eng)}
src = {}
for blk in re.findall(r'<phrase>\n.*?</phrase>', eng, re.S):
    m = re.search(r'id: (\S+)\n', blk)
    t = re.search(r'<source>\n    \*: "([^"]*)"', blk)
    if m and t:
        src[m.group(1)] = t.group(1)

for code, name in LANGS.items():
    tsv = os.path.join(HERE, "wheel_%s.tsv" % code)
    rows = [l.rstrip("\n").split("\t", 1) for l in open(tsv, encoding="utf-8") if "\t" in l and not l.startswith("#")]
    path = os.path.join(ROOT, "apps/lang/%s.lang" % name)
    s = open(path, encoding="utf-8").read()
    n = 0
    for pid, text in rows:
        if pid not in meta:
            print("unknown id", pid); continue
        assert '"' not in text, (pid, text)
        # keep printf formats intact
        assert re.findall(r'%[ds%]', text) == re.findall(r'%[ds%]', src[pid]), (code, pid)
        desc, user = meta[pid]
        block = ('<phrase>\n  id: %s\n  desc: %s\n  user: %s\n  <source>\n    *: "%s"\n  </source>\n'
                 '  <dest>\n    *: "%s"\n  </dest>\n  <voice>\n    *: "%s"\n  </voice>\n</phrase>\n'
                 % (pid, desc, user, src[pid], text, text))
        s, k = re.subn(r'<phrase>\n  id: %s\n.*?</phrase>\n' % re.escape(pid), lambda m: block, s, flags=re.S)
        if not k:
            s += block
        n += 1
    open(path, "w", encoding="utf-8").write(s)
    print("%s: %d phrases" % (name, n))
