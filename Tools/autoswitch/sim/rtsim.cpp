// Realistic typing: types each message of a corpus twice into the real engine (through sim.cpp's copy of the Mac
// app's logic) — once cleanly, once like a person: typos fixed with Backspace, Backspace into the last word, the
// cursor moved away and back (also mid-word), clicks, an edit somewhere earlier, a ⌥Z round trip with an English
// word in between. The person looks at the screen after each slip and retypes the word if it isn't what they meant.
//
// Checks (each failure prints the message and the actions that led to it):
//   guard     a key deleted text from before the last cursor move / click / switch (the engine edited text it
//             never wrote: "xin ⌥Z hello ⌥Z ⌫ s" gave "xin heín", D49)
//   elsewhere typing at an earlier spot didn't insert exactly what was typed there
//   stuck     the person couldn't get the word right even by deleting and retyping it in its language (auto switch
//             keeps disagreeing, or the corpus label is off); check.sh locks the count instead of failing on it
// Metric: how often a slip needed the whole word retyped (friction), per kind of slip.
//
// Build (from Tools/autoswitch):  clang++ -std=c++17 -O2 -w -DAS_TESTING -I../../Engine -I../../Engine/platforms \
//     -o sim/rtsim sim/rtsim.cpp ../../Engine/Engine.cpp ../../Engine/Vietnamese.cpp ../../Engine/SmartSwitchKey.cpp
// Run: sim/rtsim units.txt [--seed N] [--show N] [--only MSG] [--trace]
#define main sim_main
#include "sim.cpp"
#undef main
#include <random>
#include <map>

static U tail;                 // text after the cursor
static size_t guardPos = 0;    // P.screen[0..guardPos) was there before the last cursor move / click / switch
static std::string acts;       // what happened in this message, for the report
static bool tracing = false;

enum Kind { TYPO, PREVWORD, CURSOR, CLICK, ELSEWHERE, SWITCHTRIP, PUNCT, NKINDS };
static const char* kindName[NKINDS] = {"typo", "back-into-word", "cursor-trip", "click", "edit-elsewhere", "switch-trip", "stray-punct"};
struct Stat { long ops = 0, retyped = 0; };
static Stat stats[NKINDS];
static long guardFails = 0, elsewhereFails = 0, stuck = 0, msgNo = 0;
static int showLeft = 5;
static int whyKind = -1;          // --why KIND: show the slips of this kind that needed fixing

static U text() { return P.screen + tail; }
static void note(const std::string& s) { acts += s; }

static void report(const char* what, const std::string& detail) {
    if (showLeft-- <= 0) return;
    fprintf(stderr, "%s (message %ld): %s\n   actions: %s\n", what, msgNo, detail.c_str(), acts.c_str());
}

/** A key the person presses. Backspace may take the char before the cursor; nothing else may reach before guardPos. */
static void key(Uint16 k, bool shift) {
    size_t before = P.screen.size();
    U old = P.screen;
    press(k, shift);
    size_t common = 0; while (common < old.size() && common < P.screen.size() && old[common] == P.screen[common]) common++;
    size_t floor = k == KEY_DELETE && before > 0 ? std::min(guardPos, before - 1) : guardPos;
    if (common < floor) {
        guardFails++;
        report("GUARD", "a key changed text from before the last cursor move/click/switch: '" + utf8(old) + "' -> '" + utf8(P.screen) + "'");
    }
    if (k == KEY_DELETE && P.screen.size() < guardPos) guardPos = P.screen.size();
    if (tracing) fprintf(stderr, "  key %d%s -> '%s|%s'\n", k, shift ? "^" : "", utf8(P.screen).c_str(), utf8(tail).c_str());
}
static void typeKeys(const std::string& s) { for (char c : s) { Uint16 k; bool sh; if (keyFor((unsigned char)c, k, sh)) key(k, sh); } }
static void backspace(int n) { for (int i = 0; i < n; i++) { key(KEY_DELETE, false); note("⌫"); } }

/** Mac app on a click: VikeyCore's mouse-down (a new session; English mode forgets its word). */
static void click() {
    if (vLanguage == 1) vKeyHandleEvent(vKeyEvent::Mouse, vKeyEventState::MouseDown, 0);
    else { P.enCount = 0; P.enValid = true; P.prevN = 0; P.enAfterDouble = false; P.navOffset = 0; vAutoSwitchForget(); }
    guardPos = P.screen.size();
}
static void arrow(bool left) {
    press(left ? KEY_LEFT : KEY_RIGHT, false);
    if (left && !P.screen.empty()) { tail.insert(tail.begin(), P.screen.back()); P.screen.pop_back(); }
    if (!left && !tail.empty()) { P.screen.push_back(tail.front()); tail.erase(tail.begin()); }
    guardPos = P.screen.size();
}
static void switchKey() { hotkey(); guardPos = P.screen.size(); note("⌥Z"); }

