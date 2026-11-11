#!/usr/bin/env python3
"""Scores Proofread results against eval.jsonl.
  errors fixed        listed errors whose correction is in the output and the wrong form is gone
  fully right         output = the correct text (ignoring a capital first letter and end punctuation the model adds)
  damaged             output changed a word that wasn't wrong (rephrased, translated, other words) ↓
  correct untouched   already-correct sentences given back unchanged (same tolerance)
  no answer           the model failed or its answer was rejected (the keyboard shows "Couldn't fix") ↓
usage: score.py eval.jsonl results.jsonl [--label NAME] [--show N]"""
import difflib, json, re, sys
from collections import defaultdict
ev = {j["id"]: j for j in map(json.loads, open(sys.argv[1], encoding="utf-8"))}
res = {j["id"]: j.get("output") for j in map(json.loads, open(sys.argv[2], encoding="utf-8"))}
label = sys.argv[sys.argv.index("--label") + 1] if "--label" in sys.argv else ""
show = int(sys.argv[sys.argv.index("--show") + 1]) if "--show" in sys.argv else 0

def norm(s):
    s = s.strip().rstrip(".!?…").strip()
    return (s[:1].lower() + s[1:]) if s else s
def toks(s): return re.findall(r"[\w']+|[^\w\s]", s, re.U)

agg = defaultdict(lambda: [0] * 7)   # errors, fixed, sentences, right, damaged, (correct n, untouched)
noans = 0; bad_examples = []
for i, it in ev.items():
    out = res.get(i)
    a = agg[it["kind"]]; a[2] += 1
    if out is None: noans += 1; continue
    o, c = norm(out), norm(it["correct"])
    if it["errors"]:
        for e in it["errors"]:
            a[0] += 1
            fixed = re.search(r'(?<!\w)' + re.escape(e["correct"].lower()) + r'(?!\w)', o.lower()) and \
                    not re.search(r'(?<!\w)' + re.escape(e["typed"].lower()) + r'(?!\w)', o.lower())
            a[1] += bool(fixed)
    if o == c: a[3] += 1
    # damage: a word of the correct text that wasn't part of an error, changed or dropped; or words added
    ct, ot = [t.lower() for t in toks(c)], [t.lower() for t in toks(o)]
    target, left = set(), set()   # words of the fixes; words of the mistakes (left in = not fixed, not damage)
    for e in it["errors"]:
        for t in toks(e["correct"].lower()): target.add(t)
        for t in toks(e["typed"].lower()): left.add(t)
    dmg = False
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, ct, ot, autojunk=False).get_opcodes():
        if op == "equal": continue
        if op in ("replace", "delete") and ct[i1:i2] and all(t in target or not re.match(r"\w", t) for t in ct[i1:i2]): continue   # at a mistake: a miss or another fix, not damage
        if all(t in left or not re.match(r"\w", t) for t in ot[j1:j2]) and not any(re.match(r"\w", t) for t in ct[i1:i2]): continue   # a mistake's words left in, or punctuation
        dmg = True
    if dmg: a[4] += 1; bad_examples.append((it["kind"], it["typed"], out, it["correct"]))
    if not it["errors"]:
        a[5] += 1; a[6] += o == c

T = [sum(a[k] for a in agg.values()) for k in range(7)]
pct = lambda x, y: f"{100 * x / y:.1f}%" if y else "–"
print(f"Proofread eval{' (' + label + ')' if label else ''}: {T[2]} sentences")
print(f"  errors fixed       {pct(T[1], T[0])}  ({T[1]}/{T[0]})")
print(f"  fully right        {pct(T[3], T[2])}")
print(f"  damaged            {pct(T[4], T[2])}")
print(f"  correct untouched  {pct(T[6], T[5])}")
print(f"  no answer          {pct(noans, T[2])}")
for k, a in sorted(agg.items()):
    print(f"    {k:12} n={a[2]:3}  fixed {pct(a[1], a[0]):>6}  right {pct(a[3], a[2]):>6}  damaged {pct(a[4], a[2]):>6}" + (f"  untouched {pct(a[6], a[5])}" if a[5] else ""))
for k, t, o, c in bad_examples[:show]: print(f"  DAMAGE [{k}] {t!r}\n     got  {o!r}\n     want {c!r}")
if "--json" in sys.argv:
    json.dump({"fixed": T[1] / max(T[0], 1), "right": T[3] / T[2], "damaged": T[4] / T[2], "untouched": T[6] / max(T[5], 1), "noanswer": noans / T[2]},
              open(sys.argv[sys.argv.index("--json") + 1], "w"))
