#include "Core.h"
#include "Prefs.h"
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/stat.h>
#include <sys/time.h>

int vLanguage = 1, vSwitchKeyStatus = 0x7A000206, vCheckSpelling = 1, vUseModernOrthography = 1;
int vRestoreIfWrongSpelling = 1, vFixRecommendBrowser = 0, vUseSmartSwitchKey = 1, vOtherLanguage = 1;
int vAutoSwitchByTyping = 1, vPerformLayoutCompat = 0;

namespace vikey { namespace core {
static const int EMPTY_HOTKEY = 0xFE0000FE;
#define OTHER_CONTROL_KEY ((_flag & F_CONTROL) || (_flag & F_ALT) || (_flag & F_WIN))
static Output _out;
static vKeyHookState* pData = nullptr;
static unsigned _flag = 0, _lastFlag = 0;
static bool _modifierChordUsed = false;   // the modifiers held went with a click or another key: no modifier-only hotkey until all are released
static bool _hotkeyHeld = false;   // the switch hotkey's key is down (with or without its modifiers): IBus has no repeat flag, so presses before its release are auto-repeat; a click or focus_out clears it (a missed release)
static Uint16 _keycode = 0;
static std::function<void(int)> _observer;
static std::string _frontApp = "UnknownApp";
static int _languageTemp = 0;
static std::vector<Byte> savedSmartSwitchKeyData;

static Uint32 _enKeys[MAX_BUFF]; static int _enCount = 0; static bool _enValid = true, _enAfterShortcut = false, _enAfterDouble = false;
// ← then → back to the same spot inside a word: the English word is still the one being typed (D51)
static Uint32 _enNavKeys[MAX_BUFF]; static int _enNavCount = 0, _enNavPrevN = 0, _enNavOffset = 0;
static bool _enNavValid = true, _enNavAfterDouble = false;
static Uint32 _enPrevKeys[2][MAX_BUFF]; static int _enPrevCounts[2]; static int _enPrevN = 0;
static bool _enPrevRestored = false;   // _enPrevKeys[0] was put back to English by auto switch (D35)
// keys: the original word (English mode can go on typing it after an undo, D32)
struct AutoFixUndo { bool active; int removeCount; char32_t text[MAX_BUFF + 1]; int textCount; int language; Uint32 keys[MAX_BUFF]; int keyCount; };
static AutoFixUndo _autoFixUndo = {};
// after an undo the restored word stays the current word; finishing it unchanged doesn't redo the fix (D32)
static Uint32 _undoneKeys[MAX_BUFF]; static int _undoneCount = 0;
static int vEmojiHotkey = 0;   // the emoji picker's hotkey (0 = off)
static void StartEnglishWord(bool valid) { _enCount = 0; _enValid = valid; _enAfterShortcut = false; }
static void ResetEnglishWord(bool valid) { _enAfterDouble = false; _undoneCount = 0; StartEnglishWord(valid); _enPrevN = 0; _enNavOffset = 0; vAutoSwitchForget(); }   // also the D51 round trip: after a click or a switch → must not bring the old word back

// ---- Typing log --------------------------------------------------------------------------------
int vTypingLogKeys = 1, vTypingLogEnabled = 0;
static std::string _tlPending; static std::mutex _tlMutex;
static char32_t _tlWord[64]; static int _tlWordLen = 0; static int _tlWordLang = 1; static bool _tlWordTruncated = false; static bool _tlSinceAuto = false;
static std::string _tlApp;
static std::string utf8(const std::u32string& w) { std::string o; for (char32_t c : w) { if (c < 0x80) o += (char)c; else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); } else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); } } return o; }
static std::string jsonEscape(const std::string& s) { std::string o; for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += c; } return o; }
std::string typingLogPath() { return prefs::dataDir() + "/typing-log.jsonl"; }
static std::string TLTime() { struct timeval tv; gettimeofday(&tv, nullptr); struct tm t; localtime_r(&tv.tv_sec, &t); char b[64]; strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &t); char z[16]; strftime(z, sizeof z, "%z", &t); char o[96]; snprintf(o, sizeof o, "%s.%03d%c%c%c:%c%c", b, (int)(tv.tv_usec / 1000), z[0], z[1], z[2], z[3], z[4]); return o; }
static std::string TLToday() { time_t n = time(nullptr); struct tm t; localtime_r(&n, &t); char b[16]; strftime(b, sizeof b, "%Y-%m-%d", &t); return b; }
static void TLCount(const char* name) { if (!vTypingLogEnabled) return; std::string key = "TLStat_" + TLToday() + "_" + name; prefs::setInt(key.c_str(), prefs::getInt(key.c_str(), 0) + 1); }
void flushTypingLog() {
    prefs::flushStats();
    std::string chunk; { std::lock_guard<std::mutex> g(_tlMutex); chunk.swap(_tlPending); }
    if (chunk.empty()) return;
    std::string path = typingLogPath(); mkdir(prefs::dataDir().c_str(), 0700);
    struct stat st; if (stat(path.c_str(), &st) == 0 && st.st_size > 20 * 1024 * 1024) { std::string old = path.substr(0, path.size() - 6) + ".1.jsonl"; ::remove(old.c_str()); rename(path.c_str(), old.c_str()); }
    FILE* f = fopen(path.c_str(), "ab"); if (!f) return; fwrite(chunk.data(), 1, chunk.size(), f); fclose(f);
}
void clearTypingLog() { { std::lock_guard<std::mutex> g(_tlMutex); _tlPending.clear(); } std::string p = typingLogPath(); ::remove(p.c_str()); ::remove((p.substr(0, p.size() - 6) + ".1.jsonl").c_str()); }
static bool TLKeysAllowed() {
    static const char* excluded[] = { "1password", "bitwarden", "keepassxc", "keepass", "enpass", "nordpass", "gnome-terminal", "gnome-terminal-server", "konsole", "xterm", "alacritty", "kitty", "wezterm", "tilix", "terminator", "ptyxis", "org.gnome.ptyxis", "foot" };
    std::string app = _tlApp; std::transform(app.begin(), app.end(), app.begin(), ::tolower);
    for (auto e : excluded) if (app == e) return false;
    return vTypingLogKeys != 0;
}
static void TLWrite(std::string fields) {
    if (!vTypingLogEnabled) return;
    std::string line = "{" + fields + ",\"t\":\"" + TLTime() + "\",\"app\":\"" + jsonEscape(_tlApp) + "\"}\n";
    // no file write here (this runs in the IBus key callback): the 10 s flush timer writes it, and a few seconds of
    // typing is a few KB
    std::lock_guard<std::mutex> g(_tlMutex); _tlPending += line;
}
static const char* TLLang(int lang) { return lang ? "vi" : "en"; }
static void TLEndWord(const std::string& reason) {
    if (_tlWordLen == 0) return;
    bool hasLetter = false; for (int x = 0; x < _tlWordLen; x++) if (_tlWord[x] != 0x232B) { hasLetter = true; break; }
    if (hasLetter) TLCount(_tlWordLang ? "words_vi" : "words_en");
    if (hasLetter && TLKeysAllowed()) {
        std::string f = std::string("\"e\":\"word\",\"lang\":\"") + TLLang(_tlWordLang) + "\",\"keys\":\"" + jsonEscape(utf8(std::u32string(_tlWord, _tlWordLen))) + "\",\"end\":\"" + jsonEscape(reason) + "\"";
        if (_tlWordTruncated) f += ",\"truncated\":true";
        TLWrite(f);
    }
    _tlWordLen = 0; _tlWordTruncated = false;
}
static void TLAppendChar(char32_t c) { if (_tlWordLen == 0) _tlWordLang = vLanguage; if (_tlWordLen < 64) _tlWord[_tlWordLen++] = c; else _tlWordTruncated = true; }
static bool IsLetterKey(Uint16 key) { for (Uint16 k : {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z}) if (k == key) return true; return false; }
static std::string TLKeyName(Uint16 key, bool shift) {
    switch (key) { case KEY_SPACE: return "space"; case KEY_RETURN: case KEY_ENTER: return "enter"; case KEY_TAB: return "tab"; case KEY_ESC: return "esc"; case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN: return "arrow"; }
    Uint16 ch = keyCodeToCharacter(key | (shift ? CAPS_MASK : 0)); return ch ? utf8(std::u32string(1, (char32_t)ch)) : "other";
}
static void TLNoteKey(Uint16 key, unsigned flags) {
    if (!vTypingLogEnabled) return;
    _tlSinceAuto = false; bool shift = flags & F_SHIFT;
    if (flags & (F_CONTROL | F_ALT | F_WIN)) { std::string combo; if (flags & F_CONTROL) combo += "Ctrl+"; if (flags & F_ALT) combo += "Alt+"; if (shift) combo += "Shift+"; if (flags & F_WIN) combo += "Super+"; std::string k = key == KEY_DELETE ? "Backspace" : TLKeyName(key, false); std::transform(k.begin(), k.end(), k.begin(), ::toupper); combo += k; TLEndWord(combo); return; }
    bool caps = shift || (flags & F_CAPS);
    if (key == KEY_DELETE) { TLAppendChar(0x232B); return; }
    if (IsLetterKey(key) || (IS_NUMBER_KEY(key) && !shift)) { Uint16 ch = keyCodeToCharacter(key | (caps ? CAPS_MASK : 0)); if (ch) TLAppendChar((char32_t)ch); return; }
    TLEndWord(TLKeyName(key, shift));
}
static void TLAuto(const char* rule, int to) { TLCount((std::string("auto_") + rule).c_str()); TLWrite(std::string("\"e\":\"auto\",\"rule\":\"") + rule + "\",\"to\":\"" + TLLang(to) + "\""); _tlSinceAuto = true; }
static void TLManual(const char* via, int to) { TLEndWord("switch"); bool afterAuto = _tlSinceAuto; _tlSinceAuto = false; TLCount("manual"); if (afterAuto) TLCount("manual_after_auto"); TLWrite(std::string("\"e\":\"manual\",\"via\":\"") + via + "\",\"to\":\"" + TLLang(to) + "\"" + (afterAuto ? ",\"after_auto\":true" : "")); }
static void TLUndo() { TLEndWord("undo"); _tlSinceAuto = false; TLCount("undo"); TLWrite(std::string("\"e\":\"undo\",\"via\":\"backspace\",\"to\":\"") + TLLang(_autoFixUndo.language) + "\""); }
static void TLContext(int to) { TLEndWord("context"); TLCount("context"); TLWrite(std::string("\"e\":\"context\",\"to\":\"") + TLLang(to) + "\""); }

