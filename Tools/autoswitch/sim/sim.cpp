// Types a mixed Vietnamese/English corpus into the real Vikey engine, key by key, with an
// "ideal user" who presses the switch hotkey only when the next word would come out wrong.
// Mirrors App/Core/VikeyCore.mm (English-mode word buffer, auto switch, hotkey undo).
// Build: see README.md.   Run: sim units.txt [--off] [--start en|vi] [--show N]
#include "Engine.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

int vLanguage = 1, vSwitchKeyStatus = 0, vCheckSpelling = 1, vUseModernOrthography = 1, vRestoreIfWrongSpelling = 1,
    vFixRecommendBrowser = 1, vUseSmartSwitchKey = 1, vOtherLanguage = 1, vAutoSwitchByTyping = 1;
void* vTestSaveState();
void vTestRestoreState(void*);
void vTestFreeState(void*);

static vKeyHookState* H;
static bool trace = false;
typedef std::u16string U;

struct Platform {                    // everything VikeyCore.mm keeps outside the engine
    U screen;
    Uint32 enKeys[MAX_BUFF]; int enCount = 0; bool enValid = true; bool enAfterDouble = false;
    Uint32 prevKeys[2][MAX_BUFF]; int prevCounts[2] = {0, 0}; int prevN = 0;
    bool undoActive = false; int undoRemove = 0; U undoText;
    int autoSwitches = 0, undos = 0;
    int runSide = -1, runCount = 0, earlySwitches = 0;   // Early Switch (D28)
    bool prevRestored = false;                           // prevKeys[0] was put back to English by auto switch (D35)
    Uint32 navKeys[MAX_BUFF]; int navCount = 0, navPrevN = 0, navOffset = 0; bool navValid = true, navAfterDouble = false;   // D51
};
static Platform P;
static int earlyN() { static int n = getenv("SIM_EARLY") ? atoi(getenv("SIM_EARLY")) : 0; return n; }

static Uint16 letterKey(char c) {
    static const Uint16 k[26] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    return k[c - 'a'];
}
static bool isLetterKey(Uint16 key) { for (char c = 'a'; c <= 'z'; c++) if (letterKey(c) == key) return true; return false; }
// char -> (key, shift)
static bool keyFor(char16_t c, Uint16& key, bool& shift) {
    shift = false;
    if (c >= 'a' && c <= 'z') { key = letterKey((char)c); return true; }
    if (c >= 'A' && c <= 'Z') { key = letterKey((char)(c + 32)); shift = true; return true; }
    static const Uint16 digits[10] = {KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9};
    if (c >= '0' && c <= '9') { key = digits[c - '0']; return true; }
    switch (c) {
        case ' ': key = KEY_SPACE; return true;   case ',': key = KEY_COMMA; return true;
        case '.': key = KEY_DOT; return true;     case ';': key = KEY_SEMICOLON; return true;
        case ':': key = KEY_SEMICOLON; shift = true; return true;
        case '?': key = KEY_SLASH; shift = true; return true;
        case '!': key = KEY_1; shift = true; return true;
        case '\'': key = KEY_QUOTE; return true;  case '"': key = KEY_QUOTE; shift = true; return true;
        case '(': key = KEY_9; shift = true; return true;  case ')': key = KEY_0; shift = true; return true;
        case '-': key = KEY_MINUS; return true;   case '/': key = KEY_SLASH; return true;
        case '\n': key = KEY_RETURN; return true;
    }
    return false;
}
static char16_t charOf(Uint32 d) { return (d & CHAR_CODE_MASK) ? (char16_t)(d & 0xFFFF) : (char16_t)keyCodeToCharacter(d); }
static bool isWhitespace(Uint16 key) { return key == KEY_SPACE || key == KEY_RETURN || key == KEY_ENTER || key == KEY_TAB; }
static char16_t typedChar(Uint16 key, bool shift) { if (key == KEY_RETURN) return u'\n'; return keyCodeToCharacter(key | (shift ? CAPS_MASK : 0)); }

