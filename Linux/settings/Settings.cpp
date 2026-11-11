// vikey-settings: the Vikey window on Linux (GTK4 + libadwaita), 1:1 with the Mac app's MainView:
// header (icon, Vikey, hotkey hint, Tiếng Việt / English), grouped rows with switches and (?) tips.
// Also: the typing-log window (--log), About (--about) with Ghi nhận (--acknowledgements), the first-run guide
// (--guide, Guide.cpp), the emoji picker (--emoji, Emoji.cpp), the update check (--check-update [--quiet]) and the
// crash report question (--crash-report [--dialog], CrashReport.cpp).
// Settings live in ~/.config/vikey/settings.json; the IBus engine watches that file.
#include "Common.h"
#include <vector>
#include <map>
#include <fstream>
#include <cstring>
#include <ctime>
#include <algorithm>
using namespace vikey;

// ---- shared helpers (Common.h) ------------------------------------------------------------------------------
namespace ui {
int currentHotkey() { int hk = prefs::getInt("SwitchKeyStatus", 0); return hk ? hk : 0x7A000206; }
static std::string keyName(int key) {
    static const struct { int k; const char* n; } N[] = {{0,"A"},{11,"B"},{8,"C"},{2,"D"},{14,"E"},{3,"F"},{5,"G"},{4,"H"},{34,"I"},{38,"J"},{40,"K"},{37,"L"},{46,"M"},{45,"N"},{31,"O"},{35,"P"},{12,"Q"},{15,"R"},{1,"S"},{17,"T"},{32,"U"},{9,"V"},{13,"W"},{7,"X"},{16,"Y"},{6,"Z"},
        {18,"1"},{19,"2"},{20,"3"},{21,"4"},{23,"5"},{22,"6"},{26,"7"},{28,"8"},{25,"9"},{29,"0"},{49,"Space"},{36,"Enter"},{48,"Tab"},{50,"`"},{27,"-"},{24,"="},{33,"["},{30,"]"},{42,"\\"},{41,";"},{39,"'"},{43,","},{47,"."},{44,"/"}};
    for (auto& e : N) if (e.k == key) return e.n; return "#" + std::to_string(key);
}
std::string hotkeyDisplay(int hk) {
    if ((hk & ~0x8000) == 0) return "Chưa đặt";
    std::string s; if (hk & 0x100) s += "Ctrl+"; if (hk & 0x200) s += "Alt+"; if (hk & 0x800) s += "Shift+"; if (hk & 0x400) s += "Super+";
    int k = hk & 0xFF; if (k != 0xFE) s += keyName(k); else if (!s.empty()) s.pop_back();
    return s.empty() ? "—" : s;
}
int engineKeyOfKeyval(guint kv) {
    if (kv >= GDK_KEY_a && kv <= GDK_KEY_z) kv -= 32;
    static const int L[] = {0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
    if (kv >= GDK_KEY_A && kv <= GDK_KEY_Z) return L[kv - GDK_KEY_A];
    static const struct { guint kv; int k; } M[] = {{GDK_KEY_1,18},{GDK_KEY_2,19},{GDK_KEY_3,20},{GDK_KEY_4,21},{GDK_KEY_5,23},{GDK_KEY_6,22},{GDK_KEY_7,26},{GDK_KEY_8,28},{GDK_KEY_9,25},{GDK_KEY_0,29},{GDK_KEY_space,49},{GDK_KEY_Return,36},{GDK_KEY_Tab,48},{GDK_KEY_grave,50},{GDK_KEY_minus,27},{GDK_KEY_equal,24},{GDK_KEY_bracketleft,33},{GDK_KEY_bracketright,30},{GDK_KEY_backslash,42},{GDK_KEY_semicolon,41},{GDK_KEY_apostrophe,39},{GDK_KEY_comma,43},{GDK_KEY_period,47},{GDK_KEY_slash,44},
        {GDK_KEY_exclam,18},{GDK_KEY_at,19},{GDK_KEY_numbersign,20},{GDK_KEY_dollar,21},{GDK_KEY_percent,23},{GDK_KEY_asciicircum,22},{GDK_KEY_ampersand,26},{GDK_KEY_asterisk,28},{GDK_KEY_parenleft,25},{GDK_KEY_parenright,29}};
    for (auto& m : M) if (m.kv == kv) return m.k; return -1;
}
unsigned modsOf(GdkModifierType state) { return (state & GDK_CONTROL_MASK ? 0x100 : 0) | (state & GDK_ALT_MASK ? 0x200 : 0) | (state & GDK_SUPER_MASK ? 0x400 : 0) | (state & GDK_SHIFT_MASK ? 0x800 : 0); }
static unsigned modBitOfKeyval(guint kv) {
    switch (kv) { case GDK_KEY_Control_L: case GDK_KEY_Control_R: return 0x100; case GDK_KEY_Alt_L: case GDK_KEY_Alt_R: case GDK_KEY_Meta_L: case GDK_KEY_Meta_R: return 0x200;
        case GDK_KEY_Super_L: case GDK_KEY_Super_R: return 0x400; case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: return 0x800; } return 0;
}
struct Rec { std::function<int()> get; std::function<void(int)> set; bool allowMods, autoStart; bool recording = false; unsigned pending = 0; GtkWidget* btn; };
static Rec* recOf(GtkWidget* w) { return (Rec*)g_object_get_data(G_OBJECT(w), "rec"); }
void recorderRefresh(GtkWidget* w) { Rec* r = recOf(w); if (r) gtk_button_set_label(GTK_BUTTON(r->btn), r->recording ? "Bấm phím tắt…" : hotkeyDisplay(r->get() & ~0x8000).c_str()); }
static void recStop(Rec* r) { r->recording = false; r->pending = 0; recorderRefresh(r->btn); }
static void recStart(Rec* r) { r->recording = true; r->pending = 0; recorderRefresh(r->btn); gtk_widget_grab_focus(r->btn); }
static gboolean recKey(GtkEventControllerKey*, guint kv, guint, GdkModifierType state, gpointer p) {
    Rec* r = (Rec*)p; if (!r->recording) return FALSE;
    unsigned mods = modsOf(state);
    if (kv == GDK_KEY_Escape && !mods) { recStop(r); return TRUE; }   // Esc cancels
    if (unsigned bit = modBitOfKeyval(kv)) { r->pending |= bit | mods; return TRUE; }
    if (!mods) return TRUE;                                              // a key needs a modifier
    int key = engineKeyOfKeyval(kv); if (key < 0) return TRUE;
    r->set(key | mods); recStop(r); return TRUE;
}
static gboolean recRelease(GtkEventControllerKey*, guint kv, guint, GdkModifierType state, gpointer p) {
    Rec* r = (Rec*)p; if (!r->recording) return FALSE;
    unsigned bit = modBitOfKeyval(kv); if (!bit) return TRUE;
    if ((modsOf(state) & ~bit) != 0) return TRUE;                        // still holding some
    int n = __builtin_popcount(r->pending & 0xF00);
    if (r->allowMods && n >= 2) { r->set(0xFE | (r->pending & 0xF00)); recStop(r); }   // modifiers only: two or more, on release
    else r->pending = 0;
    return TRUE;
}
GtkWidget* recorderNew(std::function<int()> get, std::function<void(int)> set, bool allowMods, bool autoStart) {
    Rec* r = new Rec{get, set, allowMods, autoStart};
    r->btn = gtk_button_new_with_label(""); gtk_widget_set_valign(r->btn, GTK_ALIGN_CENTER); gtk_widget_set_size_request(r->btn, 130, -1);
    g_object_set_data_full(G_OBJECT(r->btn), "rec", r, [](gpointer p) { delete (Rec*)p; });
    GtkEventController* kc = gtk_event_controller_key_new(); g_signal_connect(kc, "key-pressed", G_CALLBACK(recKey), r); g_signal_connect(kc, "key-released", G_CALLBACK(recRelease), r); gtk_widget_add_controller(r->btn, kc);
    GtkEventController* fc = gtk_event_controller_focus_new(); g_signal_connect(fc, "leave", G_CALLBACK(+[](GtkEventControllerFocus*, gpointer p) { Rec* r = (Rec*)p; if (r->recording) recStop(r); }), r); gtk_widget_add_controller(r->btn, fc);
    g_signal_connect(r->btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { Rec* r = (Rec*)p; r->recording ? recStop(r) : recStart(r); }), r);
    g_signal_connect(r->btn, "map", G_CALLBACK(+[](GtkWidget*, gpointer p) { Rec* r = (Rec*)p; if (r->autoStart && !r->recording && (r->get() & ~0x8000) == 0) recStart(r); }), r);
    recorderRefresh(r->btn);
    return r->btn;
}
GtkWidget* tipTitle(const char* title, const char* tip) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2); gtk_widget_set_valign(box, GTK_ALIGN_CENTER); gtk_widget_add_css_class(box, "tiptitle");
    GtkWidget* l = gtk_label_new(title); gtk_label_set_xalign(GTK_LABEL(l), 0); gtk_box_append(GTK_BOX(box), l);
    // a "?" in a circle (the Mac's questionmark.circle): drawn with CSS, icon themes differ (Yaru's question icon is a diamond)
    GtkWidget* mb = gtk_menu_button_new(); GtkWidget* q = gtk_label_new("?"); gtk_widget_add_css_class(q, "tip-mark"); gtk_widget_set_halign(q, GTK_ALIGN_CENTER); gtk_widget_set_valign(q, GTK_ALIGN_CENTER); gtk_menu_button_set_child(GTK_MENU_BUTTON(mb), q); gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(mb), FALSE);
    gtk_widget_add_css_class(mb, "tip"); gtk_widget_set_tooltip_text(mb, tip); gtk_widget_set_valign(mb, GTK_ALIGN_CENTER);
    GtkWidget* pop = gtk_popover_new(); GtkWidget* pl = gtk_label_new(tip); gtk_label_set_wrap(GTK_LABEL(pl), TRUE); gtk_label_set_max_width_chars(GTK_LABEL(pl), 36); gtk_label_set_xalign(GTK_LABEL(pl), 0);
    for (auto f : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) f(pl, 6);
    gtk_popover_set_child(GTK_POPOVER(pop), pl); gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    gtk_box_append(GTK_BOX(box), mb);
    return box;
}
GtkWidget* wordmark(int px) {
    GtkWidget* l = gtk_label_new(nullptr);
    char m[256]; snprintf(m, sizeof m, "<span weight=\"bold\" size=\"%dpt\">Vi<span foreground=\"%s\">key</span></span>", px * 3 / 4,
        adw_style_manager_get_dark(adw_style_manager_get_default()) ? "#8C87FF" : "#4F46E5");
    gtk_label_set_markup(GTK_LABEL(l), m); gtk_label_set_xalign(GTK_LABEL(l), 0); return l;
}
void openUri(const char* uri) { gchar* argv[] = {(gchar*)"xdg-open", (gchar*)uri, nullptr}; g_spawn_async(nullptr, argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, nullptr, nullptr); }
void loadCss() {
    GtkCssProvider* p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p,
        "menubutton.tip > button { min-width: 22px; min-height: 22px; padding: 0; opacity: 0.45; background: none; box-shadow: none; }\n"
        ".tip-mark { min-width: 13px; min-height: 13px; border: 1.5px solid currentColor; border-radius: 50%; font-size: 10px; font-weight: 800; padding: 0; margin: 0; }\n"
        "box.tiptitle > menubutton { margin-left: 3px; }\n"
        "menubutton.tip > button:hover { opacity: 0.8; }\n"
        ".brand { color: #4F46E5; } .dots .on { background: #4F46E5; } .dots .off { background: alpha(currentColor, 0.25); }\n"
        ".dots > * { min-width: 7px; min-height: 7px; border-radius: 4px; }\n"
        ".telex-key { font-family: monospace; font-weight: bold; padding: 1px 6px; border-radius: 5px; background: @view_bg_color; }\n"
        ".telex-card { background: alpha(currentColor, 0.06); border-radius: 10px; padding: 14px; }\n"
        ".ok-mark { color: @success_color; }\n");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}
}  // namespace ui
using namespace ui;