// ---- Per-app memory ----------------------------------------------------------------------------
std::string frontApp() { return _frontApp; }
static void saveSmartSwitchKeyData() { getSmartSwitchKeySaveData(savedSmartSwitchKeyData); prefs::setBinary("smartSwitchKey", savedSmartSwitchKeyData.data(), savedSmartSwitchKeyData.size()); }
static void VKLanguageChanged(bool rememberForContext) {
    if (rememberForContext && vUseSmartSwitchKey) { setAppInputMethodStatus(_frontApp, vLanguage); saveSmartSwitchKeyData(); }
    prefs::setInt("InputMethod", vLanguage);
    if (_observer) _observer(vLanguage);
}
static void ApplySmartSwitch() {
    _languageTemp = getAppInputMethodStatus(_frontApp, vLanguage);
    if ((_languageTemp & 0x01) != vLanguage) {
        if (_languageTemp != -1) { vLanguage = _languageTemp & 0x01; VKLanguageChanged(false); TLContext(vLanguage); startNewSession(); }
        else saveSmartSwitchKeyData();
    }
}
void focusChanged(const std::string& app) {
    if (!app.empty()) _frontApp = app;
    ResetEnglishWord(true); _autoFixUndo.active = false;
    _flag &= F_CAPS; _lastFlag = 0; _modifierChordUsed = false;   // after Alt+Tab the modifier releases went to the other app
    TLEndWord("app"); _tlApp = _frontApp;
    if (vUseSmartSwitchKey) ApplySmartSwitch();
    startNewSession();
}

