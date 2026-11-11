#!/usr/bin/env python3
"""Every decision in DECISIONS.md has a test (a tagged case, or a test named in decisions.tsv), or says why not.
Prints "decisions with tests: N/M (open K)"; exits 1 when a decision is in neither, or a named test is missing."""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..', '..')
decisions = [(int(n), t) for n, t in re.findall(r'^## (\d+)\. (.*)$', open(os.path.join(ROOT, 'DECISIONS.md'), encoding='utf-8').read(), re.M)]
tagged = set()
for f in os.listdir(HERE):
    if not f.endswith('.tsv') or f == 'decisions.tsv': continue
    for line in open(os.path.join(HERE, f), encoding='utf-8'):
        if line.startswith('#'): continue
        tagged.update(int(n) for n in re.findall(r'(?:^|\t)D(\d+)', line))
listed, fail = {}, []
for line in open(os.path.join(HERE, 'decisions.tsv'), encoding='utf-8'):
    if line.startswith('#') or not line.strip(): continue
    d, kind, what = line.rstrip('\n').split('\t', 2)
    listed[int(d[1:])] = (kind, what)
    if kind == 'test':
        for ref in what.split(' ; '):
            path, _, needle = ref.partition('::')
            p = os.path.join(ROOT, path)
            if not os.path.exists(p) or needle.strip() and needle not in open(p, encoding='utf-8').read():
                fail.append(f'{d}: test not found: {ref}')
    elif kind not in ('none', 'open'):
        fail.append(f'{d}: unknown kind {kind!r}')
tested = opened = behavior = 0
for n, title in decisions:
    if n in tagged or listed.get(n, ('',))[0] == 'test': tested += 1; behavior += 1
    elif n in listed and listed[n][0] == 'open': opened += 1; behavior += 1
    elif n not in listed: fail.append(f'D{n} "{title}": no tagged case and no row in rules/decisions.tsv')
print(f'decisions with tests: {tested}/{behavior} (open {opened}, no behavior {len(decisions) - behavior})')
if '-v' in sys.argv:
    for n, title in decisions:
        if listed.get(n, ('',))[0] == 'open': print(f'  open D{n}: {listed[n][1]}')
for f in fail: print('FAIL ' + f)
sys.exit(1 if fail else 0)