/// Is Vikey one of the input sources (GNOME)? -1 when this desktop has no such setting.
int vikeyInInputSources() {
    GSettingsSchemaSource* src = g_settings_schema_source_get_default(); if (!src) return -1;
    GSettingsSchema* sc = g_settings_schema_source_lookup(src, "org.gnome.desktop.input-sources", TRUE); if (!sc) return -1; g_settings_schema_unref(sc);
    GSettings* s = g_settings_new("org.gnome.desktop.input-sources"); GVariant* v = g_settings_get_value(s, "sources");
    int found = 0; GVariantIter it; const char *type, *id; g_variant_iter_init(&it, v);
    while (g_variant_iter_next(&it, "(&s&s)", &type, &id)) if (!strcmp(type, "ibus") && !strcmp(id, "vikey")) found = 1;
    g_variant_unref(v); g_object_unref(s); return found;
}
void spawnSelf(const char* arg) { gchar* argv[] = {(gchar*)"vikey-settings", (gchar*)arg, nullptr}; g_spawn_async(nullptr, argv, nullptr, (GSpawnFlags)G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr); }

// ---- main window ----------------------------------------------------------------------------------------------
static GtkWidget* _hint; static GtkWidget* _seg[2]; static GtkWidget* _hotkeyRec; static GtkWidget *_emojiRow, *_emojiRec, *_setupLink, *_loginSw;
static GtkWidget* _mainWin; static guint _mainTick;
static std::map<std::string, GtkWidget*> _sw;
static void setHint() { gtk_label_set_text(GTK_LABEL(_hint), ("Bấm " + hotkeyDisplay(currentHotkey()) + " để chuyển").c_str()); recorderRefresh(_hotkeyRec); }
static void refreshEnabled() {
    gtk_widget_set_sensitive(_sw["PauseAutoAfterManual"], gtk_switch_get_active(GTK_SWITCH(_sw["AutoSwitchByTyping"])));
    gtk_widget_set_sensitive(_sw["RestoreIfInvalidWord"], gtk_switch_get_active(GTK_SWITCH(_sw["Spelling"])));
    gtk_widget_set_visible(_emojiRow, gtk_switch_get_active(GTK_SWITCH(_sw["EmojiPicker"])));
}
static void syncSeg() { int vi = prefs::getInt("InputMethod", 1); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_seg[0]), vi); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_seg[1]), !vi); }
static gboolean onSwitch(GtkSwitch* s, gboolean state, gpointer key) { prefs::setInt((const char*)key, state ? 1 : 0); gtk_switch_set_state(s, state); if (_hint) refreshEnabled(); return TRUE; }
static void onSeg(GtkToggleButton* b, gpointer which) { if (!gtk_toggle_button_get_active(b)) return; if ((prefs::getInt("InputMethod", 1) != 0) == (GPOINTER_TO_INT(which) == 0)) return; prefs::sendCommand(GPOINTER_TO_INT(which) == 0 ? "lang vi user" : "lang en user"); }
static void onStyle(GObject* dd, GParamSpec*, gpointer) { prefs::setInt("TelexStyle", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(dd))); }
static void onMark(GObject* dd, GParamSpec*, gpointer) { prefs::setInt("ModernOrthography", gtk_drop_down_get_selected(GTK_DROP_DOWN(dd)) == 0 ? 1 : 0); }
static gboolean onBeep(GtkSwitch* s, gboolean state, gpointer) { int hk = (currentHotkey() & ~0x8000) | (state ? 0x8000 : 0); prefs::setInt("SwitchKeyStatus", hk); gtk_switch_set_state(s, state); return TRUE; }
GtkWidget* switchHotkeyRecorder() { return recorderNew([] { return currentHotkey(); }, [](int k) { prefs::setInt("SwitchKeyStatus", k | (currentHotkey() & 0x8000)); }, true, false); }
GtkWidget* emojiHotkeyRecorder() {
    return recorderNew([] { return prefs::getInt("EmojiHotkey", 0); },
        [](int k) { if (k == (currentHotkey() & ~0x8000)) return; prefs::setInt("EmojiHotkey", k & ~0x8000); },   // already the Vietnamese/English switch
        false, true);
}

