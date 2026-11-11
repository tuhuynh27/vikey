#!/usr/bin/env python3
"""Runs reported.tsv (engine bugs Tony reported) against the engine via ../test/harness.
Prints "reported bugs fixed: N/M"; exits 1 if a bug marked fixed fails again."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
H = os.environ.get('VIKEY_HARNESS', os.path.join(HERE, '..', 'test', 'harness'))
rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(HERE, 'reported.tsv'), encoding='utf-8') if l.strip() and not l.startswith('#')]
passed, back = 0, []
for r in rows:
    rid, date, dec, status, start, keys, want, wlang = r[:8]
    out = subprocess.run([H, start, keys], capture_output=True, text=True).stdout
    text, _, tail = out.partition('\nswitches=')
    lang = '1' if 'lang=VN' in tail else '0'
    ok = text == want and lang == wlang
    passed += ok
    if not ok and status == 'fixed': back.append(f'  {rid} ({dec}) {keys!r}: got {text!r} lang {lang}, want {want!r} lang {wlang}  -- {r[8] if len(r) > 8 else ""}')
    if '-v' in sys.argv: print(f"  {'ok  ' if ok else 'FAIL'} {rid} {dec} {keys!r} -> {text!r}")
print(f'reported bugs fixed: {passed}/{len(rows)}')
for b in back: print('FAIL reported bug is back:\n' + b)
sys.exit(1 if back else 0)
