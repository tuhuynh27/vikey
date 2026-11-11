// Per-website memory: the host of the active tab of the browser in front, read through UI Automation.
#pragma once
#include <windows.h>
#include <string>
namespace vikey { namespace site {
bool isBrowser(const std::wstring& exe);
/// Posted to the owner window every second with the site in front; no parameters: read it with lastHost().
const UINT WM_SITE = WM_APP + 6;
/// The host of the active tab, as last read by the site thread (empty: not a browser, or no URL), and the window it
/// was read from: the read can take seconds, so the receiver drops it when another window is in front by now.
std::wstring lastHost(HWND* window);
/// Starts the UI Automation threads: one checks password fields, one polls the site every second (the active tab can
/// change without any event we see). UI Automation calls into other apps can block for seconds; on the hook thread
/// every mouse move would wait with them.
void start(HWND owner);
/// True while a password field has the focus, and also from a focus change until it has been checked: those keys
/// go through unchanged and stay out of the typing log.
bool focusedIsPassword();
/// Re-checks the focused control soon (call on focus events, with the event's hwnd, idObject and idChild); does not block.
void pollPassword(HWND hwnd, LONG idObject, LONG idChild);
}}
