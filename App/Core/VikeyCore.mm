//
//  VikeyCore.mm
//  Vikey
//
//  The keyboard core: the event tap callback that feeds the Telex engine, smart switch per
//  app/website, auto switch by typing, and the typing log.
//  Based on OpenKey by Tuyen Mai (GPL-3.0).
//
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#import "Engine.h"
#import "VikeyCore.h"

/// The switch sound (Tony picked "sparkle", 2026-10-29): two tiny notes, rising into Vietnamese, falling into English.
static void PlaySwitchSound(int language) {
    static NSSound *vi = nil, *en = nil;
    if (!vi) {
        vi = [[NSSound alloc] initWithContentsOfURL:[[NSBundle mainBundle] URLForResource:@"SwitchToVietnamese" withExtension:@"caf"] byReference:YES];
        en = [[NSSound alloc] initWithContentsOfURL:[[NSBundle mainBundle] URLForResource:@"SwitchToEnglish" withExtension:@"caf"] byReference:YES];
    }
    NSSound *s = language == 1 ? vi : en;
    if (!s) { NSBeep(); return; }
    [s stop];
    [s play];
}

// Marks keystrokes Vikey posts itself, so the callback can skip them cheaply.
#define VIKEY_EVENT_TAG 0x0F341991
#define VIKEY_BUNDLE [[NSBundle mainBundle] bundleIdentifier]

// The app being typed in, asked once per key: each lookup is a round trip to the window server,
// and one key can need it several times (KeyFrontApp, below).
#define FRONT_APP KeyFrontApp()
// ← or → with no modifier (macOS marks arrow keys with the Fn and numeric-pad flags, which OTHER_CONTROL_KEY counts)
#define PLAIN_ARROW ((_keycode == KEY_LEFT || _keycode == KEY_RIGHT) && \
                     !(_flag & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift)))
#define OTHER_CONTROL_KEY (_flag & kCGEventFlagMaskCommand) || (_flag & kCGEventFlagMaskControl) || \
                            (_flag & kCGEventFlagMaskAlternate) || (_flag & kCGEventFlagMaskSecondaryFn) || \
                            (_flag & kCGEventFlagMaskNumericPad) || (_flag & kCGEventFlagMaskHelp)

#define MAX_UNICODE_STRING  20
#define EMPTY_HOTKEY 0xFE0000FE
#define LOAD_DATA(VAR, KEY) VAR = (int)[[NSUserDefaults standardUserDefaults] integerForKey:@#KEY]

// Ignore code for Modifier keys and numpad
// Reference: https://eastmanreference.com/complete-list-of-applescript-key-codes
NSDictionary *keyStringToKeyCodeMap = @{
    // Characters from number row
    @"`": @50, @"~": @50, @"1": @18, @"!": @18, @"2": @19, @"@": @19, @"3": @20, @"#": @20, @"4": @21, @"$": @21,
    @"5": @23, @"%": @23, @"6": @22, @"^": @22, @"7": @26, @"&": @26, @"8": @28, @"*": @28, @"9": @25, @"(": @25,
    @"0": @29, @")": @29, @"-": @27, @"_": @27, @"=": @24, @"+": @24,
    // Characters from first keyboard row
    @"q": @12, @"w": @13, @"e": @14, @"r": @15, @"t": @17, @"y": @16, @"u": @32, @"i": @34, @"o": @31, @"p": @35,
    @"[": @33, @"{": @33, @"]": @30, @"}": @30, @"\\": @42, @"|": @42,
    // Characters from second keyboard row
    @"a": @0, @"s": @1, @"d": @2, @"f": @3, @"g": @5, @"h": @4, @"j": @38, @"k": @40, @"l": @37,
    @";": @41, @":": @41, @"'": @39, @"\"": @39,
    // Characters from second third row
    @"z": @6, @"x": @7, @"c": @8, @"v": @9, @"b": @11, @"n": @45, @"m": @46,
    @",": @43, @"<": @43, @".": @47, @">": @47, @"/": @44, @"?": @44
};

// Settings the engine reads (see Engine.h). Vikey keeps only what its settings expose;
// everything else stays at the value that matches a modern Unicode Telex setup.
// Emoji picker (App/Views/EmojiPicker.swift): its hotkey (0 = off), and while it is open keys pass through untouched
static int vEmojiHotkey = 0;
static volatile bool vEmojiOpen = false;
static void (^vEmojiHandler)(void) = nil;
int vLanguage = 1, vSwitchKeyStatus = 0x7A000206, vCheckSpelling = 1, vUseModernOrthography = 1;
int vRestoreIfWrongSpelling = 1, vFixRecommendBrowser = 1, vUseSmartSwitchKey = 1, vOtherLanguage = 1;
int vAutoSwitchByTyping = 1, vPerformLayoutCompat = 0;

NSNotificationName const VikeyLanguageDidChangeNotification = @"VikeyLanguageDidChange";

static void VKLanguageChanged(BOOL rememberForContext);

