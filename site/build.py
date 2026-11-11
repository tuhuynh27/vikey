#!/usr/bin/env python3
"""Builds site/dist from site/pages/*.html by wrapping each page in the shared layout (nav + footer).
No dependencies. Run: python3 site/build.py
Demo frames (assets/demo.json) come from the real engine: Tools/autoswitch/test/harness frames <lang> <keys>."""
import os, re, shutil, datetime, json

ROOT = os.path.dirname(os.path.abspath(__file__))
PAGES = os.path.join(ROOT, "pages")
PAGES_EN = os.path.join(ROOT, "pages_en")   # English (D31), same file names
DIST = os.path.join(ROOT, "dist")
YEAR = datetime.date.today().year
SITE = "https://vikey.app"
GITHUB = "https://github.com/tonyminibot/Vikey"

T = {   # layout text per language (D31: Vietnamese at /, English at /en/)
    "vi": dict(features="Chức năng", faq="Câu hỏi", changelog="Phiên bản", download="Tải về", home="Trang chủ", support="Hỗ trợ",
               faq_long="Câu hỏi thường gặp", bugs="Báo lỗi", feedback="Gửi góp ý", privacy="Quyền riêng tư", ack="Ghi nhận", source="Mã nguồn",
               license="Giấy phép GPL-3.0", switch="EN", switch_title="English", sitename="Vikey - Gõ tiếng Việt"),
    "en": dict(features="Features", faq="FAQ", changelog="Changelog", download="Download", home="Home", support="Support",
               faq_long="FAQ", bugs="Report a bug", feedback="Send feedback", privacy="Privacy", ack="Acknowledgements", source="Source code",
               license="GPL-3.0 license", switch="VI", switch_title="Tiếng Việt", sitename="Vikey - Gõ tiếng Việt"),
}

FLAG_VN = '<svg class="flag" viewBox="0 0 30 20" aria-hidden="true"><rect width="30" height="20" fill="#DA251D"/><polygon fill="#FFFF00" points="15,4 16.18,7.62 19.98,7.62 16.9,9.86 18.08,13.48 15,11.24 11.92,13.48 13.1,9.86 10.02,7.62 13.82,7.62"/></svg>'
def flag_gb(i):   # Union Jack; ids unique per copy (the switch appears twice per page)
    return (f'<svg class="flag" viewBox="0 0 60 30" aria-hidden="true"><clipPath id="gbs{i}"><path d="M0,0v30h60v-30z"/></clipPath>'
            f'<clipPath id="gbt{i}"><path d="M30,15h30v15zv15h-30zh-30v-15zv-15h30z"/></clipPath><g clip-path="url(#gbs{i})">'
            '<path d="M0,0v30h60v-30z" fill="#012169"/><path d="M0,0L60,30M60,0L0,30" stroke="#fff" stroke-width="6"/>'
            f'<path d="M0,0L60,30M60,0L0,30" clip-path="url(#gbt{i})" stroke="#C8102E" stroke-width="4"/>'
            '<path d="M30,0v30M0,15h60" stroke="#fff" stroke-width="10"/><path d="M30,0v30M0,15h60" stroke="#C8102E" stroke-width="6"/></g></svg>')

def nav(lang, pre, other):
    t = T[lang]
    def sw(i):   # the flag of the language it switches TO, so it's easy to spot
        flag = flag_gb(i) if lang == "vi" else FLAG_VN
        return f'<a class="lang" href="{other}" hreflang="{"en" if lang == "vi" else "vi"}" title="{t["switch_title"]}">{flag}{t["switch"]}</a>'
    return f"""<header class="nav"><div class="wrap">
<a class="brand" href="{pre}/"><img src="/assets/icon.png" alt="" width="34" height="34"><span>Vi<b>key</b></span></a>
<nav><a href="{pre}/#features">{t["features"]}</a><a href="{pre}/faq">{t["faq"]}</a><a href="{pre}/changelog">{t["changelog"]}</a><a href="{GITHUB}">GitHub</a>{sw(1)}<a class="btn dl-direct" data-cta="short" href="{pre}/download">{t["download"]}</a></nav>
<div class="nav-m">{sw(2)}<button class="menu-btn" type="button" aria-label="Menu" aria-expanded="false" aria-controls="mnav"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 7h16M4 12h16M4 17h16"/></svg></button></div>
</div>
<nav class="mnav" id="mnav" hidden><a href="{pre}/download">{t["download"]}</a><a href="{pre}/#features">{t["features"]}</a><a href="{pre}/faq">{t["faq"]}</a><a href="{pre}/changelog">{t["changelog"]}</a><a href="{GITHUB}">GitHub</a></nav>
</header>
<script>(()=>{{const b=document.querySelector(".menu-btn"),m=document.getElementById("mnav");if(!b)return;b.onclick=()=>{{const o=m.hidden;m.hidden=!o;b.setAttribute("aria-expanded",o)}};addEventListener("keydown",e=>{{if(e.key==="Escape"){{m.hidden=true;b.setAttribute("aria-expanded",false)}}}})}})()</script>
"""