static void applyOutput(Uint16 key, bool shift) {
    for (int i = 0; i < H->backspaceCount && !P.screen.empty(); i++) P.screen.pop_back();
    for (int i = H->newCharCount - 1; i >= 0; i--) P.screen.push_back(charOf(H->charData[i]));
    if (H->code == vRestore || H->code == vRestoreAndStartNewSession) { char16_t ch = typedChar(key, shift); if (ch) P.screen.push_back(ch); }
    if (H->code == vRestoreAndStartNewSession) startNewSession();
}
static void rememberFix(int corrected, Uint16 key, bool shift) {
    P.undoActive = false;
    char16_t brk = typedChar(key, shift);
    if (!brk || brk == u'\n' || vAutoSwitchUndoCount <= 0 || corrected <= 0) return;
    for (int x = 0; x < vAutoSwitchUndoCount; x++) if ((vAutoSwitchUndoData[x] & 0xFFFF) == KEY_SPACE && !(vAutoSwitchUndoData[x] & CHAR_CODE_MASK)) return;   //multi-word fix: no undo
    P.undoText.clear();
    for (int x = 0; x < vAutoSwitchUndoCount; x++) P.undoText.push_back(charOf(vAutoSwitchUndoData[x]));
    P.undoText.push_back(brk);
    P.undoRemove = corrected + 1;
    P.undoActive = true;
}
static void switchTo(int lang) { vLanguage = lang; startNewSession(); vAutoSwitchForget(); P.autoSwitches++; }

static void switchTo(int lang);
/** Early Switch (D28): N words in a row that can only be one language switch to it, before any word needs fixing. */
static void earlyWord(int side) {
    if (side < 0) { P.runCount = 0; return; }
    if (side == P.runSide) P.runCount++; else { P.runSide = side; P.runCount = 1; }
    static int dir = getenv("SIM_EARLY_DIR") ? atoi(getenv("SIM_EARLY_DIR")) : -1;   // only to this language (tests)
    if (earlyN() > 0 && P.runCount >= earlyN() && side != vLanguage && (dir < 0 || dir == side)) {
        switchTo(side); P.earlySwitches++;
        P.enCount = 0; P.enValid = true; P.prevN = 0; P.enAfterDouble = false;
    }
}