// ---- Pause auto switch after a manual switch (D21) ------------------------------------------------
static long long nowMs() { struct timeval tv; gettimeofday(&tv, nullptr); return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000; }
static long long _autoPauseUntil = 0;
static bool AutoSwitchPaused() { return nowMs() < _autoPauseUntil; }
static void PauseAutoSwitchAfterManual() { if (prefs::has("PauseAutoAfterManual") && !prefs::getInt("PauseAutoAfterManual", 1)) return; _autoPauseUntil = nowMs() + 5000; vAutoSwitchByTyping = 0; }
static void checkPauseExpired() { if (_autoPauseUntil && !AutoSwitchPaused()) { _autoPauseUntil = 0; vAutoSwitchByTyping = prefs::getInt("AutoSwitchByTyping", 1); StartEnglishWord(false); } }

static time_t _settingsMtime = 0; static long _settingsMtimeNs = 0;
void pollSettingsFile() {
    struct stat st; if (stat(prefs::settingsPath().c_str(), &st) != 0) return;
    if (st.st_mtime == _settingsMtime && st.st_mtim.tv_nsec == _settingsMtimeNs) return;
    _settingsMtime = st.st_mtime; _settingsMtimeNs = st.st_mtim.tv_nsec;
    if (prefs::changedByOthers()) reloadSettings();   // our own saves need no reload
}
void setLanguageByUser(int lang); void setLanguageQuiet(int lang);
void command(const std::string& line) {
    if (line.rfind("lang ", 0) == 0) {
        int lang = line.find(" vi") != std::string::npos ? 1 : 0; bool user = line.find("user") != std::string::npos;
        if (user) setLanguageByUser(lang); else setLanguageQuiet(lang);
        if (vTypingLogEnabled) TLWrite(std::string("\"e\":\"cmd\",\"line\":\"") + jsonEscape(line) + "\",\"as\":" + std::to_string(vAutoSwitchByTyping) + ",\"pref\":" + std::to_string(prefs::getInt("AutoSwitchByTyping", 1)) + ",\"paused\":" + (AutoSwitchPaused() ? "true" : "false") + ",\"lang\":" + std::to_string(vLanguage));
    } else if (line == "clear-log") {
        // the settings app's Clear: the engine holds the counters (and the log lines not written yet) in memory, so
        // it clears them itself, or its next flush would write them back over the cleared files
        _tlWordLen = 0; clearTypingLog(); prefs::clearStats();
    }
}
void reloadSettings() {
    prefs::reload();
    int fileLang = prefs::getInt("InputMethod", 1) ? 1 : 0;
    if (pData && fileLang != vLanguage) { _autoFixUndo.active = false; ResetEnglishWord(true); vLanguage = fileLang; VKLanguageChanged(false); startNewSession(); }
    vLanguage = fileLang;
    vCheckSpelling = prefs::getInt("Spelling", 1); vSetCheckSpelling();
    vRestoreIfWrongSpelling = prefs::getInt("RestoreIfInvalidWord", 1);
    vUseModernOrthography = prefs::getInt("ModernOrthography", 1);
    vTelexStyle = prefs::getInt("TelexStyle", 0);   // D56: Telex / Simple Telex 1 / 2
    vUseSmartSwitchKey = prefs::getInt("UseSmartSwitchKey", 1);
    vAutoSwitchByTyping = AutoSwitchPaused() ? 0 : prefs::getInt("AutoSwitchByTyping", 1);
    vTypingLogKeys = prefs::getInt("TypingLogKeys", 1);
    vTypingLogEnabled = prefs::getInt("TypingLogEnabled", 0);
    int hotkey = prefs::getInt("SwitchKeyStatus", 0);
    vSwitchKeyStatus = hotkey ? hotkey : 0x7A000206;   // default Alt+Z
    _hotkeyHeld = false;   // the old hotkey's release won't match the new one
    vEmojiHotkey = prefs::getInt("EmojiPicker", 0) ? prefs::getInt("EmojiHotkey", 0) : 0;
    if (_observer) _observer(vLanguage);
}