extern "C" {
    //app which must sent special empty character
    NSArray* _niceSpaceApp = @[@"com.sublimetext.3",
                               @"com.sublimetext.2",
                             ];
    
    NSArray* _recommendWorkaroundDisabledApp = @[@"com.apple.Spotlight"];
    
    CGEventSourceRef myEventSource = NULL;
    vKeyHookState* pData;
    CGEventRef eventBackSpaceDown;
    CGEventRef eventBackSpaceUp;
    UniChar _newChar, _newCharHi;
    CGEventRef _newEventDown, _newEventUp;
    CGKeyCode _keycode;
    CGEventFlags _flag, _lastFlag = 0, _privateFlag;
    // a key or click came while modifiers were held: they were for it, not a modifier-only hotkey (cleared when all are up)
    static bool _modifierChordUsed = false;
    #define MODIFIER_FLAGS (kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand | kCGEventFlagMaskShift)
    CGEventTapProxy _proxy;
    
    Uint16 _newCharString[MAX_UNICODE_STRING];
    Uint16 _newCharSize;
    bool _willContinuteSending = false;
    bool _willSendControlKey = false;
    
    
    int _i, _j, _k;
    Uint32 _tempChar;
    bool _hasJustUsedHotKey = false;

    int _languageTemp = 0; //use for smart switch key
    vector<Byte> savedSmartSwitchKeyData; ////use for smart switch key
    
    NSString* _frontMostApp = @"UnknownApp";

    // ---- Auto switch by typing ----------------------------------------------------------
    // The engine decides when a word belongs to the other language. This layer collects the
    // raw keys of the current word in English mode, sends the engine's output, flips the
    // language, and lets the switch hotkey undo an automatic correction right after it happens.
    static Uint32 _enKeys[MAX_BUFF];
    static int _enCount = 0;
    static bool _enValid = true;   // false when the word isn't whole (started mid-word, edited past its start, has digits)
    static bool _enAfterShortcut = false; // last key was a ⌘/⌃ shortcut (select all, cut...): Backspace then deletes a selection

    struct AutoFixUndo {
        bool active;
        int removeCount;           // characters to delete: the corrected word plus the break character
        UniChar text[MAX_BUFF + 1]; // what to type back: the original word
        int textCount;
        int language;              // language before the automatic switch
        Uint32 keys[MAX_BUFF];     // the keys of the original word (English mode can go on typing it after an undo)
        int keyCount;
    };
    // after an undo the restored word stays the current word; finishing it unchanged doesn't redo the fix
    static Uint32 _undoneKeys[MAX_BUFF];
    static int _undoneCount = 0;
    static AutoFixUndo _autoFixUndo = {};

    static bool UndoAutoFixWithBackspace();
    static void TLUndo();
    int vTypingLogKeys = 1;                     // record the keys of each word in the typing log
    int vTypingLogEnabled = 0;                  // the typing log is a debug feature: off unless turned on
    static NSString* _tlApp = @"";              // bundle id of the app being typed in, for the log
    void TLFlush();
    void TLEndWord(NSString* reason);
    static void TLContext(int to);
    static void TLAuto(NSString* rule, int to);
    void TLManual(NSString* via, int to);

    // words typed just before the current one in English mode, most recent first (look-back)
    static Uint32 _enPrevKeys[2][MAX_BUFF];
    static int _enPrevCounts[2];
    static int _enPrevN = 0;
    static bool _enPrevRestored = false;   // _enPrevKeys[0] was put back to English by auto switch (D35)

    /// Starts a new English-mode word; the previous words stay for look-back.
    static void StartEnglishWord(bool valid) {
        _enCount = 0;
        _enValid = valid;
        _enAfterShortcut = false;
    }

    /// Starts over: no current word and nothing to look back on.
    static bool _enAfterDouble = false;   // the current English word began in Vietnamese and switched on a doubled tone key
    // ← then → back to the same spot inside a word: the English word is still the one being typed (D51)
    static Uint32 _enNavKeys[MAX_BUFF]; static int _enNavCount = 0, _enNavPrevN = 0, _enNavOffset = 0;
    static bool _enNavValid = true, _enNavAfterDouble = false;

    static void ResetEnglishWord(bool valid) {
        _enAfterDouble = false;
        _undoneCount = 0;
        StartEnglishWord(valid);
        _enPrevN = 0;
        _enNavOffset = 0;   // a click or a switch while away: → must not bring the old word back at the new spot (D51)
        vAutoSwitchForget();
    }

    // ---- Smart switch per website -------------------------------------------------------
    // For browsers, the remembered language is keyed by "bundleId|hostname" instead of the
    // bundle id alone, so each site keeps its own language. The active tab's URL comes from
    // the browser's AppleScript dictionary, read off the main thread so a busy browser can
    // never stall the event tap.
    NSString* _siteHost = nil;                 // host of the active tab, nil if not a browser
    static dispatch_queue_t _siteQueue;
    static NSTimer* _siteTimer;
    static BOOL _siteCheckPending = NO;

    static NSDictionary<NSString*, NSString*>* BrowserScripts() {
        static NSDictionary* d;
        static dispatch_once_t once;
        dispatch_once(&once, ^{
            NSString* chromium = @"tell application id \"%@\" to if (count of windows) > 0 then return URL of active tab of front window";
            NSMutableDictionary* m = [NSMutableDictionary dictionary];
            for (NSString* b in @[@"com.google.Chrome", @"com.google.Chrome.beta", @"com.google.Chrome.canary", @"com.brave.Browser",
                                   @"com.microsoft.edgemac", @"company.thebrowser.Browser", @"com.vivaldi.Vivaldi",
                                   @"org.chromium.Chromium", @"com.operasoftware.Opera"]) {
                m[b] = [NSString stringWithFormat:chromium, b];
            }
            for (NSString* b in @[@"com.apple.Safari", @"com.apple.SafariTechnologyPreview"]) {
                m[b] = [NSString stringWithFormat:@"tell application id \"%@\" to if (count of windows) > 0 then return URL of current tab of front window", b];
            }
            d = m;
        });
        return d;
    }

    static BOOL PerSiteEnabled() {
        id v = [[NSUserDefaults standardUserDefaults] objectForKey:@"SmartSwitchPerSite"];
        return v == nil ? NO : [v boolValue];   // off by default: it needs the Automation permission
    }

    /// The key smart switch uses: bundle id, plus "|host" for a browser tab with a website open.
    static string SmartSwitchContextKey() {
        NSString* k = _frontMostApp;
        if (_siteHost.length > 0) k = [NSString stringWithFormat:@"%@|%@", _frontMostApp, _siteHost];
        return string(k.UTF8String);
    }

    void OnSiteChanged(void);

    /// Asks the frontmost browser for its active tab's host, then reports a change on the main thread.
    void CheckBrowserSite() {
        if (!PerSiteEnabled() || _siteCheckPending) return;
        NSString* bundle = [[NSWorkspace sharedWorkspace] frontmostApplication].bundleIdentifier;
        NSString* script = bundle ? BrowserScripts()[bundle] : nil;
        if (script == nil) {
            if (_siteHost != nil) { _siteHost = nil; }
            return;
        }
        if (_siteQueue == nil) _siteQueue = dispatch_queue_create("vikey.site", DISPATCH_QUEUE_SERIAL);
        _siteCheckPending = YES;
        dispatch_async(_siteQueue, ^{
            NSDictionary* err = nil;
            NSAppleEventDescriptor* r = [[[NSAppleScript alloc] initWithSource:script] executeAndReturnError:&err];
            NSString* host = nil;
            if (err == nil && r.stringValue.length > 0) {
                NSURL* u = [NSURL URLWithString:r.stringValue];
                if ([u.scheme hasPrefix:@"http"] && u.host.length > 0) {
                    host = u.host.lowercaseString;
                    if ([host hasPrefix:@"www."]) host = [host substringFromIndex:4];
                }
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                _siteCheckPending = NO;
                // Still the same browser in front? Otherwise the result no longer applies.
                if (![[[NSWorkspace sharedWorkspace] frontmostApplication].bundleIdentifier isEqualToString:bundle]) return;
                BOOL changed = !((host == nil && _siteHost == nil) || [host isEqualToString:_siteHost]);
                if (changed) { _siteHost = host; OnSiteChanged(); }
            });
        });
    }

    /// Polls once a second while a supported browser is in front (tab switches don't notify).
    void UpdateSiteWatcher() {
        NSString* bundle = [[NSWorkspace sharedWorkspace] frontmostApplication].bundleIdentifier;
        BOOL browser = PerSiteEnabled() && bundle != nil && BrowserScripts()[bundle] != nil;
        if (!browser) {
            [_siteTimer invalidate]; _siteTimer = nil; _siteHost = nil;
            return;
        }
        if (_siteTimer == nil) {
            _siteTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer* t) { CheckBrowserSite(); }];
            _siteTimer.tolerance = 0.3;
        }
        CheckBrowserSite();
    }

    /// After a click or a tab shortcut in a browser, look sooner than the next poll.
    void NudgeSiteCheck() {
        if (_siteTimer == nil) return;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.25 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ CheckBrowserSite(); });
    }
    
    static bool _isQwertyLayout = true;
    static bool _otherInputLanguage = false;   // the active input source is for another language (Japanese, Chinese...)

    /// Dvorak, Colemak, AZERTY...: map each key back to its QWERTY position so Telex works.
    void VKUpdateLayoutCompat() {
        TISInputSourceRef src = TISCopyCurrentKeyboardLayoutInputSource();
        NSString* sid = src ? (__bridge NSString*)TISGetInputSourceProperty(src, kTISPropertyInputSourceID) : nil;
        static NSSet* qwerty;
        if (!qwerty) qwerty = [NSSet setWithArray:@[@"com.apple.keylayout.US", @"com.apple.keylayout.ABC", @"com.apple.keylayout.USInternational-PC",
                                                     @"com.apple.keylayout.British", @"com.apple.keylayout.British-PC", @"com.apple.keylayout.Australian",
                                                     @"com.apple.keylayout.Canadian", @"com.apple.keylayout.Irish", @"com.apple.keylayout.USExtended"]];
        _isQwertyLayout = sid == nil || [qwerty containsObject:sid];
        vPerformLayoutCompat = _isQwertyLayout ? 0 : 1;
        if (src) CFRelease(src);
        // computed once per input-source change instead of on every keystroke
        _otherInputLanguage = false;
        TISInputSourceRef isource = TISCopyCurrentKeyboardInputSource();
        if (isource) {
            CFArrayRef languages = (CFArrayRef)TISGetInputSourceProperty(isource, kTISPropertyInputSourceLanguages);
            if (languages && CFArrayGetCount(languages) > 0) {
                NSString* lang = (__bridge NSString*)(CFStringRef)CFArrayGetValueAtIndex(languages, 0);
                _otherInputLanguage = ![lang isLike:@"en"];
            }
            CFRelease(isource);
        }
    }

    /// Reads Vikey's settings into the engine. Called at start and whenever a setting changes.
    // ---- Excluded apps -------------------------------------------------------------------
    // "ExcludedApps" (UserDefaults, array of bundle ids; edited in the main window, remote-control apps listed by
    // default): while one of them is in front Vikey passes every key through untouched, so no Vietnamese typing and
    // no auto switch (typing there goes to another machine). The language hotkey still works.
    static NSSet<NSString*>* _excludedApps = nil;
    static bool _frontExcluded = false;

    void queryFrontMostApp(void);

    static void UpdateFrontExcluded() {
        _frontExcluded = _frontMostApp != nil && [_excludedApps containsObject:_frontMostApp];
    }

    // ---- Auto switch per app ---------------------------------------------------------------
    // "AutoSwitchByTyping" is the default for every app. "AutoSwitchPerApp" (bundle id -> 0/1, set from the
    // menu bar) overrides it for one app. A choice equal to the default is not stored.
    static NSString* const kAutoSwitchPerApp = @"AutoSwitchPerApp";

    static int AutoSwitchGlobal() {
        id on = [[NSUserDefaults standardUserDefaults] objectForKey:@"AutoSwitchByTyping"];
        return on == nil ? 1 : ([on intValue] ? 1 : 0);
    }

    static int AutoSwitchForApp(NSString* app) {
        if (app.length > 0) {
            NSNumber* own = [[NSUserDefaults standardUserDefaults] dictionaryForKey:kAutoSwitchPerApp][app];
            if (own != nil) return [own intValue] ? 1 : 0;
        }
        return AutoSwitchGlobal();
    }

    // ---- Pause auto switch after a manual switch ----------------------------------------
    // The user just chose the language by hand: for 5 s auto switch stays out of the way (D21).
    static CFAbsoluteTime _autoPauseUntil = 0;

    static bool AutoSwitchPaused() { return CFAbsoluteTimeGetCurrent() < _autoPauseUntil; }

    void PauseAutoSwitchAfterManual() {
        id v = [[NSUserDefaults standardUserDefaults] objectForKey:@"PauseAutoAfterManual"];
        if (v != nil && ![v boolValue]) return;
        _autoPauseUntil = CFAbsoluteTimeGetCurrent() + 5;
        vAutoSwitchByTyping = 0;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5.05 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            if (AutoSwitchPaused()) return;   // a later manual switch extended it
            vAutoSwitchByTyping = AutoSwitchForApp(_frontMostApp);
            StartEnglishWord(false);          // the word being typed now wasn't followed from its start
        });
    }

    /// The app in front changed: is it excluded now, and which auto switch setting applies. (The word in progress is
    /// already reset by frontmostAppChanged: OnFocusChanged and RequestNewSession.)
    void OnAppForExclusion() {
        queryFrontMostApp();
        UpdateFrontExcluded();
        if (!AutoSwitchPaused()) vAutoSwitchByTyping = AutoSwitchForApp(_frontMostApp);   // auto switch can differ per app
    }

    int VKAutoSwitchForFrontApp() { return AutoSwitchForApp(_frontMostApp); }
    int VKFrontAppExcluded() { return _frontExcluded ? 1 : 0; }
    NSString* VKFrontAppName() {
        NSRunningApplication* a = [NSRunningApplication runningApplicationsWithBundleIdentifier:_frontMostApp].firstObject;
        return a.localizedName ?: _frontMostApp;
    }

    /// Remembers the choice for the app in front.
    void VKSetAutoSwitchForFrontApp(int on) {
        NSString* app = _frontMostApp;
        if (app.length == 0) return;
        NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
        NSMutableDictionary* m = [[d dictionaryForKey:kAutoSwitchPerApp] mutableCopy] ?: [NSMutableDictionary dictionary];
        if ((on ? 1 : 0) == AutoSwitchGlobal()) [m removeObjectForKey:app]; else m[app] = @(on ? 1 : 0);
        [d setObject:m forKey:kAutoSwitchPerApp];
        if (!AutoSwitchPaused()) vAutoSwitchByTyping = AutoSwitchForApp(app);
        StartEnglishWord(false);   // the word being typed now wasn't followed from its start
    }

    void VKLoadSettings() {
        NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
        int (^get)(NSString*, int) = ^int(NSString* key, int fallback) {
            id v = [d objectForKey:key];
            return v == nil ? fallback : [v intValue];
        };
        vLanguage = get(@"InputMethod", 1) ? 1 : 0;
        vCheckSpelling = get(@"Spelling", 1);
        vSetCheckSpelling();
        vRestoreIfWrongSpelling = get(@"RestoreIfInvalidWord", 1);
        vTeencode = get(@"Teencode", 1);   // D53: zui zẻ, zậy, gòy, thík
        vTelexStyle = get(@"TelexStyle", 0);   // D56: Telex / Simple Telex 1 / 2
        vUseModernOrthography = get(@"ModernOrthography", 1);
                vUseSmartSwitchKey = get(@"UseSmartSwitchKey", 1);
        vAutoSwitchByTyping = AutoSwitchPaused() ? 0 : AutoSwitchForApp(_frontMostApp);
        _excludedApps = [NSSet setWithArray:[d arrayForKey:@"ExcludedApps"] ?: @[]];
        UpdateFrontExcluded();
        vTypingLogKeys = get(@"TypingLogKeys", 1);
        vTypingLogEnabled = get(@"TypingLogEnabled", 0);
        int hotkey = get(@"SwitchKeyStatus", 0);
        vSwitchKeyStatus = hotkey ? hotkey : 0x7A000206;  // default ⌥Z
        vEmojiHotkey = get(@"EmojiPicker", 0) ? get(@"EmojiHotkey", 0) : 0;
        VKUpdateLayoutCompat();
    }

    void VikeyInit() {
        queryFrontMostApp();   // so the excluded-app check is right from the first key
        VKLoadSettings();
        _tlApp = [[NSWorkspace sharedWorkspace] frontmostApplication].bundleIdentifier ?: ([[NSWorkspace sharedWorkspace] frontmostApplication].localizedName ?: @"");
        [NSTimer scheduledTimerWithTimeInterval:10 repeats:YES block:^(NSTimer* t) { TLFlush(); }];
        
        myEventSource = CGEventSourceCreate(kCGEventSourceStatePrivate);
        pData = (vKeyHookState*)vKeyInit();

        eventBackSpaceDown = CGEventCreateKeyboardEvent (myEventSource, 51, true);
        CGEventSetIntegerValueField(eventBackSpaceDown, kCGEventSourceUserData, VIKEY_EVENT_TAG);
        eventBackSpaceUp = CGEventCreateKeyboardEvent (myEventSource, 51, false);
        CGEventSetIntegerValueField(eventBackSpaceUp, kCGEventSourceUserData, VIKEY_EVENT_TAG);
        
        //per-app/per-site language memory
        NSData* data = [[NSUserDefaults standardUserDefaults] objectForKey:@"smartSwitchKey"];
        initSmartSwitchKey((Byte*)data.bytes, (int)data.length);

        // keyboard layout changes (QWERTY <-> Dvorak) switch the key mapping automatically
        [[NSDistributedNotificationCenter defaultCenter] addObserverForName:(__bridge NSString*)kTISNotifySelectedKeyboardInputSourceChanged
                                                                     object:nil queue:[NSOperationQueue mainQueue]
                                                                 usingBlock:^(NSNotification* n) { VKUpdateLayoutCompat(); }];
    }
    
    /// Another app came to the front: forget the word in progress and the look-back history.
    void OnFocusChanged() {
        ResetEnglishWord(true);
        _autoFixUndo.active = false;
    }

    void RequestNewSession() {
        //send event signal to Engine
        vKeyHandleEvent(vKeyEvent::Mouse, vKeyEventState::MouseDown, 0);
    }
    
    void queryFrontMostApp() {
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        NSString* bundleId = front.bundleIdentifier;
        if (bundleId != nil && [bundleId isEqualToString:VIKEY_BUNDLE]) return; // our own windows: keep the app being typed in
        _frontMostApp = bundleId ?: (front.localizedName ?: @"UnknownApp");
    }
    

    // Per-key memo, cleared at the start of every tap callback: the frontmost app and whether Spotlight
    // is open don't change within one key, and each costs a window server round trip (the window list
    // scan grows with the number of windows on screen).
    static NSString* _keyFrontApp = nil;
    static bool _keyFrontAppReady = false;
    static int _keySpotlight = -1;   // -1 unknown, 0 hidden, 1 visible

    static void ForgetPerKeyState() {
        _keyFrontApp = nil;
        _keyFrontAppReady = false;
        _keySpotlight = -1;
    }

    NSString* KeyFrontApp() {
        if (!_keyFrontAppReady) {
            _keyFrontApp = [[NSWorkspace sharedWorkspace] frontmostApplication].bundleIdentifier;
            _keyFrontAppReady = true;
        }
        return _keyFrontApp;
    }

    static BOOL ScanForSpotlight() {
        NSArray *windows = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                                        kCGNullWindowID));
        // The Spotlight icon in the menu bar (macOS 15) is a Spotlight-owned window too: skip status items
        NSInteger statusLevel = CGWindowLevelForKey(kCGStatusWindowLevelKey);
        for (NSDictionary *window in windows) {
            if ([[window objectForKey:(__bridge NSString *)kCGWindowLayer] integerValue] == statusLevel) continue;
            if ([[window objectForKey:(__bridge NSString *)kCGWindowOwnerName] isEqualToString:@"Spotlight"]) {
                return true;
            }
        }
        return false;
    }

    BOOL isSpotlightVisible() {
        if (_keySpotlight < 0) _keySpotlight = ScanForSpotlight() ? 1 : 0;
        return _keySpotlight == 1;
    }

    // The cheap app-list checks come first, so the Spotlight scan only runs when it decides the answer.
    BOOL shouldUseRecommendWorkaround(NSString* topApp) {
        if (!vFixRecommendBrowser) return false;
        if (topApp != nil && [_recommendWorkaroundDisabledApp containsObject:topApp]) return false;
        return !isSpotlightVisible();
    }

    BOOL shouldUseSelectionReplacement(NSString* topApp) {
        return [_recommendWorkaroundDisabledApp containsObject:topApp] || isSpotlightVisible();
    }
    
    // Saved right after the current key is handled, not inside the event tap: the whole per-app and
    // per-site table is serialized, and an auto switch mid-typing would otherwise pay for it on the key.
    static bool _smartSwitchSavePending = false;
    void saveSmartSwitchKeyData() {
        if (_smartSwitchSavePending) return;
        _smartSwitchSavePending = true;
        dispatch_async(dispatch_get_main_queue(), ^{
            _smartSwitchSavePending = false;
            getSmartSwitchKeySaveData(savedSmartSwitchKeyData);
            NSData* _data = [NSData dataWithBytes:savedSmartSwitchKeyData.data() length:savedSmartSwitchKeyData.size()];
            [[NSUserDefaults standardUserDefaults] setObject:_data forKey:@"smartSwitchKey"];
        });
    }
    
    void ApplySmartSwitch(void);

    void OnActiveAppChanged() { //use for smart switch key; improved on Sep 28th, 2019
        queryFrontMostApp();
        _siteHost = nil;          // the new app's tab is looked up below; until then use the app key
        ResetEnglishWord(true);
        _autoFixUndo.active = false;
        ApplySmartSwitch();
        UpdateSiteWatcher();
    }

    void OnSiteChanged() {
        ResetEnglishWord(true);
        _autoFixUndo.active = false;
        ApplySmartSwitch();
        startNewSession();
    }

    void ApplySmartSwitch() {
        _languageTemp = getAppInputMethodStatus(SmartSwitchContextKey(), vLanguage);
        if ((_languageTemp & 0x01) != vLanguage) { //for input method
            if (_languageTemp != -1) {
                vLanguage = _languageTemp & 0x01;
                VKLanguageChanged(NO);
                TLContext(vLanguage);
                startNewSession();
            } else {
                saveSmartSwitchKeyData();
            }
        }
    }
    
    void OnInputMethodChanged() {
        ResetEnglishWord(true);   // switched from the menu or the control panel: nothing to look back on
        if (vUseSmartSwitchKey) {
            queryFrontMostApp();
            UpdateFrontExcluded();
            if (_frontExcluded) return;   // no language memory inside an excluded app (it would apply after un-excluding)
            setAppInputMethodStatus(SmartSwitchContextKey(), vLanguage);
            saveSmartSwitchKeyData();
        }
    }
    
    void OnSpellCheckingChanged() {
        vSetCheckSpelling();
    }
    
    /// Posts a key event that types the given UTF-16 text (up to 16 units per event).
    static void PostText(const UniChar* text, int count) {
        for (int start = 0; start < count; start += 16) {
            int n = MIN(16, count - start);
            CGEventRef down = CGEventCreateKeyboardEvent(myEventSource, 0, true);
            CGEventRef up = CGEventCreateKeyboardEvent(myEventSource, 0, false);
            CGEventSetIntegerValueField(down, kCGEventSourceUserData, VIKEY_EVENT_TAG);
            CGEventSetIntegerValueField(up, kCGEventSourceUserData, VIKEY_EVENT_TAG);
            CGEventKeyboardSetUnicodeString(down, n, text + start);
            CGEventKeyboardSetUnicodeString(up, n, text + start);
            CGEventTapPostEvent(_proxy, down);
            CGEventTapPostEvent(_proxy, up);
            CFRelease(down);
            CFRelease(up);
        }
    }

    /// Posts a real key press with these modifiers (used for control keys such as Return after a restore).
    void SendKeyCode(CGKeyCode key, CGEventFlags flags) {
        CGEventRef down = CGEventCreateKeyboardEvent(myEventSource, key, true);
        CGEventRef up = CGEventCreateKeyboardEvent(myEventSource, key, false);
        CGEventSetIntegerValueField(down, kCGEventSourceUserData, VIKEY_EVENT_TAG);
        CGEventSetIntegerValueField(up, kCGEventSourceUserData, VIKEY_EVENT_TAG);
        flags |= kCGEventFlagMaskNonCoalesced;
        CGEventSetFlags(down, flags);
        CGEventSetFlags(up, flags);
        CGEventTapPostEvent(_proxy, down);
        CGEventTapPostEvent(_proxy, up);
        CFRelease(down);
        CFRelease(up);
    }

    /// An invisible character that stops browser address bars from autocompleting while we rewrite a word.
    void SendEmptyCharacter() {
        UniChar c = [_niceSpaceApp containsObject:FRONT_APP] ? 0x200C : 0x202F;
        PostText(&c, 1);
    }

    void SendBackspace() {
        CGEventTapPostEvent(_proxy, eventBackSpaceDown);
        CGEventTapPostEvent(_proxy, eventBackSpaceUp);
    }

    void SendShiftAndLeftArrow() {
        CGEventRef down = CGEventCreateKeyboardEvent(myEventSource, KEY_LEFT, true);
        CGEventRef up = CGEventCreateKeyboardEvent(myEventSource, KEY_LEFT, false);
        CGEventSetIntegerValueField(down, kCGEventSourceUserData, VIKEY_EVENT_TAG);
        CGEventSetIntegerValueField(up, kCGEventSourceUserData, VIKEY_EVENT_TAG);
        CGEventSetFlags(down, CGEventGetFlags(down) | kCGEventFlagMaskShift);
        CGEventSetFlags(up, CGEventGetFlags(up) | kCGEventFlagMaskShift);
        CGEventTapPostEvent(_proxy, down);
        CGEventTapPostEvent(_proxy, up);
        CFRelease(down);
        CFRelease(up);
    }

    static bool IsLetterKey(const Uint16& key);
    /// Types the engine's new characters; for a restore, also the key that triggered it.
    void SendNewCharString() {
        UniChar text[MAX_BUFF + 2];
        int n = 0;
        for (int k = pData->newCharCount - 1; k >= 0 && n < MAX_BUFF; k--) {
            Uint32 c = pData->charData[k];
            text[n++] = (c & CHAR_CODE_MASK) ? (UniChar)(c & 0xFFFF) : (UniChar)keyCodeToCharacter(c);
        }
        bool sendControlKey = false;
        if (pData->code == vRestore || pData->code == vRestoreAndStartNewSession) {
            Uint16 ch = keyCodeToCharacter(_keycode | ((_flag & kCGEventFlagMaskShift) || ((_flag & kCGEventFlagMaskAlphaShift) && IsLetterKey(_keycode)) ? CAPS_MASK : 0));   // Caps Lock shifts letters only ("NAS," gave "NAS<")
            if (ch != 0) text[n++] = ch; else sendControlKey = true;   // Return, Tab...: send the real key after the text
        }
        if (pData->code == vRestoreAndStartNewSession) startNewSession();
        if (n > 0) PostText(text, n);
        if (sendControlKey) SendKeyCode(_keycode, _flag);   // the key press's own modifiers: ⇧↩ stays a new line, not a send
    }

    /// flags: the modifiers held. For a key hotkey (⌥Z) that's the key press's own flags, so holding ⌥ and
    /// pressing Z again (or auto-repeat) switches again instead of typing "Ω"; for modifiers alone (⌃⇧) it's
    /// the most modifiers held before the release.
    bool checkHotKey(int hotKeyData, CGEventFlags flags, bool checkKeyCode=true) {
        if ((hotKeyData & (~0x8000)) == EMPTY_HOTKEY)
            return false;
        if (HAS_CONTROL(hotKeyData) ^ GET_BOOL(flags & kCGEventFlagMaskControl))
            return false;
        if (HAS_OPTION(hotKeyData) ^ GET_BOOL(flags & kCGEventFlagMaskAlternate))
            return false;
        if (HAS_COMMAND(hotKeyData) ^ GET_BOOL(flags & kCGEventFlagMaskCommand))
            return false;
        if (HAS_SHIFT(hotKeyData) ^ GET_BOOL(flags & kCGEventFlagMaskShift))
            return false;
        if (checkKeyCode) {
            if (GET_SWITCH_KEY(hotKeyData) != _keycode)
                return false;
        }
        return true;
    }
    
    void switchLanguage() {
        PauseAutoSwitchAfterManual();
        _autoFixUndo.active = false;
        ResetEnglishWord(true);
        if (vLanguage == 0)
            vLanguage = 1;
        else
            vLanguage = 0;
        TLManual(@"hotkey", vLanguage);
        if (HAS_BEEP(vSwitchKeyStatus))
            PlaySwitchSound(vLanguage);
        VKLanguageChanged(YES);
        startNewSession();
    }
    

    /// Sends the engine's result for the current key: backspaces, then the new characters.
    void SendEngineOutput() {
        //fix autocomplete; not when the extra backspace would reach MAX_BUFF (the engine can ask for 31): then none would be sent
        if (shouldUseRecommendWorkaround(FRONT_APP) && pData->extCode != 4 && pData->backspaceCount < MAX_BUFF - 1) {
            SendEmptyCharacter();
            pData->backspaceCount++;
        }

        if (pData->backspaceCount > 0 && shouldUseSelectionReplacement(FRONT_APP)) {
            for (_i = 0; _i < pData->backspaceCount; _i++) {
                SendShiftAndLeftArrow();
            }
            pData->backspaceCount = 0;
        }
        
        //send backspace
        if (pData->backspaceCount > 0 && pData->backspaceCount < MAX_BUFF) {
            for (_i = 0; _i < pData->backspaceCount; _i++) {
                SendBackspace();
            }
        }
        
        //send new character
        SendNewCharString();
    }

    static bool IsWhitespaceKey(const Uint16& key) {
        return key == KEY_SPACE || key == KEY_RETURN || key == KEY_ENTER || key == KEY_TAB;
    }

    /// After these the next word is a whole word: whitespace, or an opening ( [ { " '.
    static bool IsWordStartKey(const Uint16& key, bool shift) {
        return IsWhitespaceKey(key) || (key == KEY_9 && shift) || key == KEY_LEFT_BRACKET || key == KEY_QUOTE;
    }

    static bool IsLetterKey(const Uint16& key) {
        switch (key) {
            case KEY_A: case KEY_B: case KEY_C: case KEY_D: case KEY_E: case KEY_F: case KEY_G: case KEY_H: case KEY_I:
            case KEY_J: case KEY_K: case KEY_L: case KEY_M: case KEY_N: case KEY_O: case KEY_P: case KEY_Q: case KEY_R:
            case KEY_S: case KEY_T: case KEY_U: case KEY_V: case KEY_W: case KEY_X: case KEY_Y: case KEY_Z:
                return true;
        }
        return false;
    }

    // ---- Typing log ---------------------------------------------------------------------
    // Records manual vs automatic language switches and, when allowed, the keys of each word,
    // so auto switch can be tuned on real typing. Words are kept in a small buffer; lines are
    // written to disk on a background queue. Nothing is recorded during secure input
    // (password fields), in password managers or in terminals (sudo prompts).
    static dispatch_queue_t _tlQueue;
    static NSMutableString* _tlPending;
    static NSMutableDictionary* _tlStats;      // day -> counter name -> count
    static UniChar _tlWord[64];
    static int _tlWordLen = 0;
    static int _tlWordLang = 1;
    static bool _tlWordTruncated = false;
    static bool _tlSinceAuto = false;          // no key or click since the last automatic switch

    NSString* TLLogPath() {
        NSString* dir = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject
                         stringByAppendingPathComponent:@"Vikey"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        return [dir stringByAppendingPathComponent:@"typing-log.jsonl"];
    }

    static NSString* TLToday() {
        static NSDateFormatter* f;
        if (!f) { f = [NSDateFormatter new]; f.dateFormat = @"yyyy-MM-dd"; f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]; }
        return [f stringFromDate:[NSDate date]];
    }

    static NSString* TLTime() {
        static NSDateFormatter* f;
        if (!f) { f = [NSDateFormatter new]; f.dateFormat = @"yyyy-MM-dd'T'HH:mm:ss.SSSZZZZZ"; f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]; }
        return [f stringFromDate:[NSDate date]];
    }

    static bool _tlStatsDirty = false;         // counted since the last save

    static void TLLoadStats() {
        if (_tlStats) return;
        NSDictionary* saved = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"TypingLogStats"];
        _tlStats = [NSMutableDictionary dictionary];
        for (NSString* day in saved) _tlStats[day] = [saved[day] mutableCopy];
    }

    static void TLCount(NSString* name) {
        if (!vTypingLogEnabled) return;
        TLLoadStats();
        NSString* day = TLToday();
        NSMutableDictionary* d = _tlStats[day];
        if (!d) { d = [NSMutableDictionary dictionary]; _tlStats[day] = d; }
        d[name] = @([d[name] integerValue] + 1);
        _tlStatsDirty = true;
    }

    void TLFlush() {
        // the stats only when they changed: the 10 s timer runs whether or not anything was typed
        if (_tlStats && _tlStatsDirty) { [[NSUserDefaults standardUserDefaults] setObject:_tlStats forKey:@"TypingLogStats"]; _tlStatsDirty = false; }
        if (_tlPending.length == 0) return;
        NSString* chunk = _tlPending;
        _tlPending = [NSMutableString new];
        if (!_tlQueue) _tlQueue = dispatch_queue_create("vikey.typinglog", DISPATCH_QUEUE_SERIAL);
        NSString* path = TLLogPath();
        dispatch_async(_tlQueue, ^{
            NSFileManager* fm = [NSFileManager defaultManager];
            NSDictionary* attrs = [fm attributesOfItemAtPath:path error:nil];
            if (attrs && [attrs fileSize] > 20 * 1024 * 1024) { // keep one previous file
                NSString* old = [[path stringByDeletingPathExtension] stringByAppendingString:@".1.jsonl"];
                [fm removeItemAtPath:old error:nil];
                [fm moveItemAtPath:path toPath:old error:nil];
            }
            if (![fm fileExistsAtPath:path]) [fm createFileAtPath:path contents:nil attributes:@{NSFilePosixPermissions: @0600}];
            NSFileHandle* h = [NSFileHandle fileHandleForWritingAtPath:path];
            [h seekToEndOfFile];
            [h writeData:[chunk dataUsingEncoding:NSUTF8StringEncoding]];
            [h closeFile];
        });
    }

    void TLClear() {
        _tlPending = [NSMutableString new];
        _tlStats = [NSMutableDictionary dictionary];
        _tlStatsDirty = false;
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"TypingLogStats"];
        NSString* path = TLLogPath();
        if (!_tlQueue) _tlQueue = dispatch_queue_create("vikey.typinglog", DISPATCH_QUEUE_SERIAL);
        dispatch_async(_tlQueue, ^{
            [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
            [[NSFileManager defaultManager] removeItemAtPath:[[path stringByDeletingPathExtension] stringByAppendingString:@".1.jsonl"] error:nil];
        });
    }

    NSDictionary* TLStatsSnapshot() {
        TLLoadStats();
        return [_tlStats copy];
    }

    /// Runs done on the main thread once everything flushed so far is on disk (and a clear has run).
    void TLAfterWrites(void (^done)(void)) {
        TLFlush();
        if (!_tlQueue) _tlQueue = dispatch_queue_create("vikey.typinglog", DISPATCH_QUEUE_SERIAL);
        dispatch_async(_tlQueue, ^{ dispatch_async(dispatch_get_main_queue(), done); });
    }

    static bool TLKeysAllowed() {
        static NSSet* excluded;
        if (!excluded) excluded = [NSSet setWithArray:@[
            // password managers
            @"com.1password.1password", @"com.agilebits.onepassword7", @"com.bitwarden.desktop", @"com.apple.Passwords",
            @"com.apple.keychainaccess", @"com.lastpass.LastPass", @"com.dashlane.dashlanephonefinal", @"in.sinew.Enpass-Desktop",
            @"com.keepassxc.keepassxc", @"com.sinew.Enpass-Desktop", @"com.nordpass.macos.NordPass",
            // terminals (sudo and ssh passwords are typed in plain view of the event tap)
            @"com.apple.Terminal", @"com.googlecode.iterm2", @"dev.warp.Warp-Stable", @"com.mitchellh.ghostty",
            @"io.alacritty", @"net.kovidgoyal.kitty", @"com.github.wez.wezterm", @"co.zeit.hyper", @"com.termius-dmg.mac"]];
        return vTypingLogKeys && !IsSecureEventInputEnabled() && ![excluded containsObject:_tlApp];
    }

    static void TLWrite(NSMutableDictionary* d) {
        if (!vTypingLogEnabled) return;
        d[@"t"] = TLTime();
        d[@"app"] = _tlApp ?: @"";
        if (_siteHost.length > 0) d[@"site"] = _siteHost;
        NSData* data = [NSJSONSerialization dataWithJSONObject:d options:0 error:nil];
        if (!data) return;
        if (!_tlPending) _tlPending = [NSMutableString new];
        [_tlPending appendString:[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]];
        [_tlPending appendString:@"\n"];
        if (_tlPending.length > 8192) TLFlush();
    }

    static inline NSString* TLLang(int lang) { return lang ? @"vi" : @"en"; }

    /// Ends the word being typed and logs it. reason: the key or action that ended it.
    void TLEndWord(NSString* reason) {
        if (_tlWordLen == 0) return;
        bool hasLetter = false;
        for (int x = 0; x < _tlWordLen; x++) if (_tlWord[x] != 0x232B) { hasLetter = true; break; }
        if (hasLetter) TLCount(_tlWordLang ? @"words_vi" : @"words_en");
        if (hasLetter && TLKeysAllowed()) {
            NSMutableDictionary* d = [@{@"e": @"word", @"lang": TLLang(_tlWordLang),
                                        @"keys": [NSString stringWithCharacters:_tlWord length:_tlWordLen], @"end": reason} mutableCopy];
            if (_tlWordTruncated) d[@"truncated"] = @YES;
            TLWrite(d);
        }
        _tlWordLen = 0;
        _tlWordTruncated = false;
    }

    static void TLAppendChar(UniChar c) {
        if (_tlWordLen == 0) _tlWordLang = vLanguage;
        if (_tlWordLen < 64) _tlWord[_tlWordLen++] = c; else _tlWordTruncated = true;
    }

    static NSString* TLKeyName(CGKeyCode key, bool shift) {
        switch (key) {
            case KEY_SPACE: return @"space";
            case KEY_RETURN: case KEY_ENTER: return @"enter";
            case KEY_TAB: return @"tab";
            case KEY_ESC: return @"esc";
            case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN: return @"arrow";
        }
        Uint16 ch = keyCodeToCharacter(key | (shift ? CAPS_MASK : 0));
        return ch ? [NSString stringWithCharacters:&ch length:1] : @"other";
    }

    /// Every key the user presses (not our own synthesized ones, not the switch hotkey).
    void TLNoteKey(CGKeyCode key, CGEventFlags flags) {
        if (!vTypingLogEnabled) return;
        _tlSinceAuto = false;
        bool shift = flags & kCGEventFlagMaskShift;
        if (flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate)) {
            // record which shortcut ended the word, e.g. ⌥⌫ (delete word) is a correction signal
            NSMutableString* combo = [NSMutableString string];
            if (flags & kCGEventFlagMaskControl) [combo appendString:@"⌃"];
            if (flags & kCGEventFlagMaskAlternate) [combo appendString:@"⌥"];
            if (shift) [combo appendString:@"⇧"];
            if (flags & kCGEventFlagMaskCommand) [combo appendString:@"⌘"];
            [combo appendString:key == KEY_DELETE ? @"⌫" : [TLKeyName(key, false) uppercaseString]];
            TLEndWord(combo);
            return;
        }
        bool caps = shift || (flags & kCGEventFlagMaskAlphaShift);
        if (key == KEY_DELETE) { TLAppendChar(0x232B); return; } // ⌫
        if (IsLetterKey(key) || (IS_NUMBER_KEY(key) && !shift)) {
            Uint16 ch = keyCodeToCharacter(key | (caps ? CAPS_MASK : 0));
            if (ch) TLAppendChar(ch);
            return;
        }
        TLEndWord(TLKeyName(key, shift));
    }

    void TLNoteClick() {
        _tlSinceAuto = false;
        TLEndWord(@"click");
    }

    void TLAppChanged() {
        TLEndWord(@"app");
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        NSString* b = front.bundleIdentifier ?: front.localizedName;
        if (b) _tlApp = b;
    }

    static void TLAuto(NSString* rule, int to) {
        TLCount([@"auto_" stringByAppendingString:rule]);
        TLWrite([@{@"e": @"auto", @"rule": rule, @"to": TLLang(to)} mutableCopy]);
        _tlSinceAuto = true;
    }

    /// The user switched language by hand. after_auto: right after an automatic switch.
    void TLManual(NSString* via, int to) {
        TLEndWord(@"switch");
        bool afterAuto = _tlSinceAuto;
        _tlSinceAuto = false;
        TLCount(@"manual");
        if (afterAuto) TLCount(@"manual_after_auto");
        NSMutableDictionary* d = [@{@"e": @"manual", @"via": via, @"to": TLLang(to)} mutableCopy];
        if (afterAuto) d[@"after_auto"] = @YES;
        TLWrite(d);
    }

    /// Backspace undid an automatic correction.
    static void TLUndo() {
        TLEndWord(@"undo");
        _tlSinceAuto = false;
        TLCount(@"undo");
        TLWrite([@{@"e": @"undo", @"via": @"backspace", @"to": TLLang(_autoFixUndo.language)} mutableCopy]);
    }

    static void TLContext(int to) {
        TLEndWord(@"context");
        TLCount(@"context");
        TLWrite([@{@"e": @"context", @"to": TLLang(to)} mutableCopy]);
    }


    /// Engine character data to UTF-16. Only valid for the Unicode code table.
    static UniChar CharacterOf(const Uint32& data) {
        if (data & CHAR_CODE_MASK) return (UniChar)(data & 0xFFFF);
        return (UniChar)keyCodeToCharacter(data);
    }

    /// Remembers how to undo the correction just sent: the original word and the break key.
    static void RememberAutoFix(int correctedCount) {
        _autoFixUndo.active = false;
        if (vAutoSwitchUndoCount <= 0 || correctedCount <= 0) return;
        // only a fix of the last word is undone; one that also fixed earlier words stays (Backspace just deletes)
        for (int x = 0; x < vAutoSwitchUndoCount; x++) if ((vAutoSwitchUndoData[x] & 0xFFFF) == KEY_SPACE && !(vAutoSwitchUndoData[x] & CHAR_CODE_MASK)) return;
        Uint16 breakChar = keyCodeToCharacter(_keycode | ((_flag & kCGEventFlagMaskShift) || ((_flag & kCGEventFlagMaskAlphaShift) && IsLetterKey(_keycode)) ? CAPS_MASK : 0));   // Caps Lock shifts letters only (Space + CAPS_MASK is 0)
        if (breakChar == 0) return; // Enter and the like have already acted
        int n = 0;
        for (int x = 0; x < vAutoSwitchUndoCount && n < MAX_BUFF; x++) {
            UniChar c = CharacterOf(vAutoSwitchUndoData[x]);
            if (c == 0) return;
            _autoFixUndo.text[n++] = c;
        }
        _autoFixUndo.textCount = n;
        _autoFixUndo.keyCount = 0;
        for (int x = 0; x < vAutoSwitchUndoCount && x < MAX_BUFF; x++) {
            if (vAutoSwitchUndoData[x] & CHAR_CODE_MASK) { _autoFixUndo.keyCount = 0; break; }   // shown text, not keys
            _autoFixUndo.keys[_autoFixUndo.keyCount++] = vAutoSwitchUndoData[x];
        }
        _autoFixUndo.removeCount = correctedCount + 1;
        _autoFixUndo.language = vLanguage;
        _autoFixUndo.active = true;
    }

    /// Backspace right after an automatic correction puts the original word back (like macOS
    /// autocorrect) and returns to the previous language. The switch hotkey stays a plain switch,
    /// because right after a correction it usually means "the next word is in the other language".
    static bool UndoAutoFixWithBackspace() {
        if (!_autoFixUndo.active) return false;
        _autoFixUndo.active = false;
        int removeCount = _autoFixUndo.removeCount;
        if (shouldUseRecommendWorkaround(FRONT_APP)) {
            SendEmptyCharacter();
            removeCount++;
        }
        for (int x = 0; x < removeCount; x++) {
            CGEventTapPostEvent(_proxy, eventBackSpaceDown);
            CGEventTapPostEvent(_proxy, eventBackSpaceUp);
        }
        PostText(_autoFixUndo.text, _autoFixUndo.textCount);
        TLUndo();
        if (vLanguage != _autoFixUndo.language) {
            vLanguage = _autoFixUndo.language;
            VKLanguageChanged(YES);
        }
        startNewSession();
        ResetEnglishWord(false); // the cursor is at the end of a word we didn't see typed
        //back in English with the typed keys on screen: they are the current word again, so typing on
        //("vaay" + "j") is checked as a whole ("vaayj" -> "vậy"); the same word again stays as typed
        if (vLanguage == 0 && _autoFixUndo.keyCount > 0) {
            memcpy(_enKeys, _autoFixUndo.keys, _autoFixUndo.keyCount * sizeof(Uint32));
            _enCount = _autoFixUndo.keyCount;
            _enValid = true;
            memcpy(_undoneKeys, _autoFixUndo.keys, _autoFixUndo.keyCount * sizeof(Uint32));
            _undoneCount = _autoFixUndo.keyCount;
        }
        return true;
    }

    static void AutoSwitchTo(int language, NSString* rule) {
        if (vLanguage == language) return;
        TLAuto(rule, language);
        vLanguage = language;
        VKLanguageChanged(YES); // updates the menu bar and the per-app/per-site memory
        startNewSession();
        vAutoSwitchForget();
    }

    static void ApplyAutoSwitchToEnglish(int correctedCount, bool atWordBreak) {
        vAutoSwitchRequest = -1;
        if (atWordBreak) RememberAutoFix(correctedCount); else _autoFixUndo.active = false;
        AutoSwitchTo(0, atWordBreak ? @"vi_word_to_en" : @"double_key");
        ResetEnglishWord(atWordBreak && IsWhitespaceKey(_keycode)); // mid-word: the rest of this word is not a whole word
        //D35: the word just put back to English is the previous word for look-back ("em is" + "ạ" -> "em í ạ")
        if (atWordBreak && _keycode == KEY_SPACE && vAutoSwitchRestoredCount > 0) {
            memcpy(_enPrevKeys[0], vAutoSwitchRestoredKeys, vAutoSwitchRestoredCount * sizeof(Uint32));
            _enPrevCounts[0] = vAutoSwitchRestoredCount;
            _enPrevN = 1;
            _enPrevRestored = true;
        }
        _enAfterDouble = !atWordBreak;   // at its end, "mixx" may become "mix"
    }

    static bool OtherInputSourceActive() {
        return _otherInputLanguage;
    }

    /// English mode key: collect the word; at its end ask the engine whether it was Vietnamese.
    /// Returns true when the key was consumed (the word was replaced and the break key re-sent).
    static bool HandleEnglishKeyForAutoSwitch() {
        if ((OTHER_CONTROL_KEY) && !PLAIN_ARROW) {
            ResetEnglishWord(true);
            _enAfterShortcut = (_flag & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl)) != 0;
            return false;
        }
        bool shift = _flag & kCGEventFlagMaskShift;
        bool caps = shift || (_flag & kCGEventFlagMaskAlphaShift);
        if ((_keycode == KEY_LEFT || _keycode == KEY_RIGHT) && !shift) {   // D51: ← → round trip inside a word
            if (_keycode == KEY_LEFT && _enNavOffset == 0 && (_enCount > 0 || _enPrevN > 0)) {
                memcpy(_enNavKeys, _enKeys, _enCount * sizeof(Uint32)); _enNavCount = _enCount; _enNavValid = _enValid;
                _enNavAfterDouble = _enAfterDouble; _enNavPrevN = _enPrevN; _enNavOffset = -1;
                StartEnglishWord(false); _enAfterDouble = false; _enPrevN = 0;
                return false;
            }
            if (_enNavOffset < 0) {
                if (_keycode == KEY_LEFT) _enNavOffset--;
                else if (++_enNavOffset == 0) {   // back at the word's end: it's that word again
                    memcpy(_enKeys, _enNavKeys, _enNavCount * sizeof(Uint32)); _enCount = _enNavCount; _enValid = _enNavValid;
                    _enAfterDouble = _enNavAfterDouble; _enPrevN = _enNavPrevN;
                }
                return false;
            }
        }
        _enNavOffset = 0;
        if (IsLetterKey(_keycode)) {
            if (_enCount < MAX_BUFF - 2) _enKeys[_enCount++] = _keycode | (caps ? CAPS_MASK : 0);
            else _enValid = false;
            _enAfterShortcut = false;
            return false;
        }
        if (_keycode == KEY_DELETE) {
            if (_enCount == 0) { _enPrevN = 0; _enAfterDouble = false; } // editing the previous word
            if (_enCount > 0) _enCount--;
            else if (!_enAfterShortcut) _enValid = false; // deleting into the previous word: we don't know its letters
            return false;
        }
        if (IS_NUMBER_KEY(_keycode) && !shift) {   // digits after a space start a word of their own ("5trawm" -> "5trăm", D33); glued to letters, a code
            _enValid = _enCount == 0 && _enValid;
            _enCount = 0;
            _enPrevN = 0;
            _enAfterDouble = false;
            return false;
        }
        bool wordEnd = _keycode == KEY_SPACE || _keycode == KEY_COMMA || _keycode == KEY_DOT || _keycode == KEY_SEMICOLON ||
                       _keycode == KEY_RETURN || _keycode == KEY_ENTER || (shift && (_keycode == KEY_SLASH || _keycode == KEY_1 || _keycode == KEY_QUOTE || _keycode == KEY_0));
        int count = _enCount;
        bool valid = _enValid;
        bool afterDouble = _enAfterDouble;
        _enAfterDouble = false;
        if (afterDouble && wordEnd && vAutoSwitchFixDoubledKey(_enKeys, count)) {
            StartEnglishWord(IsWordStartKey(_keycode, shift));
            _enPrevN = 0;
            int correctedCount = pData->newCharCount;
            SendEngineOutput();
            RememberAutoFix(correctedCount);
            return true;
        }
        // the next word is whole only after whitespace; after . _ - / ( etc. it's part of an identifier or path
        StartEnglishWord(IsWordStartKey(_keycode, shift));
        bool checked = wordEnd && valid && count >= 2 && !(vOtherLanguage && OtherInputSourceActive());
        if (_undoneCount > 0) {   // the word just put back by Backspace, finished unchanged: keep it as typed
            if (count == _undoneCount && !memcmp(_enKeys, _undoneKeys, count * sizeof(Uint32))) checked = false;
            if (wordEnd) _undoneCount = 0;
        }
        const Uint32* previous[2] = {_enPrevKeys[0], _enPrevKeys[1]};
        vAutoSwitchPrevRestored = _enPrevRestored && _enPrevN >= 1;
        bool converted = checked && vAutoSwitchCheckEnglishWord(_enKeys, count, previous, _enPrevCounts, _enPrevN);
        // remember this word for look-back only if it was followed by a single space
        if (!converted && _keycode == KEY_SPACE && valid && count >= 1) {
            _enPrevRestored = false;
            memcpy(_enPrevKeys[1], _enPrevKeys[0], sizeof(_enPrevKeys[0]));
            _enPrevCounts[1] = _enPrevCounts[0];
            memcpy(_enPrevKeys[0], _enKeys, count * sizeof(Uint32));
            _enPrevCounts[0] = count;
            if (_enPrevN < 2) _enPrevN++;
        } else {
            _enPrevN = 0;
        }
        if (!converted) return false;

        int correctedCount = pData->newCharCount;
        SendEngineOutput();
        RememberAutoFix(correctedCount);
        AutoSwitchTo(1, @"en_word_to_vi");
        return true;
    }

    // TODO: Research API to convert character into CGKeyCode more elegantly!
    int ConvertKeyStringToKeyCode(NSString *keyString, CGKeyCode fallback) {
        // Infomation about capitalization (shift/caps) is already included
        // in the original CGEvent, only find out which position on keyboard a key is pressed
        NSString *lowercasedKeyString = [keyString lowercaseString];
        if (!lowercasedKeyString) {
            return fallback;
        }
        
        NSNumber *keycode = [keyStringToKeyCodeMap objectForKey:lowercasedKeyString];

        if (keycode) {
            return [keycode intValue];
        }
        return fallback;
    }

    // If conversion fails, return fallbackKeyCode
    CGKeyCode ConvertEventToKeyboadLayoutCompatKeyCode(CGEventRef keyEvent, CGKeyCode fallbackKeyCode) {
        NSEvent *kbLayoutCompatEvent = [NSEvent eventWithCGEvent:keyEvent];
        NSString *kbLayoutCompatKeyString = kbLayoutCompatEvent.charactersIgnoringModifiers;
        return ConvertKeyStringToKeyCode(kbLayoutCompatKeyString,
                                         fallbackKeyCode);
    }

    /**
     * MAIN HOOK entry, very important function.
     * MAIN Callback.
     */
    CGEventRef VikeyEventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon) {
        // macOS turns the tap off when a callback is too slow (busy or memory-starved Mac) or
        // during secure input, and only tells us once. Turn it straight back on, otherwise
        // typing silently stops until the app is restarted.
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            [VikeyCore reenableEventTap];
            return event;
        }
        if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == VIKEY_EVENT_TAG) {
            return event;
        }
        //dont handle my event
        if (CGEventGetIntegerValueField(event, kCGEventSourceStateID) == CGEventSourceGetSourceStateID(myEventSource)) {
            return event;
        }
        _proxy = proxy;
        ForgetPerKeyState();

        _flag = CGEventGetFlags(event);
        _keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        
        if (type == kCGEventKeyDown && vPerformLayoutCompat) {
            // If conversion fail, use current keycode
           _keycode = ConvertEventToKeyboadLayoutCompatKeyCode(event, _keycode);
        }
        
        // emoji picker hotkey: a key with exactly these modifiers
        // (not in an excluded app: there the key belongs to that app)
        if (type == kCGEventKeyDown && vEmojiHotkey && !_frontExcluded && GET_SWITCH_KEY(vEmojiHotkey) == _keycode &&
            HAS_CONTROL(vEmojiHotkey) == GET_BOOL(_flag & kCGEventFlagMaskControl) && HAS_OPTION(vEmojiHotkey) == GET_BOOL(_flag & kCGEventFlagMaskAlternate) &&
            HAS_COMMAND(vEmojiHotkey) == GET_BOOL(_flag & kCGEventFlagMaskCommand) && HAS_SHIFT(vEmojiHotkey) == GET_BOOL(_flag & kCGEventFlagMaskShift)) {
            _lastFlag = 0; if (_flag & MODIFIER_FLAGS) _modifierChordUsed = true;   // ⌃⇧E: releasing ⌃⇧ afterwards isn't the ⌃⇧ switch hotkey
            if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)) return NULL;   // held: open or close once per press
            if (vEmojiHandler) dispatch_async(dispatch_get_main_queue(), vEmojiHandler);
            return NULL;
        }

        //switch language shortcut; convert hotkey
        if (type == kCGEventKeyDown) {
            if (GET_SWITCH_KEY(vSwitchKeyStatus) != _keycode) {
                _lastFlag = 0;
                if (_flag & MODIFIER_FLAGS) _modifierChordUsed = true;   // ⌃⇧K: releasing ⌃⇧ afterwards isn't the hotkey
            } else {
                if (GET_SWITCH_KEY(vSwitchKeyStatus) == _keycode && checkHotKey(vSwitchKeyStatus, _flag, GET_SWITCH_KEY(vSwitchKeyStatus) != 0xFE)){
                    // holding ⌥Z auto-repeats ~30 times a second: switch once per press, swallow the repeats
                    if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)) return NULL;
                    switchLanguage();
                    _lastFlag = 0;
                    _hasJustUsedHotKey = true;
                    return NULL;
                }
            }
            _hasJustUsedHotKey = _lastFlag != 0;
        } else if (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown) {
            // ⌃⇧-click (any button) or drag: the modifiers were for the click, so releasing them isn't the ⌃⇧ hotkey
            _lastFlag = 0;
            if (_flag & MODIFIER_FLAGS) _modifierChordUsed = true;
        } else if (type == kCGEventFlagsChanged && _modifierChordUsed) {
            // the modifiers held went with a click or another key: no modifier-only hotkey until all are released
            // (⌃⌥⇧-click, then letting go of ⌥ and ⇧ one by one, must not switch)
            _lastFlag = 0;
            if (!(_flag & MODIFIER_FLAGS)) _modifierChordUsed = false;
        } else if (type == kCGEventFlagsChanged) {
            if (_lastFlag == 0 || _lastFlag < _flag) {
                _lastFlag = _flag;
            } else if (_lastFlag > _flag)  {
                //check switch
                if (checkHotKey(vSwitchKeyStatus, _lastFlag, GET_SWITCH_KEY(vSwitchKeyStatus) != 0xFE)) {
                    _lastFlag = 0;
                    switchLanguage();
                    _hasJustUsedHotKey = true;
                    return NULL;
                }
                _lastFlag = 0;
                _hasJustUsedHotKey = false;
            }
        }

        // an excluded app (remote desktop...) is in front: every key goes through as typed
        if (_frontExcluded) return event;

        // the emoji picker reads raw keys for its search (it does its own Telex reading)
        if (vEmojiOpen) return event;

        // Backspace right after an automatic correction undoes it; any other key or click ends that chance
        if (type == kCGEventKeyDown && _keycode == KEY_DELETE && _autoFixUndo.active && !(OTHER_CONTROL_KEY)) {
            if (UndoAutoFixWithBackspace()) return NULL;
        }
        if (type == kCGEventKeyDown || type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown) {
            _autoFixUndo.active = false;
            if (type == kCGEventKeyDown) TLNoteKey(_keycode, _flag); else TLNoteClick();
        }

        // Also check correct event hooked
        if ((type != kCGEventKeyDown) && (type != kCGEventKeyUp) &&
            (type != kCGEventLeftMouseDown) && (type != kCGEventRightMouseDown) &&
            (type != kCGEventLeftMouseDragged) && (type != kCGEventRightMouseDragged))
            return event;
        
        _proxy = proxy;
        
        //If is in english mode
        if (vLanguage == 0) {
            if (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown) {
                ResetEnglishWord(true);
                if (vUseSmartSwitchKey) NudgeSiteCheck();
                return event;
            }
            if (type != kCGEventKeyDown)
                return event;
            if (vUseSmartSwitchKey && (_flag & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl))) {
                NudgeSiteCheck();
            }
            if (vAutoSwitchByTyping && HandleEnglishKeyForAutoSwitch())
                return NULL;
            return event;
        }
        
        //handle mouse
        if (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown) {
            RequestNewSession();
            if (vUseSmartSwitchKey) NudgeSiteCheck();
            return event;
        }
        if (vUseSmartSwitchKey && type == kCGEventKeyDown && (_flag & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl))) {
            NudgeSiteCheck();   // ⌘1–9, ⌃Tab, ⌘L, ⌘T, ⌘W can change the active tab
        }

        //another keyboard language (Japanese, Chinese...) is active: stay out of its way
        if (vOtherLanguage && _otherInputLanguage) return event;
        
        //handle keyboard
        if (type == kCGEventKeyDown) {
            //send event signal to Engine
            vAutoSwitchRequest = -1;
            vKeyHandleEvent(vKeyEvent::Keyboard,
                            vKeyEventState::KeyDown,
                            _keycode,
                            _flag & kCGEventFlagMaskShift ? 1 : (_flag & kCGEventFlagMaskAlphaShift ? 2 : 0),
                            (OTHER_CONTROL_KEY) && !PLAIN_ARROW);
            if (pData->code == vDoNothing) { //do nothing
                return event;
            } else if (pData->code == vWillProcess || pData->code == vRestore || pData->code == vRestoreAndStartNewSession) { //handle result signal
                
                bool autoSwitchAtBreak = vAutoSwitchRequest == 0 && !IsLetterKey(_keycode);
                int correctedCount = pData->newCharCount;
                SendEngineOutput();
                if (vAutoSwitchRequest == 0) ApplyAutoSwitchToEnglish(correctedCount, autoSwitchAtBreak);
            }
            
            return NULL;
        }
        
        return event;
    }
}

