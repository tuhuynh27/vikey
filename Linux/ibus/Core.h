// The Vikey core on Linux: the same state machine as App/Core/VikeyCore.mm and Windows/App/Core.cpp
// (Telex via Engine/, auto switch, Backspace undo, hotkey, per-app memory, typing log), with the
// platform underneath abstracted: keys come in from the IBus engine, output goes to an Output sink.
#pragma once
#include <string>
#include <functional>
#include "Engine.h"
namespace vikey { namespace core {
enum : unsigned { F_SHIFT = 1, F_CONTROL = 2, F_ALT = 4, F_WIN = 8, F_CAPS = 16 };
struct Output {
    std::function<void(int)> backspaces;                        // delete n characters before the cursor
    std::function<void(const std::u32string&)> text;            // type these characters
    std::function<void(Uint16 key, bool shift)> controlKey;     // re-send Enter/Tab/... (after a restore)
    std::function<void(int language)> switchSound;              // the switch sound: rising into Vietnamese, falling into English
    std::function<void()> openEmoji;                            // the emoji picker hotkey was pressed
};
void init(Output out);
/// A key press (not a release). Returns true when the key was consumed (output was produced).
bool keyDown(Uint16 key, bool shift, unsigned flags);
/// Modifier key press/release: tracks the modifiers-only hotkey. Returns true when it switched language.
bool modifier(unsigned bit, bool down);
/// A key press that goes to the app without keyDown (F-keys, a password field): releasing the modifiers held with it isn't the hotkey.
void otherKeyDown();
/// A key release (not a modifier).
void keyUp(Uint16 key);
void click();                       // a click, cursor move or reset: ends the word
void focusChanged(const std::string& app);   // app in front changed (per-app memory)
void reloadSettings();
void pollSettingsFile();
void command(const std::string& line);   // "lang vi quiet" | "lang en user"
/// Forget the word being typed (the cursor moved to another field without a click).
void resetTyping();
/// Types text into the focused app (the emoji picker's choice).
void insertText(const std::u32string& text);
void flushTypingLog(); void clearTypingLog(); std::string typingLogPath();
void setLanguageByUser(int lang); void setLanguageQuiet(int lang); void toggleLanguage(const char* via);
int language();
void setLanguageObserver(std::function<void(int)> f);
std::string frontApp();
void shutdown();
}}
