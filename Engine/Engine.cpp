//
//  Engine.cpp
//  Vikey (based on OpenKey)
//
//  Created by Tuyen on 1/18/19.
//  Copyright © 2019 Tuyen Mai. All rights reserved.
//
#include <iostream>
#include <algorithm>
#include "Engine.h"
#include <string.h>
#include <cmath>
#include <list>
#include "AutoSwitchData.h"
#include "EnglishFormsData.h"
#include "BigramData.h"
#include <string>
#include <unordered_map>
#include <cstdio>

static vector<Uint8> _charKeyCode = {
    KEY_BACKQUOTE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUALS,
    KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET, KEY_BACK_SLASH,
    KEY_SEMICOLON, KEY_QUOTE, KEY_COMMA, KEY_DOT, KEY_SLASH
};

static vector<Uint8> _breakCode = {
    KEY_ESC, KEY_TAB, KEY_ENTER, KEY_RETURN, KEY_LEFT, KEY_RIGHT, KEY_DOWN, KEY_UP, KEY_COMMA, KEY_DOT,
    KEY_SLASH, KEY_SEMICOLON, KEY_QUOTE, KEY_BACK_SLASH, KEY_MINUS, KEY_EQUALS, KEY_BACKQUOTE, KEY_TAB
    // The engine always uses the macOS key codes (platforms/mac.h); every platform layer maps its own
    // codes to these (Windows: VK_* -> KEY_*, with Insert/Home/End/Delete/PgUp/PgDn sent as KEY_ESC).
};


//Telex keys: tones s f r x j, doubled a o e (circumflex), w (horn/breve), d (đ), z (remove tone)
static const Uint16 TelexKey[11] = {KEY_S, KEY_F, KEY_R, KEY_X, KEY_J, KEY_A, KEY_O, KEY_E, KEY_W, KEY_D, KEY_Z};

#define IS_KEY_Z(key) (TelexKey[10] == key)
#define IS_KEY_D(key) (TelexKey[9] == key)
#define IS_KEY_W(key) (TelexKey[8] == key)
#define IS_KEY_DOUBLE(key) (TelexKey[5] == key || TelexKey[6] == key || TelexKey[7] == key)
#define IS_KEY_S(key) (TelexKey[0] == key)
#define IS_KEY_F(key) (TelexKey[1] == key)
#define IS_KEY_R(key) (TelexKey[2] == key)
#define IS_KEY_X(key) (TelexKey[3] == key)
#define IS_KEY_J(key) (TelexKey[4] == key)

#define IS_MARK_KEY(keyCode) (keyCode == KEY_S || keyCode == KEY_F || keyCode == KEY_R || keyCode == KEY_J || keyCode == KEY_X)
#define IS_BRACKET_KEY(key) (key == KEY_LEFT_BRACKET || key == KEY_RIGHT_BRACKET)

#define VSI vowelStartIndex
#define VEI vowelEndIndex
#define VWSM vowelWillSetMark
#define hBPC HookState.backspaceCount
#define hNCC HookState.newCharCount
#define hCode HookState.code
#define hExt HookState.extCode
#define hData HookState.charData
#define GET getCharacterCode

//Data to sendback to main program
vKeyHookState HookState;

//private data
/**
 * data structure of each element in TypingWord (Uint64)
 * first 2 byte is character code or key code.
 * bit 16: has caps or not
 * bit 17: has tone ^ or not
 * bit 18: has tone w or not
 * bit 19 - > 23: has mark or not (Sắc, huyền, hỏi, ngã, nặng)
 * bit 24: is standalone key? (w, [, ])
 * bit 25: is character code or keyboard code; 1: character code; 0: keycode
 */
static Uint32 TypingWord[MAX_BUFF];
static Byte _index = 0;
static vector<Uint32> _longWordHelper; //save the word when _index >= MAX_BUFF
static list<vector<Uint32>> _typingStates; //Aug 28th, 2019: typing helper, save long state of Typing word, can go back and modify the word
vector<Uint32> _typingStatesData;

/**
 * Use for restore key if invalid word
 */
static Uint32 KeyStates[MAX_BUFF];
//auto switch: keys Telex undid and dropped from the key states ("eee" -> "ee", "deepseek"), and where each
//was, oldest first (each position counts the keys kept at that moment)
#define AS_MAX_DROPS 4
static int _asDropN = 0;
static int _asDropPos[AS_MAX_DROPS];
static Uint32 _asDropKey[AS_MAX_DROPS];
/** The keys as typed: the key states with the dropped keys put back. Returns how many. */
static int asStatesWithDrops(Uint32* full, const int& maxN);
static Byte _stateIndex = 0;
static bool _statesLost = false; //a word of more than MAX_BUFF keys: KeyStates lost its first keys

static bool tempDisableKey = false;
static int capsElem;
static int key;
static int markElem;
static bool isCorect = false;
static bool isChanged = false;
static Byte vowelCount = 0;
static Byte vowelStartIndex = 0;
static Byte vowelEndIndex = 0;
static Byte vowelWillSetMark = 0;
static int i, ii, iii;
static int j;
static int k, kk;
static int l;
static bool isRestoredW;
static Uint16 keyForAEO;
static bool isCheckedGrammar;
static bool _isCaps = false;
static int _spaceCount = 0; //add: July 30th, 2019
static bool _isCharKeyCode;
static vector<Uint32> _specialChar;
static bool _useSpellCheckingBefore;

//function prototype
void findAndCalculateVowel(const bool& forGrammar=false);
void insertMark(const Uint32& markMask, const bool& canModifyFlag=true);

static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
wstring utf8ToWideString(const string& str) {
    return converter.from_bytes(str.c_str());
}

string wideStringToUtf8(const wstring& str) {
    return converter.to_bytes(str.c_str());
}

void* vKeyInit() {
    _index = 0;
    _stateIndex = 0;
    _useSpellCheckingBefore = vCheckSpelling;
    _typingStatesData.clear();
    _typingStates.clear();
    _longWordHelper.clear();
    return &HookState;
}

bool isWordBreak(const vKeyEvent& event, const vKeyEventState& state, const Uint16& data) {
    if (event == vKeyEvent::Mouse)
        return true;
    if (vTelexStyle != 0 && (data == KEY_LEFT_BRACKET || data == KEY_RIGHT_BRACKET))   //Simple Telex: brackets are punctuation (D56)
        return true;
    for (i = 0; i < _breakCode.size(); i++) {
        if (_breakCode[i] == data) {
            return true;
        }
    }
    return false;
}


void setKeyData(const Byte& index, const Uint16& keyCode, const bool& isCaps) {
    if (index < 0 || index >= MAX_BUFF)
        return;
    TypingWord[index] = keyCode | (isCaps ? CAPS_MASK : 0);
}

bool _spellingOK = false;
bool _spellingFlag = false;
bool _spellingVowelOK = false;
Byte _spellingEndIndex = 0;

/** Teencode spelling (D53, off by default): chat forms count as Vietnamese so they aren't put back to their keys:
 *  z / dz for d ("zui zẻ", "zô", "dzô"), the open rimes "ie" ("bíe") and "oy" ("gòy"), and k after i or ư ("thík",
 *  "ừk"). Idea from VietTelex (MIT). "wá" isn't possible in Telex: w is ư. */
int vTeencode = 1;   //on by default (Tony, 2026-10-31)
int vTelexStyle = 0;  //Telex (D56); 1 and 2 are the Simple Telex styles from OpenKey
#include "TeencodeData.h"
static bool _teenRule = false;   //this word is valid only by a teencode rule: kept at its end only if it's a known word
/** Is the word in TypingWord a known teencode word? (letters with ^ / +, tone digit last; see build_teencode.py) */
static bool asTeenKnown() {
    char w[MAX_BUFF * 2 + 2]; int n = 0; char tone = 0;
    for (int x = 0; x < _index && n < (int)sizeof(w) - 3; x++) {
        Uint16 key = CHR(x); char c = 0;
        static const Uint16 L[26] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,
                                     KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
        for (int q = 0; q < 26; q++) if (L[q] == key) c = 'a' + q;
        if (!c) return false;
        w[n++] = c;
        if (TypingWord[x] & TONE_MASK) w[n++] = '^';
        if (TypingWord[x] & TONEW_MASK) w[n++] = '+';
        Uint32 m = TypingWord[x] & MARK_MASK;
        if (m) tone = m == MARK1_MASK ? '1' : m == MARK2_MASK ? '2' : m == MARK3_MASK ? '3' : m == MARK4_MASK ? '4' : '5';
    }
    if (tone) w[n++] = tone;
    w[n] = 0;
    int lo = 0, hi = kTeencodeCount - 1;
    while (lo <= hi) { int mid = (lo + hi) / 2; int c = strcmp(kTeencodeWords[mid], w); if (!c) return true; if (c < 0) lo = mid + 1; else hi = mid - 1; }
    return false;
}
static bool asTeencodeVowels(const int& from, const int& to) {
    if (!vTeencode || to - from != 2) return false;
    Uint16 a = CHR(from), b = CHR(from + 1);
    bool plainA = !(TypingWord[from] & (TONE_MASK | TONEW_MASK)), plainB = !(TypingWord[from + 1] & (TONE_MASK | TONEW_MASK));
    Uint32 mark = TypingWord[from] & MARK_MASK;
    //"oy" only plain or with huyền ("gòy" = rồi): "oxy" -> "õy", "boxy" -> "bõy" would hit English words
    return plainA && plainB && ((a == KEY_I && b == KEY_E) || (a == KEY_O && b == KEY_Y && (!mark || mark == MARK2_MASK)));
}

void checkSpelling(const bool& forceCheckVowel=false) {
    _teenRule = false;
    _spellingOK = false;
    _spellingVowelOK = true;
    _spellingEndIndex = _index;
    
    if (_index > 0 && CHR(_index-1) == KEY_RIGHT_BRACKET) {
        _spellingEndIndex = _index-1;
    }
    
    if (_spellingEndIndex > 0) {
        j = 0;
        //Check first consonant
        if (vTeencode && CHR(0) == KEY_Z) { j = 1; _teenRule = true; }                                                   //teencode: z = d
        else if (vTeencode && _spellingEndIndex > 1 && CHR(0) == KEY_D && CHR(1) == KEY_Z) { j = 2; _teenRule = true; }  //dz = d
        else if (IS_CONSONANT(CHR(0))) {
            for (i = 0; i < _consonantTable.size(); i++) {
                _spellingFlag = false;
                if (_spellingEndIndex < _consonantTable[i].size())
                    _spellingFlag = true;
                for (j = 0; j < _consonantTable[i].size(); j++) {
                    if (_spellingEndIndex > j &&
                        _consonantTable[i][j] != CHR(j)) {
                        _spellingFlag = true;
                        break;
                    }
                }
                if (_spellingFlag)
                    continue;
                
                break;
            }
        }
        
        if (j == _spellingEndIndex){ //for "d" case
            _spellingOK = true;
        }
        
        //check next vowel
        k = j;
        VSI = k;
        //August 23rd, 2019: fix case "que't"
        if (CHR(VSI) == KEY_U && k > 0 && k < _spellingEndIndex-1 && CHR(VSI-1) == KEY_Q) {
            k = k + 1;
            j = k;
            VSI = k;
        } else if (_index >= 2 && CHR(0) == KEY_G && CHR(1) == KEY_I && IS_CONSONANT(CHR(2))) {
            VSI = k = j = 1; //Sep 28th: fix gìn
        }
        for (l = 0; l < 3; l++) {
            if (k < _spellingEndIndex && !IS_CONSONANT(CHR(k))) {
                k++;
                VEI = k;
            }
        }
        if (k > j) { //has vowel,
            _spellingVowelOK = false;
            //check correct combined vowel
            if (k - j > 1 && forceCheckVowel) {
                vector<vector<Uint32>>& vowelSet = _vowelCombine[CHR(j)];
                for (l = 0; l < vowelSet.size(); l++) {
                    _spellingFlag = false;
                    for (ii = 1; ii < vowelSet[l].size(); ii++) {
                        if (j + ii - 1 < _spellingEndIndex && vowelSet[l][ii] != ((CHR(j + ii - 1) | (TypingWord[j + ii - 1] & TONEW_MASK) | (TypingWord[j + ii - 1] & TONE_MASK)))) {
                            _spellingFlag = true;
                            break;
                        }
                    }
                    if (_spellingFlag || (k < _spellingEndIndex && !vowelSet[l][0]) || (j + ii - 1 < _spellingEndIndex && !IS_CONSONANT(CHR(j + ii - 1))))
                        continue;
                    
                    _spellingVowelOK = true;
                    break;
                }
            } else if (!IS_CONSONANT(CHR(j))) {
                _spellingVowelOK = true;
            }
            if (!_spellingVowelOK && asTeencodeVowels(j, k) && k == _spellingEndIndex) { _spellingVowelOK = true; _teenRule = true; }   //bíe, gòy (open)
            
            //continue check last consonant
            for (ii = 0; ii < _endConsonantTable.size(); ii++) {
                _spellingFlag = false;
   
                for (j = 0; j < _endConsonantTable[ii].size(); j++) {
                    if (_spellingEndIndex > k+j &&
                        _endConsonantTable[ii][j] != CHR(k + j)) {
                        _spellingFlag = true;
                        break;
                    }
                }
                if (_spellingFlag)
                    continue;
                
                if (k + j >= _spellingEndIndex) {
                    _spellingOK = true;
                    break;
                }
            }
            
            if (!_spellingOK && vTeencode && k + 1 == _spellingEndIndex && CHR(k) == KEY_K && k > 0 &&
                (CHR(k - 1) == KEY_I || (CHR(k - 1) == KEY_U && (TypingWord[k - 1] & TONEW_MASK))))
                { _spellingOK = true; _teenRule = true; }                                             //thík, ừk
            if (!_spellingOK && vTeencode && asTeencodeVowels(VSI, k) && k == _spellingEndIndex) { _spellingOK = true; _teenRule = true; }
            //limit: end consonant "ch", "t" can not use with "~", "`", "?"
            if (_spellingOK) {
                if (_index >= 3 && CHR(_index-1) == KEY_H && CHR(_index-2) == KEY_C && !((TypingWord[_index-3] & MARK1_MASK) || (TypingWord[_index-3] & MARK5_MASK) || !(TypingWord[_index-3] & MARK_MASK))) {
                    _spellingOK = false;
                } else if (_index >= 2 && CHR(_index-1) == KEY_T && !((TypingWord[_index-2] & MARK1_MASK) || (TypingWord[_index-2] & MARK5_MASK) || !(TypingWord[_index-2] & MARK_MASK))) {
                    _spellingOK = false;
                }
            }
        }
    } else {
        _spellingOK = true;
    }
    //the end of the word: a teencode-only spelling stays only if it's a known teencode word ("zero" isn't "zẻo")
    if (forceCheckVowel && _teenRule && _spellingOK && _spellingVowelOK && !asTeenKnown()) _spellingOK = false;
    tempDisableKey = !(_spellingOK && _spellingVowelOK);
    
    //cout<<"spelling vowel: "<<(_spellingVowelOK ? "OK": "Err")<<endl;
    //cout<<"spelling: "<<(_spellingOK ? "OK": "Err")<<endl<<endl;
}