#pragma mark - Bridge

extern "C" {
    void VikeyInit(void);
    void VKLoadSettings(void);
    CGEventRef VikeyEventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon);
    void TLAppChanged(void);
    void TLManual(NSString* via, int to);
    void PauseAutoSwitchAfterManual(void);
    void TLFlush(void);
    void TLClear(void);
    NSString* TLLogPath(void);
    NSDictionary* TLStatsSnapshot(void);
    void TLAfterWrites(void (^done)(void));
    void OnFocusChanged(void);
    void RequestNewSession(void);
    void OnActiveAppChanged(void);
    void OnInputMethodChanged(void);
    void OnAppForExclusion(void);
    int VKAutoSwitchForFrontApp(void);
    int VKFrontAppExcluded(void);
    NSString* VKFrontAppName(void);
    void VKSetAutoSwitchForFrontApp(int on);
}

/// Saves the language, remembers it for the current app/site if asked, and tells the UI.
static void VKLanguageChanged(BOOL rememberForContext) {
    [[NSUserDefaults standardUserDefaults] setInteger:vLanguage forKey:@"InputMethod"];
    if (rememberForContext) OnInputMethodChanged();
    void (^post)(void) = ^{ [[NSNotificationCenter defaultCenter] postNotificationName:VikeyLanguageDidChangeNotification object:nil]; };
    if ([NSThread isMainThread]) post(); else dispatch_async(dispatch_get_main_queue(), post);
}

