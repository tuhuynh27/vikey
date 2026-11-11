// The guide (vikey-settings --guide, and the first run): the Mac OnboardingView's steps and copy.
// Telex only -> IBus setup (Linux's step instead of the Mac's Accessibility permission: Vikey must be an input
// source, checked live through org.gnome.desktop.input-sources) -> type a few real sentences -> the switch hotkey
// -> the emoji picker (optional).
#include "Common.h"
#include <vector>
#include <cstring>
using namespace vikey;
using namespace ui;
int vikeyInInputSources();
void spawnSelf(const char* arg);

namespace {
enum Step { WELCOME, SETUP, PRACTICE, HOTKEY, EMOJI, COUNT };
const char* kNames[] = {"welcome", "setup", "practice", "hotkey", "emoji"};
struct Line { const char *keys, *result; };
const Line kLines[] = {{"order traf suwax nhes", "order trà sữa nhé"}, {"meeting lucs ba giowf nhes", "meeting lúc ba giờ nhé"}, {"thanks, camr own nhieeuf", "thanks, cảm ơn nhiều"}};
struct Guide {
    GtkApplication* app; bool thenMain; GtkWidget* win; GtkWidget* stack; GtkWidget* dots[COUNT];
    GtkWidget *back, *next, *waitBox, *doneBox;
    GtkWidget* practiceText; GtkWidget* entries[3]; GtkWidget* marks[3]; bool ok[3] = {};
    GtkWidget *typingNow, *beepSw, *hotkeyRec, *emojiSw, *emojiBox, *emojiRec, *emojiHint;
    Step step = WELCOME; int lastLang = -1;
    guint tick = 0;
} G;

GtkWidget* label(const char* text, const char* cls = nullptr, bool wrap = true) {
    GtkWidget* l = gtk_label_new(text); gtk_label_set_xalign(GTK_LABEL(l), 0); if (wrap) { gtk_label_set_wrap(GTK_LABEL(l), TRUE); gtk_label_set_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR); }
    if (cls) { gchar** parts = g_strsplit(cls, " ", 0); for (int i = 0; parts[i]; i++) gtk_widget_add_css_class(l, parts[i]); g_strfreev(parts); }
    return l;
}
GtkWidget* vbox(int spacing) { return gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing); }
GtkWidget* hbox(int spacing) { return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing); }
std::string norm(const char* s) {   // trim, NFC, lowercase (the Mac's normalize)
    gchar* t = g_strstrip(g_strdup(s)); gchar* n = g_utf8_normalize(t, -1, G_NORMALIZE_NFC); gchar* d = g_utf8_strdown(n ? n : t, -1);
    std::string r = d; g_free(t); g_free(n); g_free(d); return r;
}
int language() { return prefs::getInt("InputMethod", 1) ? 1 : 0; }

