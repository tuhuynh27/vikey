// POST /api/feedback, multipart (message, email?, kind, platform, version, lang, token, files…) or JSON without files
// -> one row in D1 (vikey-notify.feedback); attached files go to R2 (vikey-feedback) under feedback/<id>/, their keys
// in the row's `attachments` column (JSON list).
const KINDS = ["bug", "idea", "other"];
const PLATFORMS = ["macos", "windows", "linux", "ios", "android", "web"];
const MAX_FILES = 5, MAX_FILE = 25 * 1024 * 1024, MAX_TOTAL = 50 * 1024 * 1024;
const OK_EXT = /\.(png|jpe?g|gif|webp|heic|heif|mp4|mov|m4v|webm|txt|log|ips|crash|json|zip|pdf)$/i;
// Content-Type from the checked extension, never the client's f.type (an HTML type would render as a page)
const TYPES = { png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp",
  heic: "image/heic", heif: "image/heif", mp4: "video/mp4", mov: "video/quicktime", m4v: "video/x-m4v",
  webm: "video/webm", zip: "application/zip", pdf: "application/pdf" };
const typeOf = name => TYPES[name.split(".").pop().toLowerCase()] || "text/plain; charset=utf-8";
const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });
const clip = (v, n) => String(v || "").trim().slice(0, n);

export async function onRequestPost({ request, env }) {
  let d, files = [];
  try {
    if ((request.headers.get("content-type") || "").includes("multipart/form-data")) {
      const form = await request.formData();
      d = Object.fromEntries([...form.entries()].filter(([, v]) => typeof v === "string"));
      files = form.getAll("files").filter(f => typeof f === "object" && f.size > 0);
    } else d = await request.json();
  } catch { return json({ ok: false, error: "bad_request" }, 400); }
  if (d.website) return json({ ok: true });   // honeypot
  const message = clip(d.message, 5000);
  if (message.length < 3) return json({ ok: false, error: "message" }, 400);
  const email = clip(d.email, 254).toLowerCase();
  if (email && !/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(email)) return json({ ok: false, error: "email" }, 400);
  if (files.length > MAX_FILES || files.some(f => f.size > MAX_FILE || !OK_EXT.test(f.name)) ||
      files.reduce((n, f) => n + f.size, 0) > MAX_TOTAL) return json({ ok: false, error: "files" }, 400);
  const kind = KINDS.includes(d.kind) ? d.kind : "other";
  const platform = PLATFORMS.includes(d.platform) ? d.platform : "web";
  const v = await fetch("https://challenges.cloudflare.com/turnstile/v0/siteverify", { method: "POST",
    body: new URLSearchParams({ secret: env.TURNSTILE_SECRET, response: String(d.token || ""), remoteip: request.headers.get("CF-Connecting-IP") || "" }) });
  if (!(await v.json()).success) return json({ ok: false, error: "captcha" }, 403);
  const row = await env.DB.prepare("INSERT INTO feedback (message, email, kind, platform, version, lang, ua) VALUES (?, ?, ?, ?, ?, ?, ?)")
    .bind(message, email || null, kind, platform, clip(d.version, 32) || null, d.lang === "en" ? "en" : "vi", clip(request.headers.get("user-agent"), 300)).run();
  if (files.length) {
    const id = row.meta.last_row_id, keys = [];
    for (const [i, f] of files.entries()) {
      const key = `feedback/${id}/${i + 1}-${f.name.replace(/[^\w.\-]+/g, "_").slice(-80)}`;
      await env.FILES.put(key, f.stream(), { httpMetadata: { contentType: typeOf(f.name), contentDisposition: "attachment" } });
      keys.push(key);
    }
    await env.DB.prepare("UPDATE feedback SET attachments = ? WHERE id = ?").bind(JSON.stringify(keys), id).run();
  }
  return json({ ok: true });
}