static void press(Uint16 key, bool shift) {
    P.undoActive = false;
    if (vLanguage == 1) {
        vAutoSwitchRequest = -1;
        vAutoSwitchLastSide = -2;
        vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, key, shift ? 1 : 0, false);
        if (H->code == vDoNothing) {
            if (key == KEY_DELETE) { if (!P.screen.empty()) P.screen.pop_back(); }
            else { char16_t ch = typedChar(key, shift); if (ch) P.screen.push_back(ch); }
            if (vAutoSwitchLastSide != -2) earlyWord(vAutoSwitchLastSide);
        } else {
            bool atBreak = vAutoSwitchRequest == 0 && !isLetterKey(key);
            int corrected = H->newCharCount;
            applyOutput(key, shift);
            if (vAutoSwitchRequest == 0) {
                vAutoSwitchRequest = -1;
                if (atBreak) rememberFix(corrected, key, shift);
                switchTo(0);
                P.enCount = 0; P.enValid = atBreak && isWhitespace(key); P.prevN = 0;
                // D35: the word just put back to English is the previous word for look-back
                if (key == KEY_SPACE && vAutoSwitchRestoredCount > 0 && getenv("SIM_NO_RESTORED_PREV") == nullptr) {
                    memcpy(P.prevKeys[0], vAutoSwitchRestoredKeys, vAutoSwitchRestoredCount * sizeof(Uint32)); P.prevCounts[0] = vAutoSwitchRestoredCount; P.prevN = 1; P.prevRestored = true; }
                P.enAfterDouble = !atBreak;
            }
        }
        return;
    }
    // English mode (HandleEnglishKeyForAutoSwitch)
    if ((key == KEY_LEFT || key == KEY_RIGHT) && !shift) {   // D51: ← → round trip inside a word keeps the word
        if (key == KEY_LEFT && P.navOffset == 0 && (P.enCount > 0 || P.prevN > 0)) {
            memcpy(P.navKeys, P.enKeys, sizeof(P.enKeys)); P.navCount = P.enCount; P.navValid = P.enValid;
            P.navAfterDouble = P.enAfterDouble; P.navPrevN = P.prevN; P.navOffset = -1;
            P.enCount = 0; P.enValid = false; P.enAfterDouble = false; P.prevN = 0;
            return;
        }
        if (P.navOffset < 0) {
            if (key == KEY_LEFT) P.navOffset--;
            else if (++P.navOffset == 0) {
                memcpy(P.enKeys, P.navKeys, sizeof(P.enKeys)); P.enCount = P.navCount; P.enValid = P.navValid;
                P.enAfterDouble = P.navAfterDouble; P.prevN = P.navPrevN;
            }
            return;
        }
    }
    P.navOffset = 0;
    if (isLetterKey(key)) {
        if (P.enCount < MAX_BUFF - 2) P.enKeys[P.enCount++] = key | (shift ? CAPS_MASK : 0); else P.enValid = false;
        P.screen.push_back(typedChar(key, shift)); return;
    }
    if (key == KEY_DELETE) { if (P.enCount == 0) { P.prevN = 0; P.enAfterDouble = false; } if (P.enCount > 0) P.enCount--; else P.enValid = false; if (!P.screen.empty()) P.screen.pop_back(); return; }
    bool digit = false; for (Uint16 d : {KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9}) if (key == d) digit = true;
    // digits then letters: the letters are a word of their own ("5trawm" -> "5trăm", D33); nothing to look back on
    // digits after a space, then letters: the letters are a word of their own ("5trawm" -> "5trăm", D33);
    // digits glued to letters (abc1def) make it a code, not a word
    if (digit && !shift) { P.enValid = P.enCount == 0 && P.enValid; P.enCount = 0; P.prevN = 0; P.enAfterDouble = false; P.screen.push_back(typedChar(key, shift)); return; }
    bool wordEnd = key == KEY_SPACE || key == KEY_COMMA || key == KEY_DOT || key == KEY_SEMICOLON || key == KEY_RETURN ||
                   (shift && (key == KEY_SLASH || key == KEY_1 || key == KEY_QUOTE || key == KEY_0));
    int count = P.enCount; bool valid = P.enValid;
    bool afterDouble = P.enAfterDouble; P.enAfterDouble = false;
    if (afterDouble && wordEnd && vAutoSwitchFixDoubledKey(P.enKeys, count)) {
        P.enCount = 0; P.enValid = isWhitespace(key); P.prevN = 0;
        int corrected = H->newCharCount;
        applyOutput(key, shift);
        rememberFix(corrected, key, shift);
        return;
    }
    P.enCount = 0; P.enValid = isWhitespace(key) || (key == KEY_9 && shift) || key == KEY_LEFT_BRACKET || key == KEY_QUOTE;
    const Uint32* prev[2] = {P.prevKeys[0], P.prevKeys[1]};
    vAutoSwitchPrevRestored = P.prevRestored && P.prevN >= 1;
    bool converted = wordEnd && valid && count >= 2 &&
        (getenv("SIM_NO_LOOKBACK") ? vAutoSwitchCheckEnglishWord(P.enKeys, count) : vAutoSwitchCheckEnglishWord(P.enKeys, count, prev, P.prevCounts, P.prevN));
    if (!converted && wordEnd && valid && count >= 2) earlyWord(vAutoSwitchWordSide(P.enKeys, count));
    if (!converted && key == KEY_SPACE && valid && count >= 1) {
        P.prevRestored = false;
        memcpy(P.prevKeys[1], P.prevKeys[0], sizeof(P.prevKeys[0])); P.prevCounts[1] = P.prevCounts[0];
        memcpy(P.prevKeys[0], P.enKeys, count * sizeof(Uint32)); P.prevCounts[0] = count; if (P.prevN < 2) P.prevN++;
    } else P.prevN = 0;
    if (converted) {
        int corrected = H->newCharCount;
        applyOutput(key, shift);
        rememberFix(corrected, key, shift);
        switchTo(1);
        return;
    }
    char16_t ch = typedChar(key, shift); if (ch) P.screen.push_back(ch);
}

static void hotkey() {                        // switchLanguage(): a plain switch (undo is Backspace)
    P.undoActive = false;
    P.enCount = 0; P.enValid = true; P.prevN = 0; P.enAfterDouble = false; P.navOffset = 0; vAutoSwitchForget(); P.runCount = 0;
    vLanguage = vLanguage ? 0 : 1;
    startNewSession();
}

static void typeString(const std::string& s) {
    for (char c : s) { Uint16 k; bool sh; if (keyFor((unsigned char)c, k, sh)) press(k, sh); }
}

struct Unit { std::string keys; bool isvn; std::string seps; bool word; };

