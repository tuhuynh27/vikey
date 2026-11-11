#!/usr/bin/env python3
"""Google Play store graphics for Vikey for Android: icon-512, feature graphic 1024x500, phone screenshots 1080x1920.

  play_assets.py <raw_dir> <out_dir>

<raw_dir> holds real emulator screenshots (1080x2400, `adb exec-out screencap -p`, demo-mode status bar 9:41) named
<shot>_<ui>.png, ui = vi | en: type (Thu ban phim field after typing a mixed sentence, keyboard up), onb (intro page 1
with the demo card finished), sug ("Sorry I just sent teh", strip shows the auto-correct), emoji (emoji search with a
query + a result inserted), style (settings, Typing style menu open). Each slide = brand-indigo background, a short
headline + sub, the screenshot in a Pixel-style frame, and zoomed callouts cropped from the same screenshot.
"""
import sys, os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont
sys.path.insert(0, str(Path(__file__).parent))
from dl_shots import android_frame, rounded_mask, drop

ROOT = Path(__file__).resolve().parent.parent
SF = "/System/Library/Fonts/SFNS.ttf"
INK, INDIGO, MUTED = (17, 19, 24), (79, 70, 229), (91, 100, 114)

def font(size, weight="Bold"):
    f = ImageFont.truetype(SF, size); f.set_variation_by_name(weight); return f

FIELD = (42, 342, 1038, 594)          # the white "Try the keyboard" card around the text field
TEXT = (70, 378, 1010, 498)           # inside the field: the typed line only
SLIDES = [  # file, raw shot, callouts (source box, zoom target width[, vertical nudge])
    ("01_autoswitch", "type", [(TEXT, 980)]),
    ("02_mix", "onb", [("card", 1000, -44)]),
    ("03_suggest", "sug", [(TEXT, 980), ((0, 1656, 1080, 1772), 1000)]),
    ("04_emoji", "emoji", [(TEXT, 980), ((0, 1552, 1080, 1790), 1000)]),
    ("05_settings", "style", [((40, 1290, 1040, 1872), 960)]),
]
CAPTIONS = {
    "vi": [(["Tự chuyển Anh/Việt", "khi gõ"], "Cứ gõ, Vikey tự chọn ngôn ngữ"),
           (["Gõ trộn", "không cần chuyển tay"], "So với bộ gõ thông thường"),
           (["Gợi ý và", "tự sửa lỗi"], "Cả tiếng Việt lẫn tiếng Anh"),
           (["Tìm emoji", "bằng tiếng Việt"], "Gõ Telex ngay trong ô tìm"),
           (["Tuỳ chỉnh", "theo ý bạn"], "Kiểu gõ, đặt dấu, chính tả")],
    "en-US": [(["English & Vietnamese,", "switched as you type"], "Just type. Vikey picks the language."),
              (["Mix languages,", "no switching"], "Compared with a normal keyboard"),
              (["Suggestions", "and auto-correct"], "In Vietnamese and English"),
              (["Find any emoji", "by name"], "Search in English or Vietnamese"),
              (["Make it yours"], "Typing style, tone marks, spelling")],
}
UI = {"vi": "vi", "en-US": "en"}

