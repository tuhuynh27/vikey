#import "VKDriver.h"
#include "Driver.h"
#include "TouchModel.h"
#include <vector>
#include <cstring>

// Engine settings (the macOS app defines these in VikeyCore.mm)
int vLanguage = 1, vSwitchKeyStatus = 0, vCheckSpelling = 1, vUseModernOrthography = 1, vRestoreIfWrongSpelling = 1,
    vFixRecommendBrowser = 1, vUseSmartSwitchKey = 0, vOtherLanguage = 0, vAutoSwitchByTyping = 1;

@implementation VKEditResult
@end

static bool KeyFor(unichar c, Uint16 &key, bool &shift) {
    static const Uint16 letters[26] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,
                                       KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    static const Uint16 digits[10] = {KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9};
    shift = false;
    if (c >= 'a' && c <= 'z') { key = letters[c - 'a']; return true; }
    if (c >= 'A' && c <= 'Z') { key = letters[c - 'A']; shift = true; return true; }
    if (c >= '0' && c <= '9') { key = digits[c - '0']; return true; }
    switch (c) {
        case ' ': key = KEY_SPACE; return true;
        case '\n': key = KEY_RETURN; return true;
        case ',': key = KEY_COMMA; return true;
        case '.': key = KEY_DOT; return true;
        case ';': key = KEY_SEMICOLON; return true;
        case ':': key = KEY_SEMICOLON; shift = true; return true;
        case '?': key = KEY_SLASH; shift = true; return true;
        case '/': key = KEY_SLASH; return true;
        case '!': key = KEY_1; shift = true; return true;
        case '\'': key = KEY_QUOTE; return true;
        case '"': key = KEY_QUOTE; shift = true; return true;
        case '(': key = KEY_9; shift = true; return true;
        case ')': key = KEY_0; shift = true; return true;
        case '-': key = KEY_MINUS; return true;
    }
    return false;
}

@implementation VKDriver {
    VikeyDriver *_d;
}

- (instancetype)init {
    if ((self = [super init])) { _d = new VikeyDriver(); _d->pauseAfterManual = 5; }
    return self;
}
- (void)dealloc { delete _d; }

- (NSInteger)language { return vLanguage; }
- (void)setLanguage:(NSInteger)language { _d->setLanguage((int)language); }
- (BOOL)autoSwitch { return _d->autoSwitch; }
- (void)setAutoSwitch:(BOOL)on { _d->autoSwitch = on; }   // the driver sets the engine's switch on the next key, so a pause after a manual switch stays
- (double)pauseAfterManual { return _d->pauseAfterManual; }
- (void)setPauseAfterManual:(double)s { _d->pauseAfterManual = s; }
- (BOOL)spelling { return vCheckSpelling; }
- (void)setSpelling:(BOOL)on { vCheckSpelling = on; vSetCheckSpelling(); }
- (BOOL)restoreIfInvalid { return vRestoreIfWrongSpelling; }
- (void)setRestoreIfInvalid:(BOOL)on { vRestoreIfWrongSpelling = on; }
- (BOOL)modernOrthography { return vUseModernOrthography; }
- (void)setModernOrthography:(BOOL)on { vUseModernOrthography = on; }
- (NSInteger)telexStyle { return vTelexStyle; }
- (void)setTelexStyle:(NSInteger)style { vTelexStyle = (int)MAX(0, MIN(2, style)); }


- (VKEditResult *)convert:(const ::VKEdit &)e {
    VKEditResult *r = [VKEditResult new];
    r.deleteCount = e.deleteCount;
    r.text = [NSString stringWithCharacters:(const unichar *)e.text.data() length:e.text.size()];
    r.languageChanged = e.languageChanged;
    return r;
}

- (VKEditResult *)typeCharacter:(NSString *)character now:(double)now {
    Uint16 key; bool shift;
    if (character.length != 1 || !KeyFor([character characterAtIndex:0], key, shift)) {
        // not a key the engine knows (emoji, other symbols): a word break that passes through
        _d->resetContext();
        VKEditResult *r = [VKEditResult new]; r.text = character; return r;
    }
    return [self convert:_d->press(key, shift, now)];
}

