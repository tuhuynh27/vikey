// Emoji picker: data + search (port of App/Model/EmojiSearch.swift) and the popup (port of App/Views/EmojiPicker.swift).
#define _HAS_STD_BYTE 0
// Drawn with Direct2D + DirectWrite because GDI/GDI+ cannot draw colour emoji (Segoe UI Emoji's colour glyphs).
#include "Emoji.h"
#include "Core.h"
#include "Prefs.h"
#include "KeyMap.h"
#include "resource.h"
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <functional>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

namespace vikey { namespace emoji {

// ---- data ------------------------------------------------------------------------------------
struct Term {   // weight: name 3, keyword 2
    std::wstring folded; std::vector<std::wstring> words; std::set<std::wstring> exactWords; int weight;
};
struct Emoji {
    std::wstring ch; int group; double version; std::wstring nameEN, nameVI; std::vector<std::wstring> skins; int popularity;
    std::vector<Term> terms;
    const std::wstring& withSkin(int tone) const { return tone > 0 && skins.size() == 5 ? skins[tone - 1] : ch; }
};

static std::wstring lower(std::wstring s) { if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size()); return s; }
static std::vector<std::wstring> splitWords(const std::wstring& s, wchar_t sep = L' ') {
    std::vector<std::wstring> out; std::wstring cur;
    for (wchar_t c : s) { if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur); return out;
}
static std::wstring trim(const std::wstring& s) { size_t a = s.find_first_not_of(L' '), b = s.find_last_not_of(L' '); return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1); }

/// Lowercase, no accents, đ -> d (EmojiIndex.fold).
static std::wstring fold(const std::wstring& s) {
    std::wstring l = lower(s); if (l.empty()) return l;
    int n = NormalizeString(NormalizationD, l.c_str(), (int)l.size(), nullptr, 0);
    std::wstring d(n > 0 ? n : 0, 0);
    if (n > 0) { n = NormalizeString(NormalizationD, l.c_str(), (int)l.size(), &d[0], n); if (n > 0) d.resize(n); else d = l; } else d = l;
    std::wstring out; out.reserve(d.size());
    for (wchar_t c : d) { if (c >= 0x0300 && c <= 0x036F) continue; out += c == 0x0111 ? L'd' : c; }
    return out;
}
static Term makeTerm(const std::wstring& text, int weight) {
    Term t; t.folded = fold(text); t.words = splitWords(t.folded); for (auto& w : splitWords(lower(text))) t.exactWords.insert(w); t.weight = weight; return t;
}

// A small JSON reader for Emoji.json: arrays of [string, number, number, string, string, string, string, [strings], number].
struct JV { enum T { Str, Num, Arr, Other } t = Other; std::wstring s; double n = 0; std::vector<JV> a; };
struct JParser {
    const char* p; const char* e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    static void utf16(std::wstring& o, unsigned cp) { if (cp < 0x10000) o += (wchar_t)cp; else { cp -= 0x10000; o += (wchar_t)(0xD800 + (cp >> 10)); o += (wchar_t)(0xDC00 + (cp & 0x3FF)); } }
    std::wstring str() {
        std::string raw; p++;   // opening quote
        while (p < e && *p != '"') {
            if (*p == '\\') { p++; char c = *p++;
                if (c == 'u') { unsigned cp = (unsigned)strtoul(std::string(p, p + 4).c_str(), nullptr, 16); p += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= e && p[0] == '\\' && p[1] == 'u') { unsigned lo = (unsigned)strtoul(std::string(p + 2, p + 6).c_str(), nullptr, 16); p += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                    std::wstring w; utf16(w, cp); int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string u(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], n, nullptr, nullptr); raw += u; }
                else if (c == 'n') raw += '\n'; else if (c == 't') raw += '\t'; else raw += c; }
            else raw += *p++;
        }
        p++;
        int n = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &w[0], n); return w;
    }
    JV value() {
        ws(); JV v; if (p >= e) return v;
        if (*p == '"') { v.t = JV::Str; v.s = str(); }
        else if (*p == '[') { v.t = JV::Arr; p++; ws(); while (p < e && *p != ']') { v.a.push_back(value()); ws(); if (*p == ',') p++; ws(); } p++; }
        else if ((*p >= '0' && *p <= '9') || *p == '-') { v.t = JV::Num; char* end; v.n = strtod(p, &end); p = end; }
        else { while (p < e && *p != ',' && *p != ']' && *p != '}') p++; }   // true/false/null: skipped
        return v;
    }
};

static std::vector<Emoji> _all;              // every emoji this Windows can draw, in Unicode order
static std::vector<std::pair<int, std::vector<int>>> _groups;   // group -> indices into _all
static std::mutex _loadMutex; static bool _loaded = false;

