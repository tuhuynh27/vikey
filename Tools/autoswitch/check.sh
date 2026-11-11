#!/bin/zsh
# Locks down engine behavior: rule-tagged cases, golden snapshots, Telex lock, metrics.
# usage: Tools/autoswitch/check.sh          (check)
#        Tools/autoswitch/check.sh update   (accept current behavior as the new golden)
set -e
cd "$(dirname "$0")"
E=../../Engine
SRC=($E/Engine.cpp $E/Vietnamese.cpp $E/SmartSwitchKey.cpp)
clang++ -std=c++17 -O2 -w ${=EXTRA} -I$E -I$E/platforms -o test/harness test/harness.cpp $SRC
clang++ -std=c++17 -O2 -w ${=EXTRA} -DAS_TESTING -I$E -I$E/platforms -o sim/sim sim/sim.cpp $SRC
rc=0
env -u VIKEY_TELEX_STYLE python3 rules/run_rules.py || rc=1   # the default style (Telex), whatever the shell has
VIKEY_TELEX_STYLE=1 python3 rules/run_rules.py telex_simple1.tsv || rc=1   # D56 Simple Telex styles
VIKEY_TELEX_STYLE=2 python3 rules/run_rules.py telex_simple2.tsv || rc=1
python3 rules/run_reported.py || rc=1   # every engine bug Tony reported, as typed (rules/reported.tsv)
python3 rules/decisions.py || rc=1   # every decision in DECISIONS.md has a test, or says why not (rules/decisions.tsv)
python3 rules/golden.py ${1:-check} || rc=1

# Engine/Driver (what the iOS keyboard runs): same output as the simulator, and fast enough
clang++ -std=c++17 -O2 -w ${=EXTRA} -I$E -I$E/platforms -o driver/drvlazy driver/drvlazy.cpp $E/Driver.cpp $SRC
clang++ -std=c++17 -O2 -w ${=EXTRA} -I$E -I$E/platforms -o driver/bench driver/bench.cpp $E/Driver.cpp $SRC
U=${TMPDIR:-/tmp}/vikey_check_chat.units
python3 sim/prep.py sim/corpus/chat_messages.txt > $U
clang++ -std=c++17 -O2 -w ${=EXTRA} -I$E -I$E/platforms -o driver/undo_test driver/undo_test.cpp $E/Driver.cpp $SRC && driver/undo_test || rc=1
(cd driver && ./cmp.sh $U | grep -q "whole text equal: True") && echo "driver == simulator: ok" || { echo "FAIL driver output differs from the simulator"; rc=1; }
# budget: ~6x the M-series Mac mini numbers (avg ~155 ns, p99.9 ~1.8 us), so slower CI machines still pass
# shared CI machines get preempted mid-run (one run hit p99.9 44 us, max 23 ms): best of 3
{ driver/bench $U 1000 20000 || driver/bench $U 1000 20000 || driver/bench $U 1000 20000; } && echo "performance: ok" || { echo "FAIL performance budget (Engine/Driver per key)"; rc=1; }
# realistic typing (typos fixed with Backspace, Backspace into the last word, cursor ← → trips, clicks, edits
# elsewhere, ⌥Z round trips; sim/rtsim.cpp): the engine must never change text from before a cursor move, click or
# switch, and slips must stay as cheap to fix as rules/golden/rtsim.json says
clang++ -std=c++17 -O2 -w ${=EXTRA} -DAS_TESTING -I$E -I$E/platforms -o sim/rtsim sim/rtsim.cpp $SRC
U2=${TMPDIR:-/tmp}/vikey_check_synthetic.units
python3 sim/prep.py sim/corpus/synthetic_val.txt > $U2
rt=$({ for s in 1 2 3; do sim/rtsim $U --seed $s --show 3 2>&1 || true; done; sim/rtsim $U2 --seed 1 --show 3 2>&1 || true; })
python3 - "$rt" "${1:-check}" <<'PY' || rc=1
import re, sys, json, os
out, mode = sys.argv[1], sys.argv[2]
bad = sum(int(x) for x in re.findall(r'(?:guard|elsewhere) (\d+)', out))   # the engine changed text it shouldn't
fixed = {'stuck': [sum(int(x) for x in re.findall(r'stuck (\d+)', out)), 0]}   # words even retyping didn't fix (auto switch disagrees)
for k, a, b in re.findall(r'  ([a-z-]+) (\d+)/(\d+)', out):
    f = fixed.setdefault(k, [0, 0]); f[0] += int(a); f[1] += int(b)
