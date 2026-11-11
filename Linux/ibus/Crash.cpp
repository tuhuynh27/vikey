// See Crash.h. The handler only uses async-signal-safe calls (openat, write, close, time, sigaction, raise, _exit,
// and backtrace/backtrace_symbols_fd, which are made safe by calling backtrace once at install so libgcc is loaded);
// the path, the header and the time zone offset are prepared at install.
#include "Crash.h"
#include "Prefs.h"
#include <csignal>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <execinfo.h>
namespace vikey { namespace crash {
static std::string env(const char* n, const std::string& d) { const char* e = getenv(n); return e && *e ? e : d; }
std::string dir() { return env("XDG_STATE_HOME", env("HOME", "/tmp") + "/.local/state") + "/vikey/crashes"; }

static int _dirfd = -1;
static char _header[256]; static size_t _headerLen = 0;
static long _gmtoff = 0;
static volatile sig_atomic_t _inHandler = 0;
static const int kSignals[] = {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL};

// ---- async-signal-safe formatting into a fixed buffer ----------------------------------------------------------------
struct Buf { char b[512]; size_t n = 0;
    void s(const char* t) { while (*t && n < sizeof b) b[n++] = *t++; }
    void dec(unsigned long v, int width = 0) { char t[24]; int i = 0; do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 24); while (i < width && i < 24) t[i++] = '0'; while (i && n < sizeof b) b[n++] = t[--i]; }
    void hex(unsigned long v) { s("0x"); char t[20]; int i = 0; do { t[i++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v && i < 20); while (i && n < sizeof b) b[n++] = t[--i]; }
};
static void civil(long t, int& y, unsigned& mo, unsigned& d, unsigned& h, unsigned& mi, unsigned& se) {   // days -> y/m/d (H. Hinnant)
    long days = t / 86400, rem = t % 86400; if (rem < 0) { rem += 86400; days--; }
    h = (unsigned)(rem / 3600); mi = (unsigned)(rem % 3600 / 60); se = (unsigned)(rem % 60);
    days += 719468; long era = (days >= 0 ? days : days - 146096) / 146097; unsigned doe = (unsigned)(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1; mo = mp < 10 ? mp + 3 : mp - 9; y = (int)(yy + (mo <= 2));
}
static const char* sigName(int s) {
    switch (s) { case SIGSEGV: return "SIGSEGV"; case SIGBUS: return "SIGBUS"; case SIGABRT: return "SIGABRT"; case SIGFPE: return "SIGFPE"; case SIGILL: return "SIGILL"; default: return "?"; }
}
static void writeAll(int fd, const char* p, size_t n) { while (n) { ssize_t w = write(fd, p, n); if (w <= 0) return; p += w; n -= (size_t)w; } }

static void onSignal(int sig, siginfo_t* info, void*) {
    if (_inHandler) _exit(128 + sig);   // a crash inside the handler
    _inHandler = 1;
    int fd = -1;
    if (_dirfd >= 0) {
        int y; unsigned mo, d, h, mi, se; civil((long)time(nullptr) + _gmtoff, y, mo, d, h, mi, se);
        Buf name; name.s("vikey-"); name.dec((unsigned long)y, 4); name.dec(mo, 2); name.dec(d, 2); name.s("-"); name.dec(h, 2); name.dec(mi, 2); name.dec(se, 2); name.s(".txt");
        if (name.n < sizeof name.b) { name.b[name.n] = 0; fd = openat(_dirfd, name.b, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); }
        if (fd >= 0) {
            writeAll(fd, _header, _headerLen);
            Buf l; l.s("Time: "); l.dec((unsigned long)y, 4); l.s("-"); l.dec(mo, 2); l.s("-"); l.dec(d, 2); l.s(" "); l.dec(h, 2); l.s(":"); l.dec(mi, 2); l.s(":"); l.dec(se, 2); l.s(" (local)\n");
            int code = info ? info->si_code : 0;
            l.s("Signal: "); l.dec((unsigned long)sig); l.s(" ("); l.s(sigName(sig)); l.s("), code "); if (code < 0) { l.s("-"); l.dec((unsigned long)-(long)code); } else l.dec((unsigned long)code);
            if (code > 0 && sig != SIGABRT) { l.s("\nFault address: "); l.hex((unsigned long)info->si_addr); }   // si_addr is only set by a fault (a kill/abort has a pid there)
            l.s("\n\nBacktrace (module(symbol+offset) [address]):\n");
            writeAll(fd, l.b, l.n);
            void* frames[64]; int n = backtrace(frames, 64);
            backtrace_symbols_fd(frames, n, fd);
            close(fd);
        }
    }
    // default action: the process ends with the signal, and IBus starts the engine again on the next key/focus
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = SIG_DFL; sigemptyset(&sa.sa_mask); sigaction(sig, &sa, nullptr);
    raise(sig);
    _exit(128 + sig);
}

void install(const char* version) {
    std::string d = dir();
    std::string cur; for (size_t i = 1; i <= d.size(); i++) if (i == d.size() || d[i] == '/') { cur = d.substr(0, i); mkdir(cur.c_str(), 0700); }
    _dirfd = open(d.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    Buf h; h.s("Vikey crash report\nVersion: "); h.s(version); h.s("\nProcess: ibus-engine-vikey\n");
    memcpy(_header, h.b, std::min(h.n, sizeof _header)); _headerLen = std::min(h.n, sizeof _header);
    time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm); _gmtoff = tm.tm_gmtoff;
    void* warm[4]; backtrace(warm, 4);   // loads libgcc_s now: the first backtrace() may allocate
    static char altstack[64 * 1024];
    stack_t ss; memset(&ss, 0, sizeof ss); ss.ss_sp = altstack; ss.ss_size = sizeof altstack; sigaltstack(&ss, nullptr);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = onSignal; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    for (int s : kSignals) sigaction(s, &sa, nullptr);
}

std::vector<std::string> pending() {
    std::vector<std::pair<std::string, time_t>> found; std::string d = dir();
    long seen = prefs::getInt("CrashReportsSeenUntil", 0); long since = std::max(seen, (long)time(nullptr) - 3 * 86400);
    if (DIR* dp = opendir(d.c_str())) {
        while (struct dirent* e = readdir(dp)) {
            std::string n = e->d_name;
            if (n.rfind("vikey-", 0) != 0 || n.size() < 5 || n.compare(n.size() - 4, 4, ".txt")) continue;
            struct stat st; std::string p = d + "/" + n;
            if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_mtime > since) found.push_back({p, st.st_mtime});
        }
        closedir(dp);
    }
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first > b.first; });
    std::vector<std::string> out; for (auto& f : found) out.push_back(f.first); return out;
}

void selfTest(const std::string& kind) {
    if (kind == "abort") abort();
    volatile int* p = (volatile int*)(uintptr_t)8; *p = 1;   // a real fault (SIGSEGV), like a bad pointer
    abort();
}
}}