// ---- Output ----------------------------------------------------------------------------------------
static void sendEngineChars() {
    std::u32string text;
    for (int k = pData->newCharCount - 1; k >= 0 && (int)text.size() < MAX_BUFF; k--) { Uint32 c = pData->charData[k]; text += (c & CHAR_CODE_MASK) ? (char32_t)(c & 0xFFFF) : (char32_t)keyCodeToCharacter(c); }
    bool sendControlKey = false;
    if (pData->code == vRestore || pData->code == vRestoreAndStartNewSession) {
        Uint16 ch = keyCodeToCharacter(_keycode | ((_flag & F_SHIFT) || ((_flag & F_CAPS) && IsLetterKey(_keycode)) ? CAPS_MASK : 0));   // Caps Lock shifts letters only ("NAS," gave "NAS<")
        if (ch != 0) text += (char32_t)ch; else sendControlKey = true;
    }
    if (pData->code == vRestoreAndStartNewSession) startNewSession();
    if (!text.empty()) _out.text(text);
    if (sendControlKey) _out.controlKey(_keycode, (_flag & F_SHIFT) != 0);
}
static void SendEngineOutput() {
    if (pData->backspaceCount > 0 && pData->backspaceCount < MAX_BUFF) _out.backspaces(pData->backspaceCount);
    sendEngineChars();
}