void refresh() {
    for (int i = 0; i < COUNT; i++) { gtk_widget_remove_css_class(G.dots[i], i == G.step ? "off" : "on"); gtk_widget_add_css_class(G.dots[i], i == G.step ? "on" : "off"); }
    gtk_widget_set_visible(G.back, G.step != WELCOME);
    int in = getenv("VIKEY_PREVIEW_SETUP") ? 0 : vikeyInInputSources();   // screenshots of the waiting state
    bool waiting = G.step == SETUP && in == 0;
    gtk_widget_set_visible(G.waitBox, waiting); gtk_widget_set_visible(G.doneBox, G.step == SETUP && in == 1);
    const char* nextLabel = G.step == WELCOME ? "Bắt đầu" : G.step == EMOJI ? "Xong" : "Tiếp tục";
    gtk_button_set_label(GTK_BUTTON(G.next), nextLabel);
    bool emojiOn = prefs::getInt("EmojiPicker", 0) != 0; int emojiKey = prefs::getInt("EmojiHotkey", 0);
    gtk_widget_set_sensitive(G.next, !waiting && !(G.step == EMOJI && emojiOn && emojiKey == 0));
    int vi = language();
    std::string hk = hotkeyDisplay(currentHotkey());
    gtk_label_set_text(GTK_LABEL(G.practiceText), vi ? "Gõ đúng các phím bên dưới, kể cả tiếng Anh. Vikey bỏ dấu cho tiếng Việt và để nguyên tiếng Anh."
                                                    : ("Bạn đang ở tiếng Anh: bấm " + hk + " để sang tiếng Việt, rồi gõ các phím bên dưới.").c_str());
    gtk_label_set_text(GTK_LABEL(G.typingNow), vi ? "Đang gõ: Tiếng Việt" : "Đang gõ: English");
    recorderRefresh(G.hotkeyRec); recorderRefresh(G.emojiRec);
    bool beep = (currentHotkey() & 0x8000) != 0; if (gtk_switch_get_active(GTK_SWITCH(G.beepSw)) != beep) gtk_switch_set_active(GTK_SWITCH(G.beepSw), beep);
    gtk_widget_set_visible(G.emojiBox, emojiOn); gtk_widget_set_visible(G.emojiHint, emojiOn && emojiKey == 0);
}
// A running IBus lists only the engines it found when it started, so right after the install Settings can't offer
// Vikey (#50; ibus write-cache doesn't change that, a restart does). Restart it once if it runs but doesn't know Vikey.
void restartIBusIfVikeyMissing() {
    static bool done = false; if (done) return; done = true;
    GSubprocess* p = g_subprocess_new((GSubprocessFlags)(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE), nullptr, "ibus", "list-engine", "--name-only", nullptr);
    if (!p) return;   // no ibus command
    g_subprocess_communicate_utf8_async(p, nullptr, nullptr, [](GObject* o, GAsyncResult* res, gpointer) {   // async: a stuck IBus can't freeze the guide
        GSubprocess* p = G_SUBPROCESS(o); gchar* out = nullptr;
        bool ok = g_subprocess_communicate_utf8_finish(p, res, &out, nullptr, nullptr) && g_subprocess_get_successful(p);
        bool any = false, known = false; gchar** lines = g_strsplit(out ? out : "", "\n", -1);
        for (int i = 0; lines[i]; i++) { const char* l = g_strstrip(lines[i]); if (*l) any = true; if (!strcmp(l, "vikey")) known = true; }
        g_strfreev(lines); g_free(out); g_object_unref(p);
        if (!ok || !any || known) return;   // IBus isn't running (another input framework), its list failed (empty: ibus exits 0 then), or it knows Vikey
        gchar* r[] = {(gchar*)"ibus", (gchar*)"restart", nullptr}; g_spawn_async(nullptr, r, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, nullptr, nullptr);
    }, nullptr);
}
void go(Step s) {
    G.step = s; gtk_stack_set_visible_child_name(GTK_STACK(G.stack), kNames[s]); refresh();
    if (s == SETUP) restartIBusIfVikeyMissing();
    if (s == PRACTICE) g_timeout_add(200, [](gpointer) -> gboolean { gtk_widget_grab_focus(G.entries[0]); return FALSE; }, nullptr);
}
void stopTimer() { if (G.tick) { g_source_remove(G.tick); G.tick = 0; } }
void finish() { prefs::setInt("OnboardingDone", 1); stopTimer(); bool m = G.thenMain; GtkApplication* app = G.app; gtk_window_destroy(GTK_WINDOW(G.win)); if (m) buildMain(app); }