static void resetAll() {
    vKeyInit(); startNewSession(); vAutoSwitchForget();
    P = Platform(); tail.clear(); guardPos = 0; vLanguage = 1;
}

struct K { Uint16 k; bool shift; char c; };
static std::vector<K> keysOf(const std::vector<Unit>& msg, std::vector<int>& wordStart) {
    std::vector<K> out;
    for (const Unit& u : msg) {
        int start = (int)out.size();
        for (char c : u.keys) { Uint16 k; bool sh; if (keyFor((unsigned char)c, k, sh)) out.push_back({k, sh, c}); }
        for (size_t i = start; i < out.size(); i++) wordStart.push_back(start);
        for (char c : u.seps) { Uint16 k; bool sh; if (keyFor((unsigned char)c, k, sh)) { out.push_back({k, sh, c}); wordStart.push_back((int)out.size()); } }
    }
    return out;
}

/** A letter without its Vietnamese marks (đ stays d); 0 for anything else. */
static char plain(char16_t c) {
    if (c < 128) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ? (char)c : 0;
    static const char16_t* groups[] = {u"àáảãạăằắẳẵặâầấẩẫậ", u"èéẻẽẹêềếểễệ", u"ìíỉĩị", u"òóỏõọôồốổỗộơờớởỡợ", u"ùúủũụưừứửữự", u"ỳýỷỹỵ", u"đ",
                                       u"ÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬ", u"ÈÉẺẼẸÊỀẾỂỄỆ", u"ÌÍỈĨỊ", u"ÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢ", u"ÙÚỦŨỤƯỪỨỬỮỰ", u"ỲÝỶỸỴ", u"Đ"};
    static const char base[] = "aeiouydAEIOUYD";
    for (int g = 0; g < 14; g++) for (const char16_t* p = groups[g]; *p; p++) if (*p == c) return base[g];
    return 0;
}

static char16_t upperVN(char16_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE) return c - 0x20;                                   // à á â ã è é ê ì í ò ó ô õ ù ú ý
    if (c >= 0x1EA0 && c <= 0x1EF9) return (c & 1) ? c - 1 : c;                    // ạ ả ấ … (pairs: upper even)
    if (c == 0x103 || c == 0x111 || c == 0x129 || c == 0x169 || c == 0x1A1 || c == 0x1B0) return c - 1;   // ă đ ĩ ũ ơ ư
    return c;
}

/** The Telex keys a person types to get one character back ("ó" -> "os", "ư" -> "uw", "đ" -> "dd"). */
static std::string telexFor(char16_t c) {
    static const char16_t* forms[] = {u"aàáảãạ", u"ăằắẳẵặ", u"âầấẩẫậ", u"eèéẻẽẹ", u"êềếểễệ", u"iìíỉĩị", u"oòóỏõọ", u"ôồốổỗộ",
                                      u"ơờớởỡợ", u"uùúủũụ", u"ưừứửữự", u"yỳýỷỹỵ"};
    static const char* keys[] = {"a", "aw", "aa", "e", "ee", "i", "o", "oo", "ow", "u", "uw", "y"};
    static const char* tones[] = {"", "f", "s", "r", "x", "j"};
    if (c == u'đ') return "dd";
    if (c == u'Đ') return "DD";
    for (int upper = 0; upper < 2; upper++)
        for (int g = 0; g < 12; g++) for (int t = 0; t < 6; t++) {
            char16_t f = forms[g][t];
            if (upper) f = upperVN(f);
            if (f == c) { std::string k = keys[g]; if (upper) k[0] = (char)toupper(k[0]); return k + tones[t]; }
        }
    if (c < 128) return std::string(1, (char)c);
    return "";
}

static const char* rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
static char neighbour(char c, std::mt19937& rng) {
    for (int r = 0; r < 3; r++) for (int i = 0; rows[r][i]; i++) if (rows[r][i] == c) {
        std::vector<char> n;
        if (i > 0) n.push_back(rows[r][i - 1]);
        if (rows[r][i + 1]) n.push_back(rows[r][i + 1]);
        if (r > 0 && i < (int)strlen(rows[r - 1])) n.push_back(rows[r - 1][i]);
        if (r < 2 && i < (int)strlen(rows[r + 1])) n.push_back(rows[r + 1][i]);
        return n[rng() % n.size()];
    }
    return 'x';
}


/** Puts the screen back to `want` the way a person would (delete back to where it differs, type the rest; here the
 *  rest is pasted), with the cursor at the end, then a click: the person carries on from a fresh start. */