static GtkWidget* row(GtkWidget* group, const char* title, const char* tip) {
    GtkWidget* r = adw_action_row_new(); adw_action_row_add_prefix(ADW_ACTION_ROW(r), tipTitle(title, tip));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), r); return r;
}
static GtkWidget* switchRow(GtkWidget* group, const char* title, const char* tip, const char* key, int def, GCallback cb = nullptr) {
    GtkWidget* r = row(group, title, tip);
    GtkWidget* sw = gtk_switch_new(); gtk_widget_set_valign(sw, GTK_ALIGN_CENTER); gtk_switch_set_active(GTK_SWITCH(sw), key[0] == '!' ? ((currentHotkey() & 0x8000) != 0) : prefs::getInt(key, def) != 0);
    g_signal_connect(sw, "state-set", cb ? cb : G_CALLBACK(onSwitch), (gpointer)key);
    adw_action_row_add_suffix(ADW_ACTION_ROW(r), sw); adw_action_row_set_activatable_widget(ADW_ACTION_ROW(r), sw);
    if (key[0] != '!') _sw[key] = sw; return sw;
}
static GtkWidget* group(GtkWidget* page, const char* title) { GtkWidget* g = adw_preferences_group_new(); if (title) adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title); adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(g)); return g; }
static void refreshSetup() { int in = vikeyInInputSources(); gtk_widget_set_visible(_setupLink, in == 0); gtk_widget_set_visible(_hint, in != 0); gtk_switch_set_active(GTK_SWITCH(_loginSw), in != 0); }
static void onSettingsFile(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer) { prefs::reload(); syncSeg(); setHint(); }

static void onFeedback(GtkButton*, gpointer) {   // no lambdas in G_CALLBACK (see the Linux compile fixes)
    g_app_info_launch_default_for_uri("https://vikey.app/feedback?platform=linux&v=" VIKEY_VERSION, nullptr, nullptr);
}