- (VKEditResult *)backspaceAt:(double)now { return [self convert:_d->press(KEY_DELETE, false, now)]; }
- (void)manualSwitchAt:(double)now { _d->manualSwitch(now); }
- (void)resetContext { _d->resetContext(); }
@end

char VKPickLetter(float x, float y, const char *prefix, int vietnamese, const VKKeyRect *keys, int n) {
    std::vector<TMKey> k((size_t)n);
    for (int i = 0; i < n; i++) k[i] = {keys[i].letter, keys[i].x, keys[i].y, keys[i].w, keys[i].h};
    return TouchModel::pick(x, y, prefix, vietnamese, k.data(), n);
}

static std::vector<TMKey> TMKeys(const VKKeyRect *keys, int n) {
    std::vector<TMKey> k((size_t)n);
    for (int i = 0; i < n; i++) k[i] = {keys[i].letter, keys[i].x, keys[i].y, keys[i].w, keys[i].h};
    return k;
}

int VKCandidates(const char *typed, const float *xs, const float *ys, int vietnamese, const VKKeyRect *keys, int n, VKCandidate *out, int max) {
    std::vector<TMKey> k = TMKeys(keys, n);
    std::vector<TMCandidate> c((size_t)max);
    int count = TouchModel::candidates(typed, xs, ys, vietnamese, k.data(), n, c.data(), max);
    for (int i = 0; i < count; i++) {
        memcpy(out[i].letters, c[i].letters, 32);
        memcpy(out[i].text, c[i].text, sizeof(out[i].text));
        out[i].textLen = c[i].textLen; out[i].vietnamese = c[i].vietnamese; out[i].score = c[i].score;
    }
    return count;
}

bool VKShouldCorrect(const char *typed, const VKCandidate *c, int count) {
    std::vector<TMCandidate> t((size_t)count);
    for (int i = 0; i < count; i++) {
        memcpy(t[i].letters, c[i].letters, 32); memcpy(t[i].text, c[i].text, sizeof(t[i].text));
        t[i].textLen = c[i].textLen; t[i].vietnamese = c[i].vietnamese; t[i].score = c[i].score;
    }
    return TouchModel::shouldCorrect(typed, t.data(), count);
}

int VKCompletions(const char *prefix, int vietnamese, VKCandidate *out, int max) {
    int count = 0;
    for (int pass = 0; pass < 2; pass++) {
        int lang = pass == 0 ? vietnamese : !vietnamese;
        char words[8][32]; int scores[8];
        int n = vAutoSwitchCompletions(lang, prefix, words, scores, 8);
        for (int i = 0; i < n && count < max; i++) {
            VKCandidate c = {};
            strncpy(c.letters, words[i], 31);
            int len = 0;
            if (vAutoSwitchWordScore(lang, words[i], (Uint16 *)c.text, &len) <= -1000 || len <= 0) continue;
            c.textLen = len; c.vietnamese = lang; c.score = scores[i] / 10.0 * 2.302585 + (pass == 0 ? 0 : -1.5);
            bool dup = false;
            for (int j = 0; j < count; j++) if (out[j].textLen == len && !memcmp(out[j].text, c.text, len * 2)) dup = true;
            if (!dup) out[count++] = c;
        }
    }
    for (int i = 1; i < count; i++) for (int j = i; j > 0 && out[j].score > out[j - 1].score; j--) { VKCandidate t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
    return count;
}

bool VKIsVietnameseSpelling(const char *letters) { return vAutoSwitchIsVietnameseSpelling(letters); }
bool VKIsWord(int vietnamese, const char *letters) { return vAutoSwitchWordScore(vietnamese, letters, nullptr, nullptr) > -1000; }
int VKWordScore(int vietnamese, const char *letters) { return vAutoSwitchWordScore(vietnamese, letters, nullptr, nullptr); }