/// Segoe UI Emoji decides what is shown (newer emoji show as boxes, flags as letters): an emoji is drawable when
/// its first code point has a glyph and the whole sequence shapes into one glyph cluster.
struct GlyphCheck {
    IDWriteFactory* dw = nullptr; IDWriteTextFormat* fmt = nullptr; IDWriteFontFace* face = nullptr;
    GlyphCheck() {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw);
        if (dw) dw->CreateTextFormat(L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 28, L"", &fmt);
        IDWriteFontCollection* col = nullptr; if (dw) dw->GetSystemFontCollection(&col);
        UINT32 idx = 0; BOOL exists = FALSE; if (col) col->FindFamilyName(L"Segoe UI Emoji", &idx, &exists);
        IDWriteFontFamily* fam = nullptr; if (exists) col->GetFontFamily(idx, &fam);
        IDWriteFont* font = nullptr; if (fam) fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
        if (font) font->CreateFontFace(&face);
        if (font) font->Release(); if (fam) fam->Release(); if (col) col->Release();
    }
    ~GlyphCheck() { if (face) face->Release(); if (fmt) fmt->Release(); if (dw) dw->Release(); }
    bool drawable(const std::wstring& s) {
        if (!face || !fmt || s.empty()) return true;
        unsigned cp = s[0]; if (cp >= 0xD800 && cp <= 0xDBFF && s.size() > 1) cp = 0x10000 + ((cp - 0xD800) << 10) + (s[1] - 0xDC00);
        UINT16 gi = 0; if (FAILED(face->GetGlyphIndices(&cp, 1, &gi)) || gi == 0) return false;
        IDWriteTextLayout* lay = nullptr; if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, 1000, 100, &lay)) || !lay) return true;
        UINT32 n = 0; lay->GetClusterMetrics(nullptr, 0, &n); lay->Release();
        return n <= 1;
    }
};

static void loadIndex() {
    std::lock_guard<std::mutex> g(_loadMutex);
    if (_loaded) return;
    HRSRC r = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_EMOJI_JSON), RT_RCDATA);
    HGLOBAL h = r ? LoadResource(GetModuleHandleW(nullptr), r) : nullptr;
    const char* data = h ? (const char*)LockResource(h) : nullptr; DWORD size = r ? SizeofResource(GetModuleHandleW(nullptr), r) : 0;
    if (data && size) {
        JParser jp{data, data + size}; JV root = jp.value();
        GlyphCheck gc;
        for (auto& row : root.a) {
            if (row.t != JV::Arr || row.a.size() < 8 || row.a[0].t != JV::Str) continue;
            Emoji e; e.ch = row.a[0].s; if (e.ch.empty()) continue;
            if (e.ch.size() >= 2 && e.ch[0] == 0xD83C && e.ch[1] >= 0xDDE6 && e.ch[1] <= 0xDDFF) continue;   // country flags: Segoe UI Emoji draws them as two letters
            if (e.ch.find(L"\xDB40") != std::wstring::npos) continue;   // tag sequences (England, Scotland, Wales): not in Segoe UI Emoji
            if (!gc.drawable(e.ch)) continue;
            e.group = (int)row.a[1].n; e.version = row.a[2].n; e.nameEN = row.a[3].s; e.nameVI = row.a[4].s;
            for (auto& s : {e.nameEN, e.nameVI}) if (!s.empty()) e.terms.push_back(makeTerm(s, 3));
            std::wstring kw = row.a[5].s + L" | " + row.a[6].s; size_t p = 0;
            while (p <= kw.size()) { size_t q = kw.find(L" | ", p); std::wstring k = kw.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p); if (!k.empty()) e.terms.push_back(makeTerm(k, 2)); if (q == std::wstring::npos) break; p = q + 3; }
            // emoticons (":D", "=))", "^_^") rank like names: typing one exactly puts its emoji first (Tony)
            if (row.a.size() > 9 && row.a[9].t == JV::Str) for (auto& t : splitWords(row.a[9].s)) e.terms.push_back(makeTerm(t, 3));
            if (row.a[7].t == JV::Arr) for (auto& s : row.a[7].a) e.skins.push_back(s.s);
            e.popularity = row.a.size() > 8 ? (int)row.a[8].n : 0;
            _all.push_back(std::move(e));
        }
    }
    std::map<int, std::vector<int>> gm; for (int i = 0; i < (int)_all.size(); i++) gm[_all[i].group].push_back(i);
    for (auto& kv : gm) _groups.push_back(kv);
    _loaded = true;
}