def footer(lang, pre):
    t = T[lang]
    return f"""<footer class="foot"><div class="wrap cols">
<div class="col"><p class="muted">© {YEAR} Tony Dinh</p></div>
<div class="col"><h4>Vikey</h4><a href="{pre}/">{t["home"]}</a><a href="{pre}/#features">{t["features"]}</a><a href="{pre}/download">{t["download"]}</a><a href="{pre}/changelog">{t["changelog"]}</a></div>
<div class="col"><h4>{t["support"]}</h4><a href="{pre}/faq">{t["faq_long"]}</a><a href="{pre}/feedback">{t["feedback"]}</a><a href="{GITHUB}/issues">{t["bugs"]}</a><a href="{pre}/privacy">{t["privacy"]}</a><a href="{pre}/acknowledgements">{t["ack"]}</a></div>
<div class="col"><h4>{t["source"]}</h4><a href="{GITHUB}">GitHub</a><a href="{GITHUB}/blob/main/LICENSE">{t["license"]}</a><a href="https://x.com/tdinh_me">@tdinh_me</a></div>
</div></footer>"""

HEAD = """<!doctype html><html lang="{lang}"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title}</title>
<meta name="description" content="{description}">
<link rel="icon" href="/assets/icon.png"><link rel="apple-touch-icon" href="/assets/icon.png">
<meta property="og:site_name" content="Vikey - Gõ tiếng Việt"><meta name="application-name" content="Vikey - Gõ tiếng Việt"><meta property="og:title" content="{title}"><meta property="og:description" content="{description}">
<meta property="og:image" content="{site}/assets/og.png"><meta property="og:image:width" content="1200"><meta property="og:image:height" content="630"><meta property="og:type" content="website"><meta property="og:url" content="{site}{path}">
<meta name="twitter:card" content="summary_large_image"><meta name="twitter:image" content="{site}/assets/og.png">
{canonical}
{alternates}
<link rel="stylesheet" href="/style.css?v={ver}">
</head><body{bodyclass}>"""

def release():
    p = os.path.join(ROOT, "release.json")
    return json.load(open(p)) if os.path.exists(p) else {}

PLATFORMS = ("macos", "windows", "linux", "ios", "android")
PNAME = dict(macos="macOS", windows="Windows", linux="Linux", ios="iOS", android="Android")
PREQ = {"vi": dict(macos="macOS 13 trở lên", windows="Windows 10 và 11", linux="Ubuntu, Debian, Fedora, Arch", ios="iPhone, iOS 17+", android="Điện thoại Android"),
        "en": dict(macos="macOS 13 or later", windows="Windows 10 and 11", linux="Ubuntu, Debian, Fedora, Arch", ios="iPhone, iOS 17+", android="Android phones")}
PTXT = {"vi": dict(dl="Tải về", store="App Store", soon="Sắp có", notify="Báo tôi khi có", arm="Bản cho chip ARM", play="Google Play"),
        "en": dict(dl="Download", store="App Store", soon="Soon", notify="Notify me", arm="ARM64 build", play="Google Play")}

def downloads(rel):
    """Where each platform's download lives, from release.json; None = no build yet ("pending" + notify form).
    Release lanes add their platform to release.json: windows {x64, arm64}, linux {deb_x64, rpm_x64, pacman_x64, deb_arm64, rpm_arm64, pacman_arm64}, ios {app_store_url},
    android {download_url}. The Mac lane owns the top-level keys."""
    win, lin, ios, andr = (rel.get(k) or {} for k in ("windows", "linux", "ios", "android"))
    d = dict(macos={"url": rel["download_url"]} if rel.get("download_url") else None,
             windows={"url": win["x64"]["download_url"], "arm": (win.get("arm64") or {}).get("download_url")} if (win.get("x64") or {}).get("download_url") else None,
             linux={"url": lin["deb_x64"]["download_url"], "rpm": (lin.get("rpm_x64") or {}).get("download_url"),
                    "pacman": (lin.get("pacman_x64") or {}).get("download_url"),
                    "deb_arm": (lin.get("deb_arm64") or {}).get("download_url"), "rpm_arm": (lin.get("rpm_arm64") or {}).get("download_url"),
                    "pacman_arm": (lin.get("pacman_arm64") or {}).get("download_url")}
                   if (lin.get("deb_x64") or {}).get("download_url") else None,
             ios={"url": ios["app_store_url"]} if ios.get("app_store_url") else None,
             android={"url": andr["download_url"]} if andr.get("download_url") else None)
    return d