void checkGrammar(const int& deltaBackSpace) {
    if (_index <= 1 || _index >= MAX_BUFF)
        return;
    
    findAndCalculateVowel(true);
    if (vowelCount == 0)
        return;
    
    isCheckedGrammar = false;
    
    l = VSI;
    
    //if N key for case: "thuơn", "ưoi", "ưom", "ưoc"
    if (_index >= 3) {
        for (i = _index-1; i >= 0; i--) {
            if (CHR(i) == KEY_N || CHR(i) == KEY_C || CHR(i) == KEY_I ||
                CHR(i) == KEY_M || CHR(i) == KEY_P || CHR(i) == KEY_T) {
                if (i - 2 >= 0 && CHR(i - 1) == KEY_O && CHR(i - 2) == KEY_U) {
                    if ((TypingWord[i-1] & TONEW_MASK) ^ (TypingWord[i-2] & TONEW_MASK)) {
                        TypingWord[i - 2] |= TONEW_MASK;
                        TypingWord[i - 1] |= TONEW_MASK;
                        isCheckedGrammar = true;
                        break;
                    }
                }
            }
        }
    }
    
    //check mark
    if (_index >= 2) {
        for (i = l; i <= VEI; i++) {
            if (TypingWord[i] & MARK_MASK) {
                Uint32 mark = TypingWord[i] & MARK_MASK;
                TypingWord[i] &= ~MARK_MASK;
                insertMark(mark, false);
                if (i != vowelWillSetMark)
                    isCheckedGrammar = true;
                break;
            }
        }
    }
    
    //re-arrange data to sendback
    if (isCheckedGrammar) {
        if (hCode ==vDoNothing)
            hCode = vWillProcess;
        hBPC = 0;
        
        for (i = _index - 1; i >= l; i--) {
            hBPC++;
            hData[_index - 1 - i] = GET(TypingWord[i]);
        }
        hNCC = hBPC;
        hBPC += deltaBackSpace;
        hExt = 4;
    }
}

void insertKey(const Uint16& keyCode, const bool& isCaps, const bool& isCheckSpelling=true) {
    if (_index >= MAX_BUFF) {
        _longWordHelper.push_back(TypingWord[0]); //save long word
        //left shift
        for (iii = 0; iii < MAX_BUFF - 1; iii++) {
            TypingWord[iii] = TypingWord[iii + 1];
        }
        setKeyData(_index-1, keyCode, isCaps);
    } else {
        setKeyData(_index++, keyCode, isCaps);
    }
    
    if (vCheckSpelling && isCheckSpelling)
        checkSpelling();
    
    //allow d after consonant
    if (keyCode == KEY_D && _index - 2 >= 0 && IS_CONSONANT(CHR(_index - 2)))
        tempDisableKey = false;
}

/** Makes the typed keys match the word on screen again: the keys a person would type for TypingWord[0.._index)
 *  (letter, then its vowel key, the tone key last). After Backspace took a letter that was made from several keys
 *  ("có" ⌫ took o and s), the key list still had some of them, so the next key worked on a word that wasn't there
 *  ("có" ⌫ o gave "coo", D50). */
static bool asResyncStates() {   //false: the keys couldn't be rebuilt for the whole word
    static const Uint16 toneKeys[5] = {KEY_S, KEY_F, KEY_R, KEY_X, KEY_J};
    Uint32 tone = 0, up = _index > 1 ? CAPS_MASK : 0; int n = 0, x = 0;   //a lone capital was likely Shift: "Oof" stays "Oof"
    for (int y = 0; y < _index; y++) up &= TypingWord[y];   //an all-caps word (Caps Lock): its vowel and tone keys were capitals too ("VIEEJT" gave "VIEeTj")
    for (; x < _index && n < MAX_BUFF - 3; x++) {
        Uint32 c = TypingWord[x];
        if (c & CHAR_CODE_MASK) return false;              //a character the keys can't rebuild: leave the list alone
        Uint32 caps = c & CAPS_MASK;
        if ((c & STANDALONE_MASK) && (c & TONEW_MASK)) { KeyStates[n++] = KEY_W | caps; }
        else {
            KeyStates[n++] = CHR(x) | caps;
            if (c & TONE_MASK) KeyStates[n++] = CHR(x) | up;     //â ê ô đ: the letter twice
            if (c & TONEW_MASK) KeyStates[n++] = KEY_W | up;     //ă ơ ư
        }
        if (c & MARK_MASK) tone = c & MARK_MASK;
    }
    if (tone) {
        int m = tone == MARK1_MASK ? 0 : tone == MARK2_MASK ? 1 : tone == MARK3_MASK ? 2 : tone == MARK4_MASK ? 3 : 4;
        KeyStates[n++] = toneKeys[m] | up;
    }
    _stateIndex = n;
    _asDropN = 0;
    return x == _index;                                    //false: the word was longer than the key list holds
}

void insertState(const Uint16& keyCode, const bool& isCaps) {
    if (_stateIndex >= MAX_BUFF) {
        _statesLost = true;
        //left shift
        for (iii = 0; iii < MAX_BUFF - 1; iii++) {
            KeyStates[iii] = KeyStates[iii + 1];
        }
        KeyStates[_stateIndex-1] = keyCode | (isCaps ? CAPS_MASK : 0);
    } else {
        KeyStates[_stateIndex++] = keyCode | (isCaps ? CAPS_MASK : 0);
    }
}

/** The word can be put back to its keys: KeyStates has all of them, and the apps can delete it (they send
 *  fewer than MAX_BUFF backspaces). A longer word stays as Telex made it (restoring it lost its first letter, or
 *  typed it twice). */
static bool wordFitsBuffer() { return !_statesLost && _index < MAX_BUFF; }

void saveWord() {
    //save word history
    {
        if (_index > 0) {
            if (_longWordHelper.size() > 0) { //save long word first
                _typingStatesData.clear();
                for (i = 0; i < _longWordHelper.size(); i++) {
                    if (i != 0 && i % MAX_BUFF == 0) { //save if overflow
                        _typingStates.push_back(_typingStatesData);
                        _typingStatesData.clear();
                    }
                    _typingStatesData.push_back(_longWordHelper[i]);
                }
                _typingStates.push_back(_typingStatesData);
                _longWordHelper.clear();
            }
            
            //save current word
            _typingStatesData.clear();
            for (i = 0; i < _index; i++) {
                _typingStatesData.push_back(TypingWord[i]);
            }
            _typingStates.push_back(_typingStatesData);
        }
    }
}

void saveWord(const Uint32& keyCode, const int& count) {
    _typingStatesData.clear();
    for (i = 0; i < count; i++) {
        _typingStatesData.push_back(keyCode);
    }
    _typingStates.push_back(_typingStatesData);
}

void saveSpecialChar() {
    _typingStatesData.clear();
    for (i = 0; i < _specialChar.size(); i++) {
        _typingStatesData.push_back(_specialChar[i]);
    }
    _typingStates.push_back(_typingStatesData);
    _specialChar.clear();
}

void restoreLastTypingState() {
    if (_typingStates.size() > 0) {
        _typingStatesData = _typingStates.back();
        _typingStates.pop_back();
        if (_typingStatesData.size() > 0){
            if (_typingStatesData[0] == KEY_SPACE) {
                _spaceCount = (int)_typingStatesData.size();
                _index = 0;
            } else if (std::find(_charKeyCode.begin(), _charKeyCode.end(), (Uint16)_typingStatesData[0]) != _charKeyCode.end()) {
                _index = 0;
                _specialChar = _typingStatesData;
                checkSpelling();
            } else {
                for (i = 0; i < _typingStatesData.size(); i++) {
                    TypingWord[i] = _typingStatesData[i];
                }
                _index = (Byte)_typingStatesData.size();
                //the word is back after Backspace took the punctuation behind it ("việt." ⌫): its keys must come back
                //too, or a restore at the next space put back only the keys typed since ("việt." ⌫ "nam " gave "nam")
                _stateIndex = 0;
                _statesLost = !asResyncStates();   //keys can't be rebuilt for the whole word: never restore it
            }
        }
    }
}

void startNewSession() {
    _asDropN = 0;
    _index = 0;
    hBPC = 0;
    hNCC = 0;
    tempDisableKey = false;
    _stateIndex = 0;
    _statesLost = false;
    _longWordHelper.clear();
}

void checkCorrectVowel(vector<vector<Uint16>>& charset, int& i, int& k, const Uint16& markKey) {
    //ignore "qu" case
    if (_index >= 2 && CHR(_index-1) == KEY_U && CHR(_index-2) == KEY_Q) {
        isCorect = false;
        return;
    }
    k = _index - 1;
    for (j = (int)charset[i].size() - 1; j >= 0; j--) {
        //teencode (D53): the k / y entries marked END_CONSONANT_MASK count ("thík", "ừk", "gòy")
        bool teen = vTeencode && (charset[i][j] & END_CONSONANT_MASK) &&
                    (Uint16)(charset[i][j] & ~END_CONSONANT_MASK) == CHR(k) && (CHR(k) == KEY_K || (CHR(k) == KEY_Y && IS_KEY_F(markKey)));
        if (charset[i][j] != CHR(k) && !teen) {
            isCorect = false;
            return;
        }
        k--;
        if (k < 0)
            break;
    }
    
    //limit mark for end consonant: "C", "T"
    if (isCorect && charset[i].size() > 1 && (IS_KEY_F(markKey) || IS_KEY_X(markKey) || IS_KEY_R(markKey))) {
        if (charset[i][1] == KEY_C || charset[i][1] == KEY_T) {
            isCorect = false;
        } else if (charset[i].size() > 2 && (charset[i][2] == KEY_T)) {
            isCorect = false;
        }
    }
    
    if (isCorect && k >= 0) {
        if (CHR(k) == CHR(k+1)) {
            isCorect = false;
        }
    }
}

Uint32 getCharacterCode(const Uint32& data) {
    capsElem = (data & CAPS_MASK) ? 0 : 1;
    key = data & CHAR_MASK;
    if (data & MARK_MASK) { //has mark
        markElem = -2;
        switch (data & MARK_MASK) {
            case MARK1_MASK:
                markElem = 0;
                break;
            case MARK2_MASK:
                markElem = 2;
                break;
            case MARK3_MASK:
                markElem = 4;
                break;
            case MARK4_MASK:
                markElem = 6;
                break;
            case MARK5_MASK:
                markElem = 8;
                break;
        }
        markElem += capsElem;
        
        switch (key) {
            case KEY_A:
            case KEY_O:
            case KEY_U:
            case KEY_E:
                if ((data & TONE_MASK) == 0 && (data & TONEW_MASK) == 0)
                    markElem += 4;
                break;
        }
        
        if (data & TONE_MASK) {
            key |= TONE_MASK;
        } else if (data & TONEW_MASK) {
            key |= TONEW_MASK;
        }
        if (_unicodeTable.find(key) == _unicodeTable.end())
            return data & ~STANDALONE_MASK; //not found (a standalone w that lost its ư is just u, never a raw flagged code)
        
        return _unicodeTable[key][markElem] | CHAR_CODE_MASK;
    } else { //doesn't has mark
        if (_unicodeTable.find(key) == _unicodeTable.end())
            return data & ~STANDALONE_MASK; //not found (a standalone w that lost its ư is just u, never a raw flagged code)
        
        if (data & TONE_MASK) {
            return _unicodeTable[key][capsElem] | CHAR_CODE_MASK;
        } else if (data & TONEW_MASK) {
            return _unicodeTable[key][capsElem + 2] | CHAR_CODE_MASK;
        } else {
            return data & ~STANDALONE_MASK; //not found (a standalone w that lost its ư is just u, never a raw flagged code)
        }
    }
    
    return 0;
}

void findAndCalculateVowel(const bool& forGrammar) {
    vowelCount = 0;
    VSI = VEI = 0;
    for (iii = _index - 1; iii >= 0; iii--) {
        if (IS_CONSONANT(CHR(iii))) {
            if (vowelCount > 0)
                break;
        } else {  //is vowel
            if (vowelCount == 0)
                VEI = iii;
            if (!forGrammar) {
                if ((iii-1 >= 0 && (CHR(iii) == KEY_I && CHR(iii-1) == KEY_G)) ||
                    (iii-1 >= 0 && (CHR(iii) == KEY_U && CHR(iii-1) == KEY_Q))) {
                    break;
                }
            }
            VSI = iii;
            vowelCount++;
        }
    }
    //August 26th, 2019: don't count "u" at "q u" as a vowel
    if (VSI - 1 >= 0 && CHR(VSI) == KEY_U && CHR(VSI-1) == KEY_Q) {
        VSI++;
        vowelCount--;
    }
}

void removeMark() {
    findAndCalculateVowel(true);
    isChanged = false;
    if (_index > 0) {
        for (i = VSI; i <= VEI; i++) {
            if (TypingWord[i] & MARK_MASK) {
                TypingWord[i] &= ~MARK_MASK;
                isChanged = true;
            }
        }
    }
    if (isChanged) {
        hCode = vWillProcess;
        hBPC = 0;
        
        for (i = _index - 1; i >= VSI; i--) {
            hBPC++;
            hData[_index - 1 - i] = GET(TypingWord[i]);
        }
        hNCC = hBPC;
    } else {
        hCode = vDoNothing;
    }
}

bool canHasEndConsonant() {
    vector<vector<Uint32>>& vo = _vowelCombine[CHR(VSI)];
    for (ii = 0; ii < vo.size(); ii++) {
        kk = VSI;
        for (iii = 1; iii < vo[ii].size(); iii++) {
            if (kk > VEI || ((CHR(kk) | (TypingWord[kk] & TONE_MASK) | (TypingWord[kk] & TONEW_MASK)) != vo[ii][iii])) {
                break;
            }
            kk++;
        }
        if (iii >= vo[ii].size()) {
            return vo[ii][0] == 1;
        }
    }
    return false;
}