// ---- Hotkey / language -------------------------------------------------------------------------------
/// flags: for a key hotkey (Alt+Z) the key press's own, so Alt held and Z again switches again; for modifiers
/// alone (Ctrl+Shift) the most modifiers held before the release
static bool checkHotKey(int hotKeyData, unsigned flags, bool checkKeyCode = true) {
    if ((hotKeyData & (~0x8000)) == EMPTY_HOTKEY) return false;
    if (HAS_CONTROL(hotKeyData) ^ GET_BOOL(flags & F_CONTROL)) return false;
    if (HAS_OPTION(hotKeyData) ^ GET_BOOL(flags & F_ALT)) return false;
    if (HAS_COMMAND(hotKeyData) ^ GET_BOOL(flags & F_WIN)) return false;
    if (HAS_SHIFT(hotKeyData) ^ GET_BOOL(flags & F_SHIFT)) return false;
    if (checkKeyCode && GET_SWITCH_KEY(hotKeyData) != _keycode) return false;
    return true;
}
void toggleLanguage(const char* via) {
    PauseAutoSwitchAfterManual(); _autoFixUndo.active = false; ResetEnglishWord(true);
    vLanguage = vLanguage == 0 ? 1 : 0;
    TLManual(via, vLanguage);
    if (HAS_BEEP(vSwitchKeyStatus) && _out.switchSound) _out.switchSound(vLanguage);
    VKLanguageChanged(true); startNewSession();
}
void setLanguageByUser(int lang) { if (vLanguage == lang) return; PauseAutoSwitchAfterManual(); _autoFixUndo.active = false; ResetEnglishWord(true); vLanguage = lang; TLManual("menu", vLanguage); VKLanguageChanged(true); startNewSession(); }
void setLanguageQuiet(int lang) { _autoFixUndo.active = false; ResetEnglishWord(true); _autoPauseUntil = 0; vAutoSwitchByTyping = prefs::getInt("AutoSwitchByTyping", 1); vLanguage = lang ? 1 : 0; VKLanguageChanged(false); startNewSession(); }
int language() { return vLanguage; }
void setLanguageObserver(std::function<void(int)> f) { _observer = f; }

