// POST /api/crash, multipart: platform, version, os, report (one file, the crash report the user agreed to send)
// -> R2 vikey-feedback under crash/<platform>/<id>-<name>, one row in D1 (vikey-notify.crashes).
// Sent by the desktop apps on the launch after a crash, only after the user says yes. No typed text is in a report.
const PLATFORMS = ["macos", "windows", "linux"];
const MAX = 2 * 1024 * 1024;
const OK_EXT = /\.(ips|crash|txt|log|dmp|json)$/i;
const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });
const clip = (v, n) => String(v || "").trim().slice(0, n);

export async function onRequestPost({ request, env }) {
  let form;
  try { form = await request.formData(); } catch { return json({ ok: false, error: "bad_request" }, 400); }
  const platform = String(form.get("platform") || "");
  const f = form.get("report");
  if (!PLATFORMS.includes(platform) || !f || typeof f !== "object" || f.size === 0 || f.size > MAX || !OK_EXT.test(f.name))
    return json({ ok: false, error: "bad_report" }, 400);
  const version = clip(form.get("version"), 32), os = clip(form.get("os"), 120);
  const row = await env.DB.prepare("INSERT INTO crashes (platform, version, os, size) VALUES (?, ?, ?, ?)").bind(platform, version || null, os || null, f.size).run();
  const id = row.meta.last_row_id;
  const key = `crash/${platform}/${id}-${f.name.replace(/[^\w.\-]+/g, "_").slice(-80)}`;
  await env.FILES.put(key, f.stream(), { httpMetadata: { contentType: "text/plain; charset=utf-8" } });
  await env.DB.prepare("UPDATE crashes SET file = ? WHERE id = ?").bind(key, id).run();
  return json({ ok: true, id });
}
