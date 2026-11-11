#define _HAS_STD_BYTE 0
#include "CrashReporter.h"
#include "Prefs.h"
#include "resource.h"
#include <commctrl.h>
#include <winhttp.h>
#include <shlobj.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <exception>
#include <string>
#include <vector>
#include <algorithm>

#ifndef VIKEY_BUILD
#define VIKEY_BUILD "0"
#endif
#if defined(_M_ARM64)
#define VIKEY_ARCH "arm64"
#elif defined(_M_X64)
#define VIKEY_ARCH "x64"
#else
#define VIKEY_ARCH "x86"
#endif

namespace vikey { namespace crash {

static const wchar_t* AUTO_KEY = L"CrashReportsAuto";        // the user chose "always send, don't ask"
static const wchar_t* SEEN_KEY = L"CrashReportsSeenUntil";   // unix seconds, as a string
static const wchar_t* DEFAULT_ENDPOINT = L"https://vikey.app/api/crash";

// Everything the crash handler needs is prepared at install time: it runs in a broken process and should not allocate.
static wchar_t _dir[MAX_PATH];
static char _os[160];
static volatile LONG _writing = 0;
static DWORD _mainThread = 0;

// ---- helpers ------------------------------------------------------------------------------------
static std::string narrow(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s; }
static std::wstring widen(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w; }

static std::wstring regString(HKEY root, const wchar_t* path, const wchar_t* name) {
    wchar_t buf[128] = {0}; DWORD size = sizeof(buf);
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return L"";
    return buf;
}
static DWORD regDword(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD v = 0, size = sizeof(v);
    RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &v, &size); return v;
}

/// "Windows 11 Pro 25H2 (10.0.26200.7019, arm64)"
static std::string windowsVersion() {
    typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v = {sizeof(v)};
    if (auto f = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) f(&v);
    const wchar_t* cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::wstring edition = regString(HKEY_LOCAL_MACHINE, cv, L"EditionID"), display = regString(HKEY_LOCAL_MACHINE, cv, L"DisplayVersion");
    DWORD ubr = regDword(HKEY_LOCAL_MACHINE, cv, L"UBR");
    const char* machine = "unknown";
    USHORT pm = 0, nm = 0;
    typedef BOOL (WINAPI *IsWow64Process2Fn)(HANDLE, USHORT*, USHORT*);
    if (auto f = (IsWow64Process2Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2")) f(GetCurrentProcess(), &pm, &nm);
    if (nm == IMAGE_FILE_MACHINE_ARM64) machine = "arm64"; else if (nm == IMAGE_FILE_MACHINE_AMD64) machine = "x64"; else if (nm == IMAGE_FILE_MACHINE_I386) machine = "x86";
    char buf[160];
    _snprintf_s(buf, _TRUNCATE, "Windows %s%s%s%s%s (%lu.%lu.%lu.%lu, %s)", v.dwBuildNumber >= 22000 ? "11" : "10",
        edition.empty() ? "" : " ", narrow(edition).c_str(), display.empty() ? "" : " ", narrow(display).c_str(),
        v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, ubr, machine);
    return buf;
}

// ---- writing the report (in the crashing process) -----------------------------------------------
struct Out { char buf[16384]; int len = 0;
    void add(const char* fmt, ...) { va_list a; va_start(a, fmt); int n = _vsnprintf_s(buf + len, sizeof(buf) - len, _TRUNCATE, fmt, a); va_end(a); if (n > 0) len += n; else len = (int)strlen(buf); }
};
static Out _out;   // static: no allocation, and a stack overflow leaves little room

/// "Vikey.exe+0x1a2b3": the module file name (no folder) and the offset in it.
static void addLocation(Out& o, DWORD64 pc) {
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)pc, &m) && m) {
        wchar_t path[MAX_PATH] = {0}; GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\'); name = name ? name + 1 : path;
        char n8[MAX_PATH * 3] = {0}; WideCharToMultiByte(CP_UTF8, 0, name, -1, n8, sizeof(n8), nullptr, nullptr);
        o.add("%s+0x%llx", n8, (unsigned long long)(pc - (DWORD64)m));
    } else o.add("0x%016llx (no module)", (unsigned long long)pc);
}

