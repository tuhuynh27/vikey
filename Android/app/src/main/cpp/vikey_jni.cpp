// JNI face of Engine/Driver + TouchModel for the Android keyboard (mirrors iOS/Shared/VKDriver.mm).
#include <jni.h>
#include <string>
#include <vector>
#include <cstring>
#include "Driver.h"
#include "TouchModel.h"

int vLanguage = 1, vSwitchKeyStatus = 0, vCheckSpelling = 1, vUseModernOrthography = 1, vRestoreIfWrongSpelling = 1,
    vFixRecommendBrowser = 1, vUseSmartSwitchKey = 0, vOtherLanguage = 0, vAutoSwitchByTyping = 1;

static bool keyFor(jchar c, Uint16& key, bool& shift) {
    static const Uint16 letters[26] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    static const Uint16 digits[10] = {KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9};
    shift = false;
    if (c >= 'a' && c <= 'z') { key = letters[c - 'a']; return true; }
    if (c >= 'A' && c <= 'Z') { key = letters[c - 'A']; shift = true; return true; }
    if (c >= '0' && c <= '9') { key = digits[c - '0']; return true; }
    switch (c) {
        case ' ': key = KEY_SPACE; return true; case '\n': key = KEY_RETURN; return true; case ',': key = KEY_COMMA; return true;
        case '.': key = KEY_DOT; return true; case ';': key = KEY_SEMICOLON; return true; case ':': key = KEY_SEMICOLON; shift = true; return true;
        case '?': key = KEY_SLASH; shift = true; return true; case '/': key = KEY_SLASH; return true; case '!': key = KEY_1; shift = true; return true;
        case '\'': key = KEY_QUOTE; return true; case '"': key = KEY_QUOTE; shift = true; return true; case '(': key = KEY_9; shift = true; return true;
        case ')': key = KEY_0; shift = true; return true; case '-': key = KEY_MINUS; return true;
    }
    return false;
}
static VikeyDriver* D(jlong h) { return reinterpret_cast<VikeyDriver*>(h); }
/// Edit result packed as a Java String: first char = deleteCount, second = flags (1 = language changed), then the text.
static jstring packEdit(JNIEnv* env, const VKEdit& e) {
    std::u16string s; s.push_back((char16_t)e.deleteCount); s.push_back((char16_t)(e.languageChanged ? 1 : 0)); s += e.text;
    return env->NewString(reinterpret_cast<const jchar*>(s.data()), (jsize)s.size());
}
static std::vector<TMKey> keysOf(JNIEnv* env, jbyteArray letters, jfloatArray rects) {
    std::vector<TMKey> k; jsize n = env->GetArrayLength(letters);
    jbyte* l = env->GetByteArrayElements(letters, nullptr); jfloat* r = env->GetFloatArrayElements(rects, nullptr);
    for (jsize i = 0; i < n; i++) k.push_back({(char)l[i], r[i * 4], r[i * 4 + 1], r[i * 4 + 2], r[i * 4 + 3]});
    env->ReleaseByteArrayElements(letters, l, JNI_ABORT); env->ReleaseFloatArrayElements(rects, r, JNI_ABORT);
    return k;
}
/// Candidates packed as one String per candidate: letters + '\t' + text + '\t' + vietnamese + '\t' + score
static jobjectArray packCandidates(JNIEnv* env, const std::vector<TMCandidate>& c, int n) {
    jclass str = env->FindClass("java/lang/String"); jobjectArray out = env->NewObjectArray(n, str, nullptr);
    for (int i = 0; i < n; i++) {
        std::u16string s; for (const char* p = c[i].letters; *p; p++) s.push_back((char16_t)*p); s.push_back(u'\t');
        s.append(reinterpret_cast<const char16_t*>(c[i].text), (size_t)c[i].textLen); s.push_back(u'\t'); s.push_back(c[i].vietnamese ? u'1' : u'0'); s.push_back(u'\t');
        std::string sc = std::to_string(c[i].score); for (char ch : sc) s.push_back((char16_t)ch);
        env->SetObjectArrayElement(out, i, env->NewString(reinterpret_cast<const jchar*>(s.data()), (jsize)s.size()));
    }
    return out;
}

