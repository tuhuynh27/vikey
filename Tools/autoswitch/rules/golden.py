#!/usr/bin/env python3
"""Golden snapshots of engine behavior. usage: golden.py check | update

1. Mixed text: the chat corpus (with and without typos) and the synthetic val set typed through the engine
   with no manual switching. Any change in output is listed word by word.
2. Telex lock: every Vietnamese syllable typed in Vietnamese mode in 3 tone orders must keep
   producing the same text and language.
3. Metrics: manual switches per 100 words (ideal user) must not get worse.
"""
import json, os, re, subprocess, sys, unicodedata
HERE = os.path.dirname(os.path.abspath(__file__))
AS = os.path.dirname(HERE)
GOLD = os.path.join(HERE, 'golden')
TMP = os.environ.get('TMPDIR', '/tmp')
SIM = os.path.join(AS, 'sim', 'sim')
HARNESS = os.path.join(AS, 'test', 'harness')
update = len(sys.argv) > 1 and sys.argv[1] == 'update'
os.makedirs(GOLD, exist_ok=True)
failed = False

CORPORA = {'chat': 'chat_messages.txt', 'synthetic': 'synthetic_val.txt'}   # sim/corpus (README there)

def units(typos, corpus='chat'):
    out = os.path.join(TMP, f'golden_{corpus}_{typos}.units')
    args = ['python3', os.path.join(AS, 'sim', 'prep.py'), os.path.join(AS, 'sim', 'corpus', CORPORA[corpus])]
    if typos: args += ['--typos', str(typos)]
    with open(out, 'w') as f: subprocess.run(args, stdout=f, check=True)
    return out

def compare(name, new_lines, describe):
    global failed
    path = os.path.join(GOLD, name)
    if update or not os.path.exists(path):
        open(path, 'w', encoding='utf-8').write('\n'.join(new_lines) + '\n')
        print(f'{name}: saved {len(new_lines)} lines'); return
    old = open(path, encoding='utf-8').read().rstrip('\n').split('\n')
    diffs = [(i, o, n) for i, (o, n) in enumerate(zip(old, new_lines)) if o != n]
    if len(old) != len(new_lines): diffs.append((-1, f'{len(old)} lines', f'{len(new_lines)} lines'))
    if not diffs:
        print(f'{name}: unchanged ({len(new_lines)} lines)'); return
    failed = True
    print(f'{name}: {len(diffs)} line(s) changed')
    for i, o, n in diffs[:40]: describe(o, n)
    if len(diffs) > 40: print(f'  ... {len(diffs) - 40} more')

def describe_text(o, n):
    og, want = (o.split('\t') + [''])[:2]
    ng = n.split('\t')[0]
    ow, nw, ww = og.split(' '), ng.split(' '), want.split(' ')
    changed = [f'{a!r} -> {b!r}' for a, b in zip(ow, nw) if a != b]
    print(f'  intended: {want}\n    before: {og}\n    now:    {ng}\n    words:  {", ".join(changed[:6])}')

# 1. mixed text: the chat corpus (with and without typos) and our synthetic val set (chat, mixed, VI-only, EN-only)
RUNS = [('chat', 0), ('chat', 0.03), ('synthetic', 0)]
for corpus, typos in RUNS:
    u = units(typos, corpus)
    dump = os.path.join(TMP, f'golden_dump_{corpus}_{typos}.txt')
    subprocess.run([SIM, u, '--lazy', '--dump', dump], check=True, capture_output=True)
    compare(f'{corpus}{"_typos" if typos else ""}_lazy.tsv', open(dump, encoding='utf-8').read().rstrip('\n').split('\n'), describe_text)

# 2. Telex lock
sys.path.insert(0, os.path.join(AS))
src = open(os.path.join(AS, 'build_dict.py')).read().split('def load')[0]
ns = {'__file__': os.path.join(AS, 'build_dict.py')}; exec(src, ns)
TONES = {'́': 's', '̀': 'f', '̉': 'r', '̃': 'x', '̣': 'j'}
def telex_variants(word):
    word = unicodedata.normalize('NFC', word)
    letters, tone, tone_at = [], '', -1
    for ch in word:
        d = unicodedata.normalize('NFD', ch); base, marks = d[0], d[1:]
        if base == 'đ': letters.append(('dd', False)); continue
        k = base
        for m in marks:
            if m in TONES: tone, tone_at = TONES[m], len(letters)
            elif m == '̂': k += base
            elif m in '̛̆': k += 'w'
            else: return []
        letters.append((k, base in 'aeiouy'))
    keys = [k for k, _ in letters]
    if not tone: return [''.join(keys)] if ''.join(keys).isascii() else []
    end = ''.join(keys) + tone
    at = keys[:]; at[tone_at] += tone
    last = max(i for i, (k, v) in enumerate(letters) if v); cl = keys[:]; cl[last] += tone
    return sorted(v for v in {end, ''.join(at), ''.join(cl)} if v.isascii())
syl = []
for line in open(os.path.join(AS, 'vi_50k.txt'), encoding='utf-8'):
    w, c = line.split()
    k = ns['vn_key'](w)
    if k and ns['SYL'].match(k) and int(c) >= 5 and len(w) <= 7 and w[-1] not in 'srx':
        syl.append((w, int(c)))
counts = {}
for w, c in syl:
    for v in telex_variants(w): counts[v] = max(counts.get(v, 0), c)
cases = sorted(counts)
res = subprocess.run([HARNESS, 'lines'], input=''.join(f'1\t{k} \n' for k in cases), capture_output=True, text=True, check=True).stdout.rstrip('\n').split('\n')
lines = [f'{k}\t{r}' for k, r in zip(cases, res)]
compare('telex_vn_mode.tsv', lines, lambda o, n: print(f'  {o.split(chr(9))[0]}: before {o.split(chr(9))[1:]!r} now {n.split(chr(9))[1:]!r}'))

# 3. metrics
metrics = {}
for corpus, typos in RUNS:
    out = subprocess.run([SIM, os.path.join(TMP, f'golden_{corpus}_{typos}.units')], capture_output=True, text=True).stdout
    m = re.search(r'manual switches (\d+).*wrong words (\d+)', out)
    metrics[f'{corpus}{"_typos" if typos else ""}'] = {'manual': int(m.group(1)), 'wrong': int(m.group(2))}
mpath = os.path.join(GOLD, 'metrics.json')
if update or not os.path.exists(mpath):
    json.dump(metrics, open(mpath, 'w'), indent=1); print('metrics: saved', metrics)
else:
    old = json.load(open(mpath))
    for k, v in metrics.items():
        if k not in old: print(f"metrics {k}: new (run golden.py update)"); failed = True; continue
        o = old[k]
        status = 'worse' if (v['manual'] > o['manual'] or v['wrong'] > o['wrong']) else 'ok'
        if status == 'worse': failed = True
        print(f"metrics {k}: manual switches {o['manual']} -> {v['manual']}, wrong words {o['wrong']} -> {v['wrong']}  [{status}]")

if failed:
    print('\nBehavior changed. If every change above is intended, run: golden.py update')
sys.exit(1 if failed else 0)