// ---- Telex reader (EmojiSearch.swift Telex) ---------------------------------------------------
static const std::wstring VOWELS = L"aăâeêioôơuưy";
static bool isVowel(wchar_t c) { return VOWELS.find(c) != std::wstring::npos; }
static wchar_t marked(wchar_t base, int tone) {
    static const std::map<wchar_t, std::wstring> marks = {
        {L'a', L"áàảãạ"}, {L'ă', L"ắằẳẵặ"}, {L'â', L"ấầẩẫậ"}, {L'e', L"éèẻẽẹ"}, {L'ê', L"ếềểễệ"}, {L'i', L"íìỉĩị"},
        {L'o', L"óòỏõọ"}, {L'ô', L"ốồổỗộ"}, {L'ơ', L"ớờởỡợ"}, {L'u', L"úùủũụ"}, {L'ư', L"ứừửữự"}, {L'y', L"ýỳỷỹỵ"}};
    auto i = marks.find(base); return i == marks.end() ? base : i->second[tone - 1];
}
/// Where the tone mark goes (modern style: hoà, thuý).
static int toneIndex(const std::wstring& w) {
    size_t start = 0;
    if (w.size() > 2 && w[0] == L'q' && w[1] == L'u') start = 2;
    else if (w.size() > 2 && w[0] == L'g' && w[1] == L'i' && isVowel(w[2])) start = 2;
    std::vector<int> idx; for (size_t i = start; i < w.size(); i++) if (isVowel(w[i])) idx.push_back((int)i);
    if (idx.empty()) return -1;
    int first = idx[0], lastV = idx.back();
    std::vector<int> nucleus; for (int j = first; std::find(idx.begin(), idx.end(), j) != idx.end(); j++) nucleus.push_back(j);
    for (auto it = nucleus.rbegin(); it != nucleus.rend(); ++it) if (std::wstring(L"ăâêôơư").find(w[*it]) != std::wstring::npos) return *it;
    if (nucleus.size() == 1) return first;
    bool endsInConsonant = lastV < (int)w.size() - 1;
    if (endsInConsonant || nucleus.size() == 3) return nucleus[1];
    std::wstring pair = {w[nucleus[0]], w[nucleus[1]]};
    return (pair == L"oa" || pair == L"oe" || pair == L"uy") ? nucleus[1] : nucleus[0];
}
static std::wstring telexWord(const std::wstring& keys) {
    std::wstring out; int tone = 0; wchar_t toneKey = 0;
    static const std::map<wchar_t, int> tones = {{L's', 1}, {L'f', 2}, {L'r', 3}, {L'x', 4}, {L'j', 5}};
    for (wchar_t k : lower(keys)) {
        wchar_t last = out.empty() ? 0 : out.back();
        if (k == L'd' && last == L'd') out.back() = L'đ';
        else if (k == L'a' && last == L'a') out.back() = L'â';
        else if (k == L'e' && last == L'e') out.back() = L'ê';
        else if (k == L'o' && last == L'o') out.back() = L'ô';
        else if (k == L'w') {   // ươ (uow / uwow), ă, ơ, ư; a lone w is ư
            int i = -1; for (int j = (int)out.size() - 1; j >= 0; j--) if (out[j] == L'a' || out[j] == L'o' || out[j] == L'u') { i = j; break; }
            bool restNoVowel = true; if (i >= 0) for (size_t j = i + 1; j < out.size(); j++) if (isVowel(out[j])) restNoVowel = false;
            if (i >= 0 && (restNoVowel || i == (int)out.size() - 1)) {
                wchar_t c = out[i]; out[i] = c == L'a' ? L'ă' : c == L'o' ? L'ơ' : L'ư';
                if (c == L'o' && i > 0 && out[i - 1] == L'u') out[i - 1] = L'ư';
                if (c == L'u' && i + 1 < (int)out.size() && out[i + 1] == L'o') out[i + 1] = L'ơ';
            } else out += L'ư';
        }
        else if (k == L'z') { tone = 0; toneKey = 0; }
        else {
            auto t = tones.find(k); bool hasVowel = false; for (wchar_t c : out) if (isVowel(c)) hasVowel = true;
            if (t != tones.end() && hasVowel) { if (toneKey == k) { tone = 0; toneKey = 0; out += k; } else { tone = t->second; toneKey = k; } }
            else out += k;
        }
    }
    if (tone == 0) return out;
    int i = toneIndex(out); if (i < 0) return out;
    out[i] = marked(out[i], tone); return out;
}
std::wstring telexConvert(const std::wstring& s) {
    std::wstring out, cur; for (wchar_t c : s) { if (c == L' ') { out += telexWord(cur) + L' '; cur.clear(); } else cur += c; }
    return out + telexWord(cur);
}

// ---- search (EmojiIndex.search / score) ---------------------------------------------------------
struct QWord { std::wstring exact, folded; };
/// Every query word must start some word of one term. A hit in the emoji's name beats one in its keywords,
/// a whole word beats a prefix, and accents count when given.
static int score(const std::vector<QWord>& words, const std::vector<Term>& terms) {
    int total = 0; std::wstring whole; for (size_t i = 0; i < words.size(); i++) { if (i) whole += L' '; whole += words[i].folded; }
    for (auto& w : words) {
        int best = 0; bool accented = w.exact != w.folded;
        for (auto& t : terms) {
            bool name = t.weight == 3; int s = 0;
            if (t.folded == whole) s = name ? 100 : 65;
            else for (auto& tw : t.words) if (tw.compare(0, w.folded.size(), w.folded) == 0) {
                s = std::max(s, tw.size() == w.folded.size() ? (name ? 70 : 55) : (name ? 45 : 30));
                if (tw.size() == w.folded.size()) break;
            }
            if (s <= 0) continue;
            if (t.exactWords.count(w.exact)) s += 10; else if (accented) s -= 40;
            if (name) s += std::max(0, 6 - (int)t.words.size());
            best = std::max(best, s);
        }
        if (best <= 0) return 0;
        total += best;
    }
    return total / (int)words.size();
}
static std::vector<int> search(const std::wstring& raw, int limit) {
    std::wstring q = trim(lower(raw)); if (q.empty()) return {};
    std::vector<std::wstring> variants = {q}; std::wstring t = telexConvert(q); if (t != q) variants.push_back(t);
    std::vector<std::vector<QWord>> queries;
    for (auto& v : variants) { std::vector<QWord> ws; for (auto& w : splitWords(v)) ws.push_back({w, fold(w)}); queries.push_back(ws); }
    struct S { int score, i; }; std::vector<S> scored;
    for (int i = 0; i < (int)_all.size(); i++) {
        int best = 0; for (auto& ws : queries) best = std::max(best, score(ws, _all[i].terms));
        if (best > 0) scored.push_back({best * 2 + _all[i].popularity, i});
    }
    std::sort(scored.begin(), scored.end(), [](const S& a, const S& b) { return a.score != b.score ? a.score > b.score : a.i < b.i; });
    std::vector<int> out; for (auto& s : scored) { if ((int)out.size() >= limit) break; out.push_back(s.i); } return out;
}