static DWORD64 pcOf(const CONTEXT& c) {
#if defined(_M_ARM64)
    return c.Pc;
#elif defined(_M_X64)
    return c.Rip;
#else
    return c.Eip;
#endif
}

/// Walks the stack with the unwind data in each module (RtlVirtualUnwind): no DbgHelp, no symbols, no allocation.
static void walk(Out& o, CONTEXT& ctx);
static void addStack(Out& o, CONTEXT ctx) {
    __try { walk(o, ctx); } __except (EXCEPTION_EXECUTE_HANDLER) { o.add("  (stack walk stopped: bad frame)\n"); }
}
static void walk(Out& o, CONTEXT& ctx) {
    o.add("\nStack (module+offset):\n");
#if defined(_M_X64) || defined(_M_ARM64)
    for (int i = 0; i < 64; i++) {
        DWORD64 pc = pcOf(ctx);
        if (!pc) break;
        o.add("%3d  ", i); addLocation(o, pc); o.add("\n");
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(pc, &base, nullptr);
        if (fn) {
            PVOID handlerData = nullptr; DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, pc, fn, &ctx, &handlerData, &frame, nullptr);
        } else {   // a leaf function: the return address is on top of the stack (x64) or in the link register (ARM64)
#if defined(_M_X64)
            if (!ctx.Rsp) break;
            ctx.Rip = *(DWORD64*)ctx.Rsp; ctx.Rsp += 8;
#else
            if (ctx.Lr == ctx.Pc) break;
            ctx.Pc = ctx.Lr;
#endif
        }
        if (pcOf(ctx) == pc) break;
    }
#else
    o.add("  (stack walk not supported on this architecture)\n");
#endif
}

static const char* codeName(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN (fail fast)";
    case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
    case 0xE06D7363: return "C++ exception";
    default: return "";
    }
}

/// reason: nullptr for a hardware/SEH exception (rec is set), else "std::terminate", "abort", ...
static void writeReport(const char* reason, EXCEPTION_RECORD* rec, CONTEXT* ctx) {
    if (InterlockedExchange(&_writing, 1)) return;   // one report per crash
    Out& o = _out; o.len = 0; o.buf[0] = 0;
    SYSTEMTIME t; GetLocalTime(&t);
    o.add("Vikey crash report\n");
    o.add("Vikey: %s (%s) %s\n", VIKEY_VERSION, VIKEY_BUILD, VIKEY_ARCH);
    o.add("OS: %s\n", _os);
    o.add("Time: %04d-%02d-%02d %02d:%02d:%02d (local)\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    o.add("Thread: %lu%s\n", GetCurrentThreadId(), GetCurrentThreadId() == _mainThread ? " (main)" : "");
    if (rec) {
        o.add("Exception: 0x%08lX %s", rec->ExceptionCode, codeName(rec->ExceptionCode));
        if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && rec->NumberParameters >= 2) {
            ULONG_PTR kind = rec->ExceptionInformation[0];
            o.add(" (%s at 0x%llx)", kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute" : "access", (unsigned long long)rec->ExceptionInformation[1]);
        }
        o.add("\nAddress: "); addLocation(o, (DWORD64)rec->ExceptionAddress); o.add("\n");
    } else {
        o.add("Exception: %s\n", reason);
    }
    if (ctx) addStack(o, *ctx);

    wchar_t path[MAX_PATH];
    _snwprintf_s(path, _TRUNCATE, L"%s\\Vikey-%04d-%02d-%02d-%02d%02d%02d.txt", _dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) { DWORD w = 0; WriteFile(f, o.buf, (DWORD)o.len, &w, nullptr); FlushFileBuffers(f); CloseHandle(f); }
}

static LONG WINAPI onException(EXCEPTION_POINTERS* ep) {
    writeReport(nullptr, ep->ExceptionRecord, ep->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;   // quit without the Windows error dialog
}

/// Software failures (no exception record): report from here, then end the process.
[[noreturn]] static void fail(const char* reason) {
    CONTEXT ctx; RtlCaptureContext(&ctx);
    writeReport(reason, nullptr, &ctx);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) {}
}
static void onTerminate() { fail("std::terminate"); }
static void onPurecall() { fail("pure virtual function call"); }
static void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) { fail("invalid parameter (CRT)"); }
static void onAbort(int) { fail("abort"); }

