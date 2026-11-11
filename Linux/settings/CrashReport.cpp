// vikey-settings --crash-report [--dialog]: started by the engine after it crashed (Linux/ibus/Crash.cpp wrote the
// report, IBus started the engine again). Like the Mac's CrashReporter.swift: one question for the newest report from
// the last 3 days, every pending report marked handled either way; "always" (CrashReportsAuto) sends without asking.
// The question is a desktop notification (the engine has no window; notify-send is already used for the update
// notice), with an Adwaita dialog when there is no notification server with actions, or with --dialog.
// The report has no typed text: signal, fault address, backtrace (module+offset), Vikey version; the home folder
// is reduced to "~". Sent with curl (already a package dependency) to vikey.app/api/crash.
#include "Common.h"
#include "../ibus/Crash.h"
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/utsname.h>
#include <glib/gstdio.h>
using namespace vikey;

static const char* kTitle = "Vikey vừa bị thoát đột ngột";
static const char* kBody = "Gửi báo cáo lỗi để Vikey sửa lỗi này? Báo cáo chỉ có thông tin kỹ thuật: vị trí lỗi trong mã, phiên bản Vikey và Linux. Không có chữ bạn đã gõ.";

static std::string osDescription() {   // "Ubuntu 24.04.1 LTS, Linux 6.8.0-45-generic aarch64"
    std::string name = "Linux";
    std::ifstream f("/etc/os-release"); std::string l;
    while (std::getline(f, l)) if (l.rfind("PRETTY_NAME=", 0) == 0) { name = l.substr(12); if (name.size() >= 2 && (name[0] == '"' || name[0] == '\'')) name = name.substr(1, name.size() - 2); }
    struct utsname u; if (uname(&u) == 0) name += std::string(", ") + u.sysname + " " + u.release + " " + u.machine;
    return name.substr(0, 120);
}
static void logLine(const std::string& s) {
    std::ofstream(crash::dir() + "/sent.log", std::ios::app) << (long)time(nullptr) << " " << s << "\n";
}

/// Upload one report (blocking, curl). Returns the server's answer ({"ok":true,"id":N}) or "" on failure.
static std::string sendReport(const std::string& path) {
    std::ifstream f(path, std::ios::binary); if (!f) return "";
    std::stringstream ss; ss << f.rdbuf(); std::string text = ss.str().substr(0, 1900000);
    std::string home = g_get_home_dir(); if (home.size() > 1) for (size_t p = 0; (p = text.find(home, p)) != std::string::npos; ) { text.replace(p, home.size(), "~"); p += 1; }
    gchar* tmpDir = g_dir_make_tmp("vikey-crash-XXXXXX", nullptr); if (!tmpDir) return "";
    std::string name = path.substr(path.rfind('/') + 1), tmp = std::string(tmpDir) + "/" + name;
    { std::ofstream o(tmp, std::ios::binary); o << text; }
    const char* url = getenv("VIKEY_CRASH_URL"); if (!url || !*url) url = "https://vikey.app/api/crash";   // override for tests
    std::string fPlatform = "platform=linux", fVersion = std::string("version=") + VIKEY_VERSION, fOs = "os=" + osDescription();
    std::string fReport = "report=@" + tmp + ";filename=" + name + ";type=text/plain";
    gchar* argv[] = {(gchar*)"curl", (gchar*)"-sS", (gchar*)"--max-time", (gchar*)"30",
        (gchar*)"--form-string", (gchar*)fPlatform.c_str(), (gchar*)"--form-string", (gchar*)fVersion.c_str(), (gchar*)"--form-string", (gchar*)fOs.c_str(),
        (gchar*)"-F", (gchar*)fReport.c_str(), (gchar*)url, nullptr};
    gchar* out = nullptr; gint status = 0;
    bool ran = g_spawn_sync(nullptr, argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, &out, nullptr, &status, nullptr);
    std::string resp = ran && status == 0 && out ? out : ""; g_free(out);
    g_unlink(tmp.c_str()); g_rmdir(tmpDir); g_free(tmpDir);
    bool ok = resp.find("\"ok\":true") != std::string::npos;
    logLine((ok ? "sent " : "failed ") + name + " " + resp);
    g_printerr("Vikey crash report %s: %s\n", ok ? "sent" : "not sent", resp.c_str());
    return ok ? resp : "";
}

static std::string _report; static bool _sendChosen = false;
// A dialog without a parent window doesn't emit "closed" (libadwaita 1.5): the answer ends the process. The upload
// (blocking) waits until the dialog has gone from the screen.
static void onDialogResponse(AdwAlertDialog*, const char* r, gpointer check) {
    _sendChosen = !strcmp(r, "send");
    if (_sendChosen && gtk_check_button_get_active(GTK_CHECK_BUTTON(check))) prefs::setInt("CrashReportsAuto", 1);
    g_timeout_add(400, [](gpointer app) -> gboolean {
        if (_sendChosen) sendReport(_report);
        g_application_release(G_APPLICATION(app)); g_application_quit(G_APPLICATION(app)); return G_SOURCE_REMOVE; }, g_application_get_default());
}
static void askWithDialog(GtkApplication* app) {
    AdwDialog* d = adw_alert_dialog_new(kTitle, kBody);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "no", "Không gửi", "send", "Gửi báo cáo", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "send", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d), "send"); adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(d), "no");
    GtkWidget* check = gtk_check_button_new_with_label("Luôn gửi, không hỏi lại"); gtk_widget_set_halign(check, GTK_ALIGN_CENTER);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d), check);
    g_signal_connect(d, "response", G_CALLBACK(onDialogResponse), check);
    g_application_hold(G_APPLICATION(app));
    adw_dialog_present(d, nullptr);
}
/// The notification: blocks until the user picks an action or closes it. False when it couldn't be shown.
static bool askWithNotification() {
    gchar* argv[] = {(gchar*)"notify-send", (gchar*)"-a", (gchar*)"Vikey", (gchar*)"-i", (gchar*)"vikey", (gchar*)"-u", (gchar*)"normal",
        (gchar*)"-A", (gchar*)"send=Gửi báo cáo", (gchar*)"-A", (gchar*)"always=Luôn gửi, không hỏi lại", (gchar*)"-A", (gchar*)"no=Không gửi",
        (gchar*)kTitle, (gchar*)kBody, nullptr};
    gchar* out = nullptr; gint status = 0;
    if (!g_spawn_sync(nullptr, argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, &out, nullptr, &status, nullptr) || status != 0) { g_free(out); return false; }
    std::string a = out ? out : ""; g_free(out);
    if (a.rfind("always", 0) == 0) prefs::setInt("CrashReportsAuto", 1);
    if (a.rfind("send", 0) == 0 || a.rfind("always", 0) == 0) sendReport(_report);
    return true;   // closed without an answer = not sent
}

void crashReport(GtkApplication* app, bool dialog) {
    std::vector<std::string> files = crash::pending();
    if (files.empty()) return;
    _report = files.front();
    prefs::setInt("CrashReportsSeenUntil", (int)time(nullptr));   // handled, whatever the answer
    if (prefs::getInt("CrashReportsAuto", 0)) { sendReport(_report); return; }
    if (dialog || !askWithNotification()) askWithDialog(app);
}
