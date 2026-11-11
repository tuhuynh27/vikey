// Drives the real Vikey engine with key sequences and simulates the text field, mirroring
// what App/Core/VikeyCore.mm does with the engine's output (Unicode code table).
#include "Engine.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <cstdlib>

int vLanguage = 1, vSwitchKeyStatus = 0, vCheckSpelling = 1, vUseModernOrthography = 1, vRestoreIfWrongSpelling = 1,
    vFixRecommendBrowser = 1, vUseSmartSwitchKey = 1, vOtherLanguage = 1, vAutoSwitchByTyping = 1;

static vKeyHookState* H;
static std::u16string screen;
static Uint32 enKeys[MAX_BUFF]; static int enCount = 0; static bool enValid = true;
static Uint32 prevKeys[2][MAX_BUFF]; static int prevCounts[2]; static int prevN = 0; static bool prevRestored = false;
int switches = 0;

static Uint16 keyOf(char c) {
    switch (c) {
        case 'a': return KEY_A; case 'b': return KEY_B; case 'c': return KEY_C; case 'd': return KEY_D; case 'e': return KEY_E;
        case 'f': return KEY_F; case 'g': return KEY_G; case 'h': return KEY_H; case 'i': return KEY_I; case 'j': return KEY_J;
        case 'k': return KEY_K; case 'l': return KEY_L; case 'm': return KEY_M; case 'n': return KEY_N; case 'o': return KEY_O;
        case 'p': return KEY_P; case 'q': return KEY_Q; case 'r': return KEY_R; case 's': return KEY_S; case 't': return KEY_T;
        case 'u': return KEY_U; case 'v': return KEY_V; case 'w': return KEY_W; case 'x': return KEY_X; case 'y': return KEY_Y;
        case 'z': return KEY_Z; case ' ': return KEY_SPACE; case ',': return KEY_COMMA; case '.': return KEY_DOT;
        case '\n': return KEY_RETURN; case '<': return KEY_DELETE; case '_': return KEY_MINUS; case '-': return KEY_MINUS; case '/': return KEY_SLASH; case '1': return KEY_1; case '2': return KEY_2;
        case '\'': return KEY_QUOTE; case '"': return KEY_QUOTE; case ')': return KEY_0; case '(': return KEY_9; case '[': return KEY_LEFT_BRACKET; case ']': return KEY_RIGHT_BRACKET; case ';': return KEY_SEMICOLON; case '?': return KEY_SLASH;
    }
    return 0xFFFF;
}
static bool enAfterDouble = false;
static int enNavOff = 0;   // D51: ← count away from the English word's end; like _enNavOffset, any other key, click or switch resets it
static bool isLetter(char c) { return c >= 'a' && c <= 'z'; }
static char16_t charOf(Uint32 d) { return (d & CHAR_CODE_MASK) ? (char16_t)(d & 0xFFFF) : (char16_t)keyCodeToCharacter(d); }

static void applyOutput(Uint16 key, bool caps) {
    for (int i = 0; i < H->backspaceCount && !screen.empty(); i++) screen.pop_back();
    for (int i = H->newCharCount - 1; i >= 0; i--) screen.push_back(charOf(H->charData[i]));
    if (H->code == vRestore || H->code == vRestoreAndStartNewSession) {
        Uint16 ch = keyCodeToCharacter(key | (caps ? CAPS_MASK : 0));
        if (ch) screen.push_back(ch); else if (key == KEY_RETURN) screen.push_back(u'\n');
    }
    if (H->code == vRestoreAndStartNewSession) startNewSession();
}

