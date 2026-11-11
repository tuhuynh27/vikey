#undef WIN32_LEAN_AND_MEAN   // UIAutomation needs the OLE/RPC types windows.h leaves out (before any header pulls windows.h in)
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include "Site.h"
#include "Prefs.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace vikey { namespace site {

// Everything UI Automation runs on two threads of its own, each with its own client (COM is per thread). The keyboard
// and mouse hooks run on the main thread: a UIA call into a busy browser blocks for seconds, and the cursor would
// crawl until it returned.

bool isBrowser(const std::wstring& exeIn) {
    std::wstring exe = exeIn; std::transform(exe.begin(), exe.end(), exe.begin(), ::towlower);
    static const wchar_t* list[] = {L"chrome.exe", L"msedge.exe", L"brave.exe", L"vivaldi.exe", L"opera.exe", L"opera_gx.exe", L"firefox.exe", L"chromium.exe", L"arc.exe"};
    for (auto b : list) if (exe == b) return true;
    return false;
}

static bool looksLikeUrl(const std::wstring& v) {
    if (v.empty() || v.size() > 2048) return false;
    if (v.rfind(L"http://", 0) == 0 || v.rfind(L"https://", 0) == 0) return true;
    if (v.find(L' ') != std::wstring::npos) return false;
    size_t dot = v.find(L'.');
    return dot != std::wstring::npos && dot > 0 && dot + 1 < v.size();
}

static std::wstring hostOf(std::wstring v) {
    if (v.rfind(L"file:", 0) == 0) return L"";   // a local file has no site
    size_t p = v.find(L"://"); if (p != std::wstring::npos) v = v.substr(p + 3);
    size_t e = v.find_first_of(L"/?#:"); if (e != std::wstring::npos) v = v.substr(0, e);
    std::transform(v.begin(), v.end(), v.begin(), ::towlower);
    if (v.rfind(L"www.", 0) == 0) v = v.substr(4);
    if (v.find(L'.') == std::wstring::npos && v != L"localhost") return L"";   // "C:/vk/x.html", "edge", a search: not a site
    return v;
}

/// The exe of the window in front (not core::frontAppName(): that string is the main thread's).
static std::wstring frontExe(HWND w) {
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!p) return L"";
    wchar_t path[MAX_PATH] = {0}; DWORD n = MAX_PATH; std::wstring name;
    if (QueryFullProcessImageNameW(p, 0, path, &n)) { std::wstring s(path); size_t i = s.find_last_of(L'\\'); name = i == std::wstring::npos ? s : s.substr(i + 1); }
    CloseHandle(p);
    return name;
}

/// Reads the address bar of the foreground browser; empty when not a browser or no URL.
static std::wstring currentHost(IUIAutomation* uia, HWND w) {
    if (!isBrowser(frontExe(w))) return L"";
    IUIAutomationElement* root = nullptr; if (FAILED(uia->ElementFromHandle(w, &root)) || !root) return L"";
    std::wstring host;
    VARIANT vt; vt.vt = VT_I4; vt.lVal = UIA_EditControlTypeId;
    IUIAutomationCondition* cond = nullptr;
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, vt, &cond)) && cond) {
        IUIAutomationElementArray* arr = nullptr;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, cond, &arr)) && arr) {
            int n = 0; arr->get_Length(&n);
            for (int i = 0; i < n && n < 200 && host.empty(); i++) {
                IUIAutomationElement* el = nullptr; if (FAILED(arr->GetElement(i, &el)) || !el) continue;
                IUIAutomationValuePattern* vp = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, (void**)&vp)) && vp) {
                    BSTR b = nullptr; if (SUCCEEDED(vp->get_CurrentValue(&b)) && b) { std::wstring v(b, SysStringLen(b)); SysFreeString(b); if (looksLikeUrl(v)) host = hostOf(v); }
                    vp->Release();
                }
                el->Release();
            }
            arr->Release();
        }
        cond->Release();
    }
    root->Release();
    return host;
}

/// Vikey's own windows are in front: UI Automation calls into them wait on the main thread (the main window took ~30 s
/// to show while it was busy), and there is no site or password field to read there.
static bool ownWindowInFront(HWND w) { DWORD pid = 0; GetWindowThreadProcessId(w, &pid); return pid == GetCurrentProcessId(); }