// ---- usage (EmojiUsage): each pick adds 1, older scores fade; stored as "emoji\tscore\n" lines ------------
static const wchar_t* STARTER[] = {L"😂", L"❤️", L"🥰", L"😍", L"😭", L"👍", L"🙏", L"😊", L"🔥", L"😘", L"🤣", L"🥺", L"😅", L"🎉", L"💕", L"😁", L"🙂", L"😉",
                                   L"🤔", L"👏", L"😎", L"💪", L"✨", L"😢", L"🙌", L"💯", L"😆", L"🤗", L"😴", L"👌"};
static std::map<std::wstring, double> usageRead() {
    std::map<std::wstring, double> m; std::wstring s = prefs::getString(L"EmojiUsage", L"");
    for (auto& line : splitWords(s, L'\n')) { size_t t = line.find(L'\t'); if (t != std::wstring::npos) m[line.substr(0, t)] = _wtof(line.substr(t + 1).c_str()); }
    return m;
}
static void usageRecord(const std::wstring& ch) {
    auto m = usageRead(); for (auto& kv : m) kv.second *= 0.97; m[ch] += 1;
    if (m.size() > 80) { std::vector<std::pair<double, std::wstring>> v; for (auto& kv : m) v.push_back({kv.second, kv.first}); std::sort(v.begin(), v.end()); for (size_t i = 0; i < m.size() - 80; i++) m.erase(v[i].second); }
    std::wstring s; for (auto& kv : m) { wchar_t b[32]; swprintf_s(b, L"%.4f", kv.second); s += kv.first + L"\t" + b + L"\n"; }
    prefs::setString(L"EmojiUsage", s);
}
static std::vector<std::wstring> usageFrequent(int n) {
    auto m = usageRead(); std::vector<std::pair<std::wstring, double>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    std::vector<std::wstring> list; for (auto& kv : v) list.push_back(kv.first);
    for (auto s : STARTER) if ((int)list.size() < n && std::find(list.begin(), list.end(), s) == list.end()) list.push_back(s);
    if ((int)list.size() > n) list.resize(n); return list;
}

// ---- model (EmojiPickerModel) --------------------------------------------------------------------
static const int COLS = 9; static const float CELL = 44, WIDTH = COLS * CELL + 24, HEIGHT = 404;
static const float SEARCH_H = 14 + 24 + 8, FOOTER_H = 44, GRID_PAD = 8, GRID_X = 12;
static const wchar_t* GROUP_VI[] = {L"Mặt cười & cảm xúc", L"Người & cơ thể", L"", L"Động vật & thiên nhiên", L"Đồ ăn & thức uống", L"Du lịch & địa điểm", L"Hoạt động", L"Đồ vật", L"Biểu tượng", L"Cờ"};
static const wchar_t* GROUP_EN[] = {L"Smileys & Emotion", L"People & Body", L"", L"Animals & Nature", L"Food & Drink", L"Travel & Places", L"Activities", L"Objects", L"Symbols", L"Flags"};

struct Row { bool header; std::wstring title; int start, count; float y, h; };
static std::wstring _raw; static bool _allSelected = false; static bool _vietnamese = true;
static std::vector<int> _results, _visible, _recents;   // indices into _all
static std::vector<Row> _rows; static float _contentH = 0, _scrollY = 0;
static int _selected = 0; static int _tone = 0; static POINT _lastMouse = {-1, -1};
static bool _searching() { return !trim(_raw).empty(); }
static std::wstring _display() { return _vietnamese ? telexConvert(_raw) : _raw; }
static const Emoji* _current() { return _selected >= 0 && _selected < (int)_visible.size() ? &_all[_visible[_selected]] : nullptr; }