GtkWidget* welcomePage() {
    GtkWidget* b = vbox(14);
    gtk_box_append(GTK_BOX(b), label("Vikey dùng kiểu gõ Telex", "title-2"));
    gtk_box_append(GTK_BOX(b), label("Vikey chỉ hỗ trợ Telex, kiểu gõ phổ biến nhất. Bạn gõ tiếng Anh bình thường, Vikey tự nhận ra và không bỏ dấu nhầm.", "dim-label"));
    GtkWidget* grid = gtk_grid_new(); gtk_grid_set_column_spacing(GTK_GRID(grid), 18); gtk_grid_set_row_spacing(GTK_GRID(grid), 8); gtk_widget_add_css_class(grid, "telex-card");
    const char* t[][2] = {{"aa","â"},{"aw","ă"},{"ee","ê"},{"oo","ô"},{"ow","ơ"},{"uw","ư"},{"dd","đ"},{"w","ư"},{"s","sắc"},{"f","huyền"},{"r","hỏi"},{"x","ngã"},{"j","nặng"},{"z","xoá dấu"}};
    for (int i = 0; i < 14; i++) {
        GtkWidget* c = hbox(6); gtk_box_append(GTK_BOX(c), label(t[i][0], "telex-key", false)); gtk_box_append(GTK_BOX(c), label("→", "dim-label", false)); gtk_box_append(GTK_BOX(c), label(t[i][1], nullptr, false));
        gtk_grid_attach(GTK_GRID(grid), c, i % 4, i / 4, 1, 1);
    }
    gtk_widget_set_halign(grid, GTK_ALIGN_START); gtk_box_append(GTK_BOX(b), grid);
    return b;
}
GtkWidget* numbered(int n, const char* text) {
    GtkWidget* h = hbox(8); char num[4]; snprintf(num, sizeof num, "%d", n);
    GtkWidget* c = label(num, "step-num", false); gtk_label_set_xalign(GTK_LABEL(c), 0.5); gtk_widget_set_valign(c, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(h), c); gtk_box_append(GTK_BOX(h), label(text, "dim-label")); return h;
}
void openKeyboardSettings() {
    gchar* argv[] = {(gchar*)"gnome-control-center", (gchar*)"keyboard", nullptr};
    if (!g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr)) { gchar* a2[] = {(gchar*)"ibus-setup", nullptr}; g_spawn_async(nullptr, a2, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr); }
}
GtkWidget* setupPage() {
    GtkWidget* b = vbox(16);
    gtk_box_append(GTK_BOX(b), label("Thêm Vikey vào nguồn nhập", "title-2"));
    gtk_box_append(GTK_BOX(b), label("Vikey là một bộ gõ IBus: thêm Vikey vào nguồn nhập để gõ tiếng Việt trong mọi ứng dụng. Mọi thứ xử lý ngay trên máy, không gửi đi đâu.", "dim-label"));
    GtkWidget* steps = vbox(8);
    gtk_box_append(GTK_BOX(steps), numbered(1, "Bấm nút bên dưới để mở Cài đặt › Bàn phím"));
    gtk_box_append(GTK_BOX(steps), numbered(2, "Bấm + trong Nguồn nhập, chọn Vietnamese › Vietnamese (Vikey)"));
    gtk_box_append(GTK_BOX(steps), numbered(3, "Quay lại đây, Vikey sẽ tự nhận ra"));
    gtk_box_append(GTK_BOX(b), steps);
    GtkWidget* btn = gtk_button_new_with_label("Mở Cài đặt bàn phím"); gtk_widget_add_css_class(btn, "suggested-action"); gtk_widget_add_css_class(btn, "pill"); gtk_widget_set_halign(btn, GTK_ALIGN_START);
    g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) { openKeyboardSettings(); }), nullptr);
    gtk_box_append(GTK_BOX(b), btn);
    return b;
}
void checkLine(int i) {
    bool ok = norm(gtk_editable_get_text(GTK_EDITABLE(G.entries[i]))) == norm(kLines[i].result);
    gtk_image_set_from_icon_name(GTK_IMAGE(G.marks[i]), ok ? "emblem-ok-symbolic" : "radio-symbolic");
    if (ok) { gtk_widget_add_css_class(G.marks[i], "ok-mark"); gtk_widget_remove_css_class(G.marks[i], "dim-label"); } else { gtk_widget_remove_css_class(G.marks[i], "ok-mark"); gtk_widget_add_css_class(G.marks[i], "dim-label"); }
    if (ok && !G.ok[i] && i + 1 < 3) gtk_widget_grab_focus(G.entries[i + 1]);   // the next field starts a new word, as after a click
    G.ok[i] = ok;
}
GtkWidget* practicePage() {
    GtkWidget* b = vbox(12);
    gtk_box_append(GTK_BOX(b), label("Gõ thử", "title-2"));
    G.practiceText = label("", "dim-label"); gtk_label_set_lines(GTK_LABEL(G.practiceText), 2); gtk_widget_set_size_request(G.practiceText, -1, 40);   // both texts take two lines: switching VI/EN must not move the fields (Tony)
    gtk_widget_set_valign(G.practiceText, GTK_ALIGN_START); gtk_box_append(GTK_BOX(b), G.practiceText);
    for (int i = 0; i < 3; i++) {
        GtkWidget* item = vbox(4); GtkWidget* cap = hbox(6);
        gtk_box_append(GTK_BOX(cap), label("Gõ", "dim-label", false)); gtk_box_append(GTK_BOX(cap), label(kLines[i].keys, "telex-key", false));
        gtk_box_append(GTK_BOX(cap), label("→", "dim-label", false)); gtk_box_append(GTK_BOX(cap), label(kLines[i].result, nullptr, false));
        gtk_box_append(GTK_BOX(item), cap);
        GtkWidget* row = hbox(8); G.entries[i] = gtk_entry_new(); gtk_widget_set_hexpand(G.entries[i], TRUE);
        g_signal_connect(G.entries[i], "changed", G_CALLBACK(+[](GtkEditable*, gpointer p) { checkLine(GPOINTER_TO_INT(p)); }), GINT_TO_POINTER(i));
        g_signal_connect(G.entries[i], "activate", G_CALLBACK(+[](GtkEntry*, gpointer p) { int i = GPOINTER_TO_INT(p); gtk_widget_grab_focus(G.entries[i + 1 < 3 ? i + 1 : i]); }), GINT_TO_POINTER(i));
        G.marks[i] = gtk_image_new_from_icon_name("radio-symbolic"); gtk_image_set_pixel_size(GTK_IMAGE(G.marks[i]), 20); gtk_widget_add_css_class(G.marks[i], "dim-label");
        gtk_box_append(GTK_BOX(row), G.entries[i]); gtk_box_append(GTK_BOX(row), G.marks[i]); gtk_box_append(GTK_BOX(item), row);
        gtk_box_append(GTK_BOX(b), item);
    }
    return b;
}
GtkWidget* hotkeyPage() {
    GtkWidget* b = vbox(14);
    gtk_box_append(GTK_BOX(b), label("Phím chuyển Anh/Việt", "title-2"));
    gtk_box_append(GTK_BOX(b), label("Vikey tự chuyển khi bạn gõ lẫn tiếng Anh. Khi muốn tự chuyển, bấm phím tắt này. Bấm vào ô để đổi phím.", "dim-label"));
    GtkWidget* h = hbox(12);
    G.hotkeyRec = recorderNew([] { return currentHotkey(); }, [](int k) { prefs::setInt("SwitchKeyStatus", k | (currentHotkey() & 0x8000)); refresh(); }, true, false);
    G.typingNow = label("", "dim-label", false);
    gtk_box_append(GTK_BOX(h), G.hotkeyRec); gtk_box_append(GTK_BOX(h), G.typingNow); gtk_box_append(GTK_BOX(b), h);
    GtkWidget* s = hbox(12); GtkWidget* l = label("Âm báo khi chuyển", nullptr, false); gtk_widget_set_hexpand(l, TRUE);
    G.beepSw = gtk_switch_new(); gtk_widget_set_valign(G.beepSw, GTK_ALIGN_CENTER);
    g_signal_connect(G.beepSw, "state-set", G_CALLBACK(+[](GtkSwitch* sw, gboolean on, gpointer) -> gboolean { prefs::setInt("SwitchKeyStatus", (currentHotkey() & ~0x8000) | (on ? 0x8000 : 0)); gtk_switch_set_state(sw, on); return TRUE; }), nullptr);
    gtk_box_append(GTK_BOX(s), l); gtk_box_append(GTK_BOX(s), G.beepSw); gtk_box_append(GTK_BOX(b), s);
    return b;
}
GtkWidget* emojiPage() {
    GtkWidget* b = vbox(14);
    GtkWidget* t = hbox(8); gtk_box_append(GTK_BOX(t), label("Bảng emoji", "title-2", false)); GtkWidget* opt = label("không bắt buộc", "dim-label", false); gtk_widget_set_valign(opt, GTK_ALIGN_BASELINE_CENTER); gtk_box_append(GTK_BOX(t), opt);
    gtk_box_append(GTK_BOX(b), t);
    gtk_box_append(GTK_BOX(b), label("Bấm một phím tắt để mở bảng emoji ở giữa màn hình. Tìm bằng tiếng Việt hay tiếng Anh đều được: \"ngựa\" hay \"horse\" đều ra 🐴.", "dim-label"));
    GtkWidget* s = hbox(12); GtkWidget* l = label("Bật bảng emoji", nullptr, false); gtk_widget_set_hexpand(l, TRUE);
    G.emojiSw = gtk_switch_new(); gtk_widget_set_valign(G.emojiSw, GTK_ALIGN_CENTER); gtk_switch_set_active(GTK_SWITCH(G.emojiSw), prefs::getInt("EmojiPicker", 0) != 0);
    g_signal_connect(G.emojiSw, "state-set", G_CALLBACK(+[](GtkSwitch* sw, gboolean on, gpointer) -> gboolean { prefs::setInt("EmojiPicker", on ? 1 : 0); gtk_switch_set_state(sw, on); refresh(); return TRUE; }), nullptr);
    gtk_box_append(GTK_BOX(s), l); gtk_box_append(GTK_BOX(s), G.emojiSw); gtk_box_append(GTK_BOX(b), s);
    G.emojiBox = hbox(12); gtk_box_append(GTK_BOX(G.emojiBox), label("Phím mở bảng emoji", nullptr, false));
    G.emojiRec = recorderNew([] { return prefs::getInt("EmojiHotkey", 0); }, [](int k) { if (k == (currentHotkey() & ~0x8000)) return; prefs::setInt("EmojiHotkey", k & ~0x8000); refresh(); }, false, true);
    gtk_box_append(GTK_BOX(G.emojiBox), G.emojiRec); gtk_box_append(GTK_BOX(b), G.emojiBox);
    G.emojiHint = label("Bấm một phím kèm Ctrl, Alt hoặc Super, ví dụ Ctrl+Alt+E.", "dim-label"); gtk_box_append(GTK_BOX(b), G.emojiHint);
    return b;
}
void onNext(GtkButton*, gpointer) {
    switch (G.step) {
    case WELCOME: go(vikeyInInputSources() == 1 && !getenv("VIKEY_PREVIEW_SETUP") ? PRACTICE : SETUP); break;
    case EMOJI: finish(); break;
    default: go((Step)(G.step + 1));
    }
}
}  // namespace