void buildMain(GtkApplication* app) {
    GtkWidget* win = adw_application_window_new(app); gtk_window_set_title(GTK_WINDOW(win), "Vikey"); int h = 860;   // every row without scrolling when the screen is tall enough (the Mac window doesn't scroll)
    if (GListModel* mons = gdk_display_get_monitors(gdk_display_get_default())) if (GdkMonitor* m = (GdkMonitor*)g_list_model_get_item(mons, 0)) { GdkRectangle r; gdk_monitor_get_geometry(m, &r); h = std::min(h, r.height - 110); g_object_unref(m); }
    gtk_window_set_default_size(GTK_WINDOW(win), 480, h);
    GtkWidget* tv = adw_toolbar_view_new(); GtkWidget* hb = adw_header_bar_new(); adw_header_bar_set_show_title(ADW_HEADER_BAR(hb), FALSE); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);
    GtkWidget* page = adw_preferences_page_new();
    // header: icon, wordmark + hint (or the IBus setup link), segmented Tiếng Việt / English
    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12); gtk_widget_set_margin_start(head, 12); gtk_widget_set_margin_end(head, 12); gtk_widget_set_margin_bottom(head, 8);
    GtkWidget* icon = gtk_image_new_from_icon_name("vikey"); gtk_image_set_pixel_size(GTK_IMAGE(icon), 44); gtk_box_append(GTK_BOX(head), icon);
    GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2); gtk_widget_set_hexpand(col, TRUE); gtk_widget_set_valign(col, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(col), wordmark(22));
    _hint = gtk_label_new(""); gtk_widget_add_css_class(_hint, "dim-label"); gtk_label_set_xalign(GTK_LABEL(_hint), 0); gtk_box_append(GTK_BOX(col), _hint);
    _setupLink = gtk_button_new_with_label("Thêm Vikey vào nguồn nhập…"); gtk_widget_add_css_class(_setupLink, "link"); gtk_widget_set_halign(_setupLink, GTK_ALIGN_START);   // the Mac's "Cấp quyền Trợ năng…"
    g_signal_connect(_setupLink, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) { spawnSelf("--guide"); }), nullptr); gtk_box_append(GTK_BOX(col), _setupLink);
    gtk_box_append(GTK_BOX(head), col);
    GtkWidget* seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_add_css_class(seg, "linked"); gtk_widget_set_valign(seg, GTK_ALIGN_CENTER);
    _seg[0] = gtk_toggle_button_new_with_label("Tiếng Việt"); _seg[1] = gtk_toggle_button_new_with_label("English"); gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(_seg[1]), GTK_TOGGLE_BUTTON(_seg[0]));
    gtk_box_append(GTK_BOX(seg), _seg[0]); gtk_box_append(GTK_BOX(seg), _seg[1]); gtk_box_append(GTK_BOX(head), seg); syncSeg();
    g_signal_connect(_seg[0], "toggled", G_CALLBACK(onSeg), GINT_TO_POINTER(0)); g_signal_connect(_seg[1], "toggled", G_CALLBACK(onSeg), GINT_TO_POINTER(1));
    GtkWidget* g0 = adw_preferences_group_new(); adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(g0));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g0), head);
    // the switch hotkey + its sound
    GtkWidget* g1 = group(page, nullptr);
    GtkWidget* hkRow = row(g1, "Phím chuyển Anh/Việt", "Bấm tổ hợp phím này để chuyển giữa tiếng Việt và tiếng Anh. Bấm vào ô để đặt phím khác.");
    _hotkeyRec = switchHotkeyRecorder();
    adw_action_row_add_suffix(ADW_ACTION_ROW(hkRow), _hotkeyRec);
    switchRow(g1, "Âm báo khi chuyển", "Phát một âm ngắn mỗi lần bạn bấm phím chuyển Anh/Việt: lên giọng khi sang tiếng Việt, xuống giọng khi sang tiếng Anh.", "!beep", 0, G_CALLBACK(onBeep));
    // auto switch
    GtkWidget* g2 = group(page, "Tự chuyển");
    switchRow(g2, "Tự chuyển Anh/Việt", "Gõ một từ tiếng Anh khi đang ở tiếng Việt (hoặc ngược lại), Vikey tự sửa từ đó và chuyển chế độ. Bấm Xoá ngay sau đó để hoàn tác.", "AutoSwitchByTyping", 1);
    switchRow(g2, "Tạm dừng 5 giây", "Khi bạn tự bấm chuyển Anh/Việt, Vikey không tự chuyển trong 5 giây, để giữ đúng lựa chọn của bạn.", "PauseAutoAfterManual", 1);
    // Linux: Wayland gives an input method no app identity, so only X11 apps are told apart (PLAN.md)
    switchRow(g2, "Nhớ theo ứng dụng", "Vikey nhớ bạn dùng tiếng Anh hay tiếng Việt ở mỗi ứng dụng và tự chuyển lại khi bạn quay về ứng dụng đó. Trên Wayland chỉ với ứng dụng X11.", "UseSmartSwitchKey", 1);
    // Vietnamese typing
    GtkWidget* g3 = group(page, "Gõ tiếng Việt");
    GtkWidget* styleRow = row(g3, "Kiểu gõ", "Telex: [ ra ơ, ] ra ư, w ra ư. Telex đơn giản 1: [ ] là dấu ngoặc, w đứng riêng vẫn là w. Telex đơn giản 2: [ ] là dấu ngoặc, w vẫn ra ư. Cả ba kiểu đều gõ uw, ow, aw ra ư, ơ, ă.");
    const char* styles[] = {"Telex", "Telex đơn giản 1", "Telex đơn giản 2", nullptr}; GtkWidget* sd = gtk_drop_down_new_from_strings(styles); gtk_widget_set_valign(sd, GTK_ALIGN_CENTER);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(sd), (guint)std::min(2, std::max(0, prefs::getInt("TelexStyle", 0))));
    g_signal_connect(sd, "notify::selected", G_CALLBACK(onStyle), nullptr); adw_action_row_add_suffix(ADW_ACTION_ROW(styleRow), sd);
    GtkWidget* markRow = row(g3, "Đặt dấu", "Vị trí dấu thanh với oa, oe, uy: kiểu mới (hoà, thuý) hoặc kiểu cũ (hòa, thúy).");
    const char* marks[] = {"hoà, thuý", "hòa, thúy", nullptr}; GtkWidget* dd = gtk_drop_down_new_from_strings(marks); gtk_widget_set_valign(dd, GTK_ALIGN_CENTER); gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), prefs::getInt("ModernOrthography", 1) ? 0 : 1);
    g_signal_connect(dd, "notify::selected", G_CALLBACK(onMark), nullptr); adw_action_row_add_suffix(ADW_ACTION_ROW(markRow), dd);
    switchRow(g3, "Kiểm tra chính tả", "Chỉ bỏ dấu khi từ đang gõ đúng quy tắc chính tả tiếng Việt.", "Spelling", 1);
    switchRow(g3, "Trả lại phím khi sai", "Khi gõ xong một từ sai chính tả tiếng Việt, Vikey trả lại đúng các phím bạn đã gõ.", "RestoreIfInvalidWord", 1);
    // emoji picker
    GtkWidget* g4 = group(page, nullptr);
    switchRow(g4, "Bảng emoji", "Bấm phím tắt để mở bảng emoji ở giữa màn hình. Tìm bằng tiếng Việt hoặc tiếng Anh, gõ Telex vẫn tìm được: \"ngựa\", \"horse\" đều ra 🐴. Enter để chèn, Esc để đóng.", "EmojiPicker", 0);
    _emojiRow = row(g4, "Phím mở bảng emoji", "Một phím kèm Ctrl, Alt hoặc Super, ví dụ Ctrl+Alt+E. Bấm vào ô để đặt phím khác.");
    _emojiRec = emojiHotkeyRecorder(); adw_action_row_add_suffix(ADW_ACTION_ROW(_emojiRow), _emojiRec);
    // system
    GtkWidget* g5 = group(page, nullptr);
    // IBus starts Vikey with the session while it is an input source: the switch shows that (set it up in the guide)
    _loginSw = gtk_switch_new(); gtk_widget_set_valign(_loginSw, GTK_ALIGN_CENTER); gtk_widget_set_sensitive(_loginSw, FALSE);
    GtkWidget* lr = row(g5, "Mở Vikey khi đăng nhập", "IBus tự mở Vikey mỗi khi bạn đăng nhập, khi Vikey có trong Cài đặt › Bàn phím › Nguồn nhập."); adw_action_row_add_suffix(ADW_ACTION_ROW(lr), _loginSw);
    switchRow(g5, "Tự động cập nhật", "Mỗi ngày Vikey kiểm tra bản mới trên vikey.app và báo cho bạn khi có. Bản mới cài bằng gói .deb/.rpm từ vikey.app.", "AutoUpdate", 1);
    switchRow(g5, "Tự gửi báo cáo lỗi", "Khi Vikey bị thoát đột ngột, tự gửi báo cáo lỗi lần mở sau mà không hỏi. Báo cáo chỉ có thông tin kỹ thuật: vị trí lỗi trong mã, phiên bản Vikey và Linux. Không có chữ bạn đã gõ.", "CrashReportsAuto", 0);
    // send feedback (main, 1.0.25): opens vikey.app/feedback in the browser, with the platform and version
    GtkWidget* fb = row(g5, "Góp ý", "Báo lỗi hoặc góp ý cho Vikey: mở trang vikey.app/feedback trong trình duyệt.");   // same row as the Mac's
    GtkWidget* fbBtn = gtk_button_new_with_label("Gửi góp ý…"); gtk_widget_set_valign(fbBtn, GTK_ALIGN_CENTER);
    g_signal_connect(fbBtn, "clicked", G_CALLBACK(onFeedback), nullptr); adw_action_row_add_suffix(ADW_ACTION_ROW(fb), fbBtn);
    setHint(); refreshEnabled(); refreshSetup();
    _mainWin = win;
    if (_mainTick) { g_source_remove(_mainTick); _mainTick = 0; }
    _mainTick = g_timeout_add_seconds(2, [](gpointer) -> gboolean { if (!_mainWin) return G_SOURCE_REMOVE; refreshSetup(); return G_SOURCE_CONTINUE; }, nullptr);
    g_signal_connect(win, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) { _mainWin = nullptr; if (_mainTick) { g_source_remove(_mainTick); _mainTick = 0; } }), nullptr);
    GFileMonitor* mon = g_file_monitor_file(g_file_new_for_path(prefs::settingsPath().c_str()), G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr); if (mon) g_signal_connect(mon, "changed", G_CALLBACK(onSettingsFile), nullptr);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), page); adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), tv);
    gtk_window_present(GTK_WINDOW(win));
}