@implementation VikeyCore

+ (void)requestBrowserAccess:(void (^)(BOOL denied))done {
    NSMutableArray<NSString*>* running = [NSMutableArray array];
    for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
        if (a.bundleIdentifier && BrowserScripts()[a.bundleIdentifier]) [running addObject:a.bundleIdentifier];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL denied = NO;
        for (NSString* b in running) {
            // the same read per-site memory does: shows the system prompt the first time
            NSDictionary* err = nil;
            [[[NSAppleScript alloc] initWithSource:BrowserScripts()[b]] executeAndReturnError:&err];
            int st = [err[NSAppleScriptErrorNumber] intValue];
            if (st == errAEEventNotPermitted) denied = YES;
        }
        dispatch_async(dispatch_get_main_queue(), ^{ done(denied); });
    });
}

static BOOL _inited = NO;
static CFMachPortRef _eventTap = NULL;
static CFRunLoopSourceRef _runLoopSource = NULL;
static NSTimer* _watchdog = nil;

+ (BOOL)isAccessibilityTrusted {
    return AXIsProcessTrusted();
}

+ (void)requestAccessibility {
    NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
    AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

+ (BOOL)start {
    if (_eventTap) return YES;
    if (!_inited) { VikeyInit(); _inited = YES; }
    CGEventMask mask = (1 << kCGEventKeyDown) | (1 << kCGEventKeyUp) | (1 << kCGEventFlagsChanged) |
                       (1 << kCGEventLeftMouseDown) | (1 << kCGEventRightMouseDown) | (1 << kCGEventOtherMouseDown);
    _eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, mask, VikeyEventCallback, NULL);
    if (!_eventTap) return NO;
    _runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, _eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), _runLoopSource, kCFRunLoopCommonModes);
    CGEventTapEnable(_eventTap, true);
    // Safety net: if macOS ever leaves the tap off (missed disable event, dead port), bring it back within 2 s.
    if (!_watchdog) {
        _watchdog = [NSTimer scheduledTimerWithTimeInterval:2.0 repeats:YES block:^(NSTimer* t) {
            if (!_eventTap) return;
            if (!CFMachPortIsValid(_eventTap)) { [VikeyCore stop]; [VikeyCore start]; [VikeyCore resetTyping]; return; }
            if (!CGEventTapIsEnabled(_eventTap)) [VikeyCore reenableEventTap];
        }];
    }
    return YES;
}

