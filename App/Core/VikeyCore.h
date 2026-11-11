//
//  VikeyCore.h
//  The bridge between the SwiftUI app and the keyboard core (event tap + Telex engine).
//
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

/// Posted on the main thread whenever the typing language changes (hotkey, auto switch, smart switch, menu).
extern NSNotificationName const VikeyLanguageDidChangeNotification;

@interface VikeyCore : NSObject

/// Vikey needs Accessibility permission to read and rewrite keystrokes.
+ (BOOL)isAccessibilityTrusted;
/// Asks macOS for Accessibility permission (shows the system prompt the first time).
+ (void)requestAccessibility;

/// Starts the event tap. Returns NO if macOS refused (permission missing).
+ (BOOL)start;
+ (void)stop;
+ (BOOL)isRunning;
+ (void)reenableEventTap;

/// 1 = Vietnamese, 0 = English.
+ (int)language;
/// A language change made by the user from Vikey's UI (menu bar or settings).
+ (void)setLanguageByUser:(int)language;

/// Re-reads settings from UserDefaults into the engine after the UI changed one.
+ (void)reloadSettings;

/// Auto switch for the app in front: the "AutoSwitchByTyping" setting unless this app has its own choice.
+ (BOOL)autoSwitchEnabledForFrontApp;
+ (void)setAutoSwitchForFrontApp:(BOOL)on;
+ (BOOL)frontAppIsExcluded;
+ (NSString *)frontAppName;

/// Asks macOS for permission to read the tab address of every running browser (per-site memory).
/// Calls back on the main thread with YES when the user has refused it for at least one browser.
+ (void)requestBrowserAccess:(void (^)(BOOL denied))done;

/// App and space changes, forwarded from NSWorkspace notifications.
+ (void)frontmostAppChanged;
+ (void)activeSpaceChanged;

/// Emoji picker: called on the main thread when its hotkey is pressed (EmojiPicker / EmojiHotkey defaults).
+ (void)setEmojiHotkeyHandler:(void (^)(void))handler;
/// While open, keys reach the picker untouched (no Vietnamese typing).
+ (void)setEmojiPickerOpen:(BOOL)open;
/// The key code a hotkey recorded from this key press is stored as: on Dvorak, AZERTY... the key that types
/// "z" is recorded as Z, the same way the event tap reads it.
+ (int)hotkeyKeyCodeForEvent:(NSEvent *)event;
/// Forget the word being typed (the cursor moved to another field without a click).
+ (void)resetTyping;
/// Types text into the focused app.
+ (void)insertText:(NSString *)text;

/// Typing log.
+ (NSString *)typingLogPath;
+ (NSDictionary<NSString *, NSDictionary<NSString *, NSNumber *> *> *)typingLogStats;
+ (void)flushTypingLog;
/// Flushes, then calls done on the main thread once the log file has everything (and any clear has run).
+ (void)flushTypingLogThen:(void (^)(void))done;
+ (void)clearTypingLog;

@end

NS_ASSUME_NONNULL_END
