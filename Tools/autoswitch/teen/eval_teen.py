#!/usr/bin/env python3
"""Teencode check: the 50 "teen" chat lines of VietTelex's handwritten corpus (MIT, © 2026 Phil Trinh,
github.com/ptrinh/viettelex TelexCore/Corpus/mixed-typing-handwritten.txt), typed in Telex as a person would,
in Vietnamese mode, through test/harness. Words marked /e are English. Prints lines and words that come out
exactly as written.  usage: eval_teen.py [--harness PATH] [-v]"""
import os, re, subprocess, sys, unicodedata
HERE = os.path.dirname(os.path.abspath(__file__))
H = sys.argv[sys.argv.index('--harness') + 1] if '--harness' in sys.argv else os.path.join(HERE, '..', 'test', 'harness')
FORMS = {"a": "aàáảãạ", "aw": "ăằắẳẵặ", "aa": "âầấẩẫậ", "e": "eèéẻẽẹ", "ee": "êềếểễệ", "i": "iìíỉĩị", "o": "oòóỏõọ",
         "oo": "ôồốổỗộ", "ow": "ơờớởỡợ", "u": "uùúủũụ", "uw": "ưừứửữự", "y": "yỳýỷỹỵ"}
TONES = ["", "f", "s", "r", "x", "j"]
def telex(w):
    out, tone = "", ""
    for c in w:
        lo = c.lower(); up = c != lo
        if lo == "đ": out += "DD" if up else "dd"; continue
        for k, f in FORMS.items():
            if lo in f:
                t = f.index(lo); tone = TONES[t] or tone
                out += (k[0].upper() + k[1:]) if up else k; break
        else: out += c
    return out + tone
lines = [l.strip() for l in open(os.path.join(HERE, 'teen_viettelex.txt'), encoding='utf-8') if l.strip()]
ok_lines = ok_words = n_words = 0
for line in lines:
    want = line.replace('/e', '')
    keys = ''.join(w[:-2] if w.endswith('/e') else telex(w) for w in re.split(r'(\s+|[,.?!])', line) if w) + ' '
    got = subprocess.run([H, '1', keys], capture_output=True, text=True).stdout.split('\nswitches=')[0].rstrip(' ')
    ws, gs = want.split(), got.split()
    good = sum(a == b for a, b in zip(ws, gs)) if len(ws) == len(gs) else 0
    ok_words += good; n_words += len(ws); ok_lines += got == want
    if '-v' in sys.argv and got != want: print(f'  {keys!r}\n     got  {got}\n     want {want}')
print(f'teencode (VietTelex teen lines): lines right {ok_lines}/{len(lines)}, words right {ok_words}/{n_words} ({100*ok_words/n_words:.1f}%)')
