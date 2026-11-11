#!/usr/bin/env python3
"""Vikey quality scorecard, from the public repo only (runs on CI).

  scorecard.py                      the current engine
  scorecard.py --compare REF        and the engine at git REF (e.g. origin/main, HEAD~5), side by side
  scorecard.py --json out.json      also write the numbers

Numbers (higher is better unless marked ↓):
  accuracy            words that come out as meant if you never press the switch key (lazy typer),
                      on the chat corpus, the chat corpus with 3% typos, and the synthetic val set
  manual switches ↓   switch presses per 1,000 words for a typer who switches whenever the next word
                      would come out wrong (ideal typer)
  text safety ↓       keys that changed text they shouldn't (before a cursor move, click or switch, or an
                      edit made elsewhere that came out wrong) in realistic typing; must be 0
  slip recovery       slips (typo + Backspace, Backspace into the last word, cursor ← → trips, clicks,
                      edits elsewhere, switch round trips, stray punctuation) fixed without retyping a word
  auto-correct        iOS/Android: right words changed on clean typing ↓, wrong words left on sloppy typing ↓
  speed ↓             engine time per key (ns)
  edge cases          rules.tsv cases this engine passes (the current list, so an older engine shows the gap)
  reported bugs       engine bugs Tony reported (rules/reported.tsv, his exact keys) that this engine gets right
The tools (simulators, harness) are always the current ones; only the engine changes with --compare.
"""
import json, os, re, subprocess, sys, tempfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
AS = os.path.join(ROOT, 'Tools', 'autoswitch')
CORPORA = {'chat': 'chat_messages.txt', 'synthetic': 'synthetic_val.txt'}

def sh(args, **kw): return subprocess.run(args, capture_output=True, text=True, **kw).stdout

def build(engine, out):
    os.makedirs(out, exist_ok=True)
    src = [os.path.join(engine, f) for f in ('Engine.cpp', 'Vietnamese.cpp', 'SmartSwitchKey.cpp')]
    inc = ['-I' + engine, '-I' + os.path.join(engine, 'platforms')]
    cc = ['clang++', '-std=c++17', '-O2', '-w'] + inc
    for name, path, extra in [('sim', 'sim/sim.cpp', ['-DAS_TESTING']), ('rtsim', 'sim/rtsim.cpp', ['-DAS_TESTING']),
                              ('harness', 'test/harness.cpp', []),
                              ('touchsim', 'touch/touchsim.cpp', [os.path.join(engine, 'TouchModel.cpp'), os.path.join(engine, 'Driver.cpp')]),
                              ('bench', 'driver/bench.cpp', [os.path.join(engine, 'Driver.cpp')])]:
        files = [f for f in extra if f.endswith('.cpp')]; flags = [f for f in extra if not f.endswith('.cpp')]
        subprocess.run(cc + flags + ['-o', os.path.join(out, name), os.path.join(AS, path)] + files + src, check=True)

def units(corpus, typos, tmp):
    p = os.path.join(tmp, f'{corpus}_{typos}.units')
    if not os.path.exists(p):
        args = ['python3', os.path.join(AS, 'sim', 'prep.py'), os.path.join(AS, 'sim', 'corpus', CORPORA[corpus])]
        if typos: args += ['--typos', str(typos)]
        with open(p, 'w') as f: subprocess.run(args, stdout=f, check=True)
    return p

