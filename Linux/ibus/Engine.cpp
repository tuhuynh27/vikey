// ibus-engine-vikey: the IBus engine. Keys arrive in process_key_event, the core decides, and the
// result goes back as deleted characters + committed text (no pre-edit: the text is typed straight into
// the app, like on macOS and Windows), except in apps that never tell IBus the text around the cursor
// (terminals, Zed, Ghostty, kitty): there the last words are kept as pre-edit until they are final (#24).
// The panel shows V / E and a menu like the Mac menu bar.
#include <ibus.h>
#include <glib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <ctime>
#include <glob.h>
#include "Core.h"
#include "Prefs.h"
#include "Crash.h"
#include "KeyMap.h"
#include "Surrounding.h"
#ifdef VIKEY_HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#endif
#ifndef VIKEY_VERSION
#define VIKEY_VERSION "0.0.0"
#endif
using namespace vikey;

typedef struct { IBusEngine parent; guint caps; gboolean enabled; IBusPropList* props; IBusProperty *pLang, *pAuto, *pOpen, *pLog, *pGuide, *pUpdate, *pAbout; guint64 shown; } VikeyEngine;   // shown: what the panel has (0 = unknown)
typedef struct { IBusEngineClass parent; } VikeyEngineClass;
static GType vikey_engine_get_type();
#define VIKEY_TYPE_ENGINE (vikey_engine_get_type())
static VikeyEngine* _active = nullptr;   // the engine with the focus
static gint64 _lastOutputUs = 0;
static bool _appPending = false;          // focus_in can come before X has moved the focus: re-check on the first key          // when we last let a key through or committed text (GTK resets the IM context right after inserting)
static IBusBus* _bus = nullptr;
static IBusEngineClass* _parentClass = nullptr;

static std::string utf8(const std::u32string& w) { std::string o; for (char32_t c : w) { if (c < 0x80) o += (char)c; else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); } else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); } } return o; }

