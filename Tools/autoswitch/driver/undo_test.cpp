// Backspace-undo then typing on (D32): the word put back is the current word again.
// Also: a switch or a click starts fresh, Backspace doesn't reopen a word typed before it.
// usage: undo_test   (exit 1 on a failure)   keys: '<' Backspace, '|' VI/EN switch, '!' click (Mac, Windows, Linux)
#include "Driver.h"
#include <cstdio>
#include <string>
int vLanguage = 1, vSwitchKeyStatus = 0, vCheckSpelling = 1, vUseModernOrthography = 1, vRestoreIfWrongSpelling = 1,
    vFixRecommendBrowser = 1, vUseSmartSwitchKey = 1, vOtherLanguage = 1, vAutoSwitchByTyping = 1;
static Uint16 K(char c) { static const Uint16 L[26] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    return c == ' ' ? KEY_SPACE : c == '<' ? KEY_DELETE : L[c - 'a']; }
static std::string type(const char* keys) {
    VikeyDriver d; vLanguage = 1; d.resetContext(); std::u16string t;
    for (const char* p = keys; *p; p++) {
        if (*p == '|') { d.manualSwitch(0); continue; }
        if (*p == '!') { vKeyHandleEvent(vKeyEvent::Mouse, vKeyEventState::MouseDown, 0); continue; }   //what the desktop apps send on a click
        const bool shift = *p >= 'A' && *p <= 'Z';
        const char letter = shift ? char(*p + ('a' - 'A')) : *p;
        VKEdit e = d.press(K(letter), shift, 0); for (int i = 0; i < e.deleteCount && !t.empty(); i++) t.pop_back(); t += e.text; }
    std::string o; for (char16_t c : t) { unsigned u = c; if (u < 0x80) o += (char)u; else if (u < 0x800) { o += (char)(0xC0 | u >> 6); o += (char)(0x80 | (u & 63)); } else { o += (char)(0xE0 | u >> 12); o += (char)(0x80 | ((u >> 6) & 63)); o += (char)(0x80 | (u & 63)); } }
    return o;
}
int main() {
    struct { const char* keys; const char* want; const char* why; } cases[] = {
        {"currentState ", "currentState ", "restore the full identifier at the boundary"},
        {"currentState <", "curentState", "Undo restores the pre-boundary spelling"},
        {"dessert ", "dessert ", "keep the typed word even when desert is also English"},
        {"dessert <", "desert", "Undo the doubled-key repair"},
        {"Sapa ", "Sapa ", "restore an unlisted multi-syllable literal"},
        {"Sapa <", "Sâp", "Undo the unknown-word repair"},
        {"been Momo ", "bên Momo ", "unknown name does not rewrite previous Vietnamese"},
        {"been Momo <", "bên Môm", "Undo changes only the unknown name"},
        {"booj source vaay ", "bộ source vây ", "English word that is Vietnamese: fixed"},
        {"booj source vaay <", "bộ source vaay", "Backspace puts the keys back, without the space"},
        {"booj source vaay <j ", "bộ source vậy ", "typing on: the whole word is checked again (Tony 2026-10-29)"},
        {"booj source vaay < ", "bộ source vaay ", "the same word again stays as typed"},
        {"xin |hello|<s", "xin hells", "after a switch Backspace doesn't reopen xin (was xin heín)"},
        {"heelo!vieejt", "hêloviệt", "a click on a word put back to its keys: the next word gets Telex (was vieejt)"},
    };
    int fails = 0;
    for (auto& c : cases) { std::string got = type(c.keys); if (got != c.want) { fails++; printf("FAIL: '%s' -> '%s', want '%s' (%s)\n", c.keys, got.c_str(), c.want, c.why); } }
    printf("undo after fix, switch, click: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