static void press(char c) {
    if (c != '{' && c != '}') enNavOff = 0;
    if (c == '{' || c == '}') {   // ← / →: the cursor moves (the screen model has no cursor: only used for round trips)
        if (vLanguage == 1) vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, c == '{' ? KEY_LEFT : KEY_RIGHT, 0, false);
        else {   // like HandleEnglishKeyForAutoSwitch (D51): a round trip keeps the word
            static Uint32 nk[MAX_BUFF]; static int nc = 0, np = 0; static bool nv = true, nd = false;
            if (c == '{' && enNavOff == 0 && (enCount > 0 || prevN > 0)) { memcpy(nk, enKeys, sizeof(enKeys)); nc = enCount; nv = enValid; nd = enAfterDouble; np = prevN; enNavOff = -1;
                                                                     enCount = 0; enValid = false; prevN = 0; enAfterDouble = false; }
            else if (enNavOff < 0) { if (c == '{') enNavOff--; else if (++enNavOff == 0) { memcpy(enKeys, nk, sizeof(enKeys)); enCount = nc; enValid = nv; enAfterDouble = nd; prevN = np; } }
            else { enCount = 0; enValid = false; prevN = 0; enAfterDouble = false; }
        }
        return;
    }
    if (c == '@') {   // a click (where the cursor already is): the apps' mouse-down
        if (vLanguage == 1) vKeyHandleEvent(vKeyEvent::Mouse, vKeyEventState::MouseDown, 0);
        else { enCount = 0; enValid = true; prevN = 0; enAfterDouble = false; vAutoSwitchForget(); }
        return;
    }
    if (c == '#') {   // the switch hotkey (⌥Z): what the apps do on a manual switch
        vLanguage = vLanguage ? 0 : 1; startNewSession(); vAutoSwitchForget(); prevN = 0; enCount = 0; enValid = true; enAfterDouble = false;
        return;
    }
    bool shifted = c == '"' || c == ')' || c == '(' || c == '?';
    bool caps = (c >= 'A' && c <= 'Z') || shifted;
    char lc = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    Uint16 key = keyOf(lc);
    if (vLanguage == 1) {
        vAutoSwitchRequest = -1;
        vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, key, caps ? 1 : 0, false);
        if (H->code == vDoNothing) {
            if (key == KEY_DELETE) { if (!screen.empty()) screen.pop_back(); }
            else if (key == KEY_RETURN) screen.push_back(u'\n');
            else screen.push_back(keyCodeToCharacter(key | (caps ? CAPS_MASK : 0)));
        } else applyOutput(key, caps);
        if (vAutoSwitchRequest == 0) {
            vAutoSwitchRequest = -1; vLanguage = 0; startNewSession(); vAutoSwitchForget(); prevN = 0; switches++;
            if (key == KEY_SPACE && vAutoSwitchRestoredCount > 0) {   // D35, like the apps
                memcpy(prevKeys[0], vAutoSwitchRestoredKeys, vAutoSwitchRestoredCount * sizeof(Uint32)); prevCounts[0] = vAutoSwitchRestoredCount; prevN = 1; prevRestored = true; }
            enCount = 0; enValid = (key == KEY_SPACE || key == KEY_RETURN);
            enAfterDouble = isLetter(lc);
        }
        return;
    }
    // English mode: same logic as HandleEnglishKeyForAutoSwitch
    if (isLetter(lc)) {
        if (enCount < MAX_BUFF - 2) enKeys[enCount++] = key | (caps ? CAPS_MASK : 0); else enValid = false;
        screen.push_back(c); return;
    }
    if (key == KEY_DELETE) { if (enCount == 0) { prevN = 0; enAfterDouble = false; } if (enCount > 0) enCount--; else enValid = false; if (!screen.empty()) screen.pop_back(); return; }
    if (lc >= '0' && lc <= '9') { enValid = false; enAfterDouble = false; screen.push_back(c); return; }
    bool wordEnd = key == KEY_SPACE || key == KEY_COMMA || key == KEY_DOT || key == KEY_RETURN || key == KEY_SEMICOLON ||
                   (caps && (key == KEY_SLASH || key == KEY_1 || key == KEY_QUOTE || key == KEY_0));
    int count = enCount; bool valid = enValid; enCount = 0;
    enValid = key == KEY_SPACE || key == KEY_RETURN || (key == KEY_9 && caps) || key == KEY_LEFT_BRACKET || key == KEY_QUOTE;
    bool afterDouble = enAfterDouble; enAfterDouble = false;
    if (afterDouble && wordEnd && vAutoSwitchFixDoubledKey(enKeys, count)) {
        prevN = 0; applyOutput(key, caps); return;
    }
    const Uint32* prev[2] = {prevKeys[0], prevKeys[1]};
    vAutoSwitchPrevRestored = prevRestored && prevN >= 1;
    bool converted = wordEnd && valid && count >= 2 && vAutoSwitchCheckEnglishWord(enKeys, count, prev, prevCounts, prevN);
    if (!converted && key == KEY_SPACE && valid && count >= 1) {
        prevRestored = false;
        memcpy(prevKeys[1], prevKeys[0], sizeof(prevKeys[0])); prevCounts[1] = prevCounts[0];
        memcpy(prevKeys[0], enKeys, count * sizeof(Uint32)); prevCounts[0] = count; if (prevN < 2) prevN++;
    } else prevN = 0;
    if (converted) {
        applyOutput(key, caps); vLanguage = 1; startNewSession(); vAutoSwitchForget(); switches++; return;
    }
    screen.push_back(c == '\n' ? u'\n' : (char16_t)c);
}