// ---- typing log window ---------------------------------------------------------------------------------------
static std::string tlDataDir() { return prefs::dataDir(); }
static int statSum(int days, const char* name) {
    time_t now = time(nullptr); int total = 0;
    for (auto& k : prefs::keys()) {
        if (k.rfind("TLStat_", 0) != 0 || k.size() < 19) continue;
        std::string day = k.substr(7, 10), n = k.substr(18); if (n != name) continue;
        if (days > 0) { struct tm t = {}; strptime(day.c_str(), "%Y-%m-%d", &t); if (difftime(now, mktime(&t)) > (double)days * 86400) continue; }
        total += prefs::getInt(k.c_str(), 0);
    }
    return total;
}
static std::string jget(const std::string& j, const char* key) { std::string k = std::string("\"") + key + "\":"; size_t p = j.find(k); if (p == std::string::npos) return ""; p += k.size(); if (j[p] == '"') { size_t e = p + 1; std::string o; while (e < j.size() && j[e] != '"') { if (j[e] == '\\' && e + 1 < j.size()) { e++; } o += j[e]; e++; } return o; } size_t e = j.find_first_of(",}", p); return j.substr(p, e - p); }
static void onOpenLog(GtkButton*, gpointer) { std::string d = tlDataDir(); gchar* argv[] = {(gchar*)"xdg-open", (gchar*)d.c_str(), nullptr}; g_spawn_async(nullptr, argv, nullptr, (GSpawnFlags)G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr); }
// cleared here (the engine may not be running) and by the engine, which keeps counters and log lines in memory
static void onClearLog(GtkButton*, gpointer) { ::remove((tlDataDir() + "/typing-log.jsonl").c_str()); ::remove((tlDataDir() + "/typing-log.1.jsonl").c_str()); prefs::clearStats(); prefs::sendCommand("clear-log"); }