/// A UI Automation client for the calling thread (COM is per thread), or null when it cannot be created.
/// Windows 8+ gets timeouts, so a hung app costs the calling thread 2 s, not the default 20.
static IUIAutomation* createUia() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IUIAutomation2* uia2 = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia2)))) {
        uia2->put_ConnectionTimeout(1000); uia2->put_TransactionTimeout(2000); return uia2;
    }
    IUIAutomation* uia = nullptr;
    return SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) ? uia : nullptr;
}

/// Waits up to ms for the event (null: only the timeout), pumping the messages COM in an STA needs.
/// Returns true when the event fired.
static bool waitPumping(HANDLE ev, DWORD ms) {
    ULONGLONG end = GetTickCount64() + ms;
    for (;;) {
        ULONGLONG now = GetTickCount64(); DWORD left = end > now ? (DWORD)(end - now) : 0;
        DWORD r = MsgWaitForMultipleObjects(ev ? 1 : 0, ev ? &ev : nullptr, FALSE, left, QS_ALLINPUT);
        MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        if (ev && r == WAIT_OBJECT_0) return true;
        if (r != (ev ? WAIT_OBJECT_0 + 1 : WAIT_OBJECT_0) || GetTickCount64() >= end) return false;
    }
}

// Password focus: one atomic word, the focus generation (bumped on every focus change) above a 2-bit state.
// A focus change sets "unknown", which counts as a password field: until UI Automation answers, keys go through
// unchanged and stay out of the typing log. A check publishes its answer only if no focus change came in meanwhile,
// so an older answer never overwrites a newer focus. "Assumed plain" is plain without a UI Automation read (Vikey's
// own window in front, no UI Automation client): it converts keys like plain, but never vouches for an element.
enum { FOCUS_PLAIN = 0, FOCUS_PASSWORD = 1, FOCUS_UNKNOWN = 2, FOCUS_ASSUMED_PLAIN = 3, FOCUS_STATE = 3 };
static std::atomic<unsigned long long> _focus{FOCUS_ASSUMED_PLAIN};
static HANDLE _wake = nullptr;   // pollPassword(): check now instead of at the next second
struct FocusKey {
    HWND hwnd; LONG object, child;
    bool operator==(const FocusKey& o) const { return hwnd == o.hwnd && object == o.object && child == o.child; }
};
static FocusKey _lastFocus = {};   // the element of the latest focus event (main thread only)

// A focus event names its element by hwnd + idObject + idChild, the only identity the main thread gets without a UI
// Automation call. Some apps report the same triple for different fields; the password thread notices (the same
// triple, another UI Automation runtime ID) and lists the triple here: its focus events always wait for the check.
static std::mutex _focusMutex;
static FocusKey _eventKey = {};               // _lastFocus, for the password thread
static std::vector<FocusKey> _ambiguous;      // triples seen naming more than one element
static bool isAmbiguous(const FocusKey& k) {
    std::lock_guard<std::mutex> g(_focusMutex);
    for (const auto& a : _ambiguous) if (a == k) return true;
    return false;
}

bool focusedIsPassword() { int s = (int)(_focus.load() & FOCUS_STATE); return s == FOCUS_PASSWORD || s == FOCUS_UNKNOWN; }
void pollPassword(HWND hwnd, LONG idObject, LONG idChild) {
    FocusKey key = {hwnd, idObject, idChild};
    bool same = key == _lastFocus;
    _lastFocus = key;
    { std::lock_guard<std::mutex> g(_focusMutex); _eventKey = key; }
    unsigned long long v = _focus.load();
    // the element already read by UI Automation and found plain (editors and web pages fire focus events while you type):
    // keep converting the keys and re-check quietly; a password answer still lands, as the generation is the same
    if (same && (v & FOCUS_STATE) == FOCUS_PLAIN && !isAmbiguous(key)) { if (_wake) SetEvent(_wake); return; }
    while (!_focus.compare_exchange_weak(v, ((v & ~(unsigned long long)FOCUS_STATE) + 4) | FOCUS_UNKNOWN)) {}
    if (_wake) SetEvent(_wake);
}
/// Publishes a check's answer, unless the focus changed since the check started (then a newer check is on its way).
static void publishFocus(unsigned long long seen, int state) {
    _focus.compare_exchange_strong(seen, (seen & ~(unsigned long long)FOCUS_STATE) | state);
}