static void restoreTo(const U& want) {
    while (!tail.empty()) arrow(false);
    size_t keep = 0; while (keep < P.screen.size() && keep < want.size() && P.screen[keep] == want[keep]) keep++;
    backspace((int)(P.screen.size() - keep));
    P.screen = P.screen.substr(0, keep) + want.substr(keep);
    click();
}

/** After a word with a slip in it: the word (and what follows it) must read as meant. If not, the person deletes it
 *  and types it again; if that still isn't right, they switch the language by hand and type it again. */
static void checkWord(const Unit& u, const U& base, const U& meant, const std::string& keys, int slip) {
    auto ok = [&]() { U t = text(); return tail.empty() && t.size() >= meant.size() && t.compare(t.size() - meant.size(), meant.size(), meant) == 0
                                             && t.size() - meant.size() == base.size(); };
    if (ok()) return;
    stats[slip].retyped++;
    if (whyKind == slip) report("RETYPE", std::string(kindName[slip]) + ": got '" + utf8(text().substr(base.size() > 30 ? base.size() - 30 : 0)) + "' want '..." + utf8(meant) + "'");
    for (int attempt = 0; attempt < 3; attempt++) {
        restoreTo(base);
        int need = u.isvn ? 1 : 0;
        note(attempt == 0 ? "[retype]" : attempt == 1 ? "[⌥Z retype]" : "[retype, ⌫ undo]");
        if (attempt >= 1 && vLanguage != need) switchKey();             // the language this word is in
        if (attempt < 2) { typeKeys(keys); if (ok()) return; continue; }
        // auto switch still changes it: Backspace right after the fix puts the word back as typed (D32), then
        // the person types what came after it again
        typeKeys(keys); if (ok()) return;
        backspace(1); typeKeys(u.seps);
        if (ok()) return;
    }
    stuck++;
    report("STUCK", std::string(kindName[slip]) + ": got '" + utf8(text()) + "' want '..." + utf8(meant) + "'");
    restoreTo(base + meant);
}

/** An edit somewhere else that the person undoes again: the text must be back to what it was. */
static void neutral(Kind kind, const U& before) {
    stats[kind].ops++;
    if (tail.empty() && text() == before) return;
    stats[kind].retyped++;
    if (whyKind == kind) report("RETYPE", std::string(kindName[kind]) + ": got '" + utf8(text().substr(before.size() > 40 ? before.size() - 40 : 0)) + "' want '" + utf8(before.substr(before.size() > 40 ? before.size() - 40 : 0)) + "'");
    restoreTo(before);
}