def measure(bin_dir, tmp):
    r = {}
    for corpus, typos in (('chat', 0), ('chat', 0.03), ('synthetic', 0)):
        name = corpus + ('+typos' if typos else '')
        u = units(corpus, typos, tmp)
        lz = sh([os.path.join(bin_dir, 'sim'), u, '--lazy'])
        words, wrong = map(int, re.search(r'words (\d+)\s+wrong words (\d+)', lz).groups())
        ideal = sh([os.path.join(bin_dir, 'sim'), u])
        manual = int(re.search(r'manual switches (\d+)', ideal).group(1))
        r[f'accuracy {name}'] = 100 - 100 * wrong / words
        r[f'manual switches {name}'] = 1000 * manual / words
    # realistic typing: 5 seeds on chat, 2 on synthetic
    ops, fixed_by_hand, safety, stuck, kinds = 0, 0, 0, 0, {}
    for corpus, seeds in (('chat', range(1, 6)), ('synthetic', range(1, 3))):
        u = units(corpus, 0, tmp)
        for s in seeds:
            out = sh([os.path.join(bin_dir, 'rtsim'), u, '--seed', str(s), '--show', '0'])
            g, e, st = map(int, re.search(r'guard (\d+)\s+elsewhere (\d+)\s+stuck (\d+)', out).groups())
            safety += g + e; stuck += st
            for k, a, b in re.findall(r'  ([a-z-]+) (\d+)/(\d+)', out):
                kk = kinds.setdefault(k, [0, 0]); kk[0] += int(a); kk[1] += int(b)
    for k, (a, b) in kinds.items():
        r[f'slip recovery: {k}'] = 100 - 100 * a / b if b else 100.0
        ops += b; fixed_by_hand += a
    r['slip recovery (all)'] = 100 - 100 * fixed_by_hand / ops
    r['text safety: bad edits'] = safety
    r['words even a retype couldn\'t fix'] = stuck
    u = units('chat', 0, tmp)
    c0 = sh([os.path.join(bin_dir, 'touchsim'), u, '0.0'], env=dict(os.environ, TOUCH_CORRECT='1'))
    c3 = sh([os.path.join(bin_dir, 'touchsim'), u, '0.30'], env=dict(os.environ, TOUCH_CORRECT='1'))
    r['auto-correct: right words changed (clean)'] = float(re.search(r'auto-correct: ([\d.]+)% wrong', c0).group(1))
    r['auto-correct: words left wrong (sloppy)'] = float(re.search(r'auto-correct: ([\d.]+)% wrong', c3).group(1))
    best = min(float(re.search(r'avg (\d+) ns', sh([os.path.join(bin_dir, 'bench'), u, '100000', '1000000'])).group(1)) for _ in range(3))
    r['speed: ns per key'] = best
    rules = sh(['python3', os.path.join(AS, 'rules', 'run_rules.py')], env=dict(os.environ, VIKEY_HARNESS=os.path.join(bin_dir, 'harness')))
    m = re.search(r'rules: (\d+)/(\d+)', rules)
    if m: r['locked edge cases passing'] = int(m.group(1))
    rep = sh(['python3', os.path.join(AS, 'rules', 'run_reported.py')], env=dict(os.environ, VIKEY_HARNESS=os.path.join(bin_dir, 'harness')))
    m = re.search(r'reported bugs fixed: (\d+)/(\d+)', rep)
    if m: r['reported bugs fixed'] = f'{m.group(1)}/{m.group(2)}'; r['_reported_fixed'] = int(m.group(1))
    return r

LOWER_BETTER = ('manual switches', 'text safety', "couldn't fix", 'auto-correct', 'speed')

def fmt(k, v):
    if isinstance(v, str): return v
    if 'speed' in k: return f'{v:.0f}'
    if isinstance(v, int) or k.startswith('text safety') or "couldn't" in k: return str(int(v))
    if 'manual' in k: return f'{v:.2f}'
    return f'{v:.2f}%' if 'auto-correct' in k else f'{v:.2f}%'

def main():
    a = sys.argv[1:]
    ref = a[a.index('--compare') + 1] if '--compare' in a else None
    jpath = a[a.index('--json') + 1] if '--json' in a else None
    tmp = tempfile.mkdtemp(prefix='vikey_score_')
    build(os.path.join(ROOT, 'Engine'), os.path.join(tmp, 'now'))
    now = measure(os.path.join(tmp, 'now'), tmp)
    old = None
    if ref:
        d = os.path.join(tmp, 'ref'); os.makedirs(d)
        arch = subprocess.run(['git', '-C', ROOT, 'archive', ref, 'Engine'], capture_output=True, check=True).stdout
        subprocess.run(['tar', '-x', '-C', d], input=arch, check=True)
        build(os.path.join(d, 'Engine'), os.path.join(tmp, 'refbin'))
        old = measure(os.path.join(tmp, 'refbin'), tmp)
    rows = [f'| | {ref} | now |' if old else '| | now |', '|---|---:|---:|' if old else '|---|---:|']
    for k, v in now.items():
        if k.startswith('_'): continue
        if k == 'reported bugs fixed' and old is not None:
            o = old.get(k, '–'); d = now['_reported_fixed'] - old.get('_reported_fixed', 0)
            rows.append(f'| {k} | {o} | {v}{" ✅" if d > 0 else " ⚠️" if d < 0 else ""} |'); continue
        if old is None: rows.append(f'| {k} | {fmt(k, v)} |'); continue
        o = old.get(k, '–'); mark = ''
        noise = 0.10 * abs(o) if 'speed' in k and isinstance(o, (int, float)) else 1e-9   # timing jitters
        if isinstance(v, (int, float)) and isinstance(o, (int, float)) and abs(v - o) > noise:
            better = (v < o) if any(t in k for t in LOWER_BETTER) else (v > o)
            mark = ' ✅' if better else ' ⚠️'
        rows.append(f'| {k} | {fmt(k, o) if o != "–" else o} | {fmt(k, v)}{mark} |')
    print('### Vikey quality scorecard\n\n' + '\n'.join(rows) + '\n\nPublic data only: chat corpus (21k words, plain and with 3% typos) and the synthetic val set (87k words). '
          'Realistic typing: 5 seeds on chat, 2 on synthetic. ✅ better, ⚠️ worse than the reference.')
    if jpath: json.dump({'now': now, 'ref': ref, 'ref_values': old}, open(jpath, 'w'), indent=1)

main()
