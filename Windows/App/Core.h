// The keyboard core on Windows: the low-level keyboard/mouse hooks that feed the Telex engine,
// smart switch per app/website, auto switch by typing, and the typing log. A port of
// App/Core/VikeyCore.mm (macOS) with the same behavior, decision for decision.
#pragma once
#include <windows.h>
#include <string>
#include <functional>

namespace vikey { namespace core {

bool start();                       // install hooks (needs a message loop on the calling thread)
void stop();
bool isRunning();
int language();                     // 0 English, 1 Vietnamese
void setLanguageByUser(int lang);   // from the tray menu / settings window
void setLanguageQuiet(int lang);    // tests: set the language without the 5 s pause or a log entry
void reloadSettings();              // after a setting changed in the registry
void onForegroundChanged();         // called by the foreground WinEvent hook
void onSiteChanged(const std::wstring& host);   // per-site memory (Phase 3)
std::wstring frontAppName();

/// Called on the UI thread whenever the language changes (menu icon, settings window).
void setLanguageObserver(std::function<void(int)> f);

// Emoji picker (Settings.cpp): called on the hook thread when its hotkey is pressed (EmojiPicker / EmojiHotkey prefs).
void setEmojiHotkeyHandler(std::function<void()> f);
/// While open, key downs go to the picker window as WM_APP+10 (wParam = VK, lParam = modifier flags: 1 Shift,
/// 2 Ctrl, 4 Alt, 8 Win, 16 Caps Lock); Windows' shortcuts (Alt+Tab, Win+D) or a click outside it post WM_APP+11
/// (close) and go on as usual, media keys just go on. The app in front keeps its cursor.
void setEmojiPickerOpen(bool open, HWND picker);
/// Forget the word being typed (the cursor moved to another field without a click).
void resetTyping();
/// Types text into the focused app (KEYEVENTF_UNICODE; surrogate pairs for astral emoji).
void insertText(const std::wstring& text);

// Typing log
std::wstring typingLogPath();
void flushTypingLog();
void clearTypingLog();

}} // namespace