static void relayout() {
    _visible.clear(); _rows.clear(); float y = 0;
    auto add = [&](const std::wstring& title, const std::vector<int>& list) {
        if (list.empty()) return;
        if (!title.empty()) { bool first = _rows.empty(); float h = first ? 20.0f : 28.0f; _rows.push_back({true, title, 0, 0, y, h}); y += h; }
        int start = (int)_visible.size(); _visible.insert(_visible.end(), list.begin(), list.end());
        for (int i = 0; i < (int)list.size(); i += COLS) { _rows.push_back({false, L"", start + i, std::min(COLS, (int)list.size() - i), y, CELL}); y += CELL; }
    };
    if (_searching()) add(L"", _results);
    else {
        add(_vietnamese ? L"Dùng thường xuyên" : L"Frequently Used", _recents);
        for (auto& g : _groups) if (g.first >= 0 && g.first < 10) add(_vietnamese ? GROUP_VI[g.first] : GROUP_EN[g.first], g.second);
    }
    _contentH = y;
}
static int rowOf(int i) { for (int r = 0; r < (int)_rows.size(); r++) if (!_rows[r].header && i >= _rows[r].start && i < _rows[r].start + _rows[r].count) return r; return -1; }
static float viewportH() { return HEIGHT - SEARCH_H - 1 - FOOTER_H - 1 - 2 * GRID_PAD; }
static void clampScroll() { _scrollY = std::max(0.0f, std::min(_scrollY, std::max(0.0f, _contentH - viewportH()))); }
static void scrollToSelected() {   // the least scrolling that shows the row; back on top shows the first title too
    int r = rowOf(_selected); if (r < 0) return;
    int firstEmojiRow = -1; for (int i = 0; i < (int)_rows.size(); i++) if (!_rows[i].header) { firstEmojiRow = i; break; }
    if (r == firstEmojiRow) { _scrollY = 0; return; }
    float top = _rows[r].y, bottom = top + _rows[r].h, vh = viewportH();
    if (top < _scrollY) _scrollY = top; else if (bottom > _scrollY + vh) _scrollY = bottom - vh;
    clampScroll();
}
static void doSearch() { _results = _searching() ? search(_raw, 400) : std::vector<int>(); _selected = 0; _scrollY = 0; relayout(); }
static void setRaw(const std::wstring& s) { _raw = s; _allSelected = false; doSearch(); }
static void typeText(const std::wstring& s) { setRaw(_allSelected ? s : _raw + s); }
static void deleteBack() { if (!_raw.empty()) { _raw.pop_back(); if (!_raw.empty() && _raw.back() >= 0xD800 && _raw.back() <= 0xDBFF) _raw.pop_back(); _allSelected = false; doSearch(); } }
static void deleteWord() { std::wstring r = _raw; while (!r.empty() && r.back() == L' ') r.pop_back(); while (!r.empty() && r.back() != L' ') r.pop_back(); setRaw(r); }
static void move(int d) { int n = (int)_visible.size(); if (!n) return; _selected = std::min(std::max(0, _selected + d), n - 1); scrollToSelected(); }
static void moveRow(int d) {   // the row above/below, same column (or its last emoji)
    int r = rowOf(_selected); if (r < 0) return;
    int t = r + d; while (t >= 0 && t < (int)_rows.size() && _rows[t].header) t += d;
    if (t < 0 || t >= (int)_rows.size()) return;
    _selected = _rows[t].start + std::min(_selected - _rows[r].start, _rows[t].count - 1); scrollToSelected();
}
static void resetModel(bool vietnamese) {
    _vietnamese = vietnamese; _raw.clear(); _allSelected = false; _results.clear(); _selected = 0; _scrollY = 0;
    GetCursorPos(&_lastMouse);
    _tone = prefs::getInt(L"EmojiSkinTone", 0);
    std::map<std::wstring, int> byChar; for (int i = 0; i < (int)_all.size(); i++) byChar.emplace(_all[i].ch, i);
    _recents.clear(); for (auto& c : usageFrequent(COLS * 3)) { auto it = byChar.find(c); if (it != byChar.end()) _recents.push_back(it->second); }
    relayout();
}

// ---- the popup --------------------------------------------------------------------------------------
static HINSTANCE _inst; static HWND _win; static bool _open = false; static bool _caret = true;
static ID2D1Factory* _d2d; static IDWriteFactory* _dw; static ID2D1HwndRenderTarget* _rt;
static IDWriteTextFormat *_fSearch, *_fHeader, *_fFooter, *_fEmoji, *_fEmojiFooter, *_fTone, *_fWordmark, *_fBadge, *_fEmpty;
static ID2D1SolidColorBrush *_bText, *_bSecondary, *_bTertiary, *_bBrand, *_bBrandSoft, *_bSep, *_bBadge, *_bSelect;
static float _dpi = 96;
static const D2D1_COLOR_F BRAND = D2D1::ColorF(0x4F46E5);
static const UINT TIMER_CARET = 1;

static IDWriteTextFormat* fmt(const wchar_t* face, float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL, bool center = false) {
    IDWriteTextFormat* f = nullptr; _dw->CreateTextFormat(face, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
    if (f) { f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); if (center) { f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); } f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
    return f;
}
static void ensureGraphics() {
    if (!_d2d) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &_d2d);
    if (!_dw) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&_dw);
    if (!_fSearch && _dw) {
        _fSearch = fmt(L"Segoe UI", 17); _fHeader = fmt(L"Segoe UI", 11, DWRITE_FONT_WEIGHT_SEMI_BOLD); _fFooter = fmt(L"Segoe UI", 13);
        if (_fFooter) { IDWriteInlineObject* sign = nullptr; _dw->CreateEllipsisTrimmingSign(_fFooter, &sign); DWRITE_TRIMMING tr = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; _fFooter->SetTrimming(&tr, sign); if (sign) sign->Release(); }
        _fEmoji = fmt(L"Segoe UI Emoji", 28, DWRITE_FONT_WEIGHT_NORMAL, true); _fEmojiFooter = fmt(L"Segoe UI Emoji", 22); _fTone = fmt(L"Segoe UI Emoji", 18, DWRITE_FONT_WEIGHT_NORMAL, true);
        _fWordmark = fmt(L"Segoe UI", 13, DWRITE_FONT_WEIGHT_BOLD); _fBadge = fmt(L"Segoe UI", 11, DWRITE_FONT_WEIGHT_SEMI_BOLD, true); _fEmpty = fmt(L"Segoe UI", 15, DWRITE_FONT_WEIGHT_NORMAL, true);
    }
    if (!_rt && _d2d && _win) {
        RECT rc; GetClientRect(_win, &rc);
        _d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(_win, D2D1::SizeU(rc.right, rc.bottom)), &_rt);
        if (_rt) {
            _rt->SetDpi(_dpi, _dpi);
            _rt->CreateSolidColorBrush(D2D1::ColorF(0x000000), &_bText); _rt->CreateSolidColorBrush(D2D1::ColorF(0x6E6E73), &_bSecondary);
            _rt->CreateSolidColorBrush(D2D1::ColorF(0xA0A0A5), &_bTertiary); _rt->CreateSolidColorBrush(BRAND, &_bBrand);
            _rt->CreateSolidColorBrush(D2D1::ColorF(0x4F46E5, 0.3f), &_bBrandSoft); _rt->CreateSolidColorBrush(D2D1::ColorF(0x000000, 0.1f), &_bSep);
            _rt->CreateSolidColorBrush(D2D1::ColorF(0x000000, 0.07f), &_bBadge); _rt->CreateSolidColorBrush(D2D1::ColorF(0x4F46E5, 0.22f), &_bSelect);
        }
    }
}
static void dropGraphics() {
    for (auto** b : {&_bText, &_bSecondary, &_bTertiary, &_bBrand, &_bBrandSoft, &_bSep, &_bBadge, &_bSelect}) if (*b) { (*b)->Release(); *b = nullptr; }
    if (_rt) { _rt->Release(); _rt = nullptr; }
}
static float textWidth(const std::wstring& s, IDWriteTextFormat* f) {
    if (s.empty() || !_dw) return 0; IDWriteTextLayout* l = nullptr; _dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 10000, 100, &l); if (!l) return 0;
    DWRITE_TEXT_METRICS m; l->GetMetrics(&m); l->Release(); return m.widthIncludingTrailingWhitespace;
}
static void text(const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, ID2D1Brush* b) { if (!s.empty()) _rt->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, b, (D2D1_DRAW_TEXT_OPTIONS)(D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT | D2D1_DRAW_TEXT_OPTIONS_CLIP)); }
static float gridTop() { return SEARCH_H + 1 + GRID_PAD; }
static D2D1_RECT_F cellRect(const Row& r, int c) { float x = GRID_X + c * CELL, y = gridTop() + r.y - _scrollY; return D2D1::RectF(x, y, x + CELL, y + CELL); }
static D2D1_RECT_F toneRect() { return D2D1::RectF(WIDTH - 14 - 28, HEIGHT - FOOTER_H + 8, WIDTH - 14, HEIGHT - 8); }