static void buildLog(GtkApplication* app) {
    GtkWidget* win = adw_application_window_new(app); gtk_window_set_title(GTK_WINDOW(win), "Nhật ký gõ"); gtk_window_set_default_size(GTK_WINDOW(win), 720, 720);
    GtkWidget* tv = adw_toolbar_view_new(); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), adw_header_bar_new());
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12); for (auto f : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) f(box, 16);
    auto autoN = [](int d) { return statSum(d, "auto_vi_word_to_en") + statSum(d, "auto_en_word_to_vi") + statSum(d, "auto_double_key"); };
    char s[200]; snprintf(s, sizeof s, "Hôm nay: %d lần chuyển tay, %d lần tự chuyển", statSum(1, "manual"), autoN(1));
    GtkWidget* title = gtk_label_new(s); gtk_widget_add_css_class(title, "title-3"); gtk_label_set_xalign(GTK_LABEL(title), 0); gtk_box_append(GTK_BOX(box), title);
    GtkWidget* grid = gtk_grid_new(); gtk_grid_set_column_spacing(GTK_GRID(grid), 24); gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    const char* heads[] = {"", "Hôm nay", "7 ngày", "Tất cả"}; for (int c = 0; c < 4; c++) { GtkWidget* l = gtk_label_new(heads[c]); gtk_widget_add_css_class(l, "heading"); gtk_label_set_xalign(GTK_LABEL(l), c ? 1 : 0); gtk_grid_attach(GTK_GRID(grid), l, c, 0, 1, 1); }
    const char* rows[][2] = {{"Từ đã gõ","words"},{"Chuyển tay","manual"},{"Tự chuyển","auto"},{"    Tiếng Việt → Anh (cả từ)","auto_vi_word_to_en"},{"    Anh → Tiếng Việt (cả từ)","auto_en_word_to_vi"},{"    Gõ dấu hai lần → Anh","auto_double_key"},{"Hoàn tác bằng Delete","undo"},{"Đổi theo ứng dụng","context"},{"Chuyển tay mỗi 100 từ","rate"}};
    int periods[] = {1, 7, 0};
    for (int r = 0; r < 9; r++) {
        GtkWidget* l = gtk_label_new(rows[r][0]); gtk_label_set_xalign(GTK_LABEL(l), 0); gtk_widget_set_hexpand(l, TRUE); gtk_grid_attach(GTK_GRID(grid), l, 0, r + 1, 1, 1);
        for (int c = 0; c < 3; c++) { int d = periods[c]; std::string key = rows[r][1]; std::string v; int words = statSum(d, "words_vi") + statSum(d, "words_en");
            if (key == "words") v = std::to_string(words); else if (key == "auto") v = std::to_string(autoN(d)); else if (key == "rate") { if (words) { char b[32]; snprintf(b, 32, "%.1f", 100.0 * statSum(d, "manual") / words); v = b; } else v = "–"; } else v = std::to_string(statSum(d, key.c_str()));
            GtkWidget* vl = gtk_label_new(v.c_str()); gtk_label_set_xalign(GTK_LABEL(vl), 1); gtk_grid_attach(GTK_GRID(grid), vl, c + 1, r + 1, 1, 1); }
    }
    gtk_box_append(GTK_BOX(box), grid);
    GtkWidget* keysRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); GtkWidget* sw = gtk_switch_new(); gtk_switch_set_active(GTK_SWITCH(sw), prefs::getInt("TypingLogKeys", 1) != 0); g_signal_connect(sw, "state-set", G_CALLBACK(onSwitch), (gpointer)"TypingLogKeys");
    GtkWidget* kcol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2); GtkWidget* k1 = gtk_label_new("Ghi lại phím gõ của từng từ"); gtk_label_set_xalign(GTK_LABEL(k1), 0); GtkWidget* k2 = gtk_label_new("Không bao giờ ghi trong trình quản lý mật khẩu và Terminal."); gtk_widget_add_css_class(k2, "dim-label"); gtk_label_set_xalign(GTK_LABEL(k2), 0);
    gtk_box_append(GTK_BOX(kcol), k1); gtk_box_append(GTK_BOX(kcol), k2); gtk_box_append(GTK_BOX(keysRow), sw); gtk_box_append(GTK_BOX(keysRow), kcol); gtk_box_append(GTK_BOX(box), keysRow);
    GtkWidget* h = gtk_label_new("Sự kiện gần đây"); gtk_widget_add_css_class(h, "heading"); gtk_label_set_xalign(GTK_LABEL(h), 0); gtk_box_append(GTK_BOX(box), h);
    GtkWidget* list = gtk_list_box_new(); gtk_widget_add_css_class(list, "boxed-list"); gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    std::ifstream f(tlDataDir() + "/typing-log.jsonl"); std::vector<std::string> lines; std::string l; while (std::getline(f, l)) if (!l.empty()) lines.push_back(l);
    int n = 0;
    for (auto it = lines.rbegin(); it != lines.rend() && n < 200; ++it, ++n) {
        std::string e = jget(*it, "e"), to = jget(*it, "to") == "vi" ? "Tiếng Việt" : "Anh", t = jget(*it, "t"), app = jget(*it, "app");
        if (e == "cmd" || e == "key" || e == "reload" || e == "reset") { n--; continue; }   // engine debug events
        std::string time = t.size() >= 19 ? t.substr(5, 14) : t; for (auto& ch : time) if (ch == 'T') ch = ' ';
        std::string title, detail;
        if (e == "word") { title = jget(*it, "lang") == "vi" ? "Từ (Việt)" : "Từ (Anh)"; detail = jget(*it, "keys") + "   [" + jget(*it, "end") + "]"; }
        else if (e == "auto") { title = "Tự chuyển → " + to; std::string r = jget(*it, "rule"); detail = r == "vi_word_to_en" ? "từ tiếng Anh gõ khi đang ở Tiếng Việt" : r == "en_word_to_vi" ? "từ tiếng Việt gõ khi đang ở Anh" : "gõ dấu hai lần"; }
        else if (e == "manual") { title = "Chuyển tay → " + to; detail = jget(*it, "via") == "menu" ? "bằng menu" : "bằng phím tắt"; }
        else if (e == "undo") { title = "Hoàn tác → " + to; detail = "bằng Delete"; } else if (e == "context") title = "Theo ứng dụng → " + to; else title = e;
        GtkWidget* row = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), (time + "  ·  " + app + "  ·  " + title).c_str()); adw_action_row_set_subtitle(ADW_ACTION_ROW(row), detail.c_str()); gtk_list_box_append(GTK_LIST_BOX(list), row);
    }
    GtkWidget* sc = gtk_scrolled_window_new(); gtk_widget_set_vexpand(sc, TRUE); gtk_widget_set_size_request(sc, -1, 220); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), list); gtk_box_append(GTK_BOX(box), sc);
    GtkWidget* btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); GtkWidget* open = gtk_button_new_with_label("Mở file log"); GtkWidget* clear = gtk_button_new_with_label("Xoá log…"); gtk_widget_add_css_class(clear, "destructive-action"); gtk_widget_set_hexpand(open, TRUE); gtk_widget_set_halign(open, GTK_ALIGN_START);
    g_signal_connect(open, "clicked", G_CALLBACK(onOpenLog), nullptr);
    g_signal_connect(clear, "clicked", G_CALLBACK(onClearLog), nullptr);
    gtk_box_append(GTK_BOX(btns), open); gtk_box_append(GTK_BOX(btns), clear); gtk_box_append(GTK_BOX(box), btns);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), box); adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), tv); gtk_window_present(GTK_WINDOW(win));
}