// ---- output to the app ---------------------------------------------------------------------------
// Two ways to change text the app already has:
//  * direct: delete_surrounding_text + commit_text, for apps that report the text around the cursor;
//  * pre-edit: for apps that never do (terminals, Zed). On Wayland the desktop (GNOME Shell, KWin) relays IBus for
//    every app and always claims SURROUNDING_TEXT, but these apps ignore the delete (the reporter saw "goõ" for
//    "gõ") or crash on it (GNOME Terminal 3.52 on Ubuntu 24.04); forwarded BackSpace keys are dropped on Wayland.
//    So the engine keeps the current word and the two before it (what auto switch may still rewrite) as pre-edit,
//    commits what is older, and commits everything before any key it lets through (Enter, Tab, arrows, shortcuts).
// Which one: an app that supports it sends its surrounding text (set_surrounding_text) after it gets the focus;
// one that never does uses pre-edit.
static bool _surroundingSeen = false;      // the focused app has sent its surrounding text since focus_in
static int _reportedCursor = -1;           // where it last said the cursor is (-1: not since focus_in)
static const bool _clampDeletes = surroundingClampNeeded(getenv("XDG_SESSION_TYPE"), getenv("XDG_CURRENT_DESKTOP"));   // GNOME Wayland (#32)
static std::u32string _pre;                // pre-edit text not committed yet
static bool preeditMode() {
    if (!_active || !(_active->caps & IBUS_CAP_PREEDIT_TEXT)) return false;
    return !(_active->caps & IBUS_CAP_SURROUNDING_TEXT) || !_surroundingSeen;
}
static void showPre() {
    if (!_active) return;
    IBusText* t = ibus_text_new_from_string(utf8(_pre).c_str());
    if (!_pre.empty()) ibus_text_append_attribute(t, IBUS_ATTR_TYPE_UNDERLINE, IBUS_ATTR_UNDERLINE_SINGLE, 0, (guint)_pre.size());
    // COMMIT: if the app resets or loses the focus, it keeps the pre-edit text as typed
    ibus_engine_update_preedit_text_with_mode(IBUS_ENGINE(_active), t, (guint)_pre.size(), !_pre.empty(), IBUS_ENGINE_PREEDIT_COMMIT);
}
static void commitRaw(const std::u32string& t) { if (_active && !t.empty()) ibus_engine_commit_text(IBUS_ENGINE(_active), ibus_text_new_from_string(utf8(t).c_str())); }
static void flushPre() {
    if (_pre.empty()) return;
    std::u32string t = _pre; _pre.clear(); commitRaw(t); showPre();   // commit, then clear: IBus Wayland sends each as its own update, and Chromium keeps a pre-edit cleared before the commit, so the text came twice (#35)
}
static void trimPre() {   // commit all but the last two words and the current one
    int seps = 0; size_t cut = std::u32string::npos;
    for (size_t i = _pre.size(); i-- > 0;) if (_pre[i] == U' ' || _pre[i] == U'\n' || _pre[i] == U'\t') { if (++seps == 3) { cut = i + 1; break; } }
    if (cut == std::u32string::npos) { if (_pre.size() <= 160) return; cut = _pre.size() - 80; }   // a long run without spaces
    std::u32string head = _pre.substr(0, cut); _pre.erase(0, cut); commitRaw(head);
}
static void outBackspaces(int n) {
    if (!_active || n <= 0) return;
    if (preeditMode()) {
        int k = std::min<int>(n, (int)_pre.size()); _pre.erase(_pre.size() - k); n -= k; showPre();
        if (n <= 0) return;   // more than the pre-edit holds: the app has to delete committed text (rare; best effort below)
    }
    if (_active->caps & IBUS_CAP_SURROUNDING_TEXT) { if (_clampDeletes) n = surroundingDeleteCount(n, _reportedCursor); if (n > 0) ibus_engine_delete_surrounding_text(IBUS_ENGINE(_active), -n, n); }   // the app's last report, not IBus's copy: that one drops by our deletes, skips our commits and outlives focus_in
    else for (int i = 0; i < n; i++) { ibus_engine_forward_key_event(IBUS_ENGINE(_active), IBUS_KEY_BackSpace, 14, 0); ibus_engine_forward_key_event(IBUS_ENGINE(_active), IBUS_KEY_BackSpace, 14, IBUS_RELEASE_MASK); }
}
static void outText(const std::u32string& t) {
    if (!_active) return;
    if (preeditMode()) { _pre += t; trimPre(); showPre(); return; }
    flushPre(); commitRaw(t);
}
// the key being handled: a restore re-sends the key itself, not the engine's stand-in for it (Home, End, Delete,
// Page Up/Down are KEY_ESC to the engine: re-sending Escape left the cursor where it was)
static guint _keyval = 0, _keyHw = 0, _keyState = 0;
static void outControlKey(Uint16 key, bool shift) {
    if (!_active) return;
    flushPre();
    guint ks = _keyval ? _keyval : keysymOf(key), hw = _keyval ? _keyHw : 0; if (!ks) return;
    guint st = _keyval ? _keyState & (IBUS_SHIFT_MASK | IBUS_LOCK_MASK | IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK) : (shift ? IBUS_SHIFT_MASK : 0);   // the key's own modifiers: Ctrl+Enter stays Ctrl+Enter
    ibus_engine_forward_key_event(IBUS_ENGINE(_active), ks, hw, st); ibus_engine_forward_key_event(IBUS_ENGINE(_active), ks, hw, st | IBUS_RELEASE_MASK);
}
// The switch sound (Tony picked "sparkle"): two tiny notes, rising into Vietnamese, falling into English. The same
// files as the Mac app (App/Resources/Sounds/*.caf → Linux/sounds/*.wav), played in-process with GStreamer so the
// sound follows the key with no process start; a system bell when GStreamer can't play them.
static GstElement* _player = nullptr;
static void outSwitchSound(int language) {
    std::string file = std::string(VIKEY_DATADIR) + "/sounds/" + (language == 1 ? "SwitchToVietnamese.wav" : "SwitchToEnglish.wav");
    if (!_player) _player = gst_element_factory_make("playbin", "vikey-sound");
    if (_player && g_file_test(file.c_str(), G_FILE_TEST_EXISTS)) {
        gst_element_set_state(_player, GST_STATE_NULL);
        gchar* uri = g_filename_to_uri(file.c_str(), nullptr, nullptr); g_object_set(_player, "uri", uri, nullptr); g_free(uri);
        if (gst_element_set_state(_player, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) return;
    }
    gchar* argv[] = {(gchar*)"canberra-gtk-play", (gchar*)"-i", (gchar*)"bell", nullptr};
    g_spawn_async(nullptr, argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, nullptr, nullptr);
}
static void launchSettings(const char* arg, const char* arg2 = nullptr);
// The emoji picker is a window of vikey-settings (--emoji). It takes the keyboard focus, so the engine sees a
// focus_out; its choice comes back through the command file ("emoji 🐴") and is committed to the app once the
// focus is back there. The picker uses GTK's simple input method (not IBus), so its own typing never reaches us.
static std::string _pendingEmoji;
static void outOpenEmoji() { launchSettings("--emoji", nullptr); }
static void commitPendingEmoji() {
    if (_pendingEmoji.empty() || !_active) return;
    core::resetTyping();
    ibus_engine_commit_text(IBUS_ENGINE(_active), ibus_text_new_from_string(_pendingEmoji.c_str()));
    _pendingEmoji.clear(); _lastOutputUs = g_get_monotonic_time();
}

// ---- the app in front (per-app memory): X11 / XWayland only -----------------------------------------
static std::string frontAppX11() {
#ifdef VIKEY_HAVE_X11
    static Display* d = nullptr; static bool tried = false;
    if (!d && !tried) {
        tried = true;
        // the engine is started by ibus-daemon (a user service) without the session's X variables: fill in GNOME's
        if (!getenv("DISPLAY")) setenv("DISPLAY", ":0", 0);
        if (!getenv("XAUTHORITY")) { const char* rt = getenv("XDG_RUNTIME_DIR"); std::string dir = rt ? rt : "/run/user/1000"; glob_t g = {}; if (glob((dir + "/.mutter-Xwaylandauth.*").c_str(), 0, nullptr, &g) == 0 && g.gl_pathc > 0) setenv("XAUTHORITY", g.gl_pathv[0], 0); globfree(&g); }
        d = XOpenDisplay(nullptr);
        // the focused window can close mid-walk (BadWindow): Xlib's default handler would exit() the engine
        XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
    }
    if (!d) return "";
    // XGetInputFocus works on XWayland too (its root has no _NET_ACTIVE_WINDOW); walk up to the window with a class
    Window w = 0; int revert = 0; XGetInputFocus(d, &w, &revert);
    for (int depth = 0; w && w != PointerRoot && w != DefaultRootWindow(d) && depth < 8; depth++) {   // None / PointerRoot: no window has the focus
        XClassHint h = {};
        if (XGetClassHint(d, w, &h)) { std::string name = h.res_class ? h.res_class : ""; if (h.res_class) XFree(h.res_class); if (h.res_name) XFree(h.res_name); if (!name.empty()) return name; }
        Window root = 0, parent = 0, *kids = nullptr; unsigned n = 0;
        if (!XQueryTree(d, w, &root, &parent, &kids, &n)) break; if (kids) XFree(kids); w = parent;
    }
    return "";
#else
    return "";
#endif
}

// ---- panel properties (the "menu bar")
static std::string hotkeyLabel() {   // "Alt+Z" (same names as vikey-settings)
    int hk = prefs::getInt("SwitchKeyStatus", 0); if (!hk) hk = 0x7A000206;
    static const struct { int k; const char* n; } N[] = {{0,"A"},{11,"B"},{8,"C"},{2,"D"},{14,"E"},{3,"F"},{5,"G"},{4,"H"},{34,"I"},{38,"J"},{40,"K"},{37,"L"},{46,"M"},{45,"N"},{31,"O"},{35,"P"},{12,"Q"},{15,"R"},{1,"S"},{17,"T"},{32,"U"},{9,"V"},{13,"W"},{7,"X"},{16,"Y"},{6,"Z"},
        {18,"1"},{19,"2"},{20,"3"},{21,"4"},{23,"5"},{22,"6"},{26,"7"},{28,"8"},{25,"9"},{29,"0"},{49,"Space"},{36,"Enter"},{48,"Tab"},{50,"`"},{27,"-"},{24,"="},{33,"["},{30,"]"},{42,"\\"},{41,";"},{39,"'"},{43,","},{47,"."},{44,"/"}};
    std::string s; if (hk & 0x100) s += "Ctrl+"; if (hk & 0x200) s += "Alt+"; if (hk & 0x800) s += "Shift+"; if (hk & 0x400) s += "Super+";
    int k = hk & 0xFF; if (k != 0xFE) { for (auto& e : N) if (e.k == k) s += e.n; } else if (!s.empty()) s.pop_back();
    return s;
}
/// Brings the panel's menu up to date. Runs after every key, so it only sends what changed: each update is a D-Bus
/// round of messages to the panel, and two of them per key made typing heavier for the whole desktop.
static void updateLangProp(VikeyEngine* e) {
    if (!e || !e->pLang) return;
    int vi = core::language();
    bool autoOn = prefs::getInt("AutoSwitchByTyping", 1) != 0, logOn = prefs::getInt("TypingLogEnabled", 0) != 0;
    std::string label = "Tiếng Việt   " + hotkeyLabel();   // the Mac's "Tiếng Việt  ⌥Z", a switch on in Vietnamese
    guint64 now = ((guint64)std::hash<std::string>()(label) << 4) | (vi ? 1 : 0) | (autoOn ? 2 : 0) | (logOn ? 4 : 0) | 8;
    guint64 was = e->shown;
    if (now == was) return;
    e->shown = now;
    if (was == 0 || (now >> 4) != (was >> 4) || ((now ^ was) & 1)) {
        ibus_property_set_label(e->pLang, ibus_text_new_from_string(label.c_str()));
        ibus_property_set_symbol(e->pLang, ibus_text_new_from_string(vi ? "V" : "E"));
        ibus_property_set_state(e->pLang, vi ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED);
        ibus_engine_update_property(IBUS_ENGINE(e), e->pLang);
    }
    if (e->pLog && (bool)ibus_property_get_visible(e->pLog) != logOn) { ibus_property_set_visible(e->pLog, logOn); ibus_engine_update_property(IBUS_ENGINE(e), e->pLog); }
    if (e->pAuto && (was == 0 || ((now ^ was) & 2))) { ibus_property_set_state(e->pAuto, autoOn ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED); ibus_engine_update_property(IBUS_ENGINE(e), e->pAuto); }
}
static void buildProps(VikeyEngine* e) {
    e->props = ibus_prop_list_new(); g_object_ref_sink(e->props);
    e->pLang = ibus_property_new("lang", PROP_TYPE_TOGGLE, ibus_text_new_from_string("Tiếng Việt"), nullptr, ibus_text_new_from_string("Chuyển Anh/Việt (Alt+Z)"), TRUE, TRUE, PROP_STATE_CHECKED, nullptr);
    ibus_property_set_symbol(e->pLang, ibus_text_new_from_string("V"));
    e->pAuto = ibus_property_new("auto", PROP_TYPE_TOGGLE, ibus_text_new_from_string("Tự chuyển Anh/Việt"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_CHECKED, nullptr);
    e->pOpen = ibus_property_new("open", PROP_TYPE_NORMAL, ibus_text_new_from_string("Mở Vikey…"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    e->pLog = ibus_property_new("log", PROP_TYPE_NORMAL, ibus_text_new_from_string("Xem nhật ký gõ…"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    e->pGuide = ibus_property_new("guide", PROP_TYPE_NORMAL, ibus_text_new_from_string("Hướng dẫn…"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    e->pUpdate = ibus_property_new("update", PROP_TYPE_NORMAL, ibus_text_new_from_string("Kiểm tra cập nhật…"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    e->pAbout = ibus_property_new("about", PROP_TYPE_NORMAL, ibus_text_new_from_string("Giới thiệu Vikey"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    // the Mac menu bar's order; the typing log is a debug item there (Gỡ lỗi), shown only while logging is on
    ibus_property_set_visible(e->pLog, prefs::getInt("TypingLogEnabled", 0) != 0);
    for (IBusProperty* p : {e->pLang, e->pAuto, e->pOpen, e->pGuide, e->pUpdate, e->pAbout, e->pLog}) { g_object_ref_sink(p); ibus_prop_list_append(e->props, p); }
}
static void launchSettings(const char* arg, const char* arg2) {
    gchar* argv[] = {(gchar*)"vikey-settings", (gchar*)arg, (gchar*)arg2, nullptr};
    // the picker must not type through IBus (it reads raw keys for its search), the other windows must (the guide's practice fields)
    gchar** env = g_get_environ(); if (!strcmp(arg, "--emoji")) { env = g_environ_setenv(env, "GTK_IM_MODULE", "gtk-im-context-simple", TRUE); env = g_environ_setenv(env, "GDK_BACKEND", "x11", TRUE); }   // X11: the picker centres itself
    g_spawn_async(nullptr, argv, env, (GSpawnFlags)G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr); g_strfreev(env);
}

// ---- IBusEngine overrides --------------------------------------------------------------------------
static unsigned flagsOf(guint state) { unsigned f = 0; if (state & IBUS_SHIFT_MASK) f |= core::F_SHIFT; if (state & IBUS_CONTROL_MASK) f |= core::F_CONTROL; if (state & IBUS_MOD1_MASK) f |= core::F_ALT; if (state & IBUS_SUPER_MASK) f |= core::F_WIN; if (state & IBUS_LOCK_MASK) f |= core::F_CAPS; return f; }
static gboolean vikey_process_key_event(IBusEngine* engine, guint keyval, guint keycode, guint state) {
    VikeyEngine* e = (VikeyEngine*)engine; _active = e;
    bool release = state & IBUS_RELEASE_MASK;
    if (!_surroundingSeen && !release) ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr);   // ask again until the field answers
    if (isModifierKeysym(keyval)) {
        unsigned bit = 0;
        if (keyval == IBUS_KEY_Shift_L || keyval == IBUS_KEY_Shift_R) bit = core::F_SHIFT; else if (keyval == IBUS_KEY_Control_L || keyval == IBUS_KEY_Control_R) bit = core::F_CONTROL;
        else if (keyval == IBUS_KEY_Alt_L || keyval == IBUS_KEY_Alt_R || keyval == IBUS_KEY_Meta_L || keyval == IBUS_KEY_Meta_R) bit = core::F_ALT; else if (keyval == IBUS_KEY_Super_L || keyval == IBUS_KEY_Super_R) bit = core::F_WIN;
        if (bit && core::modifier(bit, !release)) { updateLangProp(e); return TRUE; }
        return FALSE;
    }
    if (release) { core::keyUp(engineKey(keyval).key); return FALSE; }
    // a password/PIN field (the app's input purpose, sent on every focus in): keys go through as typed, like macOS
    // secure input: no Telex, no auto switch, and the core never sees them, so the typing log can't record them
    guint purpose = 0, hints = 0; ibus_engine_get_content_type(engine, &purpose, &hints);
    if (purpose == IBUS_INPUT_PURPOSE_PASSWORD || purpose == IBUS_INPUT_PURPOSE_PIN) { core::otherKeyDown(); flushPre(); return FALSE; }
    if (_appPending) { _appPending = false; std::string app = frontAppX11(); if (!app.empty() && app != core::frontApp()) core::focusChanged(app); }
    MappedKey m = engineKey(keyval);
    // a key we don't model: one that types a character (KP_Add, EuroSign...) ends the word, or the next letter would
    // join the word before it ("a", KP_Add, "a" gave "aâ"); F-keys and media keys don't
    if (m.key == KEY_EMPTY) { core::otherKeyDown(); flushPre(); if (g_unichar_isprint(ibus_keyval_to_unicode(keyval))) core::click(); return FALSE; }
    unsigned flags = flagsOf(state);
    _keyval = keyval; _keyHw = keycode; _keyState = state;
    bool consumed = core::keyDown(m.key, m.shift || (flags & core::F_SHIFT), flags);
    _keyval = _keyHw = 0;
    updateLangProp(e);
    _lastOutputUs = g_get_monotonic_time();
    if (!consumed && preeditMode()) {   // the key goes to the app: in pre-edit mode, type it ourselves, after the pre-edit
        if (!(flags & (core::F_CONTROL | core::F_ALT | core::F_WIN))) {
            if (keyval == IBUS_KEY_BackSpace && !_pre.empty()) { _pre.pop_back(); showPre(); return TRUE; }
            gunichar ch = ibus_keyval_to_unicode(keyval);
            if (ch >= 0x20 && ch != 0x7F && keyval != IBUS_KEY_Return && keyval != IBUS_KEY_KP_Enter && keyval != IBUS_KEY_Tab) { _pre += (char32_t)ch; trimPre(); showPre(); return TRUE; }
        }
        flushPre();   // Enter, Tab, Backspace with nothing pending, arrows, shortcuts: the app gets the text first
    }
    return consumed ? TRUE : FALSE;
}
static void vikey_focus_in(IBusEngine* engine) {
    VikeyEngine* e = (VikeyEngine*)engine; _active = e; _surroundingSeen = false; _reportedCursor = -1; _pre.clear();
    ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr);   // asks this field to send its text (apps that can, do)
    ibus_engine_register_properties(engine, e->props);
    e->shown = 0;   // the panel took the list again: send everything once
    std::string app = frontAppX11();
    core::focusChanged(app);
    _appPending = true;
    updateLangProp(e);
    commitPendingEmoji();
}
static void vikey_focus_out(IBusEngine* engine) { _pre.clear(); core::click(); if (_active == (VikeyEngine*)engine) _active = nullptr; }   // the app keeps the pre-edit (COMMIT mode)
static void vikey_reset(IBusEngine* engine) { if (!_pre.empty()) { _pre.clear(); core::click(); return; } if (g_get_monotonic_time() - _lastOutputUs < 300000) return; core::click(); }   // a reset with pre-edit: the app kept it (COMMIT mode) and the cursor moved   // a reset caused by our own key/commit is not a click
static void vikey_enable(IBusEngine* engine) { VikeyEngine* e = (VikeyEngine*)engine; e->enabled = TRUE; ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr); }
static void vikey_disable(IBusEngine* engine) { ((VikeyEngine*)engine)->enabled = FALSE; core::flushTypingLog(); }
static void vikey_set_capabilities(IBusEngine* engine, guint caps) { ((VikeyEngine*)engine)->caps = caps; }
static void vikey_set_surrounding_text(IBusEngine* engine, IBusText* text, guint cursor, guint anchor) {
    if ((VikeyEngine*)engine == _active && !_surroundingSeen) { flushPre(); _surroundingSeen = true; }   // the app reports its text: direct mode from now on
    if ((VikeyEngine*)engine == _active) _reportedCursor = (int)cursor;
    if (_parentClass && _parentClass->set_surrounding_text) _parentClass->set_surrounding_text(engine, text, cursor, anchor);
}
static void vikey_property_activate(IBusEngine* engine, const gchar* name, guint state) {
    VikeyEngine* e = (VikeyEngine*)engine;
    if (!strcmp(name, "lang")) core::setLanguageByUser(state == PROP_STATE_CHECKED ? 1 : 0);
    else if (!strcmp(name, "auto")) { prefs::setInt("AutoSwitchByTyping", state == PROP_STATE_CHECKED ? 1 : 0); core::reloadSettings(); }
    else if (!strcmp(name, "open")) launchSettings("");
    else if (!strcmp(name, "log")) launchSettings("--log");
    else if (!strcmp(name, "guide")) launchSettings("--guide");
    else if (!strcmp(name, "update")) launchSettings("--check-update");
    else if (!strcmp(name, "about")) launchSettings("--about");
    updateLangProp(e);
}
static void vikey_engine_init(VikeyEngine* e) { e->caps = 0; e->enabled = FALSE; e->shown = 0; buildProps(e); }
static void vikey_engine_class_init(VikeyEngineClass* klass) {
    IBusEngineClass* c = IBUS_ENGINE_CLASS(klass);
    _parentClass = IBUS_ENGINE_CLASS(g_type_class_peek_parent(klass));
    c->set_surrounding_text = vikey_set_surrounding_text;
    c->process_key_event = vikey_process_key_event; c->focus_in = vikey_focus_in; c->focus_out = vikey_focus_out; c->reset = vikey_reset;
    c->enable = vikey_enable; c->disable = vikey_disable; c->set_capabilities = vikey_set_capabilities; c->property_activate = vikey_property_activate;
}
static GType vikey_engine_get_type() {
    static GType t = 0;
    if (!t) { static const GTypeInfo info = {sizeof(VikeyEngineClass), nullptr, nullptr, (GClassInitFunc)vikey_engine_class_init, nullptr, nullptr, sizeof(VikeyEngine), 0, (GInstanceInitFunc)vikey_engine_init, nullptr}; t = g_type_register_static(IBUS_TYPE_ENGINE, "VikeyEngine", &info, (GTypeFlags)0); }
    return t;
}

// ---- settings file watch + periodic flush -------------------------------------------------------------
// Reload only when a write is done (not on every CHANGED: the file may be half-written).
static void readCommand() {
    std::string path = prefs::commandPath(); FILE* f = fopen(path.c_str(), "r"); if (!f) return;
    char line[256] = {0}; if (fgets(line, sizeof line, f)) { std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.rfind("crash-selftest", 0) == 0) { fclose(f); unlink(path.c_str()); crash::selfTest(l.size() > 15 ? l.substr(15) : ""); }   // testing: "crash-selftest [abort]"
        if (l.rfind("emoji ", 0) == 0) { _pendingEmoji = l.substr(6); commitPendingEmoji(); }   // committed now if the app already has the focus back, else at its focus_in
        else core::command(l); }
    fclose(f); unlink(path.c_str()); updateLangProp(_active);
}
// Auto update (Tự động cập nhật): once a day ask vikey-settings to compare vikey.app/release.json with this version
// and show a notification when there is a newer one (the package manager does the install).
static gboolean onUpdateTick(gpointer) {
    if (!prefs::getInt("AutoUpdate", 1)) return TRUE;
    long now = (long)time(nullptr); long last = prefs::getInt("LastUpdateCheck", 0);
    if (now - last < 86400) return TRUE;
    prefs::setInt("LastUpdateCheck", (int)now); launchSettings("--check-update", "--quiet"); return TRUE;
}
static void onSettingsChanged(GFileMonitor*, GFile* file, GFile*, GFileMonitorEvent ev, gpointer) {
    if (file) { char* b = g_file_get_basename(file); bool isCmd = b && !strcmp(b, "command"); g_free(b); if (isCmd) { if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_MOVED_IN || ev == G_FILE_MONITOR_EVENT_RENAMED || ev == G_FILE_MONITOR_EVENT_CREATED) readCommand(); return; } } if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_MOVED_IN || ev == G_FILE_MONITOR_EVENT_RENAMED || ev == G_FILE_MONITOR_EVENT_CREATED) { if (prefs::changedByOthers()) core::reloadSettings(); updateLangProp(_active); } }
// After a crash (IBus has started the engine again): vikey-settings sends the report (CrashReportsAuto) or asks
// first with a notification. Off the key path, a few seconds after start; the settings app marks it handled.
static gboolean onCrashCheck(gpointer) { if (!crash::pending().empty()) launchSettings("--crash-report"); return G_SOURCE_REMOVE; }
static gboolean onFlush(gpointer) { core::flushTypingLog(); return TRUE; }
static void onBusDisconnected(IBusBus*, gpointer) { core::shutdown(); ibus_quit(); }

int main(int argc, char** argv) {
    bool ibusMode = false; for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--ibus")) ibusMode = true;
    crash::install(VIKEY_VERSION);   // first: a crash anywhere after this leaves a report
    if (const char* t = getenv("VIKEY_CRASH_SELFTEST")) if (*t) crash::selfTest(t);   // testing: VIKEY_CRASH_SELFTEST=segv|abort
    ibus_init(); gst_init(nullptr, nullptr);
    core::init(core::Output{outBackspaces, outText, outControlKey, outSwitchSound, outOpenEmoji});
    _bus = ibus_bus_new();
    if (!ibus_bus_is_connected(_bus)) { fprintf(stderr, "vikey: cannot connect to the IBus daemon\n"); return 1; }
    g_signal_connect(_bus, "disconnected", G_CALLBACK(onBusDisconnected), nullptr);
    IBusFactory* factory = ibus_factory_new(ibus_bus_get_connection(_bus));
    ibus_factory_add_engine(factory, "vikey", VIKEY_TYPE_ENGINE);
    if (ibusMode) ibus_bus_request_name(_bus, "org.freedesktop.IBus.Vikey", 0);
    else {   // run by hand (development): register the component ourselves
        IBusComponent* c = ibus_component_new("org.freedesktop.IBus.Vikey", "Vikey", VIKEY_VERSION, "GPL-3.0", "Tony Dinh", "https://vikey.app", "", "ibus-vikey");
        ibus_component_add_engine(c, ibus_engine_desc_new("vikey", "Vikey", "Vikey - Gõ tiếng Việt, tự chuyển Anh/Việt", "vi", "GPL-3.0", "Tony Dinh", "vikey", "us"));
        ibus_bus_register_component(_bus, c);
    }
    GFile* dir = g_file_new_for_path(prefs::configDir().c_str()); g_mkdir_with_parents(prefs::configDir().c_str(), 0700);
    GFileMonitor* mon = g_file_monitor_directory(dir, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr); if (mon) g_signal_connect(mon, "changed", G_CALLBACK(onSettingsChanged), nullptr);
    GFile* sf = g_file_new_for_path(prefs::settingsPath().c_str());
    GFileMonitor* fmon = g_file_monitor_file(sf, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr); if (fmon) g_signal_connect(fmon, "changed", G_CALLBACK(onSettingsChanged), nullptr);
    g_timeout_add(1000, [](gpointer) -> gboolean { core::pollSettingsFile(); readCommand(); return TRUE; }, nullptr);   // belt and braces: mtime poll + command file
    g_timeout_add_seconds(10, onFlush, nullptr);
    g_timeout_add_seconds(8, onCrashCheck, nullptr);
    g_timeout_add_seconds(90, [](gpointer) -> gboolean { onUpdateTick(nullptr); g_timeout_add_seconds(3600, onUpdateTick, nullptr); return FALSE; }, nullptr);
    ibus_main();
    core::shutdown();
    return 0;
}
