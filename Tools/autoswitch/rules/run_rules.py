#!/usr/bin/env python3
"""Runs rules.tsv (or the file given) against the engine (via ../test/harness). Each failure names the decision it breaks.
Settings come from the environment, e.g. VIKEY_TELEX_STYLE=1 rules/run_rules.py telex_simple1.tsv (D56)."""
import subprocess, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(HERE, sys.argv[1] if len(sys.argv) > 1 else 'rules.tsv'), encoding='utf-8') if l.strip() and not l.startswith('#')]
inp = ''.join(f"{r[1]}\t{r[2]}\n" for r in rows)
run = subprocess.run([os.environ.get('VIKEY_HARNESS', os.path.join(HERE, '..', 'test', 'harness')), 'lines'], input=inp, capture_output=True, text=True); out = run.stdout
# harness prints "<text>\t<switches>\t<lang>" per case; text may contain newlines from Enter
parts = out.split('\t'); res = []; text = parts[0]
for k in range(1, len(parts)):
    if k % 2 == 0:
        lang, _, rest = parts[k].partition('\n'); res.append((text, lang)); text = rest
# a crash mid-run returns fewer results, and zip() below would quietly check only those
if run.returncode != 0 or len(res) != len(rows):
    err = [l for l in run.stderr.splitlines() if 'ERROR' in l or 'SUMMARY' in l][:2] or run.stderr.splitlines()[-3:]
    print(f"FAIL harness exited with {run.returncode} after {len(res)} of {len(rows)} cases", *err, sep='\n'); sys.exit(1)
fail = 0
for r, (text, lang) in zip(rows, res):
    want = r[3].replace('|', '\n')
    if text != want or lang != r[4]:
        fail += 1
        print(f"FAIL {r[0]}: {r[5]}\n     keys {r[2]!r} (start {'VN' if r[1]=='1' else 'EN'})\n     got  {text!r} lang {'VN' if lang=='1' else 'EN'}\n     want {want!r} lang {'VN' if r[4]=='1' else 'EN'}")
print(f"rules{' (' + sys.argv[1] + ')' if len(sys.argv) > 1 else ''}: {len(rows) - fail}/{len(rows)} pass")
sys.exit(1 if fail else 0)