void handleModernMark() {
    //default
    VWSM = VEI;
    hBPC = (_index - VEI);
    
    //rule 2
    if (vowelCount == 3 && ((CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_A && CHR(VSI+2) == KEY_I) ||
                            (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_Y && CHR(VSI+2) == KEY_U) ||
                            (CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_E && CHR(VSI+2) == KEY_O) ||
                            (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_Y && CHR(VSI+2) == KEY_A))) {
        VWSM = VSI + 1;
        hBPC = _index - VWSM;
    } else if ((CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_I) ||
               (CHR(VSI) == KEY_A && CHR(VSI+1) == KEY_I) ||
               (CHR(VSI)== KEY_U && CHR(VSI+1) == KEY_I) ) {
        
        VWSM = VSI;
        hBPC = _index - VWSM;
    } else if (CHR(VEI-1) == KEY_A && CHR(VEI) == KEY_Y) {
        VWSM = VEI - 1;
        hBPC = (_index - VEI) + 1;
    } else if (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_O) {
        VWSM = VSI + 1;
        hBPC = _index - VWSM;
    } else if (CHR(VSI+1) == KEY_O || CHR(VSI+1) == KEY_U) {
        VWSM = VEI - 1;
        hBPC = (_index - VEI) + 1;
    } else if (CHR(VSI) == KEY_O || CHR(VSI) == KEY_U) {
        VWSM = VEI;
        hBPC = (_index - VEI);
    }
    
    //rule 3.1
    if ((CHR(VSI) == KEY_I && (TypingWord[VSI+1] & (KEY_E | TONE_MASK))) ||
        (CHR(VSI) == KEY_Y && (TypingWord[VSI+1] & (KEY_E | TONE_MASK))) ||
        (CHR(VSI) == KEY_U && (TypingWord[VSI+1] == (KEY_O | TONE_MASK))) ||
        ((TypingWord[VSI] == (KEY_U | TONEW_MASK)) && (TypingWord[VSI+1] == (KEY_O | TONEW_MASK)))){
        
        if (VSI+2 < _index) {
            if (CHR(VSI+2) == KEY_P || CHR(VSI+2) == KEY_T ||
                CHR(VSI+2) == KEY_M || CHR(VSI+2) == KEY_N ||
                CHR(VSI+2) == KEY_O || CHR(VSI+2) == KEY_U ||
                CHR(VSI+2) == KEY_I || CHR(VSI+2) == KEY_C ||
                (VSI+3 < _index && CHR(VSI+2) == KEY_C && CHR(VSI+2) == KEY_H) ||
                (VSI+3 < _index && CHR(VSI+2) == KEY_N && CHR(VSI+2) == KEY_H) ||
                (VSI+3 < _index && CHR(VSI+2) == KEY_N && CHR(VSI+2) == KEY_G)) {
                
                VWSM = VSI + 1;
                hBPC = _index - VWSM;
            } else {
                VWSM = VSI;
                hBPC = _index - VWSM;
            }
        } else {
            VWSM = VSI;
            hBPC = _index - VWSM;
        }
    }
    //rule 3.2
    else if ((CHR(VSI) == KEY_I && (CHR(VSI) == KEY_A)) ||
             (CHR(VSI) == KEY_Y && (CHR(VSI) == KEY_A)) ||
             (CHR(VSI) == KEY_U && (CHR(VSI) == KEY_A)) ||
             (CHR(VSI) == KEY_U && (TypingWord[VSI+1] == (KEY_U | TONEW_MASK)))){
        
        VWSM = VSI;
        hBPC = _index - VWSM;
    }
    
    //rule 4
    if (vowelCount == 2) {
        if (((CHR(VSI) == KEY_I) && (CHR(VSI+1) == KEY_A)) ||
            ((CHR(VSI) == KEY_I) && (CHR(VSI+1) == KEY_U)) ||
            ((CHR(VSI) == KEY_I) && (CHR(VSI+1) == KEY_O))) {
            
            if (VSI == 0 || (CHR(VSI-1) != KEY_G)) { //dont have G
                VWSM = VSI;
                hBPC = _index - VWSM;
            } else {
                VWSM = VSI + 1;
                hBPC = _index - VWSM;
            }
        } else if ((CHR(VSI) == KEY_U) && (CHR(VSI+1) == KEY_A)) {
            if (VSI == 0 || (CHR(VSI-1) != KEY_Q)) { //dont have Q
                if (VEI + 1 >= _index || !canHasEndConsonant()) {
                    VWSM = VSI;
                    hBPC = _index - VWSM;
                }
            } else {
                VWSM = VSI + 1;
                hBPC = _index - VWSM;
            }
        } else if ((CHR(VSI) == KEY_O) && (CHR(VSI+1) == KEY_O)) { //thoong
            VWSM = VEI;
            hBPC = _index - VWSM;
        }
    }
}

void handleOldMark() {
    //default
    if (vowelCount == 0 && CHR(VEI) == KEY_I)
        VWSM = VEI;
    else
        VWSM = VSI;
    hBPC = (_index - VWSM);
    
    //rule 2
    if (vowelCount == 3 || (VEI + 1 < _index && IS_CONSONANT(CHR(VEI + 1)) && canHasEndConsonant())) {
        VWSM = VSI + 1;
        hBPC = _index - VWSM;
    }
    
    //rule 3
    for (ii = VSI; ii <= VEI; ii++) {
        if ((CHR(ii) == KEY_E && TypingWord[ii] & TONE_MASK) || (CHR(ii) == KEY_O && TypingWord[ii] & TONEW_MASK)) {
            VWSM = ii;
            hBPC = _index - VWSM;
            break;
        }
    }
    
    hNCC = hBPC;
}

void insertMark(const Uint32& markMask, const bool& canModifyFlag) {
    vowelCount = 0;
    
    if (canModifyFlag)
        hCode = vWillProcess;
    hBPC = hNCC = 0;
    
    findAndCalculateVowel();
    VWSM = 0;
    
    //detect mark position
    if (vowelCount == 1) {
        VWSM = VEI;
        hBPC = (_index - VEI);
    } else { //vowel = 2 or 3
        if (vUseModernOrthography == 0)
            handleOldMark();
        else
            handleModernMark();
        if (vTeencode && vowelCount == 2 && CHR(VSI) == KEY_O && CHR(VEI) == KEY_Y && VEI == VSI + 1) {   //teencode "gòy": on the o
            VWSM = VSI;
            hBPC = _index - VWSM;
        }
        if (TypingWord[VEI] & TONE_MASK || TypingWord[VEI] & TONEW_MASK)
            vowelWillSetMark = VEI;
    }
    
    //send data
    kk = _index - 1 - VSI;
    //if duplicate same mark -> restore
    if (TypingWord[VWSM] & markMask) {
        
        TypingWord[VWSM] &= ~MARK_MASK;
        if (canModifyFlag)
            hCode = vRestore;
        for (ii = VSI; ii < _index; ii++) {
            TypingWord[ii] &= ~MARK_MASK;
            hData[kk--] = GET(TypingWord[ii]);
        }
        //_index = 0;
        tempDisableKey = true;
    } else {
        //remove other mark
        TypingWord[VWSM] &= ~MARK_MASK;
        
        //add mark
        TypingWord[VWSM] |= markMask;
        for (ii = VSI; ii < _index; ii++) {
            if (ii != VWSM) { //remove mark for other vowel
                TypingWord[ii] &= ~MARK_MASK;
            }
            hData[kk--] = GET(TypingWord[ii]);
        }
        
        hBPC = _index - VSI;
    }
    hNCC = hBPC;
}

void insertD(const Uint16& data, const bool& isCaps) {
    hCode = vWillProcess;
    hBPC = 0;
    for (ii = _index - 1; ii >= 0; ii--) {
        hBPC++;
        if (CHR(ii) == KEY_D) { //reverse unicode char
            if (TypingWord[ii] & TONE_MASK) {
                //restore and disable temporary
                hCode = vRestore;
                TypingWord[ii] &= ~TONE_MASK;
                hData[_index - 1 - ii] = TypingWord[ii];
                tempDisableKey = true;
                break;
            } else {
                TypingWord[ii] |= TONE_MASK;
                hData[_index - 1 - ii] = GET(TypingWord[ii]);
            }
            break;
        } else { //preresent old char
            hData[_index - 1 - ii] = GET(TypingWord[ii]);
        }
    }
    hNCC = hBPC;
}

void insertAOE(const Uint16& data, const bool& isCaps) {
    findAndCalculateVowel();
    
    //remove W tone
    for (ii = VSI; ii <= VEI; ii++) {
        TypingWord[ii] &= ~TONEW_MASK;
    }
    
    hCode = vWillProcess;
    hBPC = 0;
    
    for (ii = _index - 1; ii >= 0; ii--) {
        hBPC++;
        if (CHR(ii) == data) { //reverse unicode char
            if (TypingWord[ii] & TONE_MASK) {
                //restore and disable temporary
                hCode = vRestore;
                TypingWord[ii] &= ~TONE_MASK;
                hData[_index - 1 - ii] = TypingWord[ii];
                //_index = 0;
                if (data != KEY_O) //case thoòng
                    tempDisableKey = true;
                break;
            } else {
                TypingWord[ii] |= TONE_MASK;
                if (!IS_KEY_D(data))
                    TypingWord[ii] &= ~TONEW_MASK;
                hData[_index - 1 - ii] = GET(TypingWord[ii]);
                
            }
            break;
        } else { //preresent old char
            hData[_index - 1 - ii] = GET(TypingWord[ii]);
        }
    }
    hNCC = hBPC;
}

void insertW(const Uint16& data, const bool& isCaps) {
    isRestoredW = false;
    
    findAndCalculateVowel();
    
    //remove ^ tone
    for (ii = VSI; ii <= VEI; ii++) {
        TypingWord[ii] &= ~TONE_MASK;
    }
    
    if (vowelCount > 1) {
        hBPC = _index - VSI;
        hNCC = hBPC;
        
        if (((TypingWord[VSI] & TONEW_MASK) && (TypingWord[VSI+1] & TONEW_MASK)) ||
            ((TypingWord[VSI] & TONEW_MASK) && CHR(VSI+1) == KEY_I) ||
            ((TypingWord[VSI] & TONEW_MASK) && CHR(VSI+1) == KEY_A)){
            //restore and disable temporary
            hCode = vRestore;
            
            for (ii = VSI; ii < _index; ii++) {
                TypingWord[ii] &= ~TONEW_MASK;
                hData[_index - 1 - ii] = GET(TypingWord[ii]) & ~STANDALONE_MASK;
            }
            isRestoredW = true;
            tempDisableKey = true;
        } else {
            hCode = vWillProcess;
            
            if ((CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_O)) {
                if (VSI - 2 >= 0 && TypingWord[VSI - 2] == KEY_T && TypingWord[VSI - 1] == KEY_H) {
                    TypingWord[VSI+1] |= TONEW_MASK;
                    if (VSI + 2 < _index && CHR(VSI+2) == KEY_N) {
                        TypingWord[VSI] |= TONEW_MASK;
                    }
                } else if (VSI - 1 >= 0 && TypingWord[VSI - 1] == KEY_Q) {
                    TypingWord[VSI+1] |= TONEW_MASK;
                } else {
                    TypingWord[VSI] |= TONEW_MASK;
                    TypingWord[VSI+1] |= TONEW_MASK;
                }
            } else if ((CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_A) ||
                       (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_I) ||
                       (CHR(VSI) == KEY_U && CHR(VSI+1) == KEY_U) ||
                       (CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_I)) {
                TypingWord[VSI] |= TONEW_MASK;
            } else if ((CHR(VSI) == KEY_I && CHR(VSI+1) == KEY_O) ||
                       (CHR(VSI) == KEY_O && CHR(VSI+1) == KEY_A)) {
                TypingWord[VSI+1] |= TONEW_MASK;
            } else {
                //don't do anything
                tempDisableKey = true;
                isChanged = false;
                hCode = vDoNothing;
            }
            
            for (ii = VSI; ii < _index; ii++) {
                hData[_index - 1 - ii] = GET(TypingWord[ii]);
            }
        }
        
        return;
    }
    
    hCode = vWillProcess;
    hBPC = 0;
    
    for (ii = _index - 1; ii >= 0; ii--) {
        if (ii < VSI)
            break;
        hBPC++;
        switch (CHR(ii)) {
            case KEY_A:
            case KEY_U:
            case KEY_O:
                if (TypingWord[ii] & TONEW_MASK) {
                    //restore and disable temporary
                    if (TypingWord[ii] & STANDALONE_MASK) {
                        hCode = vWillProcess;
                        if (CHR(ii) == KEY_U){
                            TypingWord[ii] = KEY_W | ((TypingWord[ii] & CAPS_MASK) ? CAPS_MASK : 0);
                        } else if (CHR(ii) == KEY_O) {
                            hCode = vRestore;
                            TypingWord[ii] = KEY_O | ((TypingWord[ii] & CAPS_MASK) ? CAPS_MASK : 0);
                            isRestoredW = true;
                        }
                        hData[_index - 1 - ii] = TypingWord[ii];
                    } else {
                        hCode = vRestore;
                        TypingWord[ii] &= ~TONEW_MASK;
                        hData[_index - 1 - ii] = TypingWord[ii];
                        isRestoredW = true;
                        //_index++;
                    }
                    
                    tempDisableKey = true;
                } else {
                    TypingWord[ii] |= TONEW_MASK;
                    TypingWord[ii] &= ~TONE_MASK;
                    hData[_index - 1 - ii] = GET(TypingWord[ii]);
                }
                break;
                
            default:
                hData[_index - 1 - ii] = GET(TypingWord[ii]);
                break;
        }
    }
    hNCC = hBPC;
    
    if (isRestoredW) {
        //_index = 0;
    }
}

static bool _appendedStandalone = false;   //this key only added ư/ơ at the end: it isn't on screen yet (D52)
void reverseLastStandaloneChar(const Uint32& keyCode, const bool& isCaps) {
    _appendedStandalone = true;
    hCode = vWillProcess;
    hBPC = 0;
    hNCC = 1;
    hExt = 4;
    TypingWord[_index - 1] = (keyCode | TONEW_MASK | STANDALONE_MASK | (isCaps ? CAPS_MASK : 0));
    hData[0] = GET(TypingWord[_index - 1]);
}

static bool _afterDigit = false;   //the word started right after a digit: a lone w is a unit (100W), not ư (D33)
void checkForStandaloneChar(const Uint16& data, const bool& isCaps, const Uint32& keyWillReverse) {
    if (_index > 0 && CHR(_index - 1) == keyWillReverse && TypingWord[_index - 1] & TONEW_MASK) {   //w, [ or ] can start a word
        hCode = vWillProcess;
        hBPC = 1;
        hNCC = 1;
        TypingWord[_index - 1] = data | (isCaps ? CAPS_MASK : 0);
        hData[0] = GET(TypingWord[_index - 1]);
        return;
    }
    
    //check standalone w -> ư
    
    if (_index > 0 && CHR(_index-1) == KEY_U && keyWillReverse == KEY_O) {
        insertKey(keyWillReverse, isCaps);
        reverseLastStandaloneChar(keyWillReverse, isCaps);
        return;
    }
    
    if (_index == 0 && _afterDigit && data == KEY_W) { insertKey(data, isCaps); return; }
    if (_index == 0) { //zero char
        insertKey(data, isCaps, false);
        reverseLastStandaloneChar(keyWillReverse, isCaps);
        return;
    } else if (_index == 1) { //1 char
        for (i = 0; i < _standaloneWbad.size(); i++) {
            if (CHR(0) == _standaloneWbad[i]) {
                insertKey(data, isCaps);
                return;
            }
        }
        insertKey(data, isCaps, false);
        reverseLastStandaloneChar(keyWillReverse, isCaps);
        return;
    } else if (_index == 2) {
        for (i = 0; i < _doubleWAllowed.size(); i++) {
            if (CHR(0) == _doubleWAllowed[i][0] && CHR(1) == _doubleWAllowed[i][1]) {
                insertKey(data, isCaps, false);
                reverseLastStandaloneChar(keyWillReverse, isCaps);
                return;
            }
        }
        insertKey(data, isCaps);
        return;
    }
    
    insertKey(data, isCaps);
}