def os_icon(p, cls="ic"):
    return f'<svg class="{cls}" aria-hidden="true"><use href="/assets/os.svg#{p}"/></svg>'

def platform_cards(lang, dl):
    t = PTXT[lang]; out = []
    for p in PLATFORMS:
        d = dl[p]
        if d:
            store = p == "ios"
            btn = f'<a class="btn dl" href="{d["url"]}"{"" if store else " download"}>{t["store"] if store else t["dl"]}</a>'
            if p == "linux":   # the package format matters more than the button: .deb, .rpm and Arch side by side
                btn = f'<div class="two three"><a class="btn dl" href="{d["url"]}" download>.deb</a>' + (f'<a class="btn dl" href="{d["rpm"]}" download>.rpm</a>' if d.get("rpm") else "") + (f'<a class="btn dl" href="{d["pacman"]}" download title="Arch Linux package">Arch</a>' if d.get("pacman") else "") + "</div>"
                arms = [f'<a href="{d[k]}" download>{n}</a>' for k, n in (("deb_arm", ".deb"), ("rpm_arm", ".rpm"), ("pacman_arm", "Arch")) if d.get(k)]
                if arms: btn += '<span class="alt">ARM64: ' + " · ".join(arms) + "</span>"
            if p == "windows" and d.get("arm"): btn += f'<a class="alt" href="{d["arm"]}" download>{t["arm"]}</a>'
            if p == "android": btn = f'<div class="two"><a class="btn dl" href="{d["url"]}" download>APK</a><a class="btn ghost soon" href="#android" data-notify="play" title="{t["notify"]}">{t["play"]}<small>{t["soon"]}</small></a></div>'
            badge = ""
            if p not in ("windows", "linux") or not (d.get("arm") or d.get("deb_arm")): btn += '<span class="alt none" aria-hidden="true">.</span>'
        else:
            btn = f'<a class="btn ghost dl" href="#{p}" data-notify="{p}">{t["notify"]}</a>'
            badge = f'<span class="badge">{t["soon"]}</span>'
            btn += '<span class="alt none" aria-hidden="true">.</span>'
        shot = f'<img class="pshot zoom" src="/assets/shots/dl-{p}.webp" width="1200" height="900" alt="Vikey {"trên" if lang == "vi" else "on"} {PNAME[p]}" loading="lazy">'
        out.append(f'<section class="pcard{"" if d else " pending"}" id="{p}" data-os="{p}">{shot}{badge}{os_icon(p, "picon")}'
                   f'<h2>{PNAME[p]}</h2><p>{PREQ[lang][p]}</p>{btn}</section>')
    return '<div class="pgrid">' + "".join(out) + "</div>"

def platform_row(lang, pre, dl):   # the small "available on" icon row on the home page
    t = PTXT[lang]
    return '<div class="plats">' + "".join(
        f'<a href="{pre}/download#{p}" title="{PNAME[p]}{"" if dl[p] else " · " + t["soon"]}"{"" if dl[p] else " class=pending"}>{os_icon(p)}<span>{PNAME[p]}</span></a>'
        for p in PLATFORMS) + "</div>"