path = os.path.join('rules', 'golden', 'rtsim.json')
gold = json.load(open(path)) if os.path.exists(path) else {}
if mode == 'update':
    json.dump({k: v[0] for k, v in fixed.items()}, open(path, 'w'), indent=1); print("realistic typing: saved " + path)
worse = [k + " " + str(v[0]) + " (was " + str(gold[k]) + ")" for k, v in fixed.items() if k in gold and v[0] > gold[k] * 1.05 + 3]
for line in out.splitlines():
    if line.startswith(("GUARD", "STUCK", "ELSEWHERE", "   actions")): print("  " + line)
summary = ", ".join(k + " " + str(v[0]) + ("/" + str(v[1]) if v[1] else "") for k, v in fixed.items())
ok = bad == 0 and (mode == "update" or not worse)
print(("realistic typing: ok" if ok else "FAIL realistic typing") + " (chat x3 seeds + synthetic: " + str(bad) + " broken; needed fixing by hand: " + summary + ")" + ("; worse: " + ", ".join(worse) if worse else ""))
sys.exit(0 if ok else 1)
PY
# fat-finger model (iOS keyboard): simulated sloppy taps must stay this much better than nearest-key
clang++ -std=c++17 -O2 -w ${=EXTRA} -I$E -I$E/platforms -o touch/touchsim touch/touchsim.cpp $E/TouchModel.cpp $E/Driver.cpp $SRC
out=$(touch/touchsim $U 0.30)
echo "$out" | sed 's/^/  /'
python3 - "$out" <<'PY' || rc=1
import re, sys
s = sys.argv[1]
near, model = map(float, re.search(r'nearest ([\d.]+)% wrong, model ([\d.]+)% wrong', s).groups())
ns = float(re.search(r'pick: (\d+) ns', s).group(1))
ok = model <= 15.0 and model < near * 0.6 and ns < 20000
print(("fat-finger: ok" if ok else "FAIL fat-finger") + f" (wrong words {near}% -> {model}%, {ns:.0f} ns per touch)")
sys.exit(0 if ok else 1)
PY
# auto-correct: on clean typing it must almost never change a right word; on sloppy typing it must help
c0=$(TOUCH_CORRECT=1 touch/touchsim $U 0.0 | grep auto-correct)
c3=$(TOUCH_CORRECT=1 touch/touchsim $U 0.30 | grep auto-correct)
echo "  clean: $c0"; echo "  sloppy: $c3"
python3 - "$c0" "$c3" "$out" <<'PY' || rc=1
import re, sys
clean = float(re.search(r'([\d.]+)% wrong', sys.argv[1]).group(1))
sloppy = float(re.search(r'([\d.]+)% wrong', sys.argv[2]).group(1))
model = float(re.search(r'model ([\d.]+)% wrong', sys.argv[3]).group(1))
ok = clean <= 0.10 and sloppy < model * 0.75
print(("auto-correct: ok" if ok else "FAIL auto-correct") + f" (clean typing {clean}% wrong, sloppy {model}% -> {sloppy}%)")
sys.exit(0 if ok else 1)
PY
# auto-correct on real data (vikey-data, private; skipped when it isn't there): real English typos must get
# fixed, real Vietnamese social text (shorthand, teencode) must survive. ViLexNorm is held out of the keep-list.
P=~/Work/vikey-data/public/evals
if [ -f $P/vilexnorm_pairs.tsv ]; then
  for t in typoeval normeval; do clang++ -std=c++17 -O2 -w ${=EXTRA} -I$E -I$E/platforms -o touch/$t touch/$t.cpp $E/TouchModel.cpp $SRC; done
  ty=$(touch/typoeval $P/typos_github.tsv 0); nv=$(touch/normeval $P/vilexnorm_pairs.tsv)
  echo "  github typos: $ty"; echo "  $nv"
  python3 - "$ty" "$nv" <<'PY' || rc=1
import re, sys
fix, wrong = map(float, re.search(r'by count: fixed ([\d.]+)%, wrong ([\d.]+)%', sys.argv[1]).groups())
broke = float(re.search(r'broke \d+ \(([\d.]+)%', sys.argv[2]).group(1))
ok = fix >= 30 and wrong <= 5 and broke <= 0.10
print(("real-data auto-correct: ok" if ok else "FAIL real-data auto-correct") + f" (English typos fixed {fix}%, wrong {wrong}%; Vietnamese social words broken {broke}%)")
sys.exit(0 if ok else 1)
PY
else echo "real-data auto-correct: skipped (no ~/Work/vikey-data)"; fi
exit $rc