void install() {
    std::wstring d = prefs::appDataDir() + L"\\Crashes";
    CreateDirectoryW(d.c_str(), nullptr);
    wcsncpy_s(_dir, d.c_str(), _TRUNCATE);
    _mainThread = GetCurrentThreadId();
    strncpy_s(_os, windowsVersion().c_str(), _TRUNCATE);
    ULONG guarantee = 64 * 1024; SetThreadStackGuarantee(&guarantee);   // room for the handler after a stack overflow
    SetUnhandledExceptionFilter(onException);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPurecall);
    _set_invalid_parameter_handler(onInvalidParameter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, onAbort);
}

__declspec(noinline) static void crashNow(volatile int* p) { *p = 42; }
void selfTestIfAsked() {
    const wchar_t* cl = GetCommandLineW();
    if (wcsstr(cl, L"--crash-self-test=terminate")) std::terminate();
    if (wcsstr(cl, L"--crash-self-test")) crashNow(nullptr);
}

// ---- next launch: ask, then send -----------------------------------------------------------------
static long long nowUnix() { return (long long)time(nullptr); }
static long long fileUnix(const FILETIME& ft) { ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime; return (long long)(u.QuadPart / 10000000ULL) - 11644473600LL; }

/// The newest report from the last 3 days that wasn't handled yet ("" if none).
static std::wstring newestPending() {
    long long seen = _wtoi64(prefs::getString(SEEN_KEY, L"0").c_str());
    long long since = std::max(seen, nowUnix() - 3 * 86400);
    WIN32_FIND_DATAW fd; std::wstring best; long long bestT = since;
    HANDLE h = FindFirstFileW((std::wstring(_dir) + L"\\Vikey-*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    do {
        long long t = fileUnix(fd.ftLastWriteTime);
        if (t > bestT) { bestT = t; best = std::wstring(_dir) + L"\\" + fd.cFileName; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

static void replaceAllNoCase(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    auto lower = [](std::string x) { for (auto& c : x) c = (char)tolower((unsigned char)c); return x; };
    std::string ls = lower(s), lf = lower(from), out; size_t i = 0;
    for (size_t at; (at = ls.find(lf, i)) != std::string::npos; i = at + lf.size()) { out.append(s, i, at - i); out += to; }
    out.append(s, i, std::string::npos); s = out;
}

static void log(const std::string& line) {
    HANDLE f = CreateFileW((std::wstring(_dir) + L"\\upload.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t; GetLocalTime(&t); char ts[32]; _snprintf_s(ts, _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string l = ts + line + "\r\n"; DWORD w; WriteFile(f, l.data(), (DWORD)l.size(), &w, nullptr); CloseHandle(f);
}

static void send(const std::wstring& report) {
    HANDLE f = CreateFileW(report.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    std::string text(std::min<DWORD>(GetFileSize(f, nullptr), 1900000), 0); DWORD got = 0;
    ReadFile(f, &text[0], (DWORD)text.size(), &got, nullptr); CloseHandle(f); text.resize(got);
    wchar_t home[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, home)) && home[0]) {
        std::string h = narrow(home); replaceAllNoCase(text, h, "~");
        std::replace(h.begin(), h.end(), '\\', '/'); replaceAllNoCase(text, h, "~");
    }
    std::string name = narrow(report.substr(report.find_last_of(L'\\') + 1));
    char rnd[32]; _snprintf_s(rnd, _TRUNCATE, "vikey-%08lx%08lx", GetTickCount(), GetCurrentProcessId());
    std::string boundary = rnd, body;
    auto field = [&](const char* k, const std::string& v) { body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + k + "\"\r\n\r\n" + v + "\r\n"; };
    field("platform", "windows"); field("version", std::string(VIKEY_VERSION) + " (" + VIKEY_BUILD + ")"); field("os", _os);
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"report\"; filename=\"" + name + "\"\r\nContent-Type: text/plain\r\n\r\n" + text + "\r\n--" + boundary + "--\r\n";

    std::wstring url = prefs::getString(L"crashEndpoint", L""); if (url.empty()) url = DEFAULT_ENDPOINT;
    URL_COMPONENTS uc = {sizeof(uc)}; wchar_t host[256], path[2048]; uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return;
    HINTERNET s = WinHttpOpen(L"Vikey/" VIKEY_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    std::wstring headers = L"Content-Type: multipart/form-data; boundary=" + widen(boundary);
    DWORD status = 0; std::string resp;
    if (r && WinHttpSendRequest(r, headers.c_str(), (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD sl = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sl, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(r, &avail) && avail && resp.size() < 4096) {
            size_t at = resp.size(); resp.resize(at + avail); DWORD n = 0;
            if (!WinHttpReadData(r, &resp[at], avail, &n)) break; resp.resize(at + n);
        }
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    char st[16]; _snprintf_s(st, _TRUNCATE, "%lu", status);
    log("sent " + name + " -> " + st + " " + resp);
}

static HRESULT CALLBACK dialogCallback(HWND h, UINT msg, WPARAM, LPARAM, LONG_PTR) {
    if (msg == TDN_CREATED) { SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); SetForegroundWindow(h); }
    return S_OK;
}

/// true = send. Sets "always send" when the box is ticked.
static bool ask() {
    const wchar_t* main = L"Vikey vừa bị thoát đột ngột";
    const wchar_t* content = L"Gửi báo cáo lỗi để Vikey sửa lỗi này? Báo cáo chỉ có thông tin kỹ thuật: vị trí lỗi trong mã, phiên bản Vikey và Windows. Không có chữ bạn đã gõ.";
    TASKDIALOG_BUTTON buttons[] = {{IDYES, L"Gửi báo cáo"}, {IDNO, L"Không gửi"}};
    TASKDIALOGCONFIG tc = {sizeof(tc)};
    tc.hInstance = GetModuleHandleW(nullptr);
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.pszWindowTitle = L"Vikey";
    tc.pszMainIcon = MAKEINTRESOURCEW(IDI_APP);
    tc.pszMainInstruction = main;
    tc.pszContent = content;
    tc.cButtons = 2; tc.pButtons = buttons; tc.nDefaultButton = IDYES;
    tc.pszVerificationText = L"Luôn gửi, không hỏi lại";
    tc.pfCallback = dialogCallback;
    int button = 0; BOOL always = FALSE;
    if (FAILED(TaskDialogIndirect(&tc, &button, nullptr, &always))) {   // no common controls 6: a plain message box
        std::wstring text = std::wstring(main) + L"\n\n" + content;
        button = MessageBoxW(nullptr, text.c_str(), L"Vikey", MB_YESNO | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
        always = FALSE;
    }
    bool yes = button == IDYES;
    if (yes && always) prefs::setInt(AUTO_KEY, 1);
    return yes;
}

static DWORD WINAPI launchThread(LPVOID p) {
    std::wstring* report = (std::wstring*)p;
    if (prefs::getInt(AUTO_KEY, 0)) send(*report);
    else { Sleep(1500); if (ask()) send(*report); }   // after the tray icon is up, like the Mac
    delete report; return 0;
}

void checkAtLaunch() {
    std::wstring newest = newestPending();
    if (newest.empty()) return;
    prefs::setString(SEEN_KEY, std::to_wstring(nowUnix()));   // handled, whatever the answer; older ones go with it
    HANDLE t = CreateThread(nullptr, 0, launchThread, new std::wstring(newest), 0, nullptr);   // never on the typing thread
    if (t) CloseHandle(t);
}

}} // namespace