// ---- Auto switch -------------------------------------------------------------------------------------
static bool IsWhitespaceKey(Uint16 key) { return key == KEY_SPACE || key == KEY_RETURN || key == KEY_ENTER || key == KEY_TAB; }
static bool IsWordStartKey(Uint16 key, bool shift) { return IsWhitespaceKey(key) || (key == KEY_9 && shift) || key == KEY_LEFT_BRACKET || key == KEY_QUOTE; }
static char32_t CharacterOf(Uint32 data) { return (data & CHAR_CODE_MASK) ? (char32_t)(data & 0xFFFF) : (char32_t)keyCodeToCharacter(data); }
static void RememberAutoFix(int correctedCount) {
    _autoFixUndo.active = false;
    if (vAutoSwitchUndoCount <= 0 || correctedCount <= 0) return;
    for (int x = 0; x < vAutoSwitchUndoCount; x++) if ((vAutoSwitchUndoData[x] & 0xFFFF) == KEY_SPACE && !(vAutoSwitchUndoData[x] & CHAR_CODE_MASK)) return;
    Uint16 breakChar = keyCodeToCharacter(_keycode | ((_flag & F_SHIFT) || ((_flag & F_CAPS) && IsLetterKey(_keycode)) ? CAPS_MASK : 0));   // Caps Lock shifts letters only (Space + CAPS_MASK is 0)
    if (breakChar == 0) return;
    int n = 0; for (int x = 0; x < vAutoSwitchUndoCount && n < MAX_BUFF; x++) { char32_t c = CharacterOf(vAutoSwitchUndoData[x]); if (c == 0) return; _autoFixUndo.text[n++] = c; }
    _autoFixUndo.textCount = n;
    _autoFixUndo.keyCount = 0;
    for (int x = 0; x < vAutoSwitchUndoCount && x < MAX_BUFF; x++) {
        if (vAutoSwitchUndoData[x] & CHAR_CODE_MASK) { _autoFixUndo.keyCount = 0; break; }   // shown text, not keys
        _autoFixUndo.keys[_autoFixUndo.keyCount++] = vAutoSwitchUndoData[x];
    }
    _autoFixUndo.removeCount = correctedCount + 1; _autoFixUndo.language = vLanguage; _autoFixUndo.active = true;
}
static bool UndoAutoFixWithBackspace() {
    if (!_autoFixUndo.active) return false;
    _autoFixUndo.active = false;
    _out.backspaces(_autoFixUndo.removeCount); _out.text(std::u32string(_autoFixUndo.text, _autoFixUndo.textCount));
    TLUndo();
    if (vLanguage != _autoFixUndo.language) { vLanguage = _autoFixUndo.language; VKLanguageChanged(true); }
    startNewSession(); ResetEnglishWord(false);   // the cursor is at the end of a word we didn't see typed
    // D32: back in English with the typed keys on screen, they are the current word again, so typing on
    // ("vaay" + "j") is checked as a whole ("vaayj" -> "vậy"); the same word again stays as typed
    if (vLanguage == 0 && _autoFixUndo.keyCount > 0) {
        memcpy(_enKeys, _autoFixUndo.keys, _autoFixUndo.keyCount * sizeof(Uint32)); _enCount = _autoFixUndo.keyCount; _enValid = true;
        memcpy(_undoneKeys, _autoFixUndo.keys, _autoFixUndo.keyCount * sizeof(Uint32)); _undoneCount = _autoFixUndo.keyCount;
    }
    return true;
}
static void AutoSwitchTo(int language, const char* rule) { if (vLanguage == language) return; TLAuto(rule, language); vLanguage = language; VKLanguageChanged(true); startNewSession(); vAutoSwitchForget(); }
static void ApplyAutoSwitchToEnglish(int correctedCount, bool atWordBreak) {
    vAutoSwitchRequest = -1;
    if (atWordBreak) RememberAutoFix(correctedCount); else _autoFixUndo.active = false;
    AutoSwitchTo(0, atWordBreak ? "vi_word_to_en" : "double_key");
    ResetEnglishWord(atWordBreak && IsWhitespaceKey(_keycode));
    // D35: the word just put back to English is the previous word for look-back ("em is" + "ạ" -> "em í ạ")
    if (atWordBreak && _keycode == KEY_SPACE && vAutoSwitchRestoredCount > 0) {
        memcpy(_enPrevKeys[0], vAutoSwitchRestoredKeys, vAutoSwitchRestoredCount * sizeof(Uint32)); _enPrevCounts[0] = vAutoSwitchRestoredCount; _enPrevN = 1; _enPrevRestored = true;
    }
    _enAfterDouble = !atWordBreak;   // at its end, "mixx" may become "mix"
}
static bool HandleEnglishKeyForAutoSwitch() {
    if (OTHER_CONTROL_KEY) { ResetEnglishWord(true); _enAfterShortcut = (_flag & (F_CONTROL | F_WIN)) != 0; return false; }
    bool shift = _flag & F_SHIFT; bool caps = shift || (_flag & F_CAPS);
    if ((_keycode == KEY_LEFT || _keycode == KEY_RIGHT) && !shift) {   // D51: ← → round trip inside a word
        if (_keycode == KEY_LEFT && _enNavOffset == 0 && (_enCount > 0 || _enPrevN > 0)) {
            memcpy(_enNavKeys, _enKeys, _enCount * sizeof(Uint32)); _enNavCount = _enCount; _enNavValid = _enValid;
            _enNavAfterDouble = _enAfterDouble; _enNavPrevN = _enPrevN; _enNavOffset = -1;
            StartEnglishWord(false); _enAfterDouble = false; _enPrevN = 0;
            return false;
        }
        if (_enNavOffset < 0) {
            if (_keycode == KEY_LEFT) _enNavOffset--;
            else if (++_enNavOffset == 0) {   // back at the word's end: it's that word again
                memcpy(_enKeys, _enNavKeys, _enNavCount * sizeof(Uint32)); _enCount = _enNavCount; _enValid = _enNavValid;
                _enAfterDouble = _enNavAfterDouble; _enPrevN = _enNavPrevN;
            }
            return false;
        }
    }
    _enNavOffset = 0;
    if (IsLetterKey(_keycode)) { if (_enCount < MAX_BUFF - 2) _enKeys[_enCount++] = _keycode | (caps ? CAPS_MASK : 0); else _enValid = false; _enAfterShortcut = false; return false; }
    if (_keycode == KEY_DELETE) { if (_enCount == 0) { _enPrevN = 0; _enAfterDouble = false; } if (_enCount > 0) _enCount--; else if (!_enAfterShortcut) _enValid = false; return false; }
    if (IS_NUMBER_KEY(_keycode) && !shift) { _enValid = _enCount == 0 && _enValid; _enCount = 0; _enPrevN = 0; _enAfterDouble = false; return false; }   // D33: digits after a space start a word of their own ("5trawm" -> "5trăm"); glued to letters, a code
    bool wordEnd = _keycode == KEY_SPACE || _keycode == KEY_COMMA || _keycode == KEY_DOT || _keycode == KEY_SEMICOLON || _keycode == KEY_RETURN || _keycode == KEY_ENTER || (shift && (_keycode == KEY_SLASH || _keycode == KEY_1 || _keycode == KEY_QUOTE || _keycode == KEY_0));
    int count = _enCount; bool valid = _enValid; bool afterDouble = _enAfterDouble; _enAfterDouble = false;
    if (afterDouble && wordEnd && vAutoSwitchFixDoubledKey(_enKeys, count)) { StartEnglishWord(IsWordStartKey(_keycode, shift)); _enPrevN = 0; int correctedCount = pData->newCharCount; SendEngineOutput(); RememberAutoFix(correctedCount); return true; }
    StartEnglishWord(IsWordStartKey(_keycode, shift));
    bool checked = wordEnd && valid && count >= 2;
    if (_undoneCount > 0) {   // D32: the word just put back by Backspace, finished unchanged: keep it as typed
        if (count == _undoneCount && !memcmp(_enKeys, _undoneKeys, count * sizeof(Uint32))) checked = false;
        if (wordEnd) _undoneCount = 0;
    }
    const Uint32* previous[2] = {_enPrevKeys[0], _enPrevKeys[1]};
    vAutoSwitchPrevRestored = _enPrevRestored && _enPrevN >= 1;
    bool converted = checked && vAutoSwitchCheckEnglishWord(_enKeys, count, previous, _enPrevCounts, _enPrevN);
    if (!converted && _keycode == KEY_SPACE && valid && count >= 1) { _enPrevRestored = false; memcpy(_enPrevKeys[1], _enPrevKeys[0], sizeof(_enPrevKeys[0])); _enPrevCounts[1] = _enPrevCounts[0]; memcpy(_enPrevKeys[0], _enKeys, count * sizeof(Uint32)); _enPrevCounts[0] = count; if (_enPrevN < 2) _enPrevN++; }
    else _enPrevN = 0;
    if (!converted) return false;
    int correctedCount = pData->newCharCount; SendEngineOutput(); RememberAutoFix(correctedCount); AutoSwitchTo(1, "en_word_to_vi");
    return true;
}