// What the word should look like: Vietnamese words as the engine writes them in Vietnamese mode.
static U golden(const Unit& u) {
    U g;
    if (!u.word) { for (char c : u.seps) g.push_back((char16_t)c); return g; }
    if (!u.isvn) { for (char c : u.keys) g.push_back((char16_t)c); }
    else {
        void* st = vTestSaveState(); Platform saved = P; int lang = vLanguage, on = vAutoSwitchByTyping;
        vAutoSwitchByTyping = 0; vLanguage = 1; startNewSession(); P.screen.clear();
        typeString(u.keys); press(KEY_SPACE, false);
        g = P.screen; if (!g.empty()) g.pop_back();
        vTestRestoreState(st); P = saved; vLanguage = lang; vAutoSwitchByTyping = on;
    }
    for (char c : u.seps) g.push_back(c == '\n' ? u'\n' : (char16_t)c);
    return g;
}

static std::string utf8(const U& s) {
    std::string o;
    for (char16_t c : s) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}

// words of b that are not matched (in order) by words of a: wrong words in a typed line
static int wrongWords(const U& a, const U& b) {
    auto split = [](const U& s) { std::vector<U> w; U cur; for (char16_t c : s) { if (c == ' ' || c == '\n') { if (!cur.empty()) w.push_back(cur); cur.clear(); } else cur.push_back(c); } if (!cur.empty()) w.push_back(cur); return w; };
    std::vector<U> x = split(a), y = split(b);
    std::vector<std::vector<int>> L(x.size() + 1, std::vector<int>(y.size() + 1, 0));
    for (size_t i = 1; i <= x.size(); i++) for (size_t j = 1; j <= y.size(); j++)
        L[i][j] = x[i-1] == y[j-1] ? L[i-1][j-1] + 1 : std::max(L[i-1][j], L[i][j-1]);
    return (int)y.size() - L[x.size()][y.size()];
}