static void paint() {
    ensureGraphics(); if (!_rt) return;
    _rt->BeginDraw();
    _rt->Clear(D2D1::ColorF(0xF7F7F8));
    // search field: magnifier, the query (Telex-read in Vietnamese mode), caret, placeholder, VI/EN badge
    float sx = 14, sy = 14, sh = 24;
    _rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(sx + 7, sy + 10), 5.5f, 5.5f), _bSecondary, 1.6f);
    _rt->DrawLine(D2D1::Point2F(sx + 11, sy + 14), D2D1::Point2F(sx + 16, sy + 19), _bSecondary, 1.8f);
    float tx = sx + 26; std::wstring disp = _display(); float tw = textWidth(disp, _fSearch);
    float badgeW = 26; D2D1_RECT_F badge = D2D1::RectF(WIDTH - 14 - badgeW, sy + 4, WIDTH - 14, sy + sh - 4);
    if (_allSelected && !disp.empty()) _rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx - 1, sy + 1, tx + tw + 1, sy + sh - 1), 3, 3), _bBrandSoft);
    text(disp, _fSearch, D2D1::RectF(tx, sy, badge.left - 8, sy + sh), _bText);
    if (_caret && !_allSelected) _rt->FillRectangle(D2D1::RectF(tx + tw + 1, sy + 3, tx + tw + 3, sy + sh - 3), _bBrand);
    if (_raw.empty()) text(_vietnamese ? L"Tìm emoji: ngựa, horse…" : L"Search emoji: horse, ngựa…", _fSearch, D2D1::RectF(tx + 4, sy, badge.left - 8, sy + sh), _bTertiary);
    _rt->FillRoundedRectangle(D2D1::RoundedRect(badge, 5, 5), _bBadge);
    text(_vietnamese ? L"VI" : L"EN", _fBadge, badge, _bSecondary);
    _rt->FillRectangle(D2D1::RectF(0, SEARCH_H, WIDTH, SEARCH_H + 1), _bSep);
    // grid
    float gt = SEARCH_H + 1, gb = HEIGHT - FOOTER_H - 1;
    _rt->PushAxisAlignedClip(D2D1::RectF(0, gt + GRID_PAD, WIDTH, gb - GRID_PAD), D2D1_ANTIALIAS_MODE_ALIASED);   // the scrolling area itself is inset (Tony)
    if (_searching() && _results.empty()) text(_vietnamese ? L"Không tìm thấy emoji nào" : L"No emoji found", _fEmpty, D2D1::RectF(0, gt + 100, WIDTH, gt + 140), _bSecondary);
    for (auto& r : _rows) {
        float y = gridTop() + r.y - _scrollY; if (y + r.h < gt || y > gb) continue;
        if (r.header) { float pad = r.h - 20; text(r.title, _fHeader, D2D1::RectF(GRID_X, y + pad, WIDTH - GRID_X, y + pad + 14), _bSecondary); continue; }
        for (int c = 0; c < r.count; c++) {
            int idx = r.start + c; D2D1_RECT_F cr = cellRect(r, c);
            if (idx == _selected) _rt->FillRoundedRectangle(D2D1::RoundedRect(cr, 8, 8), _bSelect);
            text(_all[_visible[idx]].withSkin(_tone), _fEmoji, cr, _bText);
        }
    }
    _rt->PopAxisAlignedClip();
    _rt->FillRectangle(D2D1::RectF(0, gb, WIDTH, gb + 1), _bSep);
    // footer: the selected emoji and its name, the brand, the skin tone
    float fy = HEIGHT - FOOTER_H;
    if (const Emoji* e = _current()) {
        text(e->withSkin(_tone), _fEmojiFooter, D2D1::RectF(14, fy, 14 + 32, HEIGHT), _bText);
        text(_vietnamese ? e->nameVI : e->nameEN, _fFooter, D2D1::RectF(14 + 32 + 6, fy, WIDTH - 140, HEIGHT), _bSecondary);
    }
    D2D1_RECT_F tr = toneRect();
    static const wchar_t* HANDS[] = {L"✋", L"✋🏻", L"✋🏼", L"✋🏽", L"✋🏾", L"✋🏿"};
    text(HANDS[_tone], _fTone, tr, _bText);
    _rt->FillRectangle(D2D1::RectF(tr.left - 10, fy + 14, tr.left - 9, HEIGHT - 14), _bSep);
    float wx = tr.left - 10 - 10; float keyW = textWidth(L"key", _fWordmark), viW = textWidth(L"Vi", _fWordmark);
    text(L"key", _fWordmark, D2D1::RectF(wx - keyW, fy, wx, HEIGHT), _bBrand); wx -= keyW;
    text(L"Vi", _fWordmark, D2D1::RectF(wx - viW, fy, wx, HEIGHT), _bText); wx -= viW + 5;
    HICON icon = (HICON)LoadImageW(_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (icon) {   // the app icon through GDI on the same surface, after D2D is done with this frame
        ICONINFO ii; if (GetIconInfo(icon, &ii)) {
            BITMAP bm; GetObject(ii.hbmColor, sizeof(bm), &bm); std::vector<DWORD> px(bm.bmWidth * bm.bmHeight);
            BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = bm.bmWidth; bi.bmiHeader.biHeight = -bm.bmHeight; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            HDC dc = GetDC(nullptr); GetDIBits(dc, ii.hbmColor, 0, bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS); ReleaseDC(nullptr, dc);
            for (auto& p : px) { BYTE a = p >> 24; if (a && a < 255) { BYTE r = (BYTE)(((p >> 16) & 255) * a / 255), g = (BYTE)(((p >> 8) & 255) * a / 255), b = (BYTE)((p & 255) * a / 255); p = (a << 24) | (r << 16) | (g << 8) | b; } }
            ID2D1Bitmap* bmp = nullptr; _rt->CreateBitmap(D2D1::SizeU(bm.bmWidth, bm.bmHeight), px.data(), bm.bmWidth * 4, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bmp);
            if (bmp) { _rt->DrawBitmap(bmp, D2D1::RectF(wx - 16, fy + 14, wx, fy + 30), 0.85f); bmp->Release(); }
            DeleteObject(ii.hbmColor); DeleteObject(ii.hbmMask);
        }
        DestroyIcon(icon);
    }
    if (_rt->EndDraw() == D2DERR_RECREATE_TARGET) dropGraphics();
}