/// The UI Automation runtime ID of an element (empty when it has none).
static std::vector<int> runtimeId(IUIAutomationElement* el) {
    std::vector<int> id; SAFEARRAY* sa = nullptr;
    if (FAILED(el->GetRuntimeId(&sa)) || !sa) return id;
    LONG lo = 0, hi = -1; int* data = nullptr;
    if (SUCCEEDED(SafeArrayGetLBound(sa, 1, &lo)) && SUCCEEDED(SafeArrayGetUBound(sa, 1, &hi)) && SUCCEEDED(SafeArrayAccessData(sa, (void**)&data))) {
        id.assign(data, data + (hi - lo + 1)); SafeArrayUnaccessData(sa);
    }
    SafeArrayDestroy(sa);
    return id;
}

/// The password thread: checks the focused control on every focus change and every second. Kept apart from the
/// site thread so a slow address-bar read never delays the answer the keyboard hook waits for.
static void runPassword() {
    IUIAutomation* uia = nullptr;
    FocusKey plainKey = {}; std::vector<int> plainId;   // the last element read as plain, and the triple it came with
    for (;;) {
        waitPumping(_wake, 1000);
        unsigned long long seen = _focus.load();
        // no UI Automation (yet): plain, as before this thread existed; retry rather than give up for good
        if (!uia && !(uia = createUia())) { publishFocus(seen, FOCUS_ASSUMED_PLAIN); continue; }
        if (ownWindowInFront(GetForegroundWindow())) { publishFocus(seen, FOCUS_ASSUMED_PLAIN); continue; }
        IUIAutomationElement* el = nullptr; BOOL pw = FALSE;
        if (FAILED(uia->GetFocusedElement(&el)) || !el) { publishFocus(seen, FOCUS_ASSUMED_PLAIN); continue; }
        FocusKey key; { std::lock_guard<std::mutex> g(_focusMutex); key = _eventKey; }
        el->get_CurrentIsPassword(&pw); std::vector<int> id = runtimeId(el); el->Release();
        // only when no focus event came in meanwhile: then the triple and the element read belong together
        if (((_focus.load() ^ seen) & ~(unsigned long long)FOCUS_STATE) == 0 && !id.empty()) {
            if (key == plainKey && !plainId.empty() && id != plainId) {
                std::lock_guard<std::mutex> g(_focusMutex);
                if (_ambiguous.size() >= 64) _ambiguous.clear();   // hwnds get reused; a cleared triple is learned again
                bool listed = false; for (const auto& a : _ambiguous) if (a == key) listed = true;
                if (!listed) _ambiguous.push_back(key);
            }
            if (!pw) { plainKey = key; plainId = id; }
        }
        publishFocus(seen, pw ? FOCUS_PASSWORD : FOCUS_PLAIN);
    }
}

// The site thread hands the host over here, with the window it was read from; WM_SITE carries no pointer the main
// thread would have to trust.
static std::mutex _hostMutex;
static std::wstring _host;
static HWND _hostWindow = nullptr;
std::wstring lastHost(HWND* window) { std::lock_guard<std::mutex> g(_hostMutex); *window = _hostWindow; return _host; }

/// The site thread: reads the active tab's host every second (the tab can change without any event we see) and
/// tells the owner window with WM_SITE.
static void runSite(HWND owner) {
    IUIAutomation* uia = nullptr;
    for (;;) {
        waitPumping(nullptr, 1000);
        if (!uia && !(uia = createUia())) continue;
        HWND w = GetForegroundWindow();
        if (ownWindowInFront(w)) continue;
        std::wstring host = prefs::getInt(L"SmartSwitchPerSite", 1) ? currentHost(uia, w) : L"";
        { std::lock_guard<std::mutex> g(_hostMutex); _host = host; _hostWindow = w; }
        PostMessageW(owner, WM_SITE, 0, 0);   // the main thread's onSiteChanged skips an unchanged host
    }
}

void start(HWND owner) {
    if (_wake) return;
    _wake = CreateEventW(nullptr, FALSE, TRUE, nullptr);   // signaled: the first password check runs right away
    std::thread(runPassword).detach();
    std::thread(runSite, owner).detach();
}

}}
