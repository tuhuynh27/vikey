// The feedback form: the app opens /feedback?platform=macos&v=1.0.24; Turnstile against spam; POST /api/feedback.
(() => {
  const f = document.getElementById("fb"); if (!f) return;
  const en = document.documentElement.lang === "en";
  const q = new URLSearchParams(location.search);
  const ua = navigator.userAgent;
  const guess = /iPhone|iPad/.test(ua) ? "ios" : /Android/.test(ua) ? "android" : /Windows/.test(ua) ? "windows" : /Mac OS X/.test(ua) ? "macos" : /Linux|X11/.test(ua) ? "linux" : "web";
  const platform = q.get("platform") || guess, version = q.get("v") || "";
  const NAME = { macos: "macOS", windows: "Windows", linux: "Linux", ios: "iOS", android: "Android", web: "Web" };
  f.querySelector(".meta").textContent = (en ? "Sent with: " : "Gửi kèm: ") + "Vikey " + (version ? version + " " : "") + (en ? "for " : "cho ") + (NAME[platform] || platform);
  const err = f.querySelector(".err"), btn = f.querySelector("[type=submit]");
  // attachments: picked or dropped files, listed with a remove button; checked here and again by the server
  const MAX_FILES = 5, MAX_FILE = 25 * 1024 * 1024, OK = /\.(png|jpe?g|gif|webp|heic|heif|mp4|mov|m4v|webm|txt|log|ips|crash|json|zip|pdf)$/i;
  const input = f.querySelector(".fbfiles input"), list = f.querySelector(".flist"), drop = f.querySelector(".drop");
  let files = [];
  const size = n => n < 1048576 ? Math.max(1, Math.round(n / 1024)) + " KB" : (n / 1048576).toFixed(1) + " MB";
  const show = () => {
    list.innerHTML = "";
    files.forEach((x, i) => {
      const li = document.createElement("li");
      li.innerHTML = '<span class="n"></span><span class="s"></span><button type="button" aria-label="' + (en ? "Remove" : "Bỏ") + '">×</button>';
      li.querySelector(".n").textContent = x.name; li.querySelector(".s").textContent = size(x.size);
      li.querySelector("button").onclick = () => { files.splice(i, 1); show(); };
      list.append(li);
    });
  };
  const add = picked => {
    err.textContent = "";
    for (const x of picked) {
      if (files.length >= MAX_FILES) { err.textContent = en ? "Up to 5 files." : "Tối đa 5 tệp."; break; }
      if (!OK.test(x.name)) { err.textContent = (en ? "This kind of file can't be attached: " : "Không gửi được loại tệp này: ") + x.name; continue; }
      if (x.size > MAX_FILE) { err.textContent = (en ? "Over 25 MB: " : "Lớn hơn 25 MB: ") + x.name; continue; }
      files.push(x);
    }
    show();
  };
  input.onchange = () => { add([...input.files]); input.value = ""; };
  ["dragenter", "dragover"].forEach(ev => drop.addEventListener(ev, e => { e.preventDefault(); drop.classList.add("over"); }));
  ["dragleave", "drop"].forEach(ev => drop.addEventListener(ev, e => { e.preventDefault(); drop.classList.remove("over"); }));
  drop.addEventListener("drop", e => add([...e.dataTransfer.files]));
  let token = "", wid;
  window.vkFb = () => { wid = window.turnstile.render(f.querySelector(".ts"), { sitekey: "0x4AAAAAAFqrh-eE-Wh5vzx8", language: en ? "en" : "vi", size: "flexible",
    callback: t => { token = t; }, "expired-callback": () => { token = ""; } }); };
  const s = document.createElement("script"); s.src = "https://challenges.cloudflare.com/turnstile/v0/api.js?onload=vkFb&render=explicit"; s.async = true; document.head.append(s);
  f.onsubmit = async e => {
    e.preventDefault(); err.textContent = "";
    const message = f.message.value.trim(), email = f.email.value.trim();
    if (message.length < 3) { err.textContent = en ? "Please write a few words." : "Bạn viết vài chữ nhé."; f.message.focus(); return; }
    if (email && !/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(email)) { err.textContent = en ? "Please check your email address." : "Email chưa đúng, bạn kiểm tra lại nhé."; f.email.focus(); return; }
    if (!token) { err.textContent = en ? "Please complete the check above." : "Bạn hoàn tất bước xác minh ở trên nhé."; return; }
    btn.disabled = true;
    try {
      const body = new FormData();
      Object.entries({ message, email, kind: f.kind.value, platform, version, lang: en ? "en" : "vi", website: f.website.value, token }).forEach(([k, v]) => body.append(k, v));
      files.forEach(x => body.append("files", x, x.name));
      const r = await fetch("/api/feedback", { method: "POST", body });
      if (!r.ok) throw new Error(r.status);
      f.hidden = true; document.querySelector(".fbdone").hidden = false; window.scrollTo(0, 0);
    } catch { err.textContent = en ? "Something went wrong. Please try again." : "Có lỗi, bạn thử lại nhé."; btn.disabled = false; token = ""; if (wid !== undefined) window.turnstile.reset(wid); }
  };
})();
