# Proofread eval

520 sentences for the keyboard's Proofread (Apple Intelligence, `iOS/Keyboard/AIAssist.swift`), each with its exact
errors: 200 English grammar, 90 English spelling, 80 Vietnamese without accents, 50 Vietnamese with Telex leftovers
("banj"), 50 mixed Vietnamese/English, 50 already correct. Built by `gen_eval.py` (grammar items written and annotated
by Claude and checked so the listed fixes give exactly the correct text; the other errors are injected by the script).

```sh
Tools/proofread/run.sh [label]     # this Mac's Apple Intelligence (macOS 26, Apple Intelligence on); not on CI
```

It runs the keyboard's own code (same prompt, same safety filter) and prints:
- **errors fixed**: listed errors whose correction is in the answer, with the wrong form gone
- **fully right**: the answer is the correct text (a capital first letter and end punctuation don't count)
- **damaged** ↓: the answer changed a word that wasn't wrong (rephrased, translated, a synonym)
- **correct untouched**: already-correct sentences given back unchanged
- **no answer** ↓: the model failed or its answer was rejected ("Couldn't fix")

## Results

| prompt | errors fixed | fully right | damaged | correct untouched | no answer |
|---|---:|---:|---:|---:|---:|
| 2026-10-30 baseline | 67.7% | 57.5% | 4.8% | 98.0% | 4.0% |
| + grammar examples (tense with time words, there is/are, her and me) | 68.3% | 60.0% | 8.5% | 88.0% | 3.5% |
| **+ keep right words, English words in Vietnamese (shipped)** | **68.3%** | **60.0%** | **5.2%** | **96.0%** | **3.3%** |
| same, two passes (not shipped: 2× slower for +1 pt) | 69.3% | 60.6% | 5.6% | 96.0% | 3.3% |

English grammar, fully right: 49.5% → 55.5%. The on-device model (~3B) is good at spelling (91%) and Telex
leftovers, weaker at grammar and at Vietnamese typed without any accents.