void handleMainKey(const Uint16& data, const bool& isCaps) {
    //if is Z key, remove mark
    if (IS_KEY_Z(data)) {
        removeMark();
        if (!isChanged) {
            insertKey(data, isCaps);
        }
        return;
    }
    
    if (data == KEY_LEFT_BRACKET) { //standalone key [
        checkForStandaloneChar(data, isCaps, KEY_O);
        return;
    }
    
    if (data == KEY_RIGHT_BRACKET) { //standalone key }
        checkForStandaloneChar(data, isCaps, KEY_U);
        return;
    }
    
    //if is D key
    if (IS_KEY_D(data)) {
        isCorect = false;
        isChanged = false;
        k = _index;
        for (i = 0; i < _consonantD.size(); i++) {
            if (_index < _consonantD[i].size())
                continue;
            isCorect = true;
            checkCorrectVowel(_consonantD, i, k, data);
            
            //allow d after consonant
            if (!isCorect && _index - 2 >= 0 && CHR(_index-1) == KEY_D && IS_CONSONANT(CHR(_index-2))) {
                isCorect = true;
            }
            if (isCorect) {
                isChanged = true;
                insertD(data, isCaps);
                break;
            }
        }
    
        if (!isChanged) {
            insertKey(data, isCaps);
        }
        return;
    }
    
    //if is mark key
    if (IS_MARK_KEY(data)) {
        for (auto& vowelEntry : _vowelForMark) {
            vector<vector<Uint16>>& charset = vowelEntry.second;
            isCorect = false;
            isChanged = false;
            k = _index;
            for (l = 0; l < charset.size(); l++) {
                if (_index < charset[l].size())
                    continue;
                isCorect = true;
                checkCorrectVowel(charset, l, k, data);

                if (isCorect) {
                    isChanged = true;
                    if (IS_KEY_S(data))
                        insertMark(MARK1_MASK);
                    else if (IS_KEY_F(data))
                        insertMark(MARK2_MASK);
                    else if (IS_KEY_R(data))
                        insertMark(MARK3_MASK);
                    else if (IS_KEY_X(data))
                        insertMark(MARK4_MASK);
                    else if (IS_KEY_J(data))
                        insertMark(MARK5_MASK);
                    break;
                }
            }

            if (isCorect) {
                break;
            }
        }

        if (!isChanged) {
            insertKey(data, isCaps);
        }

        return;
    }
    
    //check Vowel
    keyForAEO = data;
    vector<vector<Uint16>>& charset = _vowel[keyForAEO];
    isCorect = false;
    isChanged = false;
    k = _index;
    for (i = 0; i < charset.size(); i++) {
        if (_index < charset[i].size())
            continue;
        isCorect = true;
        checkCorrectVowel(charset, i, k, data);
        
        if (isCorect) {
            isChanged = true;
            if (IS_KEY_DOUBLE(data)) {
                insertAOE(keyForAEO, isCaps);
            } else if (IS_KEY_W(data)) {
                insertW(keyForAEO, isCaps);
            }
            break;
        }
    }
    
    if (!isChanged) {
        if (data == KEY_W && vTelexStyle != 1) {   //Simple Telex 1: a w that joins no vowel stays w (D56)
            checkForStandaloneChar(data, isCaps, KEY_U);
        } else {
            insertKey(data, isCaps);
        }
    }
}


static int asStatesWithDrops(Uint32* full, const int& maxN);
bool checkRestoreIfWrongSpelling(const int& handleCode) {
    if (!wordFitsBuffer()) return false;
    for (ii = 0; ii < _index; ii++) {
        if (!IS_CONSONANT(CHR(ii)) &&
            (TypingWord[ii] & MARK_MASK || TypingWord[ii] & TONE_MASK || TypingWord[ii] & TONEW_MASK)) {
            hCode = handleCode;
            hBPC = _index;
            hNCC = _stateIndex;
            for (i = 0; i < _stateIndex; i++) {
                TypingWord[i] = KeyStates[i];
                hData[_stateIndex - 1 - i] = TypingWord[i];
            }
            _index = _stateIndex;
            return true;
        }
    }
    return false;
}

void vSetCheckSpelling() {
    _useSpellCheckingBefore = vCheckSpelling;
}


/*==========================================================================================================*/
/* Auto switch language from typing behavior.
 * Everything here runs only at a word break (or on a doubled mark key), never per letter, and
 * uses fixed tables compiled into the binary: no allocation, no file or system calls.
 *
 * Words are compared as a Telex-normalized ASCII key: circumflex -> doubled vowel (aa ee oo),
 * breve/horn -> letter + w (aw ow uw), đ -> dd, tone letter (s f r x j) at the end.
 * That makes the check independent of the code table and of old/new tone placement.
 */
#define AS_MISSING (-1000)
#define AS_KEY_LEN 40
//Scores are 10*log10(occurrences per million). When the keys read as a word in both languages,
//leave the current language only if the word in the current language is rare (below its floor)
//and the other reading is at least AS_PREFER points (AS_PREFER/10 orders of magnitude) more common.
#ifndef AS_PREFER
#define AS_PREFER 10
#endif
#ifndef AS_VN_FLOOR
#define AS_VN_FLOOR 10      //Vietnamese words used at least ~10 per million are never switched away from
#endif
#ifndef AS_EN_FLOOR
#define AS_EN_FLOOR 99      //no floor: a tie converts when the Vietnamese reading is AS_PREFER points more common (D22; was 0)
#endif

int vAutoSwitchRequest = -1;
Uint32 vAutoSwitchUndoData[MAX_BUFF];
int vAutoSwitchUndoCount = 0;

static char _asLetter[256];
static bool _asLetterReady = false;
static bool _asWordStartOk = true;   //the current word began after a space, Enter, Tab or a fresh start (not inside foo.bar or my_var)

/* Look-back: when a word confidently switches the language, the 1-2 words typed just before it
 * (separated by single spaces) are fixed too if they were ambiguous, e.g. "thí make" -> "this make".
 * An earlier word follows the switch unless its current reading is AS_LOOKBACK_MARGIN points more common. */
#ifndef AS_LOOKBACK_WORDS
#define AS_LOOKBACK_WORDS 2
#endif
#ifndef AS_PLAIN_MARGIN
#define AS_PLAIN_MARGIN 15    //"thí makes": the earlier word must read English ~30x more often than Vietnamese (this vs thí)
#endif
#ifndef AS_LOOKBACK_MARGIN
#define AS_LOOKBACK_MARGIN 10
#endif
struct AsWord {
    Uint32 raw[MAX_BUFF];   //keys as typed
    int rawCount;
    Uint32 shown[MAX_BUFF]; //what is on screen (engine character data)
    int shownCount;
    int en, vn;             //scores of the raw keys as English and of the Vietnamese reading
    bool lookbackOk;        //could follow a switch to English
    bool same;              //looks the same in both modes (nothing to fix)
};
static AsWord _asPrev[AS_LOOKBACK_WORDS];
static int _asPrevCount = 0;
static Uint32 _asConverted[MAX_BUFF]; static int _asConvertedN = 0;   //the word auto switch just put in Vietnamese
static void navForget();

void vAutoSwitchForget() {
    navForget();   //and the word kept for a ← → round trip (D51): after a switch or a click → would bring it back elsewhere
    _asPrevCount = 0;
    _asWordStartOk = true;   //a fresh context (language switched, app changed): the next word stands on its own
    _afterDigit = false;     //not after a digit either: the one before the switch may be far back ("1" ⌥Z "abc " ⌥Z "wa" gave "wa", D33)
    _spaceCount = 0; _specialChar.clear(); _typingStates.clear();   //and Backspace can't reopen a word from before it
    if (_asConvertedN > 0) {   //except the word auto switch just turned Vietnamese: it is on screen before the break key,
        //so Backspace over that key reopens it like a word typed in Vietnamese ("treen " ⌫ + s -> "trến")
        _typingStates.push_back(vector<Uint32>(_asConverted, _asConverted + _asConvertedN)); _spaceCount = 1; _asConvertedN = 0;
    }
}

static void asInitLetters() {
    if (_asLetterReady) return;
    memset(_asLetter, 0, sizeof(_asLetter));
    const Uint16 keys[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                             KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    for (int n = 0; n < 26; n++) _asLetter[keys[n] & 0xFF] = (char)('a' + n);
    _asLetterReady = true;
}

static inline char asLetterOf(const Uint32& data) {
    Uint32 code = data & CHAR_MASK;
    return code < 256 ? _asLetter[code] : 0;
}

static int asLookup(const char* words, const unsigned int* offsets, const signed char* scores, const int& count, const char* word) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = strcmp(words + offsets[mid], word);
        if (c == 0) return scores[mid];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return AS_MISSING;
}

static inline int asVietnameseScore(const char* key) {
    int v = asLookup(kAutoSwitchVnWords, kAutoSwitchVnOffsets, kAutoSwitchVnScores, kAutoSwitchVnCount, key);
    if (v == AS_MISSING && vTeencode) {   //teencode on: its words count as Vietnamese with their own frequency (D53)
        int lo = 0, hi = kTeencodeVnCount - 1;
        while (lo <= hi) { int mid = (lo + hi) >> 1; int c = strcmp(kTeencodeVnKeys[mid], key);
            if (!c) return kTeencodeVnScores[mid]; if (c < 0) lo = mid + 1; else hi = mid - 1; }
    }
    return v;
}

static inline int asEnglishLookup(const char* word) {
    return asLookup(kAutoSwitchEnWords, kAutoSwitchEnOffsets, kAutoSwitchEnScores, kAutoSwitchEnCount, word);
}

/** A real English word form the list (subtitles) is missing: "resets", "quests" (D60; SCOWL en_US forms,
 *  Tools/autoswitch/build_forms.py). It counts as English, rarer than every listed word, and never when its
 *  letters are a Vietnamese word: "phis" (Greek letters) is phí. */
#define AS_EN_FORM_SCORE (-8)
static int asEnglishForm(const char* word) {
    if (asVietnameseScore(word) != AS_MISSING) return AS_MISSING;
    int lo = 0, hi = kEnglishFormsCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = strcmp(kEnglishFormsWords + kEnglishFormsOffsets[mid], word);
        if (c == 0) return AS_EN_FORM_SCORE;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return AS_MISSING;
}

static inline int asEnglishScore(const char* word) {
    int s = asEnglishLookup(word);
    return s != AS_MISSING ? s : asEnglishForm(word);
}

/** Normalized key of the word in TypingWord; transformed = it has a tone, ^, w-horn/breve or đ. */
static bool asWordKey(char* out, bool& transformed) {
    transformed = false;
    if (_index == 0 || !wordFitsBuffer()) return false;
    int n = 0;
    char tone = 0;
    for (int x = 0; x < _index; x++) {
        Uint32 t = TypingWord[x];
        char c = asLetterOf(t);
        if (c == 0 || n + 3 >= AS_KEY_LEN) return false;
        out[n++] = c;
        if ((t & TONE_MASK) && (c == 'a' || c == 'e' || c == 'o' || c == 'd')) {
            out[n++] = c;
            transformed = true;
        } else if ((t & TONEW_MASK) && (c == 'a' || c == 'o' || c == 'u')) {
            out[n++] = 'w';
            transformed = true;
        }
        switch (t & MARK_MASK) {
            case MARK1_MASK: tone = 's'; break;
            case MARK2_MASK: tone = 'f'; break;
            case MARK3_MASK: tone = 'r'; break;
            case MARK4_MASK: tone = 'x'; break;
            case MARK5_MASK: tone = 'j'; break;
        }
    }
    if (tone) {
        out[n++] = tone;
        transformed = true;
    }
    out[n] = 0;
    return true;
}

/** Lowercase letters of raw key codes; false if any key isn't a letter. */
static bool asRawWord(const Uint32* keys, const int& count, char* out) {
    if (count <= 0 || count >= AS_KEY_LEN) return false;
    for (int x = 0; x < count; x++) {
        out[x] = asLetterOf(keys[x]);
        if (out[x] == 0) return false;
    }
    out[count] = 0;
    return true;
}

static inline bool asEnabled() {
    return vAutoSwitchByTyping;
}

/** Vietnamese mode: a word was ended by a single space and stays Vietnamese. Keep it for look-back. */
int vAutoSwitchLastSide = -1;

// ---- Word pairs for ties (D30, experiment) ---------------------------------------------------------------
// For a word that is a word both ways (bus/bú, been/bên), how often each reading follows the previous word
// in real text ("cho con bú" vs "the bus"). Loaded at run time for now (tests); off when not loaded.
#ifndef AS_BIGRAM_W
#define AS_BIGRAM_W 2.0f   //tuned on the val split (D29): 1 -> 0.128%, 2 -> 0.124%, 3 -> 0.124% wrong words
#endif
static std::unordered_map<std::string, int>* _asBigrams = nullptr;   //loaded at run time (tests) instead of the built-in table
static float _asBigramWeight = AS_BIGRAM_W;
static int _asBigramMin = 3;
static int asBuiltInPair(const std::string& key) {
    int lo = 0, hi = kBigramCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(kBigramKeys + kBigramOffsets[mid], key.c_str());
        if (c == 0) return (int)lround(exp2(kBigramLogCounts[mid] / 8.0));
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}
void vAutoSwitchSetBigrams(const char* path, const float& weight, const int& minCount) {
    _asBigramWeight = weight; _asBigramMin = minCount;
    if (!path) return;
    FILE* f = fopen(path, "r"); if (!f) return;
    if (!_asBigrams) _asBigrams = new std::unordered_map<std::string, int>();
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* t1 = strchr(line, '\t'); if (!t1) continue; char* t2 = strchr(t1 + 1, '\t'); if (!t2) continue;
        *t2 = 0; (*_asBigrams)[std::string(line)] += atoi(t2 + 1);
    }
    fclose(f);
}
static std::string asUtf8(const Uint16* t, int n) {
    std::string s;
    for (int i = 0; i < n; i++) { unsigned c = t[i];
        if (c < 0x80) s += (char)c; else if (c < 0x800) { s += (char)(0xC0 | c >> 6); s += (char)(0x80 | (c & 63)); }
        else { s += (char)(0xE0 | c >> 12); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); } }
    return s;
}
/** The word as it shows (lowercase): the letters, or their Vietnamese reading. */
static bool asSurface(const char* letters, const bool& vietnamese, std::string& out) {
    if (!vietnamese) { out = letters; return true; }
    Uint16 t[64]; int n = 0;
    if (vAutoSwitchWordScore(1, letters, t, &n) <= AS_MISSING || n <= 0) return false;
    out = asUtf8(t, n); return true;
}
// Triples (D34, experiment): the two words before. Loaded at run time for now; off when not loaded.
static std::unordered_map<std::string, int>* _asTrigrams = nullptr;
static float _asTrigramWeight = 0;
static int _asTrigramMin = 3;
void vAutoSwitchSetTrigrams(const char* path, const float& weight, const int& minCount) {
    _asTrigramWeight = weight; _asTrigramMin = minCount;
    if (!path) return;
    FILE* f = fopen(path, "r"); if (!f) return;
    if (!_asTrigrams) _asTrigrams = new std::unordered_map<std::string, int>();
    char line[384];
    while (fgets(line, sizeof line, f)) {
        char* t1 = strchr(line, '\t'); if (!t1) continue; char* t2 = strchr(t1 + 1, '\t'); if (!t2) continue;
        char* t3 = strchr(t2 + 1, '\t'); if (!t3) continue;
        *t3 = 0; (*_asTrigrams)[std::string(line)] += atoi(t3 + 1);
    }
    fclose(f);
}
static bool asSurface(const char* letters, const bool& vietnamese, std::string& out);
/** Like asBigramVote, with the two words before ("cho con" + bú/bus). 0 when the triple is too rare. */
static int asTrigramVote(const char* p2Letters, const bool& p2Vietnamese, const char* p1Letters, const bool& p1Vietnamese, const char* letters) {
    if (!_asTrigrams || _asTrigramWeight <= 0) return 0;
    std::string a, b, vn;
    if (!asSurface(p2Letters, p2Vietnamese, a) || !asSurface(p1Letters, p1Vietnamese, b) || !asSurface(letters, true, vn)) return 0;
    auto get = [&](const std::string& w) { auto it = _asTrigrams->find(a + "\t" + b + "\t" + w); return it == _asTrigrams->end() ? 0 : it->second; };
    int cv = get(vn), ce = get(letters);
    if (cv + ce < _asTrigramMin) return 0;
    double v = 10 * log10((cv + 0.5) / (ce + 0.5));
    if (v > 30) v = 30; if (v < -30) v = -30;
    return (int)lround(v * _asTrigramWeight);
}