static void choose(const Emoji* e) {
    if (!e) return;
    std::wstring t = e->withSkin(_tone); usageRecord(e->ch);
    close();
    core::insertText(t);
}
static int cellAt(int px, int py) {   // index of the emoji under the point (client pixels), -1 if none
    float x = px * 96.0f / _dpi, y = py * 96.0f / _dpi;
    if (y < SEARCH_H + 1 || y > HEIGHT - FOOTER_H - 1) return -1;
    for (auto& r : _rows) { if (r.header) continue; float ry = gridTop() + r.y - _scrollY; if (y >= ry && y < ry + r.h) { int c = (int)((x - GRID_X) / CELL); return x >= GRID_X && c < r.count ? r.start + c : -1; } }
    return -1;
}
static wchar_t charFor(DWORD vk, unsigned flags) {
    BYTE ks[256] = {}; if (flags & 1) ks[VK_SHIFT] = 0x80; if (flags & 16) ks[VK_CAPITAL] = 1;
    if ((flags & 6) == 6) ks[VK_CONTROL] = ks[VK_MENU] = 0x80;   // AltGr (Ctrl+Alt): @ on German, ł on Polish
    wchar_t buf[8] = {}; HKL hkl = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
    int n = ToUnicodeEx(vk, MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, hkl), ks, buf, 8, 1 << 2, hkl);
    return n == 1 && buf[0] >= 0x20 ? buf[0] : 0;
}
static void onKey(DWORD vk, unsigned flags) {   // from the keyboard hook (WM_APP+10; Windows' shortcuts and media keys don't come here); EmojiPanel.keyDown
    bool ctrl = flags & 2, alt = flags & 4, win = flags & 8;
    switch (vk) {
    case VK_ESCAPE: if (_raw.empty()) close(); else setRaw(L""); break;   // Esc: clears the search, then closes
    case VK_RETURN: choose(_current()); return;
    case VK_LEFT: _allSelected = false; move(-1); break;
    case VK_RIGHT: _allSelected = false; move(1); break;
    case VK_DOWN: _allSelected = false; moveRow(1); break;
    case VK_UP: _allSelected = false; moveRow(-1); break;
    case VK_BACK: (ctrl || _allSelected) ? setRaw(L"") : alt ? deleteWord() : deleteBack(); break;
    default:
        if (ctrl && !alt) {
            if (vk == 'V') { if (OpenClipboard(nullptr)) { HANDLE h = GetClipboardData(CF_UNICODETEXT); if (h) { const wchar_t* s = (const wchar_t*)GlobalLock(h); if (s) { typeText(s); GlobalUnlock(h); } } CloseClipboard(); } }
            else if (vk == 'A') { if (!_raw.empty()) _allSelected = true; }
            else if (vk == 'C' || vk == 'X') { if (_allSelected && !_raw.empty()) { std::wstring d = _display(); if (OpenClipboard(nullptr)) { EmptyClipboard(); HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (d.size() + 1) * 2); if (g) { memcpy(GlobalLock(g), d.c_str(), (d.size() + 1) * 2); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); } CloseClipboard(); } if (vk == 'X') setRaw(L""); } }
            else if (vk == 'W') { close(); return; }
            break;
        }
        if ((alt && !ctrl) || win) break;
        if (wchar_t c = charFor(vk, flags)) typeText(std::wstring(1, c));
    }
    if (_win) InvalidateRect(_win, nullptr, FALSE);
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); paint(); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_APP + 10: onKey((DWORD)w, (unsigned)l); return 0;
    case WM_APP + 11: close(); return 0;
    case WM_APP + 12: if (w >= 0x20) { typeText(std::wstring(1, (wchar_t)w)); InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_TIMER: if (w == TIMER_CARET) { _caret = !_caret; InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_MOUSEMOVE: {   // hover selects only when the mouse really moved (not when the grid scrolled under it)
        POINT p; GetCursorPos(&p); if (p.x == _lastMouse.x && p.y == _lastMouse.y) return 0; _lastMouse = p;
        int i = cellAt(GET_X_LPARAM(l), GET_Y_LPARAM(l)); if (i >= 0 && i != _selected) { _selected = i; InvalidateRect(h, nullptr, FALSE); }
        return 0; }
    case WM_LBUTTONDOWN: {
        float x = GET_X_LPARAM(l) * 96.0f / _dpi, y = GET_Y_LPARAM(l) * 96.0f / _dpi; D2D1_RECT_F tr = toneRect();
        if (x >= tr.left - 4 && x <= tr.right + 4 && y >= tr.top - 4) { _tone = (_tone + 1) % 6; prefs::setInt(L"EmojiSkinTone", _tone); InvalidateRect(h, nullptr, FALSE); return 0; }
        int i = cellAt(GET_X_LPARAM(l), GET_Y_LPARAM(l)); if (i >= 0) { _selected = i; choose(_current()); }
        return 0; }
    case WM_MOUSEWHEEL: { _scrollY -= GET_WHEEL_DELTA_WPARAM(w) / (float)WHEEL_DELTA * CELL; clampScroll(); InvalidateRect(h, nullptr, FALSE); return 0; }
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE;
    case WM_DPICHANGED: _dpi = (float)HIWORD(w); dropGraphics(); return 0;
    case WM_DESTROY: dropGraphics(); _win = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void prepare() {
    if (_win) return;
    static bool reg = false;
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = _inst; wc.lpszClassName = L"VikeyEmoji"; wc.style = CS_DROPSHADOW; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc); reg = true; }
    _win = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"VikeyEmoji", L"Vikey Emoji", WS_POPUP, 0, 0, (int)WIDTH, (int)HEIGHT, nullptr, nullptr, _inst, nullptr);
    DWORD pref = 2 /* DWMWCP_ROUND */; DwmSetWindowAttribute(_win, 33 /* DWMWA_WINDOW_ROUNDED_PREFERENCE */, &pref, sizeof(pref));
    COLORREF border = RGB(214, 214, 218); DwmSetWindowAttribute(_win, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
}