+ (void)stop {
    if (_runLoopSource) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), _runLoopSource, kCFRunLoopCommonModes);
        CFRelease(_runLoopSource);
        _runLoopSource = NULL;
    }
    if (_eventTap) {
        CGEventTapEnable(_eventTap, false);
        CFMachPortInvalidate(_eventTap);
        CFRelease(_eventTap);
        _eventTap = NULL;
    }
}

+ (BOOL)isRunning { return _eventTap != NULL; }

+ (void)reenableEventTap {
    if (!_eventTap) return;
    CGEventTapEnable(_eventTap, true);
    [self resetTyping];   // keys typed while the tap was off reached the app unseen: the word on screen isn't the engine's
}

+ (int)language { return vLanguage; }

+ (void)setLanguageByUser:(int)language {
    if (vLanguage == language) return;
    vLanguage = language;
    PauseAutoSwitchAfterManual();
    TLManual(@"menu", language);
    VKLanguageChanged(YES);
    startNewSession();
}

+ (void)reloadSettings {
    VKLoadSettings();
    VKLanguageChanged(NO);
}

+ (void)frontmostAppChanged {
    TLAppChanged();
    OnFocusChanged();
    if (!_inited) return;
    RequestNewSession();
    OnAppForExclusion();
    if (vUseSmartSwitchKey && !_frontExcluded) OnActiveAppChanged();   // no language memory inside an excluded app
}

