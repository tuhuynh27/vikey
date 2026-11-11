// Live test for Vikey on Windows. Opens a window with an edit control, brings it to the front,
// types each case's keys with SendInput (as a user would; Vikey's hooks see them), and compares
// the text that ends up in the control with the expected text. Cases: cases.tsv (name<TAB>start
// language vi|en<TAB>keys<TAB>expected). Keys: letters, digits, space, punctuation as typed;
// \b Backspace, \n Enter, \t Tab, {alt+z} hotkey (default), {home} {end} {num0}..{num9} {add} {caps} named keys,
// {down:alt} {up:alt} (alt, ctrl, shift or one character) a key held across others or auto-repeating. Exit code = number of failures.
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>
#include "Prefs.h"

static HWND _edit;
static LRESULT CALLBACK testWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SETFOCUS && _edit) { SetFocus(_edit); return 0; }
    return DefWindowProcW(h, m, w, l);
}
static void key(WORD vk, bool shift = false) {
    INPUT in[4] = {}; int n = 0;
    if (shift) { in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_SHIFT; n++; }
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = vk; n++;
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = vk; in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    if (shift) { in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_SHIFT; in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++; }
    SendInput(n, in, sizeof(INPUT));
    for (int t = 0; t < 3; t++) { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(5); }
}
static void typeKeys(const std::wstring& s) {
    for (size_t i = 0; i < s.size(); i++) {
        wchar_t c = s[i];
        if (c == L'\\' && i + 1 < s.size()) { wchar_t d = s[++i]; if (d == L'b') key(VK_BACK); else if (d == L'n') key(VK_RETURN); else if (d == L't') key(VK_TAB); continue; }
        if (c == L'{') { size_t e = s.find(L'}', i); std::wstring cmd = s.substr(i + 1, e - i - 1); i = e;
            if (cmd == L"alt+z") { INPUT in[4] = {}; in[0].type = in[1].type = in[2].type = in[3].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_MENU; in[1].ki.wVk = 'Z'; in[2].ki.wVk = 'Z'; in[2].ki.dwFlags = KEYEVENTF_KEYUP; in[3].ki.wVk = VK_MENU; in[3].ki.dwFlags = KEYEVENTF_KEYUP; SendInput(4, in, sizeof(INPUT)); Sleep(30); }
            else if (cmd.compare(0, 5, L"down:") == 0 || cmd.compare(0, 3, L"up:") == 0) {
                bool up = cmd[0] == L'u'; std::wstring n = cmd.substr(up ? 3 : 5);
                INPUT in = {}; in.type = INPUT_KEYBOARD; in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
                in.ki.wVk = n == L"alt" ? VK_MENU : n == L"ctrl" ? VK_CONTROL : n == L"shift" ? VK_SHIFT : LOBYTE(VkKeyScanW(n[0]));
                SendInput(1, &in, sizeof(INPUT)); Sleep(30); }
            else if (cmd == L"wait") Sleep(1500);
            else if (cmd == L"home") key(VK_HOME);
            else if (cmd == L"end") key(VK_END);
            else if (cmd == L"add") key(VK_ADD);
            else if (cmd == L"caps") key(VK_CAPITAL);   // toggles Caps Lock: a case turns it off again
            else if (cmd.size() == 4 && cmd.compare(0, 3, L"num") == 0 && cmd[3] >= L'0' && cmd[3] <= L'9') key((WORD)(VK_NUMPAD0 + (cmd[3] - L'0')));
            continue; }
        SHORT v = VkKeyScanW(c);
        if (v == -1) {   // no key for it on this layout (ô, í): type the character itself, as another input tool would
            INPUT in[2] = {}; in[0].type = in[1].type = INPUT_KEYBOARD; in[0].ki.wScan = in[1].ki.wScan = c; in[0].ki.dwFlags = KEYEVENTF_UNICODE; in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT)); Sleep(30); continue;
        }
        key(LOBYTE(v), (HIBYTE(v) & 1) != 0);
    }
}
static std::wstring editText() { int n = GetWindowTextLengthW(_edit); std::wstring s(n, 0); GetWindowTextW(_edit, &s[0], n + 1); return s; }
static std::wstring fromUtf8(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w; }
static std::string toUtf8(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s; }

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::wstring casesPath = argc > 1 ? argv[1] : L"cases.tsv";
    std::ifstream f(casesPath); if (!f) { printf("cannot open cases file\n"); return 99; }
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {}; wc.lpfnWndProc = testWndProc; wc.hInstance = inst; wc.lpszClassName = L"VikeyLiveTest"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
    HWND win = CreateWindowExW(WS_EX_TOPMOST, L"VikeyLiveTest", L"Vikey live test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 600, 300, nullptr, nullptr, inst, nullptr);
    _edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 10, 10, 560, 240, win, nullptr, inst, nullptr);
    SendMessageW(_edit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    // Bring our window to the front even when started from a console (foreground lock): the Alt-key trick.
    auto toFront = [&]() {
        // attach to the foreground thread's input queue so SetForegroundWindow is allowed (no Alt key: it would open menu mode)
        DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), nullptr), me = GetCurrentThreadId();
        if (fg != me) AttachThreadInput(fg, me, TRUE);
        SetForegroundWindow(win); BringWindowToTop(win); SetActiveWindow(win); SetFocus(_edit);
        if (fg != me) AttachThreadInput(fg, me, FALSE);
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    };
    for (int i = 0; i < 10 && GetForegroundWindow() != win; i++) { toFront(); Sleep(200); }
    printf("foreground is %s, focus is %s\n", GetForegroundWindow() == win ? "the test window" : "ANOTHER window", GetFocus() == _edit ? "the edit" : "elsewhere");
    Sleep(300);
    int fails = 0, total = 0; std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> col; std::stringstream ss(line); std::string c; while (std::getline(ss, c, '\t')) col.push_back(c);
        if (col.size() < 4) continue;
        total++;
        // set the start language through the registry + a hotkey-free path: the tray app re-reads InputMethod on reloadSettings;
        // the simplest robust way is to toggle with the hotkey until the tray state matches, so we read it back from the registry.
        int want = col[1] == "vi" ? 1 : 0;
        HWND tray = FindWindowW(L"VikeyTray", nullptr);
        if (!tray) { printf("Vikey is not running\n"); return 98; }
        if (GetForegroundWindow() != win) toFront();
        SetWindowTextW(_edit, L""); SetFocus(_edit); SendMessageW(_edit, EM_SETSEL, 0, 0); Sleep(150);
        key(VK_ESCAPE);   // Esc: a word break, ends whatever the previous case left
        Sleep(50);
        // set the start language without the hotkey (the hotkey pauses auto switch for 5 s); it also starts a
        // fresh session, so the first word of the case counts as a whole word (Esc alone would not)
        SendMessageTimeoutW(tray, WM_APP + 2, want, 0, SMTO_NORMAL, 2000, nullptr);
        Sleep(100);
        typeKeys(fromUtf8(col[2]));
        for (int t = 0; t < 20; t++) { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(10); }
        std::wstring got = editText(); std::wstring exp = fromUtf8(col[3]);
        // the edit control uses \r\n
        std::wstring g2; for (wchar_t ch : got) if (ch != L'\r') g2 += ch;
        bool ok = g2 == exp;
        if (!ok) fails++;
        printf("%s %s: got \"%s\" expected \"%s\"\n", ok ? "ok  " : "FAIL", col[0].c_str(), toUtf8(g2).c_str(), toUtf8(exp).c_str());
        key(VK_ESCAPE);
    }
    printf("%d/%d passed\n", total - fails, total);
    DestroyWindow(win);
    return fails;
}
