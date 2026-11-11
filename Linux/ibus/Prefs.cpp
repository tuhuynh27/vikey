#include "Prefs.h"
#include <map>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <unistd.h>
namespace vikey { namespace prefs {
static std::map<std::string, std::string> _v;   // settings.json: raw JSON value text: "123" or "\"str\""
static std::map<std::string, std::string> _st;  // stats.json (TLStat_* counters): written only by the engine
static bool isStat(const char* k) { return strncmp(k, "TLStat_", 7) == 0; }
static std::string statsPath() { return dataDir() + "/stats.json"; }
static bool _loaded = false; static std::mutex _m;
static std::string env(const char* n, const std::string& d) { const char* e = getenv(n); return e && *e ? e : d; }
std::string configDir() { return env("XDG_CONFIG_HOME", env("HOME", "/tmp") + "/.config") + "/vikey"; }
std::string dataDir() { return env("XDG_DATA_HOME", env("HOME", "/tmp") + "/.local/share") + "/vikey"; }
std::string settingsPath() { return configDir() + "/settings.json"; }
static void mkdirp(const std::string& p) { std::string cur; for (size_t i = 1; i <= p.size(); i++) if (i == p.size() || p[i] == '/') { cur = p.substr(0, i); mkdir(cur.c_str(), 0700); } }
static std::string jstr(const std::string& s) { std::string o = "\""; for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c < 0x20) { char b[8]; snprintf(b, 8, "\\u%04x", c); o += b; } else o += c; } return o + "\""; }
static std::string unjstr(const std::string& t) {   // t includes the quotes
    std::string o; for (size_t i = 1; i + 1 < t.size(); i++) { char c = t[i]; if (c == '\\' && i + 2 < t.size()) { char d = t[++i]; if (d == 'n') o += '\n'; else if (d == 't') o += '\t'; else if (d == 'u' && i + 4 < t.size()) { o += (char)strtol(t.substr(i + 1, 4).c_str(), nullptr, 16); i += 4; } else o += d; } else o += c; } return o;
}
static void loadInto(std::map<std::string, std::string>& _v, const std::string& path);
static void load() { _loaded = true; loadInto(_v, settingsPath()); loadInto(_st, statsPath()); }
static void loadInto(std::map<std::string, std::string>& _v, const std::string& path) {
    std::ifstream f(path); if (!f) { _v.clear(); return; }
    std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
    size_t i = s.find('{'); if (i == std::string::npos || s.find('}') == std::string::npos) return;   // half-written: keep what we have
    _v.clear(); i++;
    while (i < s.size()) {
        while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == ',')) i++;
        if (i >= s.size() || s[i] == '}') break;
        if (s[i] != '"') break;
        size_t e = i + 1; while (e < s.size() && s[e] != '"') { if (s[e] == '\\') e++; e++; }
        std::string key = unjstr(s.substr(i, e - i + 1)); i = e + 1;
        while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == ':')) i++;
        std::string val;
        if (s[i] == '"') { e = i + 1; while (e < s.size() && s[e] != '"') { if (s[e] == '\\') e++; e++; } val = s.substr(i, e - i + 1); i = e + 1; }
        else { e = i; while (e < s.size() && s[e] != ',' && s[e] != '}' && !isspace((unsigned char)s[e])) e++; val = s.substr(i, e - i); i = e; }
        _v[key] = val;
    }
}
static bool saveMap(const std::map<std::string, std::string>& m, const std::string& path) {
    std::string tmp = path + ".tmp" + std::to_string(getpid());   // the engine and vikey-settings both save: one temp file each
    std::ofstream f(tmp); f << "{\n"; bool first = true; for (auto& kv : m) { f << (first ? "" : ",\n") << jstr(kv.first) << ": " << kv.second; first = false; } f << "\n}\n"; f.close();
    if (f && rename(tmp.c_str(), path.c_str()) == 0) return true;
    ::remove(tmp.c_str());   // a failed write (disk full) keeps the good file
    return false;
}
static time_t _ownSec = 0; static long _ownNs = 0;
static void noteOwnWrite() { struct stat st; if (stat(settingsPath().c_str(), &st) == 0) { _ownSec = st.st_mtime; _ownNs = st.st_mtim.tv_nsec; } }
static void save() { mkdirp(configDir()); saveMap(_v, settingsPath()); noteOwnWrite(); }
bool changedByOthers() { struct stat st; if (stat(settingsPath().c_str(), &st) != 0) return false; return st.st_mtime != _ownSec || st.st_mtim.tv_nsec != _ownNs; }
std::string commandPath() { return configDir() + "/command"; }
void sendCommand(const std::string& cmd) { mkdirp(configDir()); std::string tmp = commandPath() + ".tmp"; { std::ofstream f(tmp); f << cmd << "\n"; } rename(tmp.c_str(), commandPath().c_str()); }
static bool saveStats() { mkdirp(dataDir()); return saveMap(_st, statsPath()); }
// a counter goes up on every word typed: written by flushStats() on the engine's flush timer, not in the key callback
static bool _statsDirty = false;
void clearStats() { std::lock_guard<std::mutex> g(_m); if (!_loaded) load(); _st.clear(); _statsDirty = !saveStats(); }
void flushStats() { std::lock_guard<std::mutex> g(_m); if (_statsDirty && saveStats()) _statsDirty = false; }   // a failed write is retried on the next flush
/// Before changing a key: re-read the file, so another writer's keys survive.
static std::map<std::string, std::string>& mapFor(const char* key, bool forWrite) { if (isStat(key)) return _st; if (forWrite) loadInto(_v, settingsPath()); return _v; }
static void ensure() { if (!_loaded) load(); }
void reload() {   // after a change from the settings app or the panel menu
    std::lock_guard<std::mutex> g(_m);
    _loaded = true;
    loadInto(_v, settingsPath());
    if (!_statsDirty) loadInto(_st, statsPath());   // counters not written yet stay until flushStats() writes them
}
int getInt(const char* key, int fallback) { std::lock_guard<std::mutex> g(_m); ensure(); auto& m = mapFor(key, false); auto it = m.find(key); if (it == m.end() || it->second.empty() || it->second[0] == '"') return fallback; return atoi(it->second.c_str()); }
void setInt(const char* key, int value) { std::lock_guard<std::mutex> g(_m); ensure(); auto& m = mapFor(key, true); m[key] = std::to_string(value); if (isStat(key)) _statsDirty = true; else save(); }
std::string getString(const char* key, const std::string& fallback) { std::lock_guard<std::mutex> g(_m); ensure(); auto& m = mapFor(key, false); auto it = m.find(key); if (it == m.end() || it->second.empty() || it->second[0] != '"') return fallback; return unjstr(it->second); }
void setString(const char* key, const std::string& value) { std::lock_guard<std::mutex> g(_m); ensure(); auto& m = mapFor(key, true); m[key] = jstr(value); if (isStat(key)) _statsDirty = true; else save(); }
static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::vector<unsigned char> getBinary(const char* key) {
    std::string s = getString(key, ""); std::vector<unsigned char> out; int val = 0, bits = -8;
    for (char c : s) { const char* p = strchr(B64, c); if (!p || c == 0) continue; val = (val << 6) | (int)(p - B64); bits += 6; if (bits >= 0) { out.push_back((unsigned char)((val >> bits) & 0xFF)); bits -= 8; } }
    return out;
}
void setBinary(const char* key, const unsigned char* d, size_t n) {
    std::string o; int val = 0, bits = -6;
    for (size_t i = 0; i < n; i++) { val = (val << 8) | d[i]; bits += 8; while (bits >= 0) { o += B64[(val >> bits) & 63]; bits -= 6; } }
    if (bits > -6) o += B64[((val << 8) >> (bits + 8)) & 63]; while (o.size() % 4) o += '=';
    setString(key, o);
}
bool has(const char* key) { std::lock_guard<std::mutex> g(_m); ensure(); return mapFor(key, false).count(key) != 0; }
void remove(const char* key) { std::lock_guard<std::mutex> g(_m); ensure(); auto& m = mapFor(key, true); if (m.erase(key)) { if (isStat(key)) _statsDirty = !saveStats(); else save(); } }
std::vector<std::string> keys() { std::lock_guard<std::mutex> g(_m); ensure(); std::vector<std::string> k; for (auto& kv : _v) k.push_back(kv.first); for (auto& kv : _st) k.push_back(kv.first); return k; }
}}