/** > 0: the Vietnamese reading follows prev more often, < 0: the English one (10 = 10x); 0: no data. */
static int asBigramVote(const char* prevLetters, const bool& prevVietnamese, const char* letters) {
    if (_asBigramWeight <= 0) return 0;
    std::string p, vn;
    if (!asSurface(prevLetters, prevVietnamese, p) || !asSurface(letters, true, vn)) return 0;
    auto get = [&](const std::string& w) {
        std::string key = p + "\t" + w;
        if (!_asBigrams) return asBuiltInPair(key);
        auto it = _asBigrams->find(key); return it == _asBigrams->end() ? 0 : it->second; };
    int cv = get(vn), ce = get(letters);
    if (cv + ce < _asBigramMin) return 0;
    double v = 10 * log10((cv + 0.5) / (ce + 0.5));
    if (v > 30) v = 30; if (v < -30) v = -30;
    return (int)lround(v * _asBigramWeight);
}

// ---- Capitalised mid-sentence ties (D37) ------------------------------------------------------------------
// "Max"/"Mã", "Bar"/"Bả", "Docs"/"Dóc": in the middle of a sentence a capital means a name or a brand, and
// real Vietnamese text says which reading people write that way. Loaded at run time for now; off when not loaded.
static std::unordered_map<std::string, int>* _asCapMid = nullptr;
static int _asCapMidMin = 5;
void vAutoSwitchSetCapitalised(const char* path, const int& minCount) {
    _asCapMidMin = minCount;
    if (!path) return;
    FILE* f = fopen(path, "r"); if (!f) return;
    if (!_asCapMid) _asCapMid = new std::unordered_map<std::string, int>();
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char* t = strchr(line, '\t'); if (!t) continue; *t = 0;
        (*_asCapMid)[std::string(line)] += atoi(t + 1);
    }
    fclose(f);
}
#ifndef AS_CAP_MARGIN
#define AS_CAP_MARGIN 10   //a capitalised reading this much more common mid-sentence (10x) decides
#endif
/** Only the first key is a capital ("Max", not "MAX" or "max"). */
static bool asCapitalisedOnly(const Uint32* keys, const int& count) {
    if (count < 2 || !(keys[0] & CAPS_MASK)) return false;
    for (int x = 1; x < count; x++) if (keys[x] & CAPS_MASK) return false;
    return true;
}
/** > 0: the capitalised Vietnamese reading ("Mã") is the one written mid-sentence, < 0: the English one ("Max"). */
static int asCapVote(const char* letters) {
    if (!_asCapMid) return 0;
    std::string vn;
    if (!asSurface(letters, true, vn)) return 0;
    auto get = [&](const std::string& w) { auto it = _asCapMid->find(w); return it == _asCapMid->end() ? 0 : it->second; };
    int cv = get(vn), ce = get(letters);
    if (cv + ce < _asCapMidMin) return 0;
    double v = 10 * log10((cv + 0.5) / (ce + 0.5));
    if (v > 30) v = 30; if (v < -30) v = -30;
    return (int)lround(v);
}

#ifndef AS_NEXT_PAIR
#define AS_NEXT_PAIR 20   //how sure (tuned on val: 10 -> 0.122%, 20 -> 0.120%) (20 = 100x) the following word must be of the Vietnamese reading to flip a word kept English (D35)
#endif
/** Look-forward (D35): the word after a word kept English. > 0 when the Vietnamese reading of the earlier word
 *  ("í", "cả") is followed by this word more often than the English one ("is", "car"); 0 without data. */
static int asNextPairVote(const char* prevLetters, const char* nextLetters) {
    if (_asBigramWeight <= 0) return 0;
    std::string pv, nx;
    if (!asSurface(prevLetters, true, pv) || !asSurface(nextLetters, true, nx)) return 0;
    auto get = [&](const std::string& a) {
        std::string key = a + "\t" + nx;
        if (!_asBigrams) return asBuiltInPair(key);
        auto it = _asBigrams->find(key); return it == _asBigrams->end() ? 0 : it->second; };
    int cv = get(pv), ce = get(prevLetters);
    if (cv + ce < _asBigramMin) return 0;
    double v = 10 * log10((cv + 0.5) / (ce + 0.5));
    if (v > 30) v = 30; if (v < -30) v = -30;
    return (int)lround(v * _asBigramWeight);
}

static void asRememberWord() {
#ifdef AS_NO_LOOKBACK
    _asPrevCount = 0; return;
#endif
    if (!asEnabled()) return;
    asInitLetters();
    AsWord w;
    w.lookbackOk = false;
    w.same = false;
    w.rawCount = _stateIndex;
    w.shownCount = _index;
    if (_stateIndex <= 0 || _index <= 0 || _stateIndex >= MAX_BUFF || _longWordHelper.size() > 0) { _asPrevCount = 0; return; }
    for (int x = 0; x < _stateIndex; x++) w.raw[x] = KeyStates[x];
    for (int x = 0; x < _index; x++) w.shown[x] = GET(TypingWord[x]);
    vAutoSwitchLastSide = _asWordStartOk ? vAutoSwitchWordSide(KeyStates, _stateIndex) : -1;
    char word[AS_KEY_LEN], raw[AS_KEY_LEN];
    bool transformed;
    w.en = w.vn = AS_MISSING;
    if (_asWordStartOk && _stateIndex >= _index && asWordKey(word, transformed) && asRawWord(KeyStates, _stateIndex, raw)) {
        w.en = asEnglishScore(raw);
        if (transformed) {        //ambiguous: a real word both ways, and English not much rarer
            w.vn = asVietnameseScore(word);
            w.lookbackOk = w.en != AS_MISSING && w.vn != AS_MISSING && w.en >= w.vn - AS_LOOKBACK_MARGIN;
        } else {                  //looks the same in both modes: look-back passes through English-only words ("makes")
            w.lookbackOk = w.en != AS_MISSING && asVietnameseScore(raw) == AS_MISSING;
            w.same = true;
        }
    }
    for (int x = AS_LOOKBACK_WORDS - 1; x > 0; x--) _asPrev[x] = _asPrev[x - 1];
    _asPrev[0] = w;
    if (_asPrevCount < AS_LOOKBACK_WORDS) _asPrevCount++;
}

/** Vietnamese mode, at a word break: are the raw keys an English word the user clearly meant? */
static Uint32 _asDoubleKeys[MAX_BUFF];   //the keys of the word a doubled tone key switched to English, both presses last
static int _asDoubleCount = 0;
static int _asDoubleFirst = -1;      //where the first press is in _asDoubleKeys
static bool _asDoubleBoth = false;   //both presses are on screen
static bool asFewCapitals(const Uint32* keys, const int& count);
static bool _asApostrophe = false;   //the word ends at an apostrophe: allow contraction stems

static int asStatesWithDrops(Uint32* full, const int& maxN) {
    //newest drop first: an older drop's position never counts a newer dropped key
    int n = _stateIndex;
    if (n > maxN) n = maxN;
    memcpy(full, KeyStates, n * sizeof(Uint32));
    for (int d = _asDropN - 1; d >= 0; d--) {
        int p = _asDropPos[d];
        if (p < 0 || p > n || n + 1 > maxN) continue;
        memmove(full + p + 1, full + p, (n - p) * sizeof(Uint32));
        full[p] = _asDropKey[d]; n++;
    }
    return n;
}

// A case boundary inside the typed word is evidence for an identifier.
static bool asHasInternalCapital(const Uint32* keys, int count) {
    for (int i = 1; i < count; ++i)
        if ((keys[i] & CAPS_MASK) && !(keys[i-1] & CAPS_MASK)) return true;
    return false;
}

// A single Vietnamese syllable has one vowel nucleus. Ignore keys that
// can be Telex modifiers (including z to remove a tone), so editing
// a tone does not create a false boundary between vowel groups. In the
// English doubled-key path, z remains literal evidence rather than an edit.
static bool asHasSeparatedVowels(const Uint32* keys, int count, bool telexToneEdits) {
    bool sawVowel = false, afterConsonant = false;
    for (int i = 0; i < count; ++i) {
        const char c = asLetterOf(keys[i]);
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y') {
            if (afterConsonant) return true;
            sawVowel = true;
        } else if (sawVowel && c && c != 's' && c != 'f' && c != 'r' &&
                   c != 'x' && c != 'j' && c != 'w' && (c != 'z' || !telexToneEdits)) {
            afterConsonant = true;
        }
    }
    return false;
}

// Classify an unlisted literal only after excluding explicit Telex modifiers.
static bool asHasLiteralWordShape(const Uint32* keys, int count, bool telexToneEdits) {
    // Explicit Telex modifiers remain evidence for an unlisted Vietnamese
    // syllable or a Vietnamese typo, even with a late modifier ("mawca").
    for (int i = 1; i < count; ++i) {
        const char c = asLetterOf(keys[i]), previous = asLetterOf(keys[i-1]);
        if (c == 'w' || (c == previous && (c == 'a' || c == 'e' || c == 'o' || c == 'd')))
            return false;
    }
    return asHasInternalCapital(keys, count) || asHasSeparatedVowels(keys, count, telexToneEdits);
}

