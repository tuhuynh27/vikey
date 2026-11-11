#!/bin/bash
# Engine lock on Linux: same checks as Tools/autoswitch/check.sh, built with CMake (g++ or clang++).
# usage: Linux/check.sh [update]
set -e
root=$(cd "$(dirname "$0")/.." && pwd); as=$root/Tools/autoswitch
export PYTHONUTF8=1
cmake -S "$root/Linux" -B "$root/Linux/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DVIKEY_IBUS=OFF >/dev/null
cmake --build "$root/Linux/build"
rc=0
"$root/Linux/build/surrounding_test" || rc=1
cd "$as"
python3 rules/run_rules.py || rc=1
python3 rules/golden.py ${1:-check} || rc=1
U=${TMPDIR:-/tmp}/vikey_check_chat.units
python3 sim/prep.py sim/corpus/chat_messages.txt > "$U"
driver/undo_test || rc=1
export DRV_OUT=${TMPDIR:-/tmp}/drv.txt
driver/drvlazy "$U" --start vi >/dev/null; sim/sim "$U" --start vi --lazy --dump ${TMPDIR:-/tmp}/simdump.txt >/dev/null
python3 - <<'PY' && echo "driver == simulator: ok" || { echo "FAIL driver output differs from the simulator"; rc=1; }
import os, re
t = os.environ.get('TMPDIR', '/tmp').rstrip('/')
d = open(t + '/drv.txt', encoding='utf-8').read().split('\n')
s = [l.split('\t')[0] for l in open(t + '/simdump.txt', encoding='utf-8').read().rstrip('\n').split('\n')]
D = re.sub(r'[\W\d_]+', '', ''.join(d)); S = re.sub(r'[\W\d_]+', '', ''.join(s))
raise SystemExit(0 if D == S else 1)
PY
driver/bench "$U" 1000 20000 && echo "performance: ok" || { echo "FAIL performance budget"; rc=1; }
out=$(touch/touchsim "$U" 0.30); echo "$out" | sed 's/^/  /'
python3 - "$out" <<'PY' || rc=1
import re, sys
s = sys.argv[1]
near, model = map(float, re.search(r'nearest ([\d.]+)% wrong, model ([\d.]+)% wrong', s).groups())
ns = float(re.search(r'pick: (\d+) ns', s).group(1))
ok = model <= 15.0 and model < near * 0.6 and ns < 20000
print(("fat-finger: ok" if ok else "FAIL fat-finger") + f" (wrong words {near}% -> {model}%, {ns:.0f} ns per touch)")
sys.exit(0 if ok else 1)
PY
[ $rc = 0 ] && echo "ALL CHECKS PASSED" || { echo "CHECK FAILED"; exit 1; }