int main(int argc, char** argv) {
    H = (vKeyHookState*)vKeyInit();
    unsigned seed = 1; int only = -1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (unsigned)atoi(argv[++i]);
        if (!strcmp(argv[i], "--show") && i + 1 < argc) showLeft = atoi(argv[++i]);
        if (!strcmp(argv[i], "--only") && i + 1 < argc) only = atoi(argv[++i]);
        if (!strcmp(argv[i], "--trace")) tracing = true;
        if (!strcmp(argv[i], "--why") && i + 1 < argc) { for (int k = 0; k < NKINDS; k++) if (!strcmp(argv[i + 1], kindName[k])) whyKind = k; i++; }
    }
    std::vector<std::vector<Unit>> msgs(1);
    FILE* f = fopen(argv[1], "r"); char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == 'W') { char* k = line + 2; char* t = strchr(k, '\t'); if (!t) continue; *t = 0; msgs.back().push_back({k, t[1] == '1', "", true}); }
        else if (line[0] == 'N') { msgs.push_back({}); }
        else { if (msgs.back().empty()) msgs.back().push_back({"", false, "", false}); msgs.back().back().seps += std::string(line + 2); }
    }
    fclose(f);
    const char* english[] = {"hello", "ok", "meeting", "deadline", "test", "sorry", "file", "update"};
    long words = 0;

    for (auto& msg : msgs) {
        msgNo++;
        if (msg.empty() || (only >= 0 && msgNo != only)) continue;
        std::mt19937 rng(seed * 7919u + (unsigned)msgNo);
        resetAll(); std::vector<U> meant; for (const Unit& u : msg) meant.push_back(golden(u));   // what each word should read
        resetAll(); acts.clear();
        for (size_t ui = 0; ui < msg.size(); ui++) {
            const Unit& u = msg[ui];
            // between words: Backspace into the last word, an edit earlier in the text, or a ⌥Z round trip
            int dice = (int)(rng() % 1000);
            bool afterSpace = ui > 0 && !msg[ui - 1].seps.empty() && msg[ui - 1].seps.back() == ' ' && msg[ui - 1].word && !P.screen.empty();
            if (afterSpace && u.word && dice < 50 && P.screen.size() >= 2 && !telexFor(P.screen[P.screen.size() - 2]).empty()) {
                U before = text(); std::string again = telexFor(P.screen[P.screen.size() - 2]);
                note("{back:"); backspace(2);
                acts += again; typeKeys(again);
                key(KEY_SPACE, false); note("␣}");
                neutral(PREVWORD, before);
            } else if (afterSpace && u.word && dice < 60 && P.screen.size() > 6) {
                size_t sp = P.screen.rfind(u' ', P.screen.size() - 2);
                if (sp != U::npos && sp > 0) {
                    U before = text(); int n = (int)(P.screen.size() - sp - 1); note("{elsewhere←" + std::to_string(n) + " ok␣⌫⌫⌫→}");
                    for (int i = 0; i < n; i++) arrow(true);
                    typeKeys("ok ");
                    U want = before.substr(0, sp + 1) + u"ok " + before.substr(sp + 1);
                    if (text() != want) { elsewhereFails++; report("ELSEWHERE", "got '" + utf8(text()) + "' want '" + utf8(want) + "'"); }
                    backspace(3);
                    for (int i = 0; i < n; i++) arrow(false);
                    neutral(ELSEWHERE, before);
                }
            } else if (afterSpace && u.word && dice >= 80 && dice < 100) {   // a stray - . , / ; after the space, deleted again
                U before = text(); const char* marks = "-.,/;"; char m = marks[rng() % 5];
                note(std::string("{stray:") + m + "⌫}"); typeKeys(std::string(1, m));
                backspace(tail.empty() && text().size() > before.size() ? (int)(text().size() - before.size()) : 1);
                neutral(PUNCT, before);
            } else if (afterSpace && u.word && dice < 80) {
                U before = text(); const char* w = english[rng() % 8];
                switchKey(); note(w); typeKeys(w); switchKey();
                backspace(tail.empty() && text().size() > before.size() ? (int)(text().size() - before.size()) : (int)strlen(w));   // what it shows
                neutral(SWITCHTRIP, before);
            }
            // the word itself, maybe with a slip: a neighbouring key fixed with Backspace, the cursor moved away and
            // back, or a click
            U base = text(); int slip = -1;
            for (char c : u.keys) {
                Uint16 kk; bool sh; if (!keyFor((unsigned char)c, kk, sh)) continue;
                int d = (int)(rng() % 1000);
                bool letter = c >= 'a' && c <= 'z';
                if (letter && d < 40) {
                    U was = text();
                    int n = 1 + (rng() % 4 == 0); note("{typo:");
                    for (int i = 0; i < n; i++) { char t = neighbour(c, rng); acts += t; key(letterKey(t), false); }
                    note("}"); backspace(n); slip = TYPO; stats[TYPO].ops++;
                    // the person looks: Backspace took a whole letter ("thá" ⌫ -> "th"), so they type back what's missing
                    if (text() != was && tail.empty()) {
                        size_t same = 0; U t = text(); while (same < t.size() && same < was.size() && t[same] == was[same]) same++;
                        if (same >= base.size()) {
                            backspace((int)(t.size() - same));
                            std::string again; for (size_t i = same; i < was.size(); i++) again += telexFor(was[i]);
                            note("[" + again + "]"); typeKeys(again);
                        }
                    }
                } else if (d >= 990) {
                    int n = 1 + rng() % 4; note("{←→" + std::to_string(n) + "}");
                    size_t g = guardPos;
                    for (int i = 0; i < n; i++) arrow(true);
                    for (int i = 0; i < n; i++) arrow(false);
                    // back where it was: the word being typed may still change (D51 keeps it), text before it may not
                    { size_t sp = P.screen.find_last_of(u" \n"); guardPos = std::min(g, sp == U::npos ? (size_t)0 : sp + 1); }
                    slip = CURSOR; stats[CURSOR].ops++;
                } else if (d >= 985) {
                    note("{click}"); click(); slip = CLICK; stats[CLICK].ops++;
                }
                key(kk, sh); acts += c;
            }
            typeKeys(u.seps); for (char c : u.seps) acts += c == '\n' ? '|' : c;
            if (u.word) words++;
            if (slip >= 0 && u.word) checkWord(u, base, meant[ui], u.keys + u.seps, slip);
        }
    }
    printf("realistic typing: %ld messages, %ld words | guard %ld  elsewhere %ld  stuck %ld\n", msgNo, words, guardFails, elsewhereFails, stuck);
    printf("  slips that needed fixing by hand:");
    for (int i = 0; i < NKINDS; i++) printf("  %s %ld/%ld", kindName[i], stats[i].retyped, stats[i].ops);
    printf("\n");
    return (guardFails || elsewhereFails || stuck) ? 1 : 0;
}