static bool asShouldRestoreToEnglishCore(bool& literalOnly) {
    if (!asEnabled() || !_asWordStartOk) return false;
    asInitLetters();
    if (_stateIndex < 2 || _stateIndex < _index) return false;
    char word[AS_KEY_LEN], raw[AS_KEY_LEN];
    bool transformed;
    bool wordOk = asWordKey(word, transformed);
    //Telex dropped a key on the way ("seemed" -> "semed", "cheese" -> "chees"): with it back, an English word?
    if (_asDropN > 0 && _stateIndex >= 3 && _stateIndex + _asDropN < MAX_BUFF - 1) {   //4+ keys: "aaa" stays Telex "aa"
        Uint32 full[MAX_BUFF];
        int n = asStatesWithDrops(full, MAX_BUFF);
            bool savedTemp = tempDisableKey;
        checkSpelling(true);
        bool invalid = tempDisableKey;   //the shown word is no syllable ("chée")
        tempDisableKey = savedTemp;
        if (asRawWord(full, n, raw) && asEnglishScore(raw) != AS_MISSING && (!wordOk || !transformed || invalid || asVietnameseScore(word) == AS_MISSING)) {
            memcpy(KeyStates, full, n * sizeof(Uint32));   //restore puts these keys back
            _stateIndex = n;
            _asDropN = 0;
            return true;
        }
    }
    if (!wordOk) return false;
    if (!transformed) {
        //no marks left, but Telex ate a key on the way ("seemed" -> "semed", "cheese" -> "chees"): put back an English word
        if (asRawWord(KeyStates, _stateIndex, raw) && strcmp(raw, word) != 0 && asEnglishScore(raw) != AS_MISSING)
            return true;
#ifndef AS_PLAIN_AFTER_AMBIGUOUS
#define AS_PLAIN_AFTER_AMBIGUOUS 1
#endif
#if AS_PLAIN_AFTER_AMBIGUOUS
        //An English-only word that looks the same in both modes ("makes") right after an ambiguous word that
        //stayed Vietnamese ("thí" = this): together they are English. Switch now and fix the earlier word.
        if (_asPrevCount < 1 || !_asPrev[0].lookbackOk || _asPrev[0].same) return false;
        if (_asPrev[0].en < _asPrev[0].vn + AS_PLAIN_MARGIN) return false;   //the earlier word must lean English
        if (!asRawWord(KeyStates, _stateIndex, raw)) return false;
        return asEnglishScore(raw) != AS_MISSING && asVietnameseScore(raw) == AS_MISSING;
#else
        return false;
#endif
    }
    if (!asRawWord(KeyStates, _stateIndex, raw)) return false;
    int en = asEnglishScore(raw);
    int vn = asVietnameseScore(word);
    //a capitalised word mid-sentence (D37): which capitalised reading real text uses
    if (_asCapMid && vn != AS_MISSING && _asPrevCount >= 1 && asCapitalisedOnly(KeyStates, _stateIndex)) {   //names need not be English words (Los, Las)
        int cv = asCapVote(raw);
        if (cv <= -AS_CAP_MARGIN) return true;
        if (cv >= AS_CAP_MARGIN) return false;
    }

#ifndef AS_ACRONYM
#define AS_ACRONYM 1
#endif
#ifndef AS_ACRONYM_VN
#define AS_ACRONYM_VN 5     //below ~3 per million: tuned on Tinhte (acronyms) vs all-caps Vietnamese news
#endif
#ifndef AS_ACRONYM_VN_AMID
#define AS_ACRONYM_VN_AMID 15   //right after a lowercase word ("con NAS này"): Caps Lock Vietnamese comes in runs (D27)
#endif
#if AS_ACRONYM
    //2+ capitals (NAS, HDD, SATA, GoPro) that Telex turned into a rare or invalid syllable: an acronym or a name (D23)
    //not inside a run of capitals: no earlier word, or a lowercase one
    bool afterLower = _asPrevCount < 1 || _asPrev[0].rawCount < 1;
    if (!afterLower) { afterLower = true; for (int x = 0; x < _asPrev[0].rawCount; x++) if (_asPrev[0].raw[x] & CAPS_MASK) afterLower = false; }
    if (!asFewCapitals(KeyStates, _stateIndex) && (vn == AS_MISSING || vn < (afterLower ? AS_ACRONYM_VN_AMID : AS_ACRONYM_VN))) {
        bool vowelChanged = false;   //Vietnamese acronyms only have đ changed (VĐV, HĐND, BĐS): they stay
        for (int x = 0; x < _index; x++) {
            char c = asLetterOf(TypingWord[x]);
            if ((c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y') && (TypingWord[x] & (MARK_MASK | TONE_MASK | TONEW_MASK)))
                vowelChanged = true;
        }
        if (vowelChanged) return true;
    }
#endif
    if (en == AS_MISSING && _asApostrophe) {             //a contraction stem: doesn't, isn't, wasn't -> is it "does" + n?
        size_t n = strlen(raw);
        if (n >= 3 && raw[n - 1] == 'n') { raw[n - 1] = 0; en = asEnglishScore(raw); raw[n - 1] = 'n'; }
    }
#ifndef AS_RESTORE_UNKNOWN
#define AS_RESTORE_UNKNOWN 3   //min letters for restoring a word that is neither Vietnamese nor in the English list
#endif
#if AS_RESTORE_UNKNOWN > 0
    //not a Vietnamese syllable and not a known English word either: a foreign word (TestFlight, analytics)
    if (en == AS_MISSING) {
        if (vn != AS_MISSING || _stateIndex < AS_RESTORE_UNKNOWN) return false;
#ifndef AS_UNKNOWN_ALLOW_MODIFIERS
        //Vietnamese-only modifiers (â ê ô ă ơ ư đ) mean the user was writing Vietnamese: a typo, not a foreign
        //word. Unless the keys have several vowel groups: no single syllable has that (analytics -> "ânlytics").
        int vowelGroups = 0;
        bool inVowel = false;
        for (int x = 0; x < _stateIndex; x++) {
            char c = asLetterOf(KeyStates[x]);
            bool v = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
            if (v && !inVowel) vowelGroups++;
            inVowel = v;
        }
        if (vowelGroups <= 1)
            for (int x = 0; x < _index; x++) if (TypingWord[x] & (TONE_MASK | TONEW_MASK)) return false;
#endif
        // Restore an unknown literal only with positive evidence: the
        // rendered spelling fails, or the original looks like a multi-syllable
        // word or identifier. Recognized Vietnamese is handled above.
        // Rare but valid syllables (riệt, chịt) may be absent from the list.
        bool savedTemp = tempDisableKey;
        checkSpelling(true);
        bool invalid = tempDisableKey;
        tempDisableKey = savedTemp;
        if (invalid) return true;
        literalOnly = asHasLiteralWordShape(KeyStates, _stateIndex, true);
        return literalOnly;
    }
#else
    if (en == AS_MISSING) return false;
#endif
#ifndef AS_VN_EXTREME
#define AS_VN_EXTREME 99    //an English word this many points more common wins even over a common Vietnamese word
#endif
#ifndef AS_TIE_PREV
#define AS_TIE_PREV 5       //a tie leaning English (3x) goes English right after a clearly English word (D22)
#endif
    //word pairs (D30): the reading that follows the previous word more often in real text
    if ((_asBigramWeight > 0 || _asTrigrams) && en != AS_MISSING && vn != AS_MISSING && _asPrevCount >= 1) {
        char praw[AS_KEY_LEN], p2raw[AS_KEY_LEN];
        if (asRawWord(_asPrev[0].raw, _asPrev[0].rawCount, praw)) {
            int vote = 0;
            if (_asPrevCount >= 2 && asRawWord(_asPrev[1].raw, _asPrev[1].rawCount, p2raw))
                vote = asTrigramVote(p2raw, !_asPrev[1].same, praw, !_asPrev[0].same, raw);
            if (vote == 0) vote = asBigramVote(praw, !_asPrev[0].same, raw);
            if (vote != 0) return en - vn - vote >= AS_PREFER && (vn < AS_VN_FLOOR || vote <= -AS_PREFER);
        }
    }
    if (en != AS_MISSING && vn != AS_MISSING && en - vn >= AS_TIE_PREV && _asPrevCount >= 1 &&
        _asPrev[0].same && _asPrev[0].en != AS_MISSING && _asPrev[0].vn == AS_MISSING)
        return true;   //"video call thí" -> "this"
    return vn == AS_MISSING || (vn < AS_VN_FLOOR && en - vn >= AS_PREFER) || en - vn >= AS_VN_EXTREME;
}

/** Whether to put the word back to the keys typed. A foreign word restored as typed gets back the keys
 *  Telex undid on the way too ("deepseek", not "deepsek"); a known English word without them stays. */
static bool asShouldRestoreToEnglish(bool& literalOnly) {
    literalOnly = false;
    if (!asShouldRestoreToEnglishCore(literalOnly)) return false;
    if (_asDropN > 0 && _stateIndex >= 3 && _stateIndex + _asDropN < MAX_BUFF - 1) {
        char raw[AS_KEY_LEN];
        Uint32 full[MAX_BUFF];
        int n = asStatesWithDrops(full, MAX_BUFF - 1);
        //only for words of 2+ vowel groups (deep-seek): in one syllable "ooo" is the Telex undo ("cuooocf" -> "cuoocf")
        int groups = 0; bool inV = false;
        for (int x = 0; x < n; x++) { char c = asLetterOf(full[x]); bool v = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; if (v && !inV) groups++; inV = v; }
        if (groups >= 2 && !(asRawWord(KeyStates, _stateIndex, raw) && asEnglishScore(raw) != AS_MISSING)) {
            memcpy(KeyStates, full, n * sizeof(Uint32));
            _stateIndex = n;
            _asDropN = 0;
        }
    }
    return true;
}

/** Before a hyphen: a rare w-word ("ưi") starts an English compound ("Wi-Fi", D23). */
static bool asHyphenRestore() {
    if (!asEnabled() || !_asWordStartOk || _stateIndex < 2 || _stateIndex < _index) return false;
    asInitLetters();
    char word[AS_KEY_LEN];
    bool transformed;
    bool ok = asWordKey(word, transformed);
    if (!ok || !transformed) return false;
    //only words starting with w (Telex "ư"): "nơ-ron", "rơ-moóc", "táp-lô" are Vietnamese loanwords
    if (asLetterOf(KeyStates[0]) != 'w') return false;
    int vn = asVietnameseScore(word);
    return vn == AS_MISSING || vn < AS_VN_FLOOR;
}

/** Puts the raw keys back (like "restore if wrong spelling") and asks for English.
 *  Ambiguous words just before it (look-back) are put back to their keys too. */
bool vAutoSwitchPrevRestored = false;        //previous[0] of the next English-word check is a word auto switch put back to English
Uint32 vAutoSwitchRestoredKeys[MAX_BUFF];   //the keys of the word just put back to English (D35: a later word can look back on it)
int vAutoSwitchRestoredCount = 0;
static void asRestoreToEnglish(const int& handleCode, bool literalOnly) {
    vAutoSwitchRestoredCount = 0;
    if (_stateIndex > 0 && _stateIndex < MAX_BUFF) {
        memcpy(vAutoSwitchRestoredKeys, KeyStates, _stateIndex * sizeof(Uint32));
        vAutoSwitchRestoredCount = _stateIndex;
    }
    //how many earlier words follow the switch: consecutive, ambiguous, and fitting in the output buffer.
    //Not after an acronym (2+ capitals): Vietnamese text is full of them, so they say nothing about the
    //words before ("bên ASL", "của USA" stay Vietnamese: Tony, D42)
    int back = 0, newCount = _stateIndex, deleteCount = _index;
    bool acronym = !asFewCapitals(KeyStates, _stateIndex);
    while (!literalOnly && !acronym && back < _asPrevCount && _asPrev[back].lookbackOk &&
           newCount + _asPrev[back].rawCount + 1 < MAX_BUFF && deleteCount + _asPrev[back].shownCount + 1 < MAX_BUFF) {
        newCount += _asPrev[back].rawCount + 1;
        deleteCount += _asPrev[back].shownCount + 1;
        back++;
    }
    while (back > 0 && _asPrev[back - 1].same) {         //no need to retype unchanged words at the far end
        back--;
        newCount -= _asPrev[back].rawCount + 1;
        deleteCount -= _asPrev[back].shownCount + 1;
    }
    Uint32 out[MAX_BUFF];
    int n = 0;
    vAutoSwitchUndoCount = 0;
    for (int b = back - 1; b >= 0; b--) {
        for (int x = 0; x < _asPrev[b].rawCount; x++) out[n++] = _asPrev[b].raw[x];
        out[n++] = KEY_SPACE;
        for (int x = 0; x < _asPrev[b].shownCount; x++) vAutoSwitchUndoData[vAutoSwitchUndoCount++] = _asPrev[b].shown[x];
        vAutoSwitchUndoData[vAutoSwitchUndoCount++] = KEY_SPACE;
    }
    for (int x = 0; x < _index && vAutoSwitchUndoCount < MAX_BUFF; x++) {
        vAutoSwitchUndoData[vAutoSwitchUndoCount++] = GET(TypingWord[x]);
    }
    for (i = 0; i < _stateIndex; i++) {
        TypingWord[i] = KeyStates[i];
        out[n++] = KeyStates[i];
    }
    hCode = handleCode;
    hBPC = deleteCount;
    hNCC = n;
    for (int x = 0; x < n; x++) hData[n - 1 - x] = out[x];
    _index = _stateIndex;
    _asPrevCount = 0;
    vAutoSwitchRequest = 0;
}

/** Break keys that end a word for auto switch (not arrows, tab, esc, mouse or shortcuts). */
static bool asIsSwitchBreak(const vKeyEvent& event, const Uint16& data, const Uint8& capsStatus, const bool& otherControlKey) {
    if (event != vKeyEvent::Keyboard || otherControlKey) return false;
    return data == KEY_COMMA || data == KEY_DOT || data == KEY_SEMICOLON || data == KEY_ENTER || data == KEY_RETURN ||
           data == KEY_QUOTE ||                                 //closing quote, or an apostrophe: doesn't, we're
           (data == KEY_SLASH && capsStatus == 1) || (data == KEY_1 && capsStatus == 1) || (data == KEY_0 && capsStatus == 1) ||
           (data == KEY_9 && capsStatus == 1) ||                //"(" right after a word: web(làm (D27)
           (vTelexStyle != 0 && (data == KEY_LEFT_BRACKET || data == KEY_RIGHT_BRACKET));   //Simple Telex brackets, like ( ) (D56)
}

/** Everything the engine keeps between keys, so a word can be replayed on a scratch session. */
struct AsEngineState {
    Uint32 word[MAX_BUFF], states[MAX_BUFF];
    Byte index, stateIndex;
    bool tempDisable, useSpellingBefore, wordStartOk, statesLost;
    int dropN; int dropPos[AS_MAX_DROPS]; Uint32 dropKey[AS_MAX_DROPS];
    int spaceCount, checkSpelling, language;
    vector<Uint32> longWord, special, statesData;
    list<vector<Uint32>> typing;
    vKeyHookState hook;
};

static void asSaveState(AsEngineState& st) {
    memcpy(st.word, TypingWord, sizeof(TypingWord));
    memcpy(st.states, KeyStates, sizeof(KeyStates));
    st.index = _index; st.stateIndex = _stateIndex; st.statesLost = _statesLost;
    st.tempDisable = tempDisableKey; st.useSpellingBefore = _useSpellCheckingBefore;
    st.wordStartOk = _asWordStartOk;
    st.dropN = _asDropN; memcpy(st.dropPos, _asDropPos, sizeof(_asDropPos)); memcpy(st.dropKey, _asDropKey, sizeof(_asDropKey));
    st.spaceCount = _spaceCount; st.checkSpelling = vCheckSpelling;
    st.language = vLanguage;
    st.longWord.swap(_longWordHelper);
    st.special.swap(_specialChar);
    st.statesData.swap(_typingStatesData);
    st.typing.swap(_typingStates);
    st.hook = std::move(HookState);
}

static void asRestoreState(AsEngineState& st) {
    memcpy(TypingWord, st.word, sizeof(TypingWord));
    memcpy(KeyStates, st.states, sizeof(KeyStates));
    _index = st.index; _stateIndex = st.stateIndex; _statesLost = st.statesLost;
    tempDisableKey = st.tempDisable; _useSpellCheckingBefore = st.useSpellingBefore;
    _asWordStartOk = st.wordStartOk;
    _asDropN = st.dropN; memcpy(_asDropPos, st.dropPos, sizeof(_asDropPos)); memcpy(_asDropKey, st.dropKey, sizeof(_asDropKey));
    _spaceCount = st.spaceCount; vCheckSpelling = st.checkSpelling;
    vLanguage = st.language;
    _longWordHelper.swap(st.longWord);
    _specialChar.swap(st.special);
    _typingStatesData.swap(st.statesData);
    _typingStates.swap(st.typing);
    HookState = std::move(st.hook);
}

#ifdef AS_TESTING
//test harness only: snapshot the whole engine so a word can be tried and taken back
void* vTestSaveState() { AsEngineState* st = new AsEngineState(); asSaveState(*st); return st; }
void vTestRestoreState(void* p) { AsEngineState* st = (AsEngineState*)p; asRestoreState(*st); delete st; }
void vTestFreeState(void* p) { delete (AsEngineState*)p; }
#endif

/** Replays raw keys through the Telex engine on a scratch session. Returns the Vietnamese score of
 *  the result (AS_MISSING if it isn't a real syllable or nothing changed) and the result characters. */
static int asReplay(const Uint32* keys, const int& count, Uint32* result, int& resultCount, bool* changed = NULL, Uint32* states = NULL) {
    resultCount = 0;
    AsEngineState saved;
    asSaveState(saved);
    _spaceCount = 0;
    startNewSession();
    for (int x = 0; x < count; x++) {
        vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, (Uint16)(keys[x] & CHAR_MASK), (keys[x] & CAPS_MASK) ? 1 : 0, false);
    }
    int vn = AS_MISSING;
    char word[AS_KEY_LEN];
    bool transformed;
    bool keyOk = asWordKey(word, transformed);
    if (changed) *changed = keyOk ? transformed : true;
    if (keyOk && transformed) {
        vn = asVietnameseScore(word);
        if (vn != AS_MISSING) {
            for (int x = 0; x < _index; x++) { if (states) states[x] = TypingWord[x]; result[resultCount++] = GET(TypingWord[x]); }
        }
    }
    asRestoreState(saved);
    return vn;
}

static bool asFewCapitals(const Uint32* keys, const int& count) {
    int capitals = 0;
    for (int x = 0; x < count; x++) {
        if (!(keys[x] & CAPS_MASK)) continue;
        //DD is one letter, Đ: "DDaf Nawngx" is Đà Nẵng, while DDR, DDoS still have two capitals (D39)
        if (x > 0 && (keys[x] & CHAR_MASK) == KEY_D && (keys[x - 1] & CHAR_MASK) == KEY_D && (keys[x - 1] & CAPS_MASK)) continue;
        capitals++;
    }
    return capitals < 2;   //USA, EST, IRS, camelCase: acronyms and identifiers stay as typed
}

bool vAutoSwitchCheckEnglishWord(const Uint32* keys, const int& count,
                                 const Uint32* const* previous, const int* previousCounts, const int& previousN) {
    if (!asEnabled() || count < 2 || count >= MAX_BUFF - 1) return false;
    asInitLetters();
    char raw[AS_KEY_LEN];
    if (!asRawWord(keys, count, raw)) return false;
    //2+ capitals: acronyms and identifiers stay as typed. All capitals (Caps Lock Vietnamese, "COONG") may still
    //convert, but only to a common syllable and never when the keys are English (USA, EST stay): D23
    bool allCaps = true;
    for (int x = 0; x < count; x++) if (!(keys[x] & CAPS_MASK)) allCaps = false;
    if (!asFewCapitals(keys, count) && !allCaps) return false;
    int en = asEnglishScore(raw);

    Uint32 result[MAX_BUFF];
    int resultCount;
    Uint32 states[MAX_BUFF];   //the engine's own form of the result, for Backspace after the switch
    int vn = asReplay(keys, count, result, resultCount, NULL, states);
    //word pairs (D30): "cho con bus" -> bú, "the bus" stays
    int pairVote = 0;
    if ((_asBigramWeight > 0 || _asTrigrams) && en != AS_MISSING && vn != AS_MISSING && previousN >= 1 && previousCounts[0] >= 1) {
        char praw[AS_KEY_LEN];
        char p2raw[AS_KEY_LEN];
        if (asRawWord(previous[0], previousCounts[0], praw)) {
            if (previousN >= 2 && previousCounts[1] >= 1 && asRawWord(previous[1], previousCounts[1], p2raw))
                pairVote = asTrigramVote(p2raw, false, praw, false, raw);
            if (pairVote == 0) pairVote = asBigramVote(praw, false, raw);
        }
    }
    int capVote = 0;   //D37: a capitalised word mid-sentence goes with the capitalised reading real text uses
    if (_asCapMid && vn != AS_MISSING && previousN >= 1 && asCapitalisedOnly(keys, count)) {
        capVote = asCapVote(raw);
        if (capVote <= -AS_CAP_MARGIN) return false;
    }
    if (vn == AS_MISSING || !(en == AS_MISSING || capVote >= AS_CAP_MARGIN || (en < AS_EN_FLOOR && vn - en + pairVote >= AS_PREFER))) return false;
#ifndef AS_CAPS_VN_FLOOR
#define AS_CAPS_VN_FLOOR 15   //all capitals convert only to a common syllable (IRS stays, not "Í"): D23, D27
#endif
    if (count > 1 && allCaps && (en != AS_MISSING || vn < AS_CAPS_VN_FLOOR)) return false;
    //a lone all-caps word converts when the Telex marks it as Vietnamese: THOONG, THUOWNGF, VIEETJ (Tony, D41).
    //Acronyms rarely double a vowel or put w after a vowel, and the floor above already keeps rare syllables out
    bool telexMarked = false;
    for (int x = 0; raw[x] && raw[x + 1]; x++) {
        char a = raw[x] | 0x20, b = raw[x + 1] | 0x20;
        if ((a == b && (a == 'a' || a == 'e' || a == 'o' || a == 'd')) || (b == 'w' && (a == 'a' || a == 'o' || a == 'u'))) telexMarked = true;
    }
    if (count > 1 && allCaps && !(count >= 4 && telexMarked)) {   //only inside a run of capitals (Caps Lock on): a lone HW, CWS stays
        bool prevCaps = previousN >= 1 && previousCounts[0] >= 2;
        for (int x = 0; prevCaps && x < previousCounts[0]; x++) if (!(previous[0][x] & CAPS_MASK)) prevCaps = false;
        if (!prevCaps) return false;
    }
    //a tie right after a clearly English word stays English ("the theme", "of rust"): D22
#ifndef AS_PAIR_OVERRIDES_D22
#define AS_PAIR_OVERRIDES_D22 10   //word pairs this sure of the Vietnamese reading (10x) beat D22: "team bên US" (D34)
#endif
    if (en != AS_MISSING && previousN >= 1 && previousCounts[0] >= 1 && pairVote < AS_PAIR_OVERRIDES_D22 && !vAutoSwitchPrevRestored && capVote < AS_CAP_MARGIN) {
        char praw[AS_KEY_LEN];
        Uint32 tmp[MAX_BUFF]; int tmpCount;
        if (asRawWord(previous[0], previousCounts[0], praw)) {
            int pen = asEnglishScore(praw);
            int pvn = asReplay(previous[0], previousCounts[0], tmp, tmpCount);
            if (pen != AS_MISSING && (pvn == AS_MISSING || pen - pvn >= AS_PREFER)) return false;
        }
    }

    //look-back: ambiguous words just before it (kept English because they are English words too)
    Uint32 out[MAX_BUFF];
    int n = 0, deleteCount = count, back = 0;
    Uint32 earlier[AS_LOOKBACK_WORDS][MAX_BUFF];
    int earlierCount[AS_LOOKBACK_WORDS];
    bool earlierSame[AS_LOOKBACK_WORDS];
    for (; back < previousN && back < AS_LOOKBACK_WORDS; back++) {
        const Uint32* pk = previous[back];
        int pc = previousCounts[back];
        char praw[AS_KEY_LEN];
        if (pc < 2 || !asRawWord(pk, pc, praw) || !asFewCapitals(pk, pc)) break;
        int pen = asEnglishScore(praw);
        bool changed;
        int pvn = asReplay(pk, pc, earlier[back], earlierCount[back], &changed);
        earlierSame[back] = !changed;
        if (!changed) {           //looks the same in both modes: pass through Vietnamese-only words ("cho")
            if (pen != AS_MISSING || asVietnameseScore(praw) == AS_MISSING) break;
            earlierCount[back] = pc;
            for (int x = 0; x < pc; x++) earlier[back][x] = pk[x];
        } else if (pen == AS_MISSING || pvn == AS_MISSING ||
                   (back == 0 && vAutoSwitchPrevRestored ? asNextPairVote(praw, raw) < AS_NEXT_PAIR :   //switched on purpose: only strong evidence
                    (pvn < pen - AS_LOOKBACK_MARGIN && !(back == 0 && asNextPairVote(praw, raw) >= AS_NEXT_PAIR)))) {
            break;                //only ambiguous words that were kept English follow the switch (or, D35, ones the
                                  //word after them clearly reads as Vietnamese: "em í ạ", "hết cả mood")
        }
        if (resultCount + n + earlierCount[back] + 1 >= MAX_BUFF || deleteCount + pc + 1 >= MAX_BUFF) break;
        n += earlierCount[back] + 1;
        deleteCount += pc + 1;
    }
    while (back > 0 && earlierSame[back - 1]) {          //no need to retype unchanged words at the far end
        back--;
        deleteCount -= previousCounts[back] + 1;
    }
    n = 0;
    vAutoSwitchUndoCount = 0;
    for (int b = back - 1; b >= 0; b--) {
        for (int x = 0; x < earlierCount[b]; x++) out[n++] = earlier[b][x];
        out[n++] = KEY_SPACE;
        for (int x = 0; x < previousCounts[b]; x++) vAutoSwitchUndoData[vAutoSwitchUndoCount++] = previous[b][x];
        vAutoSwitchUndoData[vAutoSwitchUndoCount++] = KEY_SPACE;
    }
    for (int x = 0; x < resultCount; x++) out[n++] = result[x];
    for (int x = 0; x < count; x++) vAutoSwitchUndoData[vAutoSwitchUndoCount++] = keys[x];
    memcpy(_asConverted, states, resultCount * sizeof(Uint32)); _asConvertedN = resultCount;   //vAutoSwitchForget picks it up when the app switches

    hCode = vRestore;          //platform sends the new text, then the break key itself
    hBPC = (Byte)deleteCount;
    hNCC = (Byte)n;
    hExt = 3;
    for (int x = 0; x < n; x++) hData[n - 1 - x] = out[x];
    return true;
}

bool vAutoSwitchCheckEnglishWord(const Uint32* keys, const int& count) {
    return vAutoSwitchCheckEnglishWord(keys, count, NULL, NULL, 0);
}
/*==========================================================================================================*/


/** ← then → back to the same spot inside a word: the word is still the one being typed (D51). The engine keeps its
 *  state from before the first ← and puts it back when the cursor returns, instead of starting a new word there
 *  ("th ← → if" gave "thif"). Only plain arrows within the word count; anything else forgets it. */
static AsEngineState* _navSaved = NULL;
static int _navOffset = 0;      //how far left of the word's end the cursor is (≤ 0)
static int _navPrevCount = 0;  //the look-back words (they're before the word's end, not before the cursor while away)
static void navForget() { if (_navSaved) { delete _navSaved; _navSaved = NULL; } _navOffset = 0; }

void vKeyHandleEvent(const vKeyEvent& event,
                     const vKeyEventState& state,
                     const Uint16& data,
                     const Uint8& capsStatus,
                     const bool& otherControlKey) {
    if (event == vKeyEvent::Keyboard && (data == KEY_LEFT || data == KEY_RIGHT) && capsStatus != 1 && !otherControlKey) {
        if (data == KEY_LEFT && _navOffset == 0 && (_index > 0 || _spaceCount > 0)) {   //in a word, or right after one
            navForget(); _navSaved = new AsEngineState(); asSaveState(*_navSaved);
            _navPrevCount = _asPrevCount; _asPrevCount = 0;   //away from the word, look-back must not reach the old words
            //asSaveState moved the history out: the word is gone from the live state until the cursor comes back
            _navOffset = -1;
            startNewSession();
            hCode = vDoNothing; hBPC = 0; hNCC = 0; hExt = 1;
            return;
        }
        if (_navSaved && data == KEY_LEFT) { _navOffset--; hCode = vDoNothing; hBPC = 0; hNCC = 0; hExt = 1; return; }
        if (_navSaved && data == KEY_RIGHT) {
            if (++_navOffset == 0) { asRestoreState(*_navSaved); delete _navSaved; _navSaved = NULL; _asPrevCount = _navPrevCount; }   //back at the word's end
            hCode = vDoNothing; hBPC = 0; hNCC = 0; hExt = 1;
            return;
        }
    }
    if (_navSaved) navForget();
    _appendedStandalone = false;
    _isCaps = (capsStatus == 1 || //shift
               capsStatus == 2); //caps lock
    bool literalOnly = false;
    if ((IS_NUMBER_KEY(data) && capsStatus == 1)
        || otherControlKey || isWordBreak(event, state, data) || (IS_NUMBER_KEY(data) && (_index == 0 || _spaceCount > 0))) {   //a digit after a space starts a new word: "mua 1casi" -> "1cái" (D33); glued to letters (abc1def) it stays part of them
        hCode = vDoNothing;
        hBPC = 0;
        hNCC = 0;
        hExt = 1; //word break
        
        _asApostrophe = data == KEY_QUOTE && capsStatus != 1;
        //after a space the word was already checked at the space: a later ( . , etc. must not reach back over the space
        //("zẻo" space "(" gave "zzero(", D53; same as the hyphen in D52)
        bool asSwitch = (asIsSwitchBreak(event, data, capsStatus, otherControlKey) && _spaceCount == 0 && asShouldRestoreToEnglish(literalOnly)) ||
                        (data == KEY_MINUS && capsStatus != 1 && !otherControlKey && _spaceCount == 0 && asHyphenRestore());   //a hyphen after a space isn't part of the word ("wr -" gave "wr wr-", D52)
        _asApostrophe = false;
        if (asSwitch) {
            asRestoreToEnglish(vRestoreAndStartNewSession, literalOnly); //auto switch: an English word typed in Vietnamese mode
        } else if (vRestoreIfWrongSpelling && isWordBreak(event, state, data)) { //restore key if wrong spelling with break-key
            if (!tempDisableKey && vCheckSpelling) {
                checkSpelling(true); //force check spelling
            }
            if (tempDisableKey && !checkRestoreIfWrongSpelling(vRestoreAndStartNewSession)) {
                hCode = vDoNothing;
            }
        }
        
        _isCharKeyCode = state == KeyDown && std::find(_charKeyCode.begin(), _charKeyCode.end(), data) != _charKeyCode.end();
        if (!_isCharKeyCode) { //clear all line cache
            _specialChar.clear();
            _typingStates.clear();
        } else { //check and save current word
            if (_spaceCount > 0) {
                saveWord(KEY_SPACE, _spaceCount);
                _spaceCount = 0;
            } else {
                saveWord();
            }
            _specialChar.push_back(data | (_isCaps ? CAPS_MASK : 0));
            hExt = 3;//normal word
        }
        
        if (hCode == vDoNothing || event == vKeyEvent::Mouse) {   //a click: the apps drop the restore, so nothing calls startNewSession for it
            startNewSession();
            vCheckSpelling = _useSpellCheckingBefore;
        }
        _asPrevCount = 0;
        //the next word is a whole word only after whitespace, a click, a shortcut or an opening ( [ { " ',
        //not after . _ - / etc.
        _afterDigit = IS_NUMBER_KEY(data) && capsStatus != 1;
        _asWordStartOk = event == vKeyEvent::Mouse || otherControlKey || data == KEY_ENTER || data == KEY_RETURN || data == KEY_TAB ||
                         (data == KEY_9 && capsStatus == 1) || data == KEY_LEFT_BRACKET || data == KEY_QUOTE;
    } else if (data == KEY_SPACE) {
        _afterDigit = false;
        if (!tempDisableKey && vCheckSpelling) {
            checkSpelling(true); //force check spelling
        }
        if (asShouldRestoreToEnglish(literalOnly)) { //auto switch: an English word typed in Vietnamese mode
            asRestoreToEnglish(vRestore, literalOnly);
            _spaceCount++;
        } else if (vRestoreIfWrongSpelling && tempDisableKey) { //restore key if wrong spelling
            if (!checkRestoreIfWrongSpelling(vRestore)) {
                hCode = vDoNothing;
            }
            _spaceCount++;
        } else { //do nothing with SPACE KEY
            hCode = vDoNothing;
            _spaceCount++;
        }
        //look-back memory: only words separated by a single space count. A word put back as typed ("tooib") isn't
        //remembered, so it ends the history: else look-back takes the word before it as adjacent ("thí tothis makes")
        if (hCode == vDoNothing && _spaceCount == 1) asRememberWord(); else _asPrevCount = 0;
        //save word
        if (_spaceCount == 1) {
            if (_specialChar.size() > 0) {
                saveSpecialChar();
            } else {
                saveWord();
            }
        }
        vCheckSpelling = _useSpellCheckingBefore;
        _asWordStartOk = true;
    } else if (data == KEY_DELETE) {
        _asPrevCount = 0;
        hCode = vDoNothing;
        hExt = 2; //delete
        if (_specialChar.size() > 0) {
            _specialChar.pop_back();
            if (_specialChar.size() == 0) {
                restoreLastTypingState();
            }
        } else if (_spaceCount > 0) { //previous char is space
            _spaceCount--;
            if (_spaceCount == 0) { //restore word
                restoreLastTypingState();
            }
        } else {
            if (_stateIndex > 0) {
                _stateIndex--;
                while (_asDropN > 0 && _stateIndex < _asDropPos[_asDropN - 1]) _asDropN--;
            }
            if (_index > 0){
                _index--;
                //the letter that went was made from several keys and what's left is plain letters: the keys are those letters
                bool several = (TypingWord[_index] & (TONE_MASK | TONEW_MASK | MARK_MASK)) != 0;
                bool plain = true;
                for (int x = 0; x < _index && plain; x++) plain = (TypingWord[x] & (TONE_MASK | TONEW_MASK | MARK_MASK | STANDALONE_MASK | CHAR_CODE_MASK)) == 0;
                if (several && plain && _longWordHelper.empty()) asResyncStates();
                if (_longWordHelper.size() > 0) {
                    //right shift
                    for (i = MAX_BUFF - 1; i > 0; i--) {
                        TypingWord[i] = TypingWord[i-1];
                    }
                    TypingWord[0] = _longWordHelper.back();
                    _longWordHelper.pop_back();
                    _index++;
                }
                if (vCheckSpelling)
                    checkSpelling();
            }
            hBPC = 0;
            hNCC = 0;
            hExt = 2; //delete key
            if (_index == 0) {
                startNewSession();
                _specialChar.clear();
                restoreLastTypingState();
            } else { //August 23rd continue check grammar
                checkGrammar(1);
            }
        }
    } else { //START AND CHECK KEY
        if (_spaceCount > 0) {
            hBPC = 0;
            hNCC = 0;
            hExt = 0;
            startNewSession();
            //continute save space
            saveWord(KEY_SPACE, _spaceCount);
            _spaceCount = 0;
        } else if (_specialChar.size() > 0) {
            saveSpecialChar();
        }

        insertState(data, _isCaps); //save state
        
        if (!IS_SPECIALKEY(data) || tempDisableKey) { //do nothing
            hCode = vDoNothing;
            hBPC = 0;
            hNCC = 0;
            hExt = 3; //normal key
            insertKey(data, _isCaps);
        } else { //check and update key
            //restore state
            hCode = vDoNothing;
            hExt = 3; //normal key
            handleMainKey(data, _isCaps);
            //auto switch: the same tone key pressed twice undoes the tone, a strong sign of English
            if (hCode == vRestore && asEnabled() && IS_MARK_KEY(data)) {
                vAutoSwitchRequest = 0;
                //Switching to English: put the whole word back to the keys typed, not just the undone tone
                //("tomorr", not "tômrr"). The second press is added after these by the caller (vRestore).
                //Like other Telex input methods the first press goes ("ver"), unless both make an English word
                //already ("err", "boss"); the end of the word decides for good (D19, vAutoSwitchFixDoubledKey).
                if (_stateIndex >= 2 && wordFitsBuffer()) {   //a longer word gets the plain Telex undo
                    int first = -1;                                  //the press that set the tone
                    for (int x = _stateIndex - 2; x >= 0; x--)
                        if ((KeyStates[x] & CHAR_MASK) == data) { first = x; break; }
                    char raw[AS_KEY_LEN];
                    asInitLetters();
                    bool both = first < 0 || (asRawWord(KeyStates, _stateIndex, raw) && asEnglishScore(raw) != AS_MISSING);
                    hBPC = _index;
                    hNCC = 0;
                    for (int x = _stateIndex - 2; x >= 0; x--)
                        if (both || x != first) hData[hNCC++] = KeyStates[x];
                    //remember the word for the end-of-word decision
                    _asDoubleCount = _stateIndex;
                    _asDoubleFirst = first;
                    _asDoubleBoth = both;
                    memcpy(_asDoubleKeys, KeyStates, _stateIndex * sizeof(Uint32));
                }
            }
        }

        if (!IS_KEY_D(data)) {
            //moving the tone onto a just-added ư ("ís" + w -> "iứ"): that ư isn't on screen yet, so one fewer to delete
            //(it took the space before the word: "ab isw" gave "abiứ", D52)
            if (hCode == vDoNothing || _appendedStandalone) {
                checkGrammar(-1);
            } else {
                checkGrammar(0);
            }
        }
        
        if (hCode == vRestore) {
            insertKey(data, _isCaps);
            if (_stateIndex > 0 && vAutoSwitchRequest != 0) {
                if (_asDropN == AS_MAX_DROPS) { memmove(_asDropPos, _asDropPos + 1, sizeof(int) * (AS_MAX_DROPS - 1)); memmove(_asDropKey, _asDropKey + 1, sizeof(Uint32) * (AS_MAX_DROPS - 1)); _asDropN--; }
                _asDropPos[_asDropN] = _stateIndex - 1; _asDropKey[_asDropN] = KeyStates[_stateIndex - 1]; _asDropN++;
            }
            _stateIndex--;
        }
        
        //case [ ]
        if (IS_BRACKET_KEY(data) && IS_BRACKET_KEY((Uint16)hData[0])) {
            if (_index - (hCode == vWillProcess ? hBPC : 0) > 0) {
                _index--;
                saveWord();
            }
            _index = 0;
            tempDisableKey = false;
            _stateIndex = 0;
            hExt = 3;
            _specialChar.push_back(data | (_isCaps ? CAPS_MASK : 0));
        }
    }
    
    //Debug
    //cout<<"index "<<(int)_index<< ", stateIndex "<<(int)_stateIndex<<", word "<<_typingStates.size()<<", long word "<<_longWordHelper.size()<< endl;
    //cout<<"backspace "<<(int)hBPC<<endl;
    //cout<<"new char "<<(int)hNCC<<endl<<endl;
}

bool vAutoSwitchFixDoubledKey(const Uint32* keys, const int& count) {
    int n = _asDoubleCount;
    _asDoubleCount = 0;
    if (!asEnabled() || n < 2 || _asDoubleFirst < 0 || count < 0 || n + count >= AS_KEY_LEN || n + count >= MAX_BUFF) return false;
    asInitLetters();
    Uint32 full[MAX_BUFF], single[MAX_BUFF];
    memcpy(full, _asDoubleKeys, n * sizeof(Uint32));
    for (int x = 0; x < count; x++) full[n + x] = keys[x];
    int total = n + count, m = 0;
    for (int x = 0; x < total; x++) if (x != _asDoubleFirst) single[m++] = full[x];
    char rawFull[AS_KEY_LEN], rawSingle[AS_KEY_LEN];
    if (!asRawWord(full, total, rawFull) || !asRawWord(single, m, rawSingle)) return false;
    bool enFull = asEnglishScore(rawFull) != AS_MISSING, enSingle = asEnglishScore(rawSingle) != AS_MISSING;
    const Uint32* shown = _asDoubleBoth ? full : single;
    int shownCount = _asDoubleBoth ? total : m;
    const Uint32* fixed;
    int fixedCount;
    if (_asDoubleBoth && !enFull && enSingle) { fixed = single; fixedCount = m; }            //"mixx" -> "mix"
    else if (!_asDoubleBoth &&
             (enFull || (!enSingle && (!asFewCapitals(full, total) ||
                                       asHasInternalCapital(full, total) ||
                                       asHasLiteralWordShape(full, total, false))))) {
        // Keep a recognized literal even when deleting the key also spells
        // an English word. For unknown words require structural evidence.
        // This path already undid a doubled key into English; retain case
        // evidence here even when the identifier also contains a Telex key.
        fixed = full;
        fixedCount = total;
    }
    else return false;
    if (fixedCount < 1) return false;
    vAutoSwitchUndoCount = 0;   //Backspace right after puts back what was on screen
    for (int x = 0; x < shownCount; x++) vAutoSwitchUndoData[vAutoSwitchUndoCount++] = shown[x];
    hCode = vRestore;          //platform sends the new text, then the break key itself
    hBPC = (Byte)shownCount;
    hNCC = (Byte)fixedCount;
    hExt = 3;
    for (int x = 0; x < fixedCount; x++) hData[fixedCount - 1 - x] = fixed[x];
    return true;
}

// ---- Next-key probabilities for touch keyboards (fat-finger model, D25) ---------------------------
// The dictionaries are sorted, so the words starting with a prefix are one contiguous range; with a
// running sum of word weights, the mass of every next letter is two binary searches.

static double* asCumulative(const signed char* scores, int count) {
    double* cum = new double[count + 1];
    cum[0] = 0;
    for (int i = 0; i < count; i++) cum[i + 1] = cum[i] + pow(10.0, scores[i] / 10.0);
    return cum;
}

static int asLowerBound(const char* words, const unsigned int* offsets, int count, const char* key, int keyLen) {
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (strcmp(words + offsets[mid], key) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/** Words of the list that start with prefix: [first, last). */
static void asPrefixRange(const char* words, const unsigned int* offsets, int count, const char* prefix, int len, int& first, int& last) {
    char hiKey[AS_KEY_LEN + 2];
    first = asLowerBound(words, offsets, count, prefix, len);
    memcpy(hiKey, prefix, len);
    hiKey[len] = '\x7f'; hiKey[len + 1] = 0;           //just after every word with this prefix
    last = asLowerBound(words, offsets, count, hiKey, len + 1);
}

void vAutoSwitchNextKeyMass(const int& vietnamese, const char* prefix, double out[27]) {
    static double* enCum = asCumulative(kAutoSwitchEnScores, kAutoSwitchEnCount);
    static double* vnCum = asCumulative(kAutoSwitchVnScores, kAutoSwitchVnCount);
    const char* words = vietnamese ? kAutoSwitchVnWords : kAutoSwitchEnWords;
    const unsigned int* offsets = vietnamese ? kAutoSwitchVnOffsets : kAutoSwitchEnOffsets;
    const signed char* scores = vietnamese ? kAutoSwitchVnScores : kAutoSwitchEnScores;
    int count = vietnamese ? kAutoSwitchVnCount : kAutoSwitchEnCount;
    double* cum = vietnamese ? vnCum : enCum;
    int len = (int)strlen(prefix);
    for (int i = 0; i < 27; i++) out[i] = 0;
    if (len >= AS_KEY_LEN - 2) return;
    char key[AS_KEY_LEN + 2];
    memcpy(key, prefix, len);
    for (int c = 0; c < 26; c++) {
        key[len] = 'a' + c; key[len + 1] = 0;
        int a, b;
        asPrefixRange(words, offsets, count, key, len + 1, a, b);
        out[c] = cum[b] - cum[a];
    }
    int s = asLookup(words, offsets, scores, count, prefix);   //the prefix is a whole word: the word can end here
    out[26] = s == AS_MISSING ? 0 : pow(10.0, s / 10.0);
}

// ---- Dictionary access for suggestions and auto-correct (D26) ------------------------------------

static Uint16 asKeyOfLetter(char c) {
    static const Uint16 k[26] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,
                                 KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    return (c >= 'a' && c <= 'z') ? k[c - 'a'] : 0;
}

int vAutoSwitchWordScore(const int& vietnamese, const char* letters, Uint16* rendered, int* renderedLen) {
    int n = (int)strlen(letters);
    if (rendered && renderedLen) *renderedLen = 0;
    if (n == 0 || n >= AS_KEY_LEN - 1) return AS_MISSING;
    asInitLetters();
    if (!vietnamese) {
        int s = asEnglishScore(letters);
        if (s != AS_MISSING && rendered && renderedLen) { for (int i = 0; i < n; i++) rendered[i] = letters[i]; *renderedLen = n; }
        return s;
    }
    Uint32 keys[AS_KEY_LEN], result[MAX_BUFF];
    for (int i = 0; i < n; i++) { keys[i] = asKeyOfLetter(letters[i]); if (!keys[i] && letters[i] != 'a') return AS_MISSING; }
    int count = 0;
    bool changed = false;
    int vn = asReplay(keys, n, result, count, &changed);
    if (vn != AS_MISSING) {
        if (rendered && renderedLen) {
            for (int i = 0; i < count; i++) rendered[i] = (result[i] & CHAR_CODE_MASK) ? (Uint16)(result[i] & 0xFFFF) : keyCodeToCharacter(result[i]);
            *renderedLen = count;
        }
        return vn;
    }
    if (!changed) {                      //plain syllables that look the same in Telex: cho, anh, ban
        vn = asVietnameseScore(letters);
        if (vn != AS_MISSING && rendered && renderedLen) { for (int i = 0; i < n; i++) rendered[i] = letters[i]; *renderedLen = n; }
    }
    return vn;
}

int vAutoSwitchCompletions(const int& vietnamese, const char* prefix, char out[][32], int* scores, const int& maxOut) {
    const char* words = vietnamese ? kAutoSwitchVnWords : kAutoSwitchEnWords;
    const unsigned int* offsets = vietnamese ? kAutoSwitchVnOffsets : kAutoSwitchEnOffsets;
    const signed char* sc = vietnamese ? kAutoSwitchVnScores : kAutoSwitchEnScores;
    int count = vietnamese ? kAutoSwitchVnCount : kAutoSwitchEnCount;
    int len = (int)strlen(prefix);
    if (len == 0 || len >= 30) return 0;
    int a, b;
    asPrefixRange(words, offsets, count, prefix, len, a, b);
    int n = 0;
    for (int i = a; i < b; i++) {
        int s = sc[i];
        if (n == maxOut && s <= scores[n - 1]) continue;
        const char* w = words + offsets[i];
        if (strlen(w) >= 32) continue;
        int pos = n < maxOut ? n++ : n - 1;
        while (pos > 0 && scores[pos - 1] < s) { strcpy(out[pos], out[pos - 1]); scores[pos] = scores[pos - 1]; pos--; }
        strcpy(out[pos], w); scores[pos] = s;
    }
    return n;
}

bool vAutoSwitchIsVietnameseSpelling(const char* letters) {
    int n = (int)strlen(letters);
    if (n == 0 || n >= AS_KEY_LEN - 1) return false;
    Uint32 keys[AS_KEY_LEN];
    for (int i = 0; i < n; i++) { keys[i] = asKeyOfLetter(letters[i]); if (!keys[i] && letters[i] != 'a') return false; }
    AsEngineState saved;
    asSaveState(saved);
    _spaceCount = 0;
    startNewSession();
    for (int x = 0; x < n; x++) vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, (Uint16)keys[x], 0, false);
    bool savedTemp = tempDisableKey;
    checkSpelling(true);
    bool valid = !tempDisableKey && !savedTemp;
    asRestoreState(saved);
    return valid;
}

/** Early Switch (D28): which language a finished word can only be. 1 = only Vietnamese (a Vietnamese reading,
 *  no English word: "tôi", "khong"), 0 = only English (an English word with no Vietnamese reading: "hello",
 *  "makes"), -1 = both or neither ("an", "con", names). */
int vAutoSwitchWordSide(const Uint32* keys, const int& count) {
    if (!asEnabled() || count < 2 || count >= MAX_BUFF - 1) return -1;
    asInitLetters();
    char raw[AS_KEY_LEN];
    if (!asRawWord(keys, count, raw) || !asFewCapitals(keys, count)) return -1;
    int en = asEnglishScore(raw);
    Uint32 result[MAX_BUFF];
    int resultCount;
    int vn = asReplay(keys, count, result, resultCount);
    if (en != AS_MISSING && vn == AS_MISSING) return 0;
    if (vn != AS_MISSING && en == AS_MISSING) return 1;
    return -1;
}