extern "C" {
JNIEXPORT jlong JNICALL Java_app_vikey_ime_VikeyDriver_create(JNIEnv*, jclass) { auto* d = new VikeyDriver(); d->pauseAfterManual = 5; return reinterpret_cast<jlong>(d); }
JNIEXPORT void JNICALL Java_app_vikey_ime_VikeyDriver_destroy(JNIEnv*, jclass, jlong h) { delete D(h); }
JNIEXPORT jint JNICALL Java_app_vikey_ime_VikeyDriver_language(JNIEnv*, jclass, jlong) { return vLanguage; }
JNIEXPORT void JNICALL Java_app_vikey_ime_VikeyDriver_setLanguage(JNIEnv*, jclass, jlong h, jint l) { D(h)->setLanguage(l); }
JNIEXPORT void JNICALL Java_app_vikey_ime_VikeyDriver_configure(JNIEnv*, jclass, jlong h, jboolean autoSwitch, jdouble pause, jboolean spelling, jboolean restore, jboolean modern) {
    D(h)->autoSwitch = autoSwitch; D(h)->pauseAfterManual = pause;   // not vAutoSwitchByTyping: the driver sets it on the next key, so a pause after a manual switch stays
    vCheckSpelling = spelling; vSetCheckSpelling(); vRestoreIfWrongSpelling = restore; vUseModernOrthography = modern;
}
JNIEXPORT void JNICALL Java_app_vikey_ime_VikeyDriver_setTelexStyle(JNIEnv*, jclass, jint style) { vTelexStyle = style < 0 ? 0 : style > 2 ? 2 : style; }   // D56
JNIEXPORT jstring JNICALL Java_app_vikey_ime_VikeyDriver_typeChar(JNIEnv* env, jclass, jlong h, jchar c, jdouble now) {
    Uint16 key; bool shift;
    if (!keyFor(c, key, shift)) { D(h)->resetContext(); VKEdit e; e.text.push_back((char16_t)c); return packEdit(env, e); }
    return packEdit(env, D(h)->press(key, shift, now));
}
JNIEXPORT jstring JNICALL Java_app_vikey_ime_VikeyDriver_backspace(JNIEnv* env, jclass, jlong h, jdouble now) { return packEdit(env, D(h)->press(KEY_DELETE, false, now)); }
JNIEXPORT void JNICALL Java_app_vikey_ime_VikeyDriver_manualSwitch(JNIEnv*, jclass, jlong h, jdouble now) { D(h)->manualSwitch(now); }
JNIEXPORT void JNICALL Java_app_vikey_ime_VikeyDriver_resetContext(JNIEnv*, jclass, jlong h) { D(h)->resetContext(); }

JNIEXPORT jbyte JNICALL Java_app_vikey_ime_VikeyDriver_pickLetter(JNIEnv* env, jclass, jfloat x, jfloat y, jstring prefix, jint vi, jbyteArray letters, jfloatArray rects) {
    std::vector<TMKey> k = keysOf(env, letters, rects); const char* p = env->GetStringUTFChars(prefix, nullptr);
    char r = TouchModel::pick(x, y, p, vi, k.data(), (int)k.size()); env->ReleaseStringUTFChars(prefix, p); return (jbyte)r;
}
JNIEXPORT jobjectArray JNICALL Java_app_vikey_ime_VikeyDriver_candidates(JNIEnv* env, jclass, jstring typed, jfloatArray xs, jfloatArray ys, jint vi, jbyteArray letters, jfloatArray rects, jint max) {
    std::vector<TMKey> k = keysOf(env, letters, rects); const char* t = env->GetStringUTFChars(typed, nullptr);
    std::vector<TMCandidate> c((size_t)max); jfloat *px = nullptr, *py = nullptr;
    if (xs) px = env->GetFloatArrayElements(xs, nullptr); if (ys) py = env->GetFloatArrayElements(ys, nullptr);
    int n = TouchModel::candidates(t, px, py, vi, k.data(), (int)k.size(), c.data(), max);
    if (px) env->ReleaseFloatArrayElements(xs, px, JNI_ABORT); if (py) env->ReleaseFloatArrayElements(ys, py, JNI_ABORT);
    env->ReleaseStringUTFChars(typed, t);
    return packCandidates(env, c, n);
}
JNIEXPORT jboolean JNICALL Java_app_vikey_ime_VikeyDriver_shouldCorrect(JNIEnv* env, jclass, jstring typed, jobjectArray cands) {
    jsize n = env->GetArrayLength(cands); std::vector<TMCandidate> c((size_t)n);
    for (jsize i = 0; i < n; i++) {   // unpack letters \t text \t vi \t score
        jstring s = (jstring)env->GetObjectArrayElement(cands, i); const jchar* ch = env->GetStringChars(s, nullptr); jsize len = env->GetStringLength(s);
        int f = 0, li = 0, ti = 0; std::string score;
        for (jsize j = 0; j < len; j++) { if (ch[j] == '\t') { f++; continue; } if (f == 0 && li < 31) c[i].letters[li++] = (char)ch[j]; else if (f == 1 && ti < 48) c[i].text[ti++] = ch[j]; else if (f == 2) c[i].vietnamese = ch[j] == '1'; else if (f == 3) score.push_back((char)ch[j]); }
        c[i].letters[li] = 0; c[i].textLen = ti; c[i].score = score.empty() ? 0 : atof(score.c_str());
        env->ReleaseStringChars(s, ch); env->DeleteLocalRef(s);
    }
    const char* t = env->GetStringUTFChars(typed, nullptr); bool r = TouchModel::shouldCorrect(t, c.data(), (int)n); env->ReleaseStringUTFChars(typed, t); return r;
}
JNIEXPORT jobjectArray JNICALL Java_app_vikey_ime_VikeyDriver_completions(JNIEnv* env, jclass, jstring prefix, jint vi, jint max) {
    const char* p = env->GetStringUTFChars(prefix, nullptr); std::vector<TMCandidate> out; 
    for (int pass = 0; pass < 2; pass++) {
        int lang = pass == 0 ? vi : !vi; char words[8][32]; int scores[8];
        int n = vAutoSwitchCompletions(lang, p, words, scores, 8);
        for (int i = 0; i < n && (int)out.size() < max; i++) {
            TMCandidate c = {}; strncpy(c.letters, words[i], 31); int len = 0;
            if (vAutoSwitchWordScore(lang, words[i], (Uint16*)c.text, &len) <= -1000 || len <= 0) continue;
            c.textLen = len; c.vietnamese = lang; c.score = scores[i] / 10.0 * 2.302585 + (pass == 0 ? 0 : -1.5);
            bool dup = false; for (auto& o : out) if (o.textLen == len && !memcmp(o.text, c.text, len * 2)) dup = true;
            if (!dup) out.push_back(c);
        }
    }
    env->ReleaseStringUTFChars(prefix, p);
    for (size_t i = 1; i < out.size(); i++) for (size_t j = i; j > 0 && out[j].score > out[j - 1].score; j--) std::swap(out[j], out[j - 1]);
    return packCandidates(env, out, (int)out.size());
}
JNIEXPORT jint JNICALL Java_app_vikey_ime_VikeyDriver_wordScore(JNIEnv* env, jclass, jint vi, jstring letters) { const char* l = env->GetStringUTFChars(letters, nullptr); int s = vAutoSwitchWordScore(vi, l, nullptr, nullptr); env->ReleaseStringUTFChars(letters, l); return s; }
}