int main(int argc, char** argv) {
    if (getenv("VIKEY_TEENCODE")) vTeencode = atoi(getenv("VIKEY_TEENCODE"));   // D53 experiments (on by default)
    if (getenv("SIM_BIGRAM")) vAutoSwitchSetBigrams(getenv("SIM_BIGRAM"), getenv("SIM_BIGRAM_W") ? atof(getenv("SIM_BIGRAM_W")) : 1.0f,
                                                     getenv("SIM_BIGRAM_MIN") ? atoi(getenv("SIM_BIGRAM_MIN")) : 3);   // D30 experiment
    if (getenv("SIM_TRIGRAM")) vAutoSwitchSetTrigrams(getenv("SIM_TRIGRAM"), getenv("SIM_TRIGRAM_W") ? atof(getenv("SIM_TRIGRAM_W")) : 2.0f,
                                                       getenv("SIM_TRIGRAM_MIN") ? atoi(getenv("SIM_TRIGRAM_MIN")) : 3);   // D34 experiment
    if (getenv("SIM_CAP")) vAutoSwitchSetCapitalised(getenv("SIM_CAP"), getenv("SIM_CAP_MIN") ? atoi(getenv("SIM_CAP_MIN")) : 5);   // D37 experiment
    H = (vKeyHookState*)vKeyInit();
    int show = 0; vLanguage = 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--off")) vAutoSwitchByTyping = 0;
        if (!strcmp(argv[i], "--start") && i + 1 < argc) vLanguage = strcmp(argv[++i], "en") ? 1 : 0;
        if (!strcmp(argv[i], "--show") && i + 1 < argc) show = atoi(argv[++i]);
        if (!strcmp(argv[i], "--trace")) trace = true;
    }
    // read units, attaching separators to the preceding word
    std::vector<Unit> units;
    FILE* f = fopen(argv[1], "r"); char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == 'W') {
            char* k = line + 2; char* t = strchr(k, '\t'); if (!t) continue; *t = 0;
            units.push_back({k, t[1] == '1', "", true});
        } else {
            std::string sep = line[0] == 'N' ? "\n" : std::string(line + 2);
            if (units.empty()) units.push_back({"", false, "", false});
            units.back().seps += sep;
        }
    }
    fclose(f);

    long words = 0, manual = 0, errors = 0, wordsVn = 0, wordsEn = 0;
    bool lazy = false; int showLazy = 0; FILE* dump = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--lazy")) lazy = true;
        if (!strcmp(argv[i], "--show-lazy") && i + 1 < argc) showLazy = atoi(argv[++i]);
        if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = fopen(argv[++i], "w");   // one "got<TAB>want" line per message
    }
    if (lazy) {   // never switch by hand: type each message, count wrong words, then fix them by hand
        U line, want; long wrong = 0, lines = 0, perfect = 0;
        for (const Unit& u : units) {
            want += golden(u);
            typeString(u.keys); typeString(u.seps);
            if (u.word) { words++; if (u.isvn) wordsVn++; else wordsEn++; }
            if (u.seps.find('\n') != std::string::npos) {
                int w = wrongWords(P.screen, want); wrong += w; lines++; if (!w) perfect++;
                if (dump) { U g = P.screen, t = want; if (!g.empty() && g.back() == u'\n') g.pop_back(); if (!t.empty() && t.back() == u'\n') t.pop_back();
                            fprintf(dump, "%s\t%s\n", utf8(g).c_str(), utf8(t).c_str()); }
                if (w && showLazy > 0) { showLazy--; fprintf(stderr, "GOT  %s\nWANT %s\n", utf8(P.screen).c_str(), utf8(want).c_str()); }
                P.screen.clear(); want.clear();
            }
        }
        if (dump) fclose(dump);
        printf("%s lazy  words %ld  wrong words %ld (%.2f%%)  messages %ld, perfect %ld (%.1f%%)\n", vAutoSwitchByTyping ? "auto ON " : "auto OFF",
               words, wrong, 100.0 * wrong / words, lines, perfect, 100.0 * perfect / lines);
        return 0;
    }
    U expected;
    for (size_t i = 0; i < units.size(); i++) {
        const Unit& u = units[i];
        U want = expected + golden(u);
        void* st = vTestSaveState(); Platform saved = P; int lang = vLanguage;
        typeString(u.keys); typeString(u.seps);
        bool ok = P.screen == want;
        if (!ok && getenv("LOOKBACK_DEBUG")) {
            size_t a = want.size() > 60 ? want.size() - 60 : 0, b = P.screen.size() > 60 ? P.screen.size() - 60 : 0;
            fprintf(stderr, "FIRST-TRY-WRONG '%s'\n   got  %s\n   want %s\n", u.keys.c_str(), utf8(P.screen.substr(b)).c_str(), utf8(want.substr(a)).c_str());
        }
        if (!ok) {                              // the user would have switched before this word
            vTestRestoreState(st); P = saved; vLanguage = lang;
            if (getenv("SIM_MANUAL_LOG")) fprintf(stderr, "MANUAL\t%s\t%d\t%d\n", u.keys.c_str(), (int)u.isvn, lang);
            hotkey(); manual++;
            typeString(u.keys); typeString(u.seps);
            ok = P.screen == want;
            if (!ok) {
                errors++;
                if (show > 0) { show--; fprintf(stderr, "ERROR at '%s': got ...%s| want ...%s|\n", u.keys.c_str(),
                    utf8(P.screen.substr(P.screen.size() > 40 ? P.screen.size() - 40 : 0)).c_str(), utf8(want.substr(want.size() > 40 ? want.size() - 40 : 0)).c_str()); }
                P.screen = want;                // the user fixes it by hand
            }
        } else vTestFreeState(st);              // first attempt was right: keep it
        if (trace) fprintf(stderr, "unit '%s' lang=%d screen='%s' manual=%ld\n", u.keys.c_str(), vLanguage, utf8(P.screen).c_str(), manual);
        if (u.word) { words++; if (u.isvn) wordsVn++; else wordsEn++; }
        expected = want;
        if (expected.size() > 4000) { // keep memory small: drop finished lines
            size_t cut = expected.rfind(u'\n', expected.size() - 1000);
            if (cut != U::npos && P.screen.size() >= cut + 1 && P.screen.compare(0, cut + 1, expected, 0, cut + 1) == 0) {
                expected.erase(0, cut + 1); P.screen.erase(0, cut + 1);
            }
        }
    }
    printf("%s  words %ld (vi %ld, en %ld)  manual switches %ld (%.2f per 100 words)  wrong words %ld (%.2f%%)  auto %d  undo-by-hotkey %d\n",
           vAutoSwitchByTyping ? "auto ON " : "auto OFF", words, wordsVn, wordsEn, manual, 100.0 * manual / words, errors, 100.0 * errors / words,
           P.autoSwitches, P.undos);
}