+ (BOOL)autoSwitchEnabledForFrontApp { return VKAutoSwitchForFrontApp() != 0; }
+ (BOOL)frontAppIsExcluded { return VKFrontAppExcluded() != 0; }
+ (NSString *)frontAppName { return VKFrontAppName(); }
+ (void)setAutoSwitchForFrontApp:(BOOL)on { VKSetAutoSwitchForFrontApp(on ? 1 : 0); }

+ (void)activeSpaceChanged {
    if (_inited) RequestNewSession();
}

+ (void)setEmojiHotkeyHandler:(void (^)(void))handler { vEmojiHandler = [handler copy]; }
+ (void)setEmojiPickerOpen:(BOOL)open { vEmojiOpen = open; }

+ (int)hotkeyKeyCodeForEvent:(NSEvent *)event {
    // the tap matches hotkeys against the key code mapped back to QWERTY (Dvorak, AZERTY...): record the same
    int code = event.keyCode;
    if (vPerformLayoutCompat) code = ConvertKeyStringToKeyCode(event.charactersIgnoringModifiers, code);
    return code;
}

+ (void)resetTyping {
    ResetEnglishWord(true);
    _autoFixUndo.active = false;
    startNewSession();
}

+ (void)insertText:(NSString*)text {
    [self resetTyping];
    NSUInteger len = text.length;
    UniChar buf[64];
    for (NSUInteger start = 0; start < len; start += 16) {
        NSUInteger n = MIN(16, len - start);
        [text getCharacters:buf range:NSMakeRange(start, n)];
        CGEventRef down = CGEventCreateKeyboardEvent(myEventSource, 0, true), up = CGEventCreateKeyboardEvent(myEventSource, 0, false);
        CGEventSetIntegerValueField(down, kCGEventSourceUserData, VIKEY_EVENT_TAG);
        CGEventSetIntegerValueField(up, kCGEventSourceUserData, VIKEY_EVENT_TAG);
        CGEventSetFlags(down, 0); CGEventSetFlags(up, 0);
        CGEventKeyboardSetUnicodeString(down, n, buf);
        CGEventKeyboardSetUnicodeString(up, n, buf);
        CGEventPost(kCGSessionEventTap, down);
        CGEventPost(kCGSessionEventTap, up);
        CFRelease(down); CFRelease(up);
    }
}

+ (NSString*)typingLogPath { return TLLogPath(); }
+ (NSDictionary*)typingLogStats { return TLStatsSnapshot(); }
+ (void)flushTypingLog { TLFlush(); }
+ (void)flushTypingLogThen:(void (^)(void))done { TLAfterWrites(done); }
+ (void)clearTypingLog { TLClear(); }

@end
