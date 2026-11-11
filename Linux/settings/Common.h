// Shared pieces of vikey-settings: hotkey names and the shortcut recorder (same bit layout as the Mac/Windows
// apps: low byte = key code, 0xFE = modifiers only, 0x100 Ctrl, 0x200 Alt, 0x400 Super, 0x800 Shift, 0x8000 sound),
// the (?) tip next to a setting's title, and the window entry points.
#pragma once
#include <adwaita.h>
#include <string>
#include <functional>
#include "../ibus/Prefs.h"
#ifndef VIKEY_VERSION
#define VIKEY_VERSION "0.0.0"
#endif
#ifndef VIKEY_DATADIR
#define VIKEY_DATADIR "/usr/share/vikey"
#endif
namespace ui {
int currentHotkey();                            // the Vietnamese/English switch, default Alt+Z
std::string hotkeyDisplay(int hk);              // "Alt+Z", "Ctrl+Shift"; "Chưa đặt" for 0
int engineKeyOfKeyval(guint keyval);            // GDK keyval -> the engine's (Mac) key code, -1 if none
unsigned modsOf(GdkModifierType state);         // GDK modifiers -> hotkey bits
/// Click, then press the shortcut: a key with modifiers (Alt+Z) or, when allowed, two or more modifiers alone.
/// `set` gets the new value (without the sound bit). Esc cancels.
GtkWidget* recorderNew(std::function<int()> get, std::function<void(int)> set, bool allowModifiersOnly, bool autoStart);
void recorderRefresh(GtkWidget* recorder);
/// A setting's title with a (?) that shows the tip (hover: tooltip, click: popover), like the Mac's Tip view.
GtkWidget* tipTitle(const char* title, const char* tip);
/// "Vi" + "key" in brand indigo, bold.
GtkWidget* wordmark(int px);
void openUri(const char* uri);
void loadCss();
}
void buildMain(GtkApplication* app);
void buildGuide(GtkApplication* app, bool thenMain);
void buildEmoji(GtkApplication* app);
void presentAcknowledgements(GtkWidget* parent);
void buildAcknowledgements(GtkApplication* app);
void checkUpdate(GtkApplication* app, bool quiet);
void crashReport(GtkApplication* app, bool dialog);   // --crash-report [--dialog] (CrashReport.cpp)
