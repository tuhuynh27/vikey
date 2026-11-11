#!/usr/bin/env python3
"""Builds engine/AutoSwitchData.h: Vietnamese syllables + English words with frequency scores.

Sources (FrequencyWords by Hermit Dave, OpenSubtitles 2018, content CC BY-SA 4.0):
  vi_50k.txt, en_50k.txt  ("word count" per line)

Vietnamese syllables are stored as a Telex-normalized ASCII key so the engine can compare
without Unicode tables or code-table/orthography differences:
  circumflex -> doubled vowel (aa ee oo), breve/horn -> letter + w (aw ow uw), đ -> dd,
  and the tone letter (s f r x j) appended at the end.  trường -> truwowngf, hoà/hòa -> hoaf.
Score = round(10 * log10(occurrences per million)); a 10-point gap = 10x more common.
"""
import math, os, re, unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'Engine', 'AutoSwitchData.h')
TONES = {'́': 's', '̀': 'f', '̉': 'r', '̃': 'x', '̣': 'j'}
VN_LETTERS = set("abcdeghiklmnopqrstuvxy")
SYL = re.compile("^(ngh|ch|gh|gi|kh|ng|nh|ph|qu|th|tr|b|c|dd|d|g|h|k|l|m|n|p|r|s|t|v|x)?"
                 "(a|aw|aa|e|ee|i|o|oo|ow|u|uw|y)+(ch|ng|nh|c|m|n|p|t)?[sfrxj]?$")

def vn_key(word):
    out, tone = [], ''
    for ch in unicodedata.normalize('NFD', word.lower()):
        if ch in TONES:
            if tone: return None
            tone = TONES[ch]
        elif ch == '̂':
            if not out or out[-1] not in 'aeo': return None
            out.append(out[-1])
        elif ch == '̆':
            if not out or out[-1] != 'a': return None
            out.append('w')
        elif ch == '̛':
            if not out or out[-1] not in 'ou': return None
            out.append('w')
        elif ch == 'đ':
            out.append('dd')
        elif ch in VN_LETTERS:
            out.append(ch)
        else:
            return None
    return ''.join(out) + tone

def load(name):
    rows = []
    for line in open(os.path.join(HERE, name), encoding='utf-8'):
        w, c = line.split()
        rows.append((w, int(c)))
    return rows, sum(c for _, c in rows)

def score(count, total):
    return max(-120, min(120, round(10 * math.log10(count * 1e6 / total))))

vi, vi_total = load('vi_50k.txt')
vn = {}
for w, c in vi:
    k = vn_key(w)
    plain = unicodedata.normalize('NFD', w.lower())
    # Vietnamese never ends in the letters s, r, x (those are Telex tone keys): such tokens are foreign words
    if plain and plain[-1] in 'srx': continue
    if k and len(w) <= 7 and c >= 5 and SYL.match(k):
        vn[k] = vn.get(k, 0) + c
# Informal Vietnamese (D27): our crawl of forums and comments (vikey-data/freq, count_informal.py). The
# subtitles are formal; chat has particles (ạ, á), shorthand (đc, đt) and English inside. Rates are mixed
# half and half. Without vikey-data the subtitles alone are used.
INF = os.environ.get('VIKEY_INFORMAL', os.path.expanduser('~/Work/vikey-data/freq'))
INF_SHARE = float(os.environ.get('VIKEY_INFORMAL_SHARE', '0.5'))
SHORTHAND = re.compile('^dd[bcdghklmnpqrstvx]{0,2}$')   # đ, đc, đt, đk: no vowel, still Vietnamese
inf = {}
inf_total = 0
if INF_SHARE > 0 and os.path.exists(os.path.join(INF, 'vi_informal.txt')):
    inf_total = int(open(os.path.join(INF, 'total.txt')).read())
    for line in open(os.path.join(INF, 'vi_informal.txt'), encoding='utf-8'):
        w, c = line.split(); c = int(c)
        k = vn_key(w)
        plain = unicodedata.normalize('NFD', w.lower())
        if not k or (plain and plain[-1] in 'srx'): continue
        if (len(w) <= 7 and c >= 5 and SYL.match(k)) or (SHORTHAND.match(k) and c >= 50):
            inf[k] = inf.get(k, 0) + c
# Bulk web Vietnamese (vikey-data/bulk: FineWeb-2, HPLT, MADLAD, spam-filtered; D28): like the chat crawl it can only
# lower a subtitle word or add a new one (raising them hurt mixed typing)
BULK = os.path.expanduser('~/Work/vikey-data/bulk/merged')
BULK_SHARE = float(os.environ.get('VIKEY_BULK_SHARE', '0.5'))   # D28: tuned on all eval sets
if BULK_SHARE > 0 and os.path.exists(os.path.join(BULK, 'vi_counts.txt')):
    import json
    btot = json.load(open(os.path.join(BULK, 'sources.json')))['totals']['tokens_vi']
    bulk = {}
    for line in open(os.path.join(BULK, 'vi_counts.txt'), encoding='utf-8'):
        w, c = line.split(); c = int(c)
        k = vn_key(w); plain = unicodedata.normalize('NFD', w.lower())
        if k and not (plain and plain[-1] in 'srx') and len(w) <= 7 and c >= 20 and SYL.match(k):
            bulk[k] = bulk.get(k, 0) + c
    # rewrite vn as counts on the subtitle scale: (1-b)*subtitle rate + b*bulk rate
    def brate(k):
        sub, mixed = vn.get(k, 0) / vi_total, (1 - BULK_SHARE) * vn.get(k, 0) / vi_total + BULK_SHARE * bulk.get(k, 0) / btot
        return min(sub, mixed) if sub > 0 and os.environ.get('VIKEY_BULK_LOWER_ONLY', '1') == '1' else mixed
    vn = {k: brate(k) * vi_total for k in set(vn) | set(bulk)}
    vn = {k: c for k, c in vn.items() if c > 0}
