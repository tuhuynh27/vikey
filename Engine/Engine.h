//
//  Engine.h
//  Vikey (based on OpenKey)
//
//  Created by Tuyen on 1/18/19.
//  Copyright © 2019 Tuyen Mai. All rights reserved.
//

#ifndef Engine_h
#define Engine_h

#include <locale>
#include <codecvt>

#include "DataType.h"
#include "Vietnamese.h"
#include "SmartSwitchKey.h"

#define IS_DEBUG 1

#ifndef LOBYTE
#define LOBYTE(data) (data & 0xFF)
#endif // !LOBYTE
#ifndef HIBYTE
#define HIBYTE(data) ((data>>8) & 0xFF)
#endif // !HIBYTE

#define GET_SWITCH_KEY(data) (data & 0xFF)
#define HAS_CONTROL(data) ((data & 0x100) ? 1 : 0)
#define HAS_OPTION(data) ((data & 0x200) ? 1 : 0)
#define HAS_COMMAND(data) ((data & 0x400) ? 1 : 0)
#define HAS_SHIFT(data) ((data & 0x800) ? 1 : 0)
#define GET_BOOL(data) (data ? 1 : 0)
#define HAS_BEEP(data) (data & 0x8000)
#define SET_SWITCH_KEY(data, key) data = (data & 0xFF) | key
#define SET_CONTROL_KEY(data, val) data|=val<<8;
#define SET_OPTION_KEY(data, val) data|=val<<9;
#define SET_COMMAND_KEY(data, val) data|=val<<10;

//define these variable in your application
//API
/*
 * 0: English
 * 1: Vietnamese
 */
extern int vLanguage;

/*
 * first 8 bit: keycode
 * bit 8: Control on/off
 * bit 9: Option on/off
 * bit 10: Command on/off
 */
extern int vSwitchKeyStatus;

/**
 * 0: No
 * 1: Yes
 */
extern int vCheckSpelling;

/*
 * 0: òa, úy
 * 1: oà uý
*/
extern int vUseModernOrthography;

/**
 * Work together with vCheckSpelling
 * 0: No
 * 1: Yes
 *
 */
extern int vRestoreIfWrongSpelling;

/*
 * Fix recommend browser's address, excel,...
 * 0: No
 * 1: Yes
 */
extern int vFixRecommendBrowser;

/**
 * auto switch language when switch app
 * 0: No
 * 1: Yes
 */
extern int vUseSmartSwitchKey;

/**
 * 0: No; 1: Yes
 * Turn off Vietnamese when typing in another language.
 */
extern int vOtherLanguage;

/**
 * Auto switch language from typing behavior (Telex family only).
 * 0: No; 1: Yes
 */
extern int vAutoSwitchByTyping;

/**
 * Set by the engine when a keystroke should switch the language:
 * -1: no request, 0: switch to English, 1: switch to Vietnamese.
 * The platform layer applies it after sending the key's output, then resets it to -1.
 */
extern int vAutoSwitchRequest;

/**
 * What was on screen before the last automatic correction (engine character data),
 * so the platform can put it back when the user immediately presses the switch hotkey.
 */
extern Uint32 vAutoSwitchUndoData[MAX_BUFF];
extern int vAutoSwitchUndoCount;
/** The keys of the last word auto switch put back to English (so the next word can look back on it). */
extern Uint32 vAutoSwitchRestoredKeys[];
extern int vAutoSwitchRestoredCount;
/** Set by the platform: the previous word passed to vAutoSwitchCheckEnglishWord was put back to English by auto switch. */
extern bool vAutoSwitchPrevRestored;
/** Capitalised mid-sentence counts for ties (D37, experiment): "form<TAB>count" lines. */
void vAutoSwitchSetCapitalised(const char* path, const int& minCount);

/**
 * English mode, at a word break: if the raw keys of the finished word are clearly a
 * Vietnamese word typed in Telex, fills the hook state (code vRestore) with that word and
 * returns true. The platform then replaces the word and switches to Vietnamese.
 * keys: key codes with CAPS_MASK, count: number of keys.
 */
bool vAutoSwitchCheckEnglishWord(const Uint32* keys, const int& count);

/**
 * Same, with the words typed just before it (most recent first, separated by single spaces):
 * ambiguous ones are converted together with it (look-back).
 */