def build():
    rel = release()
    if os.path.exists(DIST): shutil.rmtree(DIST)
    shutil.copytree(os.path.join(ROOT, "assets"), os.path.join(DIST, "assets"))
    if os.path.isdir(os.path.join(ROOT, "downloads")):
        shutil.copytree(os.path.join(ROOT, "downloads"), os.path.join(DIST, "downloads"))
    shutil.copy(os.path.join(ROOT, "style.css"), DIST)
    if rel: shutil.copy(os.path.join(ROOT, "release.json"), DIST)
    if os.path.exists(os.path.join(ROOT, "appcast.xml")): shutil.copy(os.path.join(ROOT, "appcast.xml"), DIST)   # Sparkle feed
    ver = str(int(os.path.getmtime(os.path.join(ROOT, "style.css"))))
    for lang, srcdir, pre in (("vi", PAGES, ""), ("en", PAGES_EN, "/en")):
      for name in sorted(os.listdir(srcdir)):
        if not name.endswith(".html"): continue
        src = open(os.path.join(srcdir, name), encoding="utf-8").read()
        meta = dict(re.findall(r"<!--\s*(\w+):\s*(.*?)\s*-->", "\n".join(src.split("\n")[:6])))
        body = re.sub(r"^(<!--.*?-->\s*)+", "", src, flags=re.S)
        slug = name[:-5]
        rel_path = "/" if slug == "index" else "/" + slug
        path = pre + rel_path if pre else rel_path
        vi_path, en_path = rel_path, "/en" + rel_path
        other = en_path if lang == "vi" else vi_path
        canonical = '<meta name="robots" content="noindex">' if meta.get("noindex") else f'<link rel="canonical" href="{SITE}{path}">'
        alternates = f'<link rel="alternate" hreflang="vi" href="{SITE}{vi_path}"><link rel="alternate" hreflang="en" href="{SITE}{en_path}"><link rel="alternate" hreflang="x-default" href="{SITE}{vi_path}">'
        one = meta.get("layout") == "one"
        page = HEAD.format(lang=lang, title=meta.get("title", "Vikey"), description=meta.get("description", ""), site=SITE, path=path, ver=ver,
                           canonical=canonical, alternates=alternates, bodyclass=' class="one-page"' if one else "")
        dl = downloads(rel)
        body = body.replace("<!-- PLATFORMS -->", platform_cards(lang, dl)).replace("<!-- PLATFORM_ROW -->", platform_row(lang, pre, dl))
        script = f'<script>window.VK_DL={json.dumps(dl, separators=(",", ":"))}</script><script src="/assets/platforms.js" defer></script>'
        page += nav(lang, pre, other) + body + ("" if one else footer(lang, pre)) + script + "</body></html>\n"
        win = rel.get("windows") or {}
        lin = rel.get("linux") or {}
        subst = {"VERSION": rel.get("version"), "LIN_VERSION": lin.get("version"),
                 "LIN_DEB_X64": (lin.get("deb_x64") or {}).get("download_url"), "LIN_DEB_ARM64": (lin.get("deb_arm64") or {}).get("download_url"),
                 "LIN_RPM_X64": (lin.get("rpm_x64") or {}).get("download_url"), "LIN_RPM_ARM64": (lin.get("rpm_arm64") or {}).get("download_url"), "DOWNLOAD_URL": rel.get("download_url"), "SHA256": rel.get("sha256"),
                 "WIN_VERSION": win.get("version"), "WIN_X64_URL": (win.get("x64") or {}).get("download_url"), "WIN_ARM64_URL": (win.get("arm64") or {}).get("download_url"),
                 "WIN_X64_ZIP": (win.get("x64") or {}).get("portable_url"), "WIN_ARM64_ZIP": (win.get("arm64") or {}).get("portable_url"),
                 "WIN_X64_SHA": (win.get("x64") or {}).get("sha256"), "WIN_ARM64_SHA": (win.get("arm64") or {}).get("sha256")}
        # a page that mentions the Windows build before it is released keeps its placeholders hidden
        if not win: page = re.sub(r'<!-- windows -->.*?<!-- /windows -->', "", page, flags=re.S)
        if not lin: page = re.sub(r'<!-- linux -->.*?<!-- /linux -->', "", page, flags=re.S)
        for k in sorted(subst, key=len, reverse=True):   # WIN_VERSION before VERSION
            if subst[k]: page = page.replace(k, subst[k])
        # cache-bust assets: /assets/x.png -> /assets/x.png?v=<mtime>, so browsers never show a stale screenshot
        def bust(m):
            f = os.path.join(ROOT, m.group(1).lstrip("/"))
            return m.group(0) if not os.path.exists(f) else f'{m.group(1)}?v={int(os.path.getmtime(f))}"'
        page = re.sub(r'(/assets/[\w./-]+\.(?:png|json|svg|jpg|js))"', bust, page)
        outdir = os.path.join(DIST, "en") if pre else DIST
        os.makedirs(outdir, exist_ok=True)
        out = os.path.join(outdir, "index.html" if slug == "index" else f"{slug}.html")
        open(out, "w", encoding="utf-8").write(page)
    open(os.path.join(DIST, "_redirects"), "w").write("/features /#features 301\n/en/features /en/#features 301\n")   # the features page moved onto the home page
    open(os.path.join(DIST, "_headers"), "w").write("/*\n  X-Content-Type-Options: nosniff\n  Referrer-Policy: strict-origin-when-cross-origin\n/downloads/*\n  Content-Disposition: attachment\n")
    open(os.path.join(DIST, "robots.txt"), "w").write(f"User-agent: *\nAllow: /\nSitemap: {SITE}/sitemap.xml\n")
    pages = [n[:-5] for n in os.listdir(PAGES) if n.endswith(".html") and n != "404.html"]
    urls = "".join(f"<url><loc>{SITE}{pre}{'/' if p == 'index' else '/' + p}</loc></url>" for pre in ("", "/en") for p in sorted(pages))
    open(os.path.join(DIST, "sitemap.xml"), "w").write(f'<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">{urls}</urlset>\n')
    print("built", len(os.listdir(DIST)), "entries into site/dist")

if __name__ == "__main__":
    build()