// ---- Entry points ------------------------------------------------------------------------------------
bool modifier(unsigned bit, bool down) {
    checkPauseExpired();
    if (bit == F_CAPS) { if (down) _flag |= F_CAPS; else _flag &= ~F_CAPS; return false; }
    if (down) _flag |= bit; else _flag &= ~bit;
    if (_modifierChordUsed) {   // Ctrl+Alt+Shift+F5, then letting go of Alt and Shift one by one, must not switch
        _lastFlag = 0;
        if (!(_flag & (F_SHIFT | F_CONTROL | F_ALT | F_WIN))) _modifierChordUsed = false;
    } else if (_lastFlag == 0 || _lastFlag < _flag) _lastFlag = _flag;
    else if (_lastFlag > _flag) {
        // only a hotkey without a key: a key hotkey fired on its key-down, and _keycode is just the last key typed
        bool hit = GET_SWITCH_KEY(vSwitchKeyStatus) == 0xFE && checkHotKey(vSwitchKeyStatus, _lastFlag, false);
        _lastFlag = 0;
        if (hit) { toggleLanguage("hotkey"); return true; }
    }
    return false;
}
static void cancelModifierHotkey() { _lastFlag = 0; if (_flag & (F_SHIFT | F_CONTROL | F_ALT | F_WIN)) _modifierChordUsed = true; }
void otherKeyDown() { cancelModifierHotkey(); _hotkeyHeld = false; }   // Ctrl+Shift+F5: releasing Ctrl+Shift afterwards isn't the hotkey
void keyUp(Uint16 key) { if (key == GET_SWITCH_KEY(vSwitchKeyStatus)) _hotkeyHeld = false; }
bool keyDown(Uint16 key, bool shift, unsigned flags) {
    checkPauseExpired();
    _flag = (flags & ~(unsigned)F_SHIFT) | (shift ? F_SHIFT : 0);
    _keycode = key;
    // emoji picker hotkey: a key with exactly these modifiers
    if (vEmojiHotkey && GET_SWITCH_KEY(vEmojiHotkey) == _keycode && HAS_CONTROL(vEmojiHotkey) == GET_BOOL(_flag & F_CONTROL) && HAS_OPTION(vEmojiHotkey) == GET_BOOL(_flag & F_ALT) &&
        HAS_COMMAND(vEmojiHotkey) == GET_BOOL(_flag & F_WIN) && HAS_SHIFT(vEmojiHotkey) == GET_BOOL(_flag & F_SHIFT)) { cancelModifierHotkey(); if (_out.openEmoji) _out.openEmoji(); return true; }
    if (GET_SWITCH_KEY(vSwitchKeyStatus) != _keycode) otherKeyDown();
    else {
        bool repeat = _hotkeyHeld; _hotkeyHeld = true;   // any press, matched or not: Alt+Shift+Z, then Shift up, mustn't switch on a repeat (the Mac's autorepeat flag)
        if (checkHotKey(vSwitchKeyStatus, flags)) {   // flags, not _flag: Caps Lock's uppercase keysym isn't Shift
            if (repeat) return true;   // holding Alt+Z: switch once per press, swallow the auto-repeat
            toggleLanguage("hotkey"); _lastFlag = 0; return true;
        }
    }
    if (_keycode == KEY_DELETE && _autoFixUndo.active && !(OTHER_CONTROL_KEY)) { if (UndoAutoFixWithBackspace()) return true; }
    _autoFixUndo.active = false;
    TLNoteKey(_keycode, _flag);
    if (vLanguage == 0) return vAutoSwitchByTyping && HandleEnglishKeyForAutoSwitch();
    vAutoSwitchRequest = -1;
    vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, _keycode, (_flag & F_SHIFT) ? 1 : ((_flag & F_CAPS) ? 2 : 0), OTHER_CONTROL_KEY);
    if (vTypingLogEnabled && getenv("VIKEY_DEBUG")) TLWrite("\"e\":\"key\",\"k\":" + std::to_string(_keycode) + ",\"flag\":" + std::to_string(_flag) + ",\"code\":" + std::to_string(pData->code) + ",\"bs\":" + std::to_string(pData->backspaceCount) + ",\"nc\":" + std::to_string(pData->newCharCount) + ",\"req\":" + std::to_string(vAutoSwitchRequest));
    if (pData->code == vDoNothing) return false;
    if (pData->code == vWillProcess || pData->code == vRestore || pData->code == vRestoreAndStartNewSession) {
        bool autoSwitchAtBreak = vAutoSwitchRequest == 0 && !IsLetterKey(_keycode);
        int correctedCount = pData->newCharCount;
        SendEngineOutput();
        if (vAutoSwitchRequest == 0) ApplyAutoSwitchToEnglish(correctedCount, autoSwitchAtBreak);
        return true;
    }
    return false;
}
void resetTyping() { ResetEnglishWord(true); _autoFixUndo.active = false; startNewSession(); }
void insertText(const std::u32string& text) { resetTyping(); if (!text.empty()) _out.text(text); }
void click() { cancelModifierHotkey(); _hotkeyHeld = false; _autoFixUndo.active = false; _tlSinceAuto = false; TLEndWord("click"); if (vTypingLogEnabled) TLWrite("\"e\":\"reset\""); if (vLanguage == 0) ResetEnglishWord(true); else vKeyHandleEvent(vKeyEvent::Mouse, vKeyEventState::MouseDown, 0); }
void init(Output out) {
    _out = out;
    pData = (vKeyHookState*)vKeyInit();
    reloadSettings();
    std::vector<unsigned char> data = prefs::getBinary("smartSwitchKey");
    initSmartSwitchKey(data.data(), (int)data.size());
    _tlApp = _frontApp;
}
void shutdown() { flushTypingLog(); }
}}