bool vAutoSwitchCheckEnglishWord(const Uint32* keys, const int& count,
                                 const Uint32* const* previous, const int* previousCounts, const int& previousN);

/**
 * Forget the words remembered for look-back (language switched by hand, app changed...),
 * and the ones Backspace would go back into: they may not be what is on screen any more.
 * Right after vAutoSwitchCheckEnglishWord converted a word, Backspace history is that word (it is on screen).
 */
void vAutoSwitchForget();

/** Teencode spelling (D53): z/dz for d, open "ie"/"oy", k after i/ư. On (1) by default. Defined in Engine.cpp. */
extern int vTeencode;

/** Telex style (D56): 0 = Telex, 1 = Simple Telex 1 ([ and ] are brackets, a lone w stays w),
 *  2 = Simple Telex 2 ([ and ] are brackets, a lone w is still ư). uw / ow / aw work in all three. Defined in Engine.cpp. */
extern int vTelexStyle;

/**
 * Call at the end of the first English word after a doubled tone key switched to English (D18).
 * keys: the letters typed since the switch. When the word with both presses of the key is not
 * English but the word with one is ("mixx" -> "mix"), fills the hook state (code vRestore) with
 * the fixed word and returns true. Always forgets the pending switch.
 */
bool vAutoSwitchFixDoubledKey(const Uint32* keys, const int& count);

/**
 * For touch keyboards (fat-finger model): how likely each next key is after these letters of the
 * current word, from the dictionaries. out[0..25] = a..z, out[26] = the word ends here. Unnormalized
 * weights (occurrences per million). vietnamese: the Telex key list, else English. prefix: lowercase.
 */
void vAutoSwitchNextKeyMass(const int& vietnamese, const char* prefix, double out[27]);

/** Score (10·log10 per million) of the word these letters type: English as typed, or Vietnamese as
 *  typed in Telex (any tone order). -1000 when it isn't a word. rendered: the word's characters. */
int vAutoSwitchWordScore(const int& vietnamese, const char* letters, Uint16* rendered, int* renderedLen);

/** The most common dictionary entries starting with prefix (English words, or Vietnamese syllables as
 *  normalized Telex keys), best first. Returns how many. */
/** Whether these keys, typed in Telex, spell a valid Vietnamese syllable (spelling rules, not the list). */
bool vAutoSwitchIsVietnameseSpelling(const char* letters);
/** Early Switch (D28): 1 = the word can only be Vietnamese, 0 = only English, -1 = either or neither. */
int vAutoSwitchWordSide(const Uint32* keys, const int& count);
/** The side of the word just finished in Vietnamese mode with nothing to fix (set at its space), -1 if none. */
extern int vAutoSwitchLastSide;
/** Word pairs for ties (D30, experiment): load "prev<TAB>next<TAB>count" lines; weight 0 = off. */
void vAutoSwitchSetBigrams(const char* path, const float& weight, const int& minCount);
/** Word triples for ties (D34, experiment): "w1<TAB>w2<TAB>w3<TAB>count" lines; tried before word pairs. */
void vAutoSwitchSetTrigrams(const char* path, const float& weight, const int& minCount);

int vAutoSwitchCompletions(const int& vietnamese, const char* prefix, char out[][32], int* scores, const int& maxOut);

/**
 * Call this function first to receive data pointer
 */
void* vKeyInit();

/**
 * Convert engine character to real character
 */
Uint32 getCharacterCode(const Uint32& data);

/**
 * MAIN entry point for each key
 * event: mouse or keyboard event
 * state: additional state for event
 * data: key code
 * isCaps: caplock is on or shift key is pressing
 * otherControlKey: ctrl, option,... is pressing
 */
void vKeyHandleEvent(const vKeyEvent& event,
                     const vKeyEventState& state,
                     const Uint16& data,
                     const Uint8& capsStatus=0,
                     const bool& otherControlKey=false);

/**
 * Start a new word
 */
void startNewSession();

/**
 * reset spelling value
 */
void vSetCheckSpelling();

/**
 * some utils function
 */
wstring utf8ToWideString(const string& str);
string wideStringToUtf8(const wstring& str);

#endif /* Engine_h */