// ---- About + Ghi nhận ------------------------------------------------------------------------------------------
struct Credit { const char *name, *by, *license, *url; };
// App/Model/Acknowledgements.swift without Sparkle (the Mac's updater; Linux updates come as packages)
static const Credit kCode[] = {{"OpenKey", "Tuyen Mai", "GPL-3.0", "https://github.com/tuyenvm/OpenKey"}};
static const Credit kWordData[] = {
    {"FrequencyWords (OpenSubtitles 2018)", "Hermit Dave", "CC BY-SA 4.0", "https://github.com/hermitdave/FrequencyWords"},
    {"FineWeb-2 and FineWeb", "Hugging Face", "ODC-By 1.0", "https://huggingface.co/datasets/HuggingFaceFW/fineweb-2"},
    {"HPLT v2", "High Performance Language Technologies", "CC0", "https://hplt-project.org"},
    {"MADLAD-400", "Google", "ODC-By 1.0", "https://huggingface.co/datasets/allenai/MADLAD-400"},
    {"Reddit comments", "tensorshield/reddit_dataset_157", "MIT", "https://huggingface.co/datasets/tensorshield/reddit_dataset_157"},
    {"Vietnamese forums and reader comments", "VnExpress, Tuổi Trẻ, Tinhte, Otofun, Dạy Nhau Học", "word statistics", "https://vikey.app/acknowledgements"},
    {"Emoji names and keywords (CLDR)", "Unicode, Inc.", "Unicode License v3", "https://cldr.unicode.org"},
    {"Emojibase", "Miles Johnson", "MIT", "https://emojibase.dev"}};
static const Credit kTestData[] = {
    {"ViLexNorm", "Nguyen et al., EACL 2024", "CC BY-NC-SA 4.0", "https://github.com/ngxtnhi/ViLexNorm"},
    {"UIT-ViCTSD", "UIT NLP Group", "research use", "https://huggingface.co/datasets/tarudesu/ViCTSD"},
    {"VietMix", "EACL 2026", "research use", "https://huggingface.co/datasets/razent/vietmix"},
    {"Tap typing with touch sensing images", "Google Research", "CC BY 4.0", "https://github.com/google-research-datasets/tap-typing-with-touch-sensing-images"},
    {"How We Type: Mobile", "Aalto University", "research use", "https://userinterfaces.aalto.fi/how-we-type-mobile"},
    {"GitHub Typo Corpus", "Hagiwara & Mita, LREC 2020", "research use", "https://github.com/mhagiwara/github-typo-corpus"},
    {"Birkbeck spelling error corpus", "Roger Mitton", "free to use", "https://www.dcs.bbk.ac.uk/~roger/corpora.html"},
    {"Lists of common misspellings", "Wikipedia", "CC BY-SA 4.0", "https://en.wikipedia.org/wiki/Wikipedia:Lists_of_common_misspellings"}};
template <size_t N> static void creditGroup(GtkWidget* page, const char* title, const char* note, const Credit (&items)[N]) {
    GtkWidget* g = group(page, title); adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(g), note);
    for (auto& c : items) {
        GtkWidget* r = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), c.name); adw_action_row_set_subtitle(ADW_ACTION_ROW(r), c.by);
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(r), FALSE);
        GtkWidget* lic = gtk_label_new(c.license); gtk_widget_add_css_class(lic, "dim-label"); gtk_widget_add_css_class(lic, "caption"); adw_action_row_add_suffix(ADW_ACTION_ROW(r), lic);
        adw_action_row_add_suffix(ADW_ACTION_ROW(r), gtk_image_new_from_icon_name("adw-external-link-symbolic"));
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(r), TRUE); gtk_widget_set_tooltip_text(r, c.url);
        g_signal_connect(r, "activated", G_CALLBACK(+[](AdwActionRow* r, gpointer) { openUri(gtk_widget_get_tooltip_text(GTK_WIDGET(r))); }), nullptr);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
    }
}
static AdwDialog* acknowledgementsDialog() {
    AdwDialog* d = adw_dialog_new(); adw_dialog_set_title(d, "Ghi nhận"); adw_dialog_set_content_width(d, 520); adw_dialog_set_content_height(d, 600);
    GtkWidget* tv = adw_toolbar_view_new(); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), adw_header_bar_new());
    GtkWidget* page = adw_preferences_page_new();
    creditGroup(page, "Mã nguồn", "Vikey dùng mã nguồn của:", kCode);
    creditGroup(page, "Dữ liệu từ ngữ", "Vikey tự chuyển Anh/Việt nhờ thống kê từ ngữ đếm từ các nguồn này. Trong ứng dụng chỉ có số liệu thống kê, không có văn bản nào.", kWordData);
    creditGroup(page, "Dữ liệu kiểm thử", "Chỉ dùng để đo và cải thiện độ chính xác, không có trong ứng dụng.", kTestData);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), page); adw_dialog_set_child(d, tv);
    return d;
}
void presentAcknowledgements(GtkWidget* parent) { adw_dialog_present(acknowledgementsDialog(), parent); }
/// A dialog with no main window behind it: its own window, the app kept alive until it closes.
static void presentAlone(GtkApplication* app, AdwDialog* d) {
    g_application_hold(G_APPLICATION(app));
    g_signal_connect(d, "closed", G_CALLBACK(+[](AdwDialog*, gpointer a) { g_application_release(G_APPLICATION(a)); }), app);
    adw_dialog_present(d, nullptr);
}
void buildAcknowledgements(GtkApplication* app) { presentAlone(app, acknowledgementsDialog()); }
static void buildAbout(GtkApplication* app) {
    AdwDialog* d = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(d), "Vikey"); adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(d), "vikey"); adw_about_dialog_set_version(ADW_ABOUT_DIALOG(d), VIKEY_VERSION);
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(d), "Tony Dinh"); adw_about_dialog_set_website(ADW_ABOUT_DIALOG(d), "https://vikey.app"); adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(d), GTK_LICENSE_GPL_3_0);
    adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(d), "Gõ tiếng Việt, tự chuyển Anh/Việt cho bạn.\nDựa trên OpenKey của Tuyen Mai (GPL-3.0).");
    adw_about_dialog_add_link(ADW_ABOUT_DIALOG(d), "Ghi nhận", "vikey:acknowledgements");
    g_signal_connect(d, "activate-link", G_CALLBACK(+[](AdwAboutDialog* d, const char* uri, gpointer) -> gboolean {
        if (strcmp(uri, "vikey:acknowledgements")) return FALSE;
        presentAcknowledgements(GTK_WIDGET(d)); return TRUE; }), nullptr);
    presentAlone(app, d);
}