void buildGuide(GtkApplication* app, bool thenMain) {
    G.app = app; G.thenMain = thenMain;
    GtkCssProvider* p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p, ".step-num { min-width: 20px; min-height: 20px; border-radius: 10px; background: alpha(@window_fg_color, 0.40); color: @window_bg_color; font-weight: bold; font-size: 0.85em; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    G.win = adw_application_window_new(app); gtk_window_set_title(GTK_WINDOW(G.win), "Vikey"); gtk_window_set_default_size(GTK_WINDOW(G.win), 540, 470 + 47); gtk_window_set_resizable(GTK_WINDOW(G.win), FALSE);
    g_signal_connect(G.win, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer) -> gboolean { prefs::setInt("OnboardingDone", 1); stopTimer(); return FALSE; }), nullptr);
    GtkWidget* tv = adw_toolbar_view_new(); GtkWidget* hb = adw_header_bar_new(); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);
    GtkWidget* root = vbox(0); for (auto f : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_bottom}) f(root, 28); gtk_widget_set_margin_top(root, 8);
    // header: icon, wordmark + tagline, step dots
    GtkWidget* head = hbox(14); gtk_widget_set_margin_bottom(head, 22);
    GtkWidget* icon = gtk_image_new_from_icon_name("vikey"); gtk_image_set_pixel_size(GTK_IMAGE(icon), 48); gtk_box_append(GTK_BOX(head), icon);
    GtkWidget* col = vbox(2); gtk_widget_set_valign(col, GTK_ALIGN_CENTER); gtk_widget_set_hexpand(col, TRUE);
    gtk_box_append(GTK_BOX(col), wordmark(24)); gtk_box_append(GTK_BOX(col), label("Gõ tiếng Việt, tự chuyển Anh/Việt cho bạn.", "dim-label", false)); gtk_box_append(GTK_BOX(head), col);
    GtkWidget* dots = hbox(6); gtk_widget_add_css_class(dots, "dots"); gtk_widget_set_valign(dots, GTK_ALIGN_CENTER);
    for (int i = 0; i < COUNT; i++) { G.dots[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_set_valign(G.dots[i], GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(dots), G.dots[i]); }
    gtk_box_append(GTK_BOX(head), dots); gtk_box_append(GTK_BOX(root), head);
    G.stack = gtk_stack_new(); gtk_stack_set_transition_type(GTK_STACK(G.stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE); gtk_stack_set_transition_duration(GTK_STACK(G.stack), 150); gtk_widget_set_vexpand(G.stack, TRUE);
    GtkWidget* pages[] = {welcomePage(), setupPage(), practicePage(), hotkeyPage(), emojiPage()};
    for (int i = 0; i < COUNT; i++) { gtk_widget_set_valign(pages[i], GTK_ALIGN_START); gtk_stack_add_named(GTK_STACK(G.stack), pages[i], kNames[i]); }
    gtk_box_append(GTK_BOX(root), G.stack);
    // footer: Quay lại · (waiting for the input source) · Bắt đầu / Tiếp tục / Xong
    GtkWidget* foot = hbox(8); gtk_widget_set_margin_top(foot, 20);
    G.back = gtk_button_new_with_label("Quay lại"); g_signal_connect(G.back, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) { go((Step)(G.step - 1)); }), nullptr); gtk_box_append(GTK_BOX(foot), G.back);
    GtkWidget* sp = hbox(0); gtk_widget_set_hexpand(sp, TRUE); gtk_box_append(GTK_BOX(foot), sp);
    G.waitBox = hbox(6); GtkWidget* spin = gtk_spinner_new(); gtk_spinner_start(GTK_SPINNER(spin)); gtk_box_append(GTK_BOX(G.waitBox), spin); gtk_box_append(GTK_BOX(G.waitBox), label("Đang chờ Vikey trong nguồn nhập…", "dim-label", false)); gtk_widget_set_margin_end(G.waitBox, 6);
    G.doneBox = hbox(6); GtkWidget* okI = gtk_image_new_from_icon_name("emblem-ok-symbolic"); gtk_widget_add_css_class(okI, "ok-mark"); gtk_box_append(GTK_BOX(G.doneBox), okI); gtk_box_append(GTK_BOX(G.doneBox), label("Đã thêm Vikey", "dim-label", false)); gtk_widget_set_margin_end(G.doneBox, 6);
    gtk_box_append(GTK_BOX(foot), G.waitBox); gtk_box_append(GTK_BOX(foot), G.doneBox);
    G.next = gtk_button_new_with_label(""); gtk_widget_add_css_class(G.next, "suggested-action"); g_signal_connect(G.next, "clicked", G_CALLBACK(onNext), nullptr); gtk_box_append(GTK_BOX(foot), G.next);
    gtk_box_append(GTK_BOX(root), foot);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), root); adw_application_window_set_content(ADW_APPLICATION_WINDOW(G.win), tv);
    if (const char* s = getenv("VIKEY_GUIDE_STEP")) { int i = atoi(s); if (i >= 0 && i < COUNT) G.step = (Step)i; }   // screenshots
    go(G.step);
    // live: the input source check, the language (practice text), the hotkey
    G.tick = g_timeout_add(500, [](gpointer) -> gboolean {
        prefs::reload();
        if (G.step == SETUP && vikeyInInputSources() == 1 && !getenv("VIKEY_PREVIEW_SETUP")) go(PRACTICE); else refresh();
        return G_SOURCE_CONTINUE; }, nullptr);
    gtk_window_set_default_widget(GTK_WINDOW(G.win), G.next);
    gtk_window_present(GTK_WINDOW(G.win));
}