def background(w, h):
    """Brand indigo, lighter at the top-left, a cool blue glow on the right (like the App Store slides)."""
    s = 8; bg = Image.new("RGB", (w // s, h // s), (72, 62, 222)); d = ImageDraw.Draw(bg)
    for c, (x, y, r) in [((116, 112, 240), (0, 0, 0.55)), ((86, 98, 232), (1.0, 0.55, 0.35)), ((58, 46, 196), (0.3, 1.05, 0.5))]:
        X, Y, R = x * w / s, y * h / s, r * max(w, h) / s
        d.ellipse([X - R, Y - R, X + R, Y + R], fill=c)
    return bg.filter(ImageFilter.GaussianBlur(max(w, h) / s * 0.12)).resize((w, h), Image.LANCZOS).convert("RGBA")

def centered(d, text, f, y, w, fill):
    tw = d.textlength(text, font=f); d.text(((w - tw) / 2, y), text, font=f, fill=fill)

def fit(d, lines, size, maxw, weight="Heavy"):
    while size > 40 and max(d.textlength(l, font=font(size, weight)) for l in lines) > maxw: size -= 2
    return font(size, weight)

def white_card(raw, x=860, y0=950, y1=1800):
    """The intro's demo card: the white run down a column that misses the card's text and badge."""
    ys = [y for y in range(y0, y1) if raw.getpixel((x, y)) == (255, 255, 255)]
    return (62, ys[0], 1018, ys[-1] + 1)

def callout(raw, box, width):
    crop = raw.crop(box); k = width / crop.width
    crop = crop.resize((width, round(crop.height * k)), Image.LANCZOS).convert("RGBA")
    r = 44; m = rounded_mask(crop.size, r)
    card = Image.new("RGBA", crop.size, (0, 0, 0, 0)); card.paste(crop, (0, 0), m)
    ImageDraw.Draw(card).rounded_rectangle([0, 0, crop.width - 1, crop.height - 1], radius=r, outline=(185, 181, 245, 255), width=3)
    return card, m

def slide(raw_path, lines, sub, callouts, out):
    W, H = 1080, 1920
    bg = background(W, H); d = ImageDraw.Draw(bg)
    hf = fit(d, lines, 92, W - 120); lh = int(hf.size * 1.12); y = 118 if len(lines) == 2 else 150
    for l in lines: centered(d, l, hf, y, W, (255, 255, 255)); y += lh
    centered(d, sub, fit(d, [sub], 42, W - 140, "Regular"), y + 22, W, (226, 228, 255))
    raw = Image.open(raw_path).convert("RGB")
    ph = android_frame(raw_path, target_h=1467)          # screen 660x1467, scale 0.611 of the 1080x2400 capture
    bez = (ph.width - 660) // 2; px, py = (W - ph.width) // 2, 440
    drop(bg, ph, (px, py), blur=40, offset=24, alpha=120)
    sc = 660 / raw.width
    for box, width, *dy in callouts:
        if box == "card": box = white_card(raw)
        card, m = callout(raw, box, width)
        cx = px + bez + (box[0] + box[2]) / 2 * sc; cy = py + bez + (box[1] + box[3]) / 2 * sc
        x = int(min(max(cx - card.width / 2, (W - card.width) / 2 if card.width > 900 else 30), W - 30 - card.width))
        drop(bg, card, (x, int(cy - card.height / 2) + sum(dy)), alpha_mask=m, blur=26, offset=14, alpha=95)
    bg.convert("RGB").save(out, optimize=True)

def icon(out):
    Image.open(ROOT / "iOS/App/Assets.xcassets/AppIcon.appiconset/icon-1024.png").convert("RGBA").resize((512, 512), Image.LANCZOS).save(out)

def feature(raw_path, lines, out):
    W, H = 1024, 500
    bg = Image.new("RGB", (W // 8, H // 8), (247, 247, 252)); d = ImageDraw.Draw(bg)
    for c, (x, y, r) in [((222, 220, 252), (0.95, 0.35, 0.40)), ((233, 236, 252), (0.15, 1.0, 0.30))]:
        X, Y, R = x * W / 8, y * H / 8, r * W / 8; d.ellipse([X - R, Y - R, X + R, Y + R], fill=c)
    bg = bg.filter(ImageFilter.GaussianBlur(10)).resize((W, H), Image.LANCZOS).convert("RGBA"); d = ImageDraw.Draw(bg)
    def card(im, radius, pos, alpha=60):
        m = rounded_mask(im.size, radius)
        drop(bg, Image.composite(im, Image.new("RGBA", im.size, (0, 0, 0, 0)), m), pos, alpha_mask=m, blur=16, offset=8, alpha=alpha)
    # right: the real keyboard with the typed line above it
    raw = Image.open(raw_path).convert("RGB"); KW = 430
    kb = raw.crop((0, 1656, 1080, 2333))        # strip + keys; below are the system nav-bar hints
    pad = Image.new("RGB", (kb.width, kb.height + 14), raw.getpixel((8, 2300))); pad.paste(kb, (0, 0)); kb = pad
    kb = kb.resize((KW, round(kb.height * KW / kb.width)), Image.LANCZOS).convert("RGBA")
    kx, ky = W - KW - 52, H - kb.height - 44
    tx = raw.crop(TEXT); tx = tx.resize((KW, round(tx.height * KW / tx.width)), Image.LANCZOS).convert("RGBA")
    card(kb, 22, (kx, ky), 70); card(tx, 16, (kx, ky - tx.height - 16))
    # left: icon + wordmark, the line under it (one line per item, never wrapped)
    ic = Image.open(ROOT / "iOS/App/Assets.xcassets/AppIcon.appiconset/icon-1024.png").convert("RGBA").resize((96, 96), Image.LANCZOS)
    lf = font(36, "Semibold"); lh = 48
    x0 = 64; block = 96 + 40 + lh * len(lines); y0 = (H - block) // 2
    card(ic, 22, (x0, y0))
    wf = font(88, "Bold"); bb = d.textbbox((0, 0), "Vikey", font=wf); tx0 = x0 + 96 + 26; ty = y0 + 48 - (bb[1] + bb[3]) / 2
    d.text((tx0, ty), "Vi", font=wf, fill=INK); d.text((tx0 + d.textlength("Vi", font=wf), ty), "key", font=wf, fill=INDIGO)
    y = y0 + 96 + 40
    for l in lines:
        assert x0 + d.textlength(l, font=lf) < kx - 24, l
        d.text((x0, y), l, font=lf, fill=INK); y += lh
    bg.convert("RGB").save(out, optimize=True)

if __name__ == "__main__":
    raw_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2]); out_dir.mkdir(parents=True, exist_ok=True)
    icon(out_dir / "icon-512.png")
    feature(raw_dir / "type_vi.png", ["Gõ tiếng Việt,", "tự chuyển Anh/Việt"], out_dir / "feature-1024x500.png")
    feature(raw_dir / "type_en.png", ["Vietnamese typing,", "switches to English for you"], out_dir / "feature-1024x500-en-US.png")
    for loc, caps in CAPTIONS.items():
        (out_dir / loc).mkdir(exist_ok=True)
        for (name, shot, cos), (lines, sub) in zip(SLIDES, caps):
            slide(raw_dir / f"{shot}_{UI[loc]}.png", lines, sub, cos, out_dir / loc / f"{name}.png")
    print("done", out_dir)
