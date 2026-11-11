#!/usr/bin/env python3
"""Live typing test on the Android emulator: taps the Vikey keyboard through the real touch path and reads
the field back. Same cases as Windows/Tests/cases.tsv and Linux/Tests/livetest.py. Needs a DEBUG build
installed, the Vikey IME selected, and the app's main screen open (the "Try the keyboard" field).
Exit code = number of failures."""
import subprocess, sys, os, re, time, html
sys.path.insert(0, os.path.dirname(__file__))
from tap_type import type_text, sh, ime_top, layout, DENS
CASES = os.path.join(os.path.dirname(__file__), "..", "..", "Windows", "Tests", "cases.tsv")
FIELD_Y = None

def field_text():
    sh("uiautomator dump /sdcard/ui.xml >/dev/null"); x = sh("cat /sdcard/ui.xml")
    # uiautomator quotes an attribute with ' when the text contains "
    m = re.search(r'<node[^>]*?text=(?:"([^"]*)"|\'([^\']*)\')[^>]*class="android.widget.EditText"', x)
    return html.unescape(m.group(1) if m.group(1) is not None else m.group(2)) if m else None

def find_field():
    sh("uiautomator dump /sdcard/ui.xml >/dev/null"); x = sh("cat /sdcard/ui.xml")
    m = re.search(r'<node[^>]*class="android.widget.EditText"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', x)
    if not m: raise SystemExit("no EditText on screen")
    l, t, r, b = map(int, m.groups()); return (l + r) // 2, (t + b) // 2

def clear_field():
    # key events from adb go to the app, not the keyboard: an outside change, which resets the engine
    sh("input keyevent " + " ".join(["112"] * 40) + "; input keyevent " + " ".join(["67"] * 80))   # forward-delete, then delete
    time.sleep(0.4)

def bars_top(top):
    """Screen y where the lowest bottom bar starts: the system nav bar, or the IME's own back/switcher strip (gesture nav).
    None when dumpsys shows no nav bar at all: nothing to check against."""
    frames = re.findall(r"type=(?:navigationBars|ITYPE_NAVIGATION_BAR) frame=\[\d+,(\d+)\]\[\d+,(\d+)\]", sh("dumpsys window"))   # ITYPE_ up to Android 13; no sideHint before 15
    tops = [int(t) for t, b in frames if int(t) > int(b) / 2]   # a bottom bar starts below the middle; a side bar starts at 0
    m = re.search(r"mNavigationBarFrame=\S+ \S+ \S+ \d+,(\d+)-", sh("dumpsys input_method"))   # in the IME window's coordinates
    if m: tops.append(top + int(m.group(1)))
    if not frames and not m: return None
    return min(tops, default=1 << 30)   # only side bars: nothing to overlap

def main():
    fx, fy = find_field()
    sh("input tap %d %d" % (fx, fy)); time.sleep(1)
    sh("am broadcast -a app.vikey.DEBUG -p app.vikey --ez autocap false >/dev/null"); time.sleep(0.8)
    sh("input tap %d %d" % (fx, fy)); time.sleep(0.6)
    found = ime_top(None); top = ime_top() if found is None else found; L = layout()
    fails = 0; total = 0; bars = None if found is None else bars_top(top)   # no IME frame: the typing fallback is no origin to check the bars against
    clear = None if bars is None else top + L[" "][1] + 43 / 2 * DENS <= bars   # the bottom row's lower edge (keyH 43 dp)
    if clear is not None: total += 1; fails += not clear
    print("%-4s %-28s" % ({None: "skip", True: "ok", False: "FAIL"}[clear], "bottom row above the nav bar"))
    for line in open(CASES, encoding="utf-8"):
        if line.startswith("#") or not line.strip(): continue
        name, start, keys, expected = line.rstrip("\n").split("\t")[:4]
        total += 1
        clear_field()
        sh("am broadcast -a app.vikey.DEBUG -p app.vikey --ei lang %d >/dev/null" % (1 if start == "vi" else 0)); time.sleep(0.4)
        for part in re.split(r"(\{alt\+z\}|\\b|\\n)", keys):
            if not part: continue
            if part == "{alt+z}": vx, vy = L["\x01"]; sh("input tap %d %d" % (vx, top + vy)); time.sleep(0.2)
            elif part == "\\b": type_text("\b", top)
            elif part == "\\n": type_text("\n", top)
            else: type_text(part, top)
        time.sleep(0.5)
        got = field_text()
        ok = got == expected
        if not ok: fails += 1
        print("%-4s %-28s got=%r expected=%r" % ("ok" if ok else "FAIL", name, got, expected))
    sh("am broadcast -a app.vikey.DEBUG -p app.vikey --ez autocap true >/dev/null")
    print("%d/%d passed" % (total - fails, total))
    sys.exit(fails)
if __name__ == "__main__": main()