static std::string utf8(const std::u16string& s) {
    std::string o;
    for (char16_t c : s) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}

// usage: harness <startLang 0|1> <keys>   or   harness bench
#ifndef _MSC_VER   // weak: Engine.cpp has the real one; the quality scorecard also builds this against older engines (D56)
int vTelexStyle __attribute__((weak)) = 0;
#endif
int main(int argc, char** argv) {
    if (getenv("VIKEY_TEENCODE")) vTeencode = atoi(getenv("VIKEY_TEENCODE"));   // D53 experiments (on by default)
    if (getenv("VIKEY_TELEX_STYLE")) vTelexStyle = atoi(getenv("VIKEY_TELEX_STYLE"));   // D56: 1 / 2 = Simple Telex 1 / 2
    H = (vKeyHookState*)vKeyInit();
    if (getenv("VIKEY_AUTO_OFF")) { vAutoSwitchByTyping = 0; vRestoreIfWrongSpelling = 0; }   // a plain Telex input method, for comparison
    if (argc >= 2 && strcmp(argv[1], "lines") == 0) {
        // stdin: "<lang>\t<keys>" per line -> prints "<final text>\t<switches>\t<final lang>"
        char buf[4096];
        while (fgets(buf, sizeof buf, stdin)) {
            char* tab = strchr(buf, '\t'); if (!tab) continue;
            buf[strcspn(buf, "\n")] = 0;
            screen.clear(); switches = 0; enCount = 0; enValid = true; prevN = 0; enAfterDouble = false; enNavOff = 0; startNewSession(); vAutoSwitchForget();
            vLanguage = atoi(buf);
            for (char* p = tab + 1; *p; p++) press(*p == '|' ? '\n' : *p);
            printf("%s\t%d\t%d\n", utf8(screen).c_str(), switches, vLanguage);
        }
        return 0;
    }
    if (argc >= 4 && strcmp(argv[1], "frames") == 0) {
        // frames <lang> <keys>: one line per keystroke: "<key>\t<text>\t<lang>" (for the website demo)
        vLanguage = atoi(argv[2]);
        for (char* p = argv[3]; *p; p++) {
            press(*p);
            printf("%c\t%s\t%d\n", *p, utf8(screen).c_str(), vLanguage);
        }
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        Uint32 keys[8] = {KEY_T, KEY_R, KEY_U, KEY_O, KEY_W, KEY_F, KEY_N, KEY_G};
        auto t0 = std::chrono::steady_clock::now();
        int n = 200000;
        for (int i = 0; i < n; i++) vAutoSwitchCheckEnglishWord(keys, 8);
        auto t1 = std::chrono::steady_clock::now();
        printf("english-word check (8 keys, replay + 2 lookups): %.2f us/word\n",
               std::chrono::duration<double, std::micro>(t1 - t0).count() / n);
        return 0;
    }
    vLanguage = atoi(argv[1]);
    for (char* p = argv[2]; *p; p++) press(*p);
    printf("%s\nswitches=%d lang=%s\n", utf8(screen).c_str(), switches, vLanguage ? "VN" : "EN");
}
