#!/usr/bin/env python3
"""Types text on the Vikey Android keyboard by tapping keys through adb (the real touch path).
Usage: tap_type.py [--top PX] "text"   — letters, digits via the 123 layer, space, '\n' return, '\b' backspace, '\x01' = VI/EN key, '\x02' = Esc-like reset (taps the field).
Geometry mirrors KeyboardView.kt (402 dp base). Reads the IME window top from dumpsys unless --top is given."""
import subprocess, sys, re, time
W, DENS = 1080, 2.625
def sh(cmd): return subprocess.run(["adb", "shell", cmd], capture_output=True, text=True).stdout
def ime_top(fallback=1655):
    out = sh("dumpsys window windows")
    m = re.search(r"InputMethod\}.*?frame=\[0,(\d+)\]\[1080,2400\]", out, re.S)
    return int(m.group(1)) if m else fallback
def layout(bar_on=True):
    d = DENS; s = W / (402 * d); side = 6.67 * s * d; gap = 6 * s * d; key = (W - 2 * side - 9 * gap) / 10
    top = (11 + 44 - 3) if bar_on else 11; pitch = 54; keyH = 43
    rows = {}
    def y(r): return (top + r * pitch + keyH / 2) * d
    for i, c in enumerate("qwertyuiop"): rows[c] = (side + i * (key + gap) + key / 2, y(0))
    x0 = side + (key + gap) / 2
    for i, c in enumerate("asdfghjkl"): rows[c] = (x0 + i * (key + gap) + key / 2, y(1))
    fn = 45.3 * s * d
    rows["\x10shift"] = (side + fn / 2, y(2)); rows["\b"] = (W - side - fn / 2, y(2))
    x0 = side + (key + gap) * 1.5
    for i, c in enumerate("zxcvbnm"): rows[c] = (x0 + i * (key + gap) + key / 2, y(2))
    small = 43.3 * s * d; ret = 92.6 * s * d
    names = ["\x10layer", "\x10emoji", "\x10globe", "\x01"]
    x = side
    for n in names: rows[n] = (x + small / 2, y(3)); x += small + gap
    rows[" "] = ((x + W - side - ret - gap) / 2, y(3)); rows["\n"] = (W - side - ret / 2, y(3))
    # number layer: row0 digits, row2 .,?!' five wide keys between the function keys
    for i, c in enumerate("1234567890"): rows["#" + c] = (side + i * (key + gap) + key / 2, y(0))
    for i, c in enumerate("-/:;()$&@\""): rows["#" + c] = (side + i * (key + gap) + key / 2, y(1))
    left = side + fn + gap * 2.5; right = W - side - fn - gap * 2.5; w = (right - left - 4 * gap) / 5
    for i, c in enumerate(".,?!'"): rows["#" + c] = (left + i * (w + gap) + w / 2, y(2))
    rows["\x10sym"] = (side + fn / 2, y(2))   # #+= on the numbers layer (123 on the symbols layer)
    for i, c in enumerate("[]{}#%^*+="): rows["@" + c] = (side + i * (key + gap) + key / 2, y(0))
    for i, c in enumerate("_\\|~<>€£¥•"): rows["@" + c] = (side + i * (key + gap) + key / 2, y(1))
    return rows
# long-press alternates (KeyboardView.alternateTable): accented letter -> (base key, index)
ALT_TABLE = {"a": "aăâáàảãạ", "e": "eêéèẻẽẹ", "o": "oôơóòỏõọ", "u": "uưúùủũụ", "i": "iíìỉĩị", "y": "yýỳỷỹỵ", "d": "dđ"}
ALT = {c: (base, i) for base, s in ALT_TABLE.items() for i, c in enumerate(s) if i > 0}
def alternate_tap(base, index, top, L, d=DENS):
    """Hold the base key, slide to the alternate, lift (the view's own 450 ms hold timer shows the strip)."""
    kx, ky = L[base]; key_w = (W - 2 * 6.67 * (W / (402 * d)) * d - 9 * 6 * (W / (402 * d)) * d) / 10
    left = kx - key_w / 2; w = 34 * d; n = len(ALT_TABLE[base]); total = n * w + 8 * d
    x = max(2 * d, min(left - 4 * d, W - 2 * d - total))
    ax = x + 4 * d + index * w + w / 2; ay = ky - 43 * d / 2 - 46 * d - 6 * d + 23 * d
    sh("input motionevent DOWN %d %d" % (kx, top + ky)); time.sleep(0.65)
    sh("input motionevent MOVE %d %d" % (ax, top + ay)); time.sleep(0.1)
    sh("input motionevent UP %d %d" % (ax, top + ay)); time.sleep(0.1)
def type_text(text, top=None, bar_on=True, delay=0.06):
    top = top or ime_top(); L = layout(bar_on); cmds = []; layer = "abc"
    for ch in text:
        low = ch.lower()
        if low in L and not (ch.isdigit()):
            if layer != "abc": cmds.append(("\x10layer", L["\x10layer"])); layer = "abc"
            if ch.isupper(): cmds.append(("shift", L["\x10shift"]))
            cmds.append((ch, L[low]))
        elif "#" + ch in L:
            if layer == "abc": cmds.append(("\x10layer", L["\x10layer"])); layer = "123"
            elif layer == "sym": cmds.append(("\x10sym", L["\x10sym"])); layer = "123"
            cmds.append((ch, L["#" + ch]))
        elif "@" + ch in L:
            if layer == "abc": cmds.append(("\x10layer", L["\x10layer"])); layer = "123"
            if layer == "123": cmds.append(("\x10sym", L["\x10sym"])); layer = "sym"
            cmds.append((ch, L["@" + ch]))
        elif ch in ALT: cmds.append(("alt", ALT[ch]))
        else: raise SystemExit("no key for %r" % ch)
        if ch == " ": layer = "abc"   # space returns to letters (like iOS)
        if ch in ".,?!'" and layer == "123" and ch == "'": layer = "abc"
    batch = []
    def flush():
        if batch: sh("; ".join("input tap %d %d; sleep %.2f" % (x, top + y, delay) for x, y in batch)); batch.clear()
    for name, v in cmds:
        if name == "alt": flush(); alternate_tap(v[0], v[1], top, L)
        else: batch.append(v)
    flush()
if __name__ == "__main__":
    a = sys.argv[1:]; top = None
    if a and a[0] == "--top": top = int(a[1]); a = a[2:]
    type_text(a[0].encode().decode("unicode_escape"), top)