if inf_total:
    for w, c in vi:   # shorthand the subtitles have too
        k = vn_key(w)
        if k and SHORTHAND.match(k) and c >= 50 and k in inf: vn[k] = vn.get(k, 0) + c
    keys = set(vn) | set(inf)
    def rate(k):
        sub, mixed = vn.get(k, 0) / vi_total, (1 - INF_SHARE) * vn.get(k, 0) / vi_total + INF_SHARE * inf.get(k, 0) / inf_total
        # chat can lower formal-only words (vía, mêm) and add chat words (ạ, đc), never raise a subtitle word
        # ("lít", "thí" are common in car forums, but "list", "this" are what mixed typing means)
        return min(sub, mixed) if sub > 0 and os.environ.get('VIKEY_INFORMAL_LOWER_ONLY', '1') == '1' else mixed
    vn = {k: max(-120, min(120, round(10 * math.log10(rate(k) * 1e6)))) for k in keys if vn.get(k, 0) + inf.get(k, 0) > 0}
else:
    vn = {k: score(c, vi_total) for k, c in vn.items()}
# Not Vietnamese words, only names and loan spellings (Sê San, sê-ri) that collide with common English (Tony: D40)
for k in ('see',):
    vn.pop(k, None)

en_rows, en_total = load('en_50k.txt')
# Acronyms typed lowercase that are Vietnamese words in Telex: 'os' is ó (Tony: D48). 0 = Vietnamese wins in a
# Vietnamese sentence, English wins in an English one.
EN_OVERRIDE = {'os': 0, 'docs': 20}   # docs: D58
# Keep only words the macOS English spell checker accepts (drops subtitle junk like "ys", "af", "carm").
# en_spell_ok.txt is produced by spellfilter.swift; see README.md.
spell_ok = set(open(os.path.join(HERE, 'en_spell_ok.txt')).read().split())
en = {}
for w, c in en_rows:
    if re.fullmatch('[a-z]{2,}', w) and w in spell_ok:
        en[w] = max(en.get(w, -999), score(c, en_total))

# Subtitle junk the spell checker lets through that are common Vietnamese particles in Telex (aj = ạ): D28
for w in ('aj', 'ej', 'oj', 'ix'):
    en.pop(w, None)

# English inside Vietnamese text (visa, meme, deadline): at least as common as there (D27)
if inf_total and os.environ.get('VIKEY_EN_IN_VI', '1') == '1':
    for line in open(os.path.join(INF, 'en_in_vi.txt'), encoding='utf-8'):
        w, c = line.split(); c = int(c)
        if c >= 5 and w in en and vn_key(w) not in vn:
            en[w] = max(en[w], score(c, inf_total))

# The same from the bulk web Vietnamese (experiment, VIKEY_BULK_EN=1)
if os.environ.get('VIKEY_BULK_EN', '1') == '1' and os.path.exists(os.path.join(BULK, 'en_in_vi_counts.txt')):
    import json
    btot = json.load(open(os.path.join(BULK, 'sources.json')))['totals']['tokens_vi']
    for line in open(os.path.join(BULK, 'en_in_vi_counts.txt'), encoding='utf-8'):
        w, c = line.split(); c = int(c)
        if c >= 20 and w in en and vn_key(w) not in vn:
            en[w] = max(en[w], score(c, btot))

# Chat shorthand, programming words and identifier words mined from code: always English, high score.
for name in ('en_extra.txt', 'en_code.txt'):
    for line in open(os.path.join(HERE, name)):
        if line.startswith('#'): continue
        for w in line.split():
            en[w] = max(en.get(w, -999), 40)
en.update({w: v for w, v in EN_OVERRIDE.items() if w in en})

def emit(name, d):
    keys = sorted(d)
    offs, pos, chunks = [], 0, []
    for k in keys:
        offs.append(pos); pos += len(k) + 1
    lines = []
    for i in range(0, len(keys), 200):
        lines.append('    "' + ''.join(k + '\\0' for k in keys[i:i+200]) + '"')
    s = f'static const char {name}Words[] =\n' + '\n'.join(lines) + ';\n'
    s += f'static const unsigned int {name}Offsets[{len(keys)}] = {{' + ','.join(map(str, offs)) + '};\n'
    s += f'static const signed char {name}Scores[{len(keys)}] = {{' + ','.join(str(d[k]) for k in keys) + '};\n'
    s += f'static const int {name}Count = {len(keys)};\n'
    return s

with open(OUT, 'w') as f:
    f.write('// Generated by Tools/autoswitch/build_dict.py. Do not edit.\n')
    f.write('// Word frequencies: FrequencyWords (Hermit Dave), OpenSubtitles 2018, CC BY-SA 4.0.\n')
    f.write('#ifndef AutoSwitchData_h\n#define AutoSwitchData_h\n\n')
    f.write(emit('kAutoSwitchVn', vn) + '\n' + emit('kAutoSwitchEn', en))
    f.write('\n#endif\n')
print('vn', len(vn), 'en', len(en), 'bytes', os.path.getsize(OUT))
for w in ['treen', 'truowfng', 'moer', 'more', 'last', 'carm', 'dos', 'nos']:
    print(w, 'EN', en.get(w), 'VNkey', vn.get(w))