void install(HINSTANCE inst) {
    _inst = inst;
    std::thread([] { loadIndex(); }).detach();
    settingsChanged();
}
void settingsChanged() { if (prefs::getInt(L"EmojiPicker", 0)) prepare(); }
bool isOpen() { return _open; }
void toggle() { _open ? close() : open(); }
void open() {
    loadIndex(); prepare(); if (!_win) return;
    resetModel(core::language() == 1);
    POINT p; GetCursorPos(&p); HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST); MONITORINFO mi = {sizeof(mi)}; GetMonitorInfoW(mon, &mi);
    UINT dpi = GetDpiForWindow(_win); _dpi = (float)dpi; float s = dpi / 96.0f;
    int w = (int)(WIDTH * s), hgt = (int)(HEIGHT * s); RECT a = mi.rcWork;
    int x = (a.left + a.right) / 2 - w / 2, y = (a.top + a.bottom) / 2 - hgt / 2 - (int)((a.bottom - a.top) * 0.08);
    dropGraphics();
    SetWindowPos(_win, HWND_TOPMOST, x, y, w, hgt, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    core::setEmojiPickerOpen(true, _win); _open = true; _caret = true;
    SetTimer(_win, TIMER_CARET, 530, nullptr);
    InvalidateRect(_win, nullptr, FALSE);
}
void close() {
    if (!_open) return;
    _open = false; core::setEmojiPickerOpen(false, nullptr);
    if (_win) { KillTimer(_win, TIMER_CARET); ShowWindow(_win, SW_HIDE); }
}
void languageChanged(int lang) { if (_vietnamese != (lang == 1)) { _vietnamese = lang == 1; if (_open) { doSearch(); InvalidateRect(_win, nullptr, FALSE); } } }

}} // namespace