// ---- update check ----------------------------------------------------------------------------------------------
// vikey.app/release.json has the Linux version under "linux"; the package manager installs (.deb/.rpm from the site).
static std::string fetchRelease() {
    const char* url = getenv("VIKEY_RELEASE_URL"); if (!url || !*url) url = "https://vikey.app/release.json";   // override for tests
    gchar* argv[] = {(gchar*)"curl", (gchar*)"-fsSL", (gchar*)"--max-time", (gchar*)"15", (gchar*)url, nullptr};
    gchar* out = nullptr; gint status = 0;
    if (!g_spawn_sync(nullptr, argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, &out, nullptr, &status, nullptr) || status != 0) { g_free(out); return ""; }
    std::string s = out ? out : ""; g_free(out);
    size_t p = s.find("\"linux\""); if (p == std::string::npos) return "";
    p = s.find("\"version\"", p); if (p == std::string::npos) return "";
    p = s.find('"', s.find(':', p)); size_t e = s.find('"', p + 1); if (p == std::string::npos || e == std::string::npos) return "";
    return s.substr(p + 1, e - p - 1);
}
static bool newer(const std::string& a, const std::string& b) {   // a > b, dotted numbers
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = strtol(a.c_str() + i, nullptr, 10), y = strtol(b.c_str() + j, nullptr, 10);
        if (x != y) return x > y;
        i = a.find('.', i); j = b.find('.', j); i = i == std::string::npos ? a.size() : i + 1; j = j == std::string::npos ? b.size() : j + 1;
    }
    return false;
}
static const char* kDownload = "https://vikey.app/download#linux";
void checkUpdate(GtkApplication* app, bool quiet) {
    std::string latest = fetchRelease();
    if (quiet) {   // the engine's daily check: a notification only when there is a newer version
        if (latest.empty() || !newer(latest, VIKEY_VERSION)) return;
        std::string title = "Vikey " + latest + " đã có", body = std::string("Bạn đang dùng bản ") + VIKEY_VERSION + ".";
        gchar* argv[] = {(gchar*)"notify-send", (gchar*)"-a", (gchar*)"Vikey", (gchar*)"-i", (gchar*)"vikey", (gchar*)"-A", (gchar*)"open=Tải về", (gchar*)title.c_str(), (gchar*)body.c_str(), nullptr};
        gchar* out = nullptr;
        if (g_spawn_sync(nullptr, argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, &out, nullptr, nullptr, nullptr) && out && g_str_has_prefix(out, "open")) openUri(kDownload);
        g_free(out); return;
    }
    AdwDialog* d;
    if (latest.empty()) d = adw_alert_dialog_new("Không kiểm tra được cập nhật", "Hãy kiểm tra kết nối mạng rồi thử lại.");
    else if (newer(latest, VIKEY_VERSION)) d = adw_alert_dialog_new(("Vikey " + latest + " đã có").c_str(), (std::string("Bạn đang dùng bản ") + VIKEY_VERSION + ".").c_str());
    else d = adw_alert_dialog_new("Đã là bản mới nhất", (std::string("Vikey ") + VIKEY_VERSION + ".").c_str());
    if (!latest.empty() && newer(latest, VIKEY_VERSION)) {
        adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "later", "Để sau", "download", "Tải về", nullptr);
        adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "download", ADW_RESPONSE_SUGGESTED); adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d), "download");
        g_signal_connect(d, "response", G_CALLBACK(+[](AdwAlertDialog*, const char* r, gpointer) { if (!strcmp(r, "download")) openUri(kDownload); }), nullptr);
    } else adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "ok", "OK", nullptr);
    presentAlone(app, d);
}

// ---- entry ------------------------------------------------------------------------------------------------------
static std::string _mode; static bool _quiet = false, _dialog = false;
static void onActivate(GtkApplication* app, gpointer) {
    loadCss();
    if (_mode == "--log") buildLog(app);
    else if (_mode == "--about") buildAbout(app);
    else if (_mode == "--acknowledgements") buildAcknowledgements(app);
    else if (_mode == "--guide") buildGuide(app, false);
    else if (_mode == "--emoji") buildEmoji(app);
    else if (_mode == "--check-update") checkUpdate(app, _quiet);
    else if (_mode == "--crash-report") crashReport(app, _dialog);
    else if (!prefs::getInt("OnboardingDone", 0)) buildGuide(app, true);   // first run: the guide, then this window
    else buildMain(app);
}
int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "--quiet")) _quiet = true; else if (!strcmp(argv[i], "--dialog")) _dialog = true; else if (!strncmp(argv[i], "--", 2)) _mode = argv[i]; }
    AdwApplication* app = adw_application_new(_mode == "--emoji" ? "app.vikey.Emoji" : "app.vikey.Settings", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), nullptr);
    return g_application_run(G_APPLICATION(app), 0, nullptr);
}
