#!/usr/bin/env python3
"""Live checks for the iOS build-23 keyboard behaviours on Android (port of iOS/Tests/KeyboardSystemParityTests):
punctuation snaps back onto the word, smart dashes, Lets/wont contractions, capitals after a closing quote, and the
personal dictionary's shortcuts (Text Replacements) with backspace undo. Needs a DEBUG build, the Vikey IME selected,
the app's main screen open, and a personal-dictionary entry omw -> "On my way!":
  adb shell content insert --uri content://user_dictionary/words --bind word:s:"On my way!" --bind shortcut:s:omw \\
      --bind frequency:i:250 --bind locale:s:en_US --bind appid:i:0
Exit code = number of failures."""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from tap_type import type_text, sh, ime_top
from livetest import field_text, find_field, clear_field

CASES = [  # name, language (0 EN / 1 VI), autocap, keys, expected
    ("snap_period",     0, False, "this .", "this."),
    ("snap_comma",      0, False, "well ,", "well,"),
    ("snap_close_paren",0, False, "(ok )", "(ok)"),
    ("snap_after_switch",1, False, "booj source vaay ?", "bộ source vây?"),   # the snap-back's delete isn't a Backspace undo
    ("no_snap_open",    0, False, "a (", "a ("),
    ("smart_dash",      0, False, "a--b", "a—b"),
    ("lets_capital",    0, False, "Lets ", "Let's "),
    ("lets_lower",      0, False, "she lets ", "she lets "),
    ("wont",            0, False, "wont ", "won't "),
    ("cap_after_quote", 0, True,  'go." then ', 'Go." Then '),
    ("shortcut",        0, False, "omw ", "On my way! "),
    ("shortcut_undo",   0, False, "omw \b", "omw"),
    ("shortcut_vi",     1, False, "omw ", "On my way! "),
    ("strip_pick",      0, False, "teh\x03", "the "),   # \x03 = tap the middle strip cell (picks on lift)
    ("delete_selection",0, False, "hello world\x04\b", "hello "),   # \x04 = select the 5 characters before the cursor
    ("dash_selection",  0, False, "a-hello\x04-", "a—"),     # the smart dash's own delete leaves the selection to the typed dash
]

def main():
    fx, fy = find_field()
    sh("input tap %d %d" % (fx, fy)); time.sleep(1)
    top = ime_top(); fails = 0
    for name, lang, cap, keys, want in CASES:
        sh("am broadcast -a app.vikey.DEBUG -p app.vikey --ez autocap %s >/dev/null" % ("true" if cap else "false"))
        clear_field()
        sh("am broadcast -a app.vikey.DEBUG -p app.vikey --ei lang %d --ez letters true >/dev/null" % lang); time.sleep(0.4)
        for part in keys.split("\x03"):
            for i, sub in enumerate(part.split("\x04")):
                if i: sh("; ".join(["input keycombination -t 200 59 21; sleep 0.3"] * 5)); time.sleep(0.5)   # Shift+Left, from the app's side
                if sub: type_text(sub, top)
        if "\x03" in keys: time.sleep(0.4); sh("input tap 540 %d" % (top + int(28 * 2.625))); time.sleep(0.4)
        time.sleep(0.6)
        got = field_text(); ok = got == want; fails += not ok
        print("%-4s %-18s got=%r expected=%r" % ("ok" if ok else "FAIL", name, got, want))
    sh("am broadcast -a app.vikey.DEBUG -p app.vikey --ez autocap true >/dev/null")
    print("%d/%d passed" % (len(CASES) - fails, len(CASES))); sys.exit(fails)
if __name__ == "__main__": main()
