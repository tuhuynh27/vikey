#!/usr/bin/env python3
"""The PR report comment (.github/workflows/pr-report.yml): the engine's test and simulation metrics for the PR's
commit $SHA, each next to main's value. Logs come from the Engine rules runs (check-log, ios-log artifacts); main's
latest run is the baseline. Everything read from a log is untrusted (the PR's code produced it): only numbers that
match the expected lines are used. DRY=1 prints instead of posting."""
import io, json, os, re, sys, urllib.error, urllib.request, zipfile

REPO, SHA, TOKEN = os.environ["REPO"], os.environ["SHA"], os.environ["GH_TOKEN"]
MARK = "<!-- vikey-pr-report -->"
API = "https://api.github.com"
H = {"Authorization": f"Bearer {TOKEN}", "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}

def api(path, method="GET", body=None):
    req = urllib.request.Request(path if path.startswith("http") else API + path, method=method, headers=H,
                                 data=json.dumps(body).encode() if body is not None else None)
    with urllib.request.urlopen(req) as r:
        d = r.read(); return json.loads(d) if d else None

class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **k): return None

def download(url):
    """Artifact zips redirect to blob storage, which rejects the GitHub token: follow the redirect without it."""
    try:
        with urllib.request.build_opener(_NoRedirect).open(urllib.request.Request(url, headers=H)) as r: return r.read()
    except urllib.error.HTTPError as e:
        if e.code not in (301, 302, 303, 307, 308): raise
        with urllib.request.urlopen(e.headers["Location"]) as r: return r.read()

def logs(run):
    """{artifact name: text} of an Engine rules run."""
    out = {}
    for a in api(f'/repos/{REPO}/actions/runs/{run["id"]}/artifacts')["artifacts"]:
        if a["name"] in ("check-log", "ios-log") and not a["expired"]:
            z = zipfile.ZipFile(io.BytesIO(download(a["archive_download_url"])))
            out[a["name"]] = z.read(z.namelist()[0]).decode("utf-8", "replace")[:400_000]
    return out

def num(pat, text, cast=float, group=1):
    m = re.search(pat, text or "", re.M); return cast(m.group(group)) if m else None

def metrics(L):
    """(label, value, unit, decimals) for every metric found. Values are numbers only."""
    c, i = L.get("check-log", ""), L.get("ios-log", "")
    M = []
    def add(label, v, unit="", d=0):
        if v is not None: M.append((label, v, unit, d))
    rp, rt = num(r"^rules: (\d+)/(\d+) pass", c, int), num(r"^rules: (\d+)/(\d+) pass", c, int, 2)
    if rt: M.append(("Rule cases passing", rp, f"/{rt}", 0))
    bp, bt = num(r"^reported bugs fixed: (\d+)/(\d+)", c, int), num(r"^reported bugs fixed: (\d+)/(\d+)", c, int, 2)
    if bt: M.append(("Reported bugs fixed (rules/reported.tsv)", bp, f"/{bt}", 0))
    add("Manual switches, chat corpus", num(r"^metrics chat: manual switches \d+ -> (\d+)", c, int))
    add("Wrong words, chat corpus", num(r"^metrics chat: .*wrong words \d+ -> (\d+)", c, int))
    add("Manual switches, chat with typos", num(r"^metrics chat_typos: manual switches \d+ -> (\d+)", c, int))
    add("Wrong words, chat with typos", num(r"^metrics chat_typos: .*wrong words \d+ -> (\d+)", c, int))
    add("Manual switches, synthetic corpus", num(r"^metrics synthetic: manual switches \d+ -> (\d+)", c, int))
    add("Wrong words, synthetic corpus", num(r"^metrics synthetic: .*wrong words \d+ -> (\d+)", c, int))
    rt_line = re.search(r"^(?:FAIL )?realistic typing:? \w* ?\([^:]*: (\d+) broken; needed fixing by hand: ([^)]*)\)", c, re.M)
    if rt_line:
        add("Realistic typing, text broken", int(rt_line.group(1)))
        add("Realistic typing, fixes needed by hand", sum(int(a) for a in re.findall(r"(\d+)/\d+", rt_line.group(2))))
    add("Fat-finger, wrong words", num(r"^fat-finger: \w+ \(wrong words [\d.]+% -> ([\d.]+)%", c), "%", 2)
    add("Auto-correct, clean typing wrong", num(r"^auto-correct: \w+ \(clean typing ([\d.]+)% wrong", c), "%", 2)
    add("Auto-correct, sloppy typing wrong", num(r"^auto-correct: .*sloppy [\d.]+% -> ([\d.]+)%", c), "%", 2)
    tests = [(int(a), int(b)) for a, b in re.findall(r"Executed (\d+) tests?, with (?:\d+ tests? skipped and )?(\d+) failures?", i)]
    if tests:
        n, f = max(tests); add("iOS tests failing", f, f"/{n}")
    return M

def fmt(v, d): return f"{v:.{d}f}" if d else f"{int(v)}"

# the PR (from the API, never from an artifact) and the runs to compare
prs = [p for page in (1, 2, 3) for p in api(f"/repos/{REPO}/pulls?state=open&per_page=100&page={page}")]
pr = next((p for p in prs if p["head"]["sha"] == SHA), None)
if not pr: print("no open PR at", SHA); sys.exit(0)
# a PR's run comes from pull_request, or for a fork from a maintainer's approval (pull_request_review)
runs = [r for ev in ("pull_request", "pull_request_review")
        for r in api(f"/repos/{REPO}/actions/runs?head_sha={SHA}&event={ev}&per_page=30")["workflow_runs"]
        if r["name"] == "Engine rules" and r["conclusion"] not in ("action_required", "skipped")]
run = max(runs, key=lambda r: r["run_number"]) if runs else None
if not run or run["status"] != "completed" or run["conclusion"] == "action_required": print("engine run not done"); sys.exit(0)
mains = [r for r in api(f"/repos/{REPO}/actions/runs?branch=main&event=push&status=completed&per_page=20")["workflow_runs"] if r["name"] == "Engine rules"]
base, base_sha = {}, None
for r in mains:   # newest first; the first one that kept its logs
    base = logs(r)
    if base: base_sha = r["head_sha"]; break

pm, bm = metrics(logs(run)), {k: (v, u) for k, v, u, d in metrics(base)}   # label -> (value, its own unit)
lines = [MARK, f"**Vikey engine metrics** · `{SHA[:7]}` vs main" + (f" `{base_sha[:7]}`" if base_sha else ""), "",
         "| Metric | This PR | main | Change |", "|---|--:|--:|--:|"]
for label, v, unit, d in pm:
    if label in bm:
        b, bunit = bm[label]
        if v == b and unit == bunit: change = "="
        elif unit != bunit: change = "**changed**"
        else: change = f"**{'+' if v > b else ''}{fmt(v - b, d)}**"
        cur = f"**{fmt(v, d)}{unit}**" if change != "=" else f"{fmt(v, d)}{unit}"
        lines.append(f"| {label} | {cur} | {fmt(b, d)}{bunit} | {change} |")
    else: lines.append(f"| {label} | {fmt(v, d)}{unit} | | |")
if len(pm) == 0: lines.append("| No metrics in this run's log (the engine checks may have failed early) | | | |")
text = "\n".join(lines)

if os.environ.get("DRY"): print(text); sys.exit(0)
num_ = pr["number"]
mine = [c for page in (1, 2, 3) for c in api(f"/repos/{REPO}/issues/{num_}/comments?per_page=100&page={page}")
        if c["user"]["login"] == "github-actions[bot]" and MARK in (c["body"] or "")]
if mine: api(f'/repos/{REPO}/issues/comments/{mine[0]["id"]}', "PATCH", {"body": text}); print("updated #", num_)
else: api(f"/repos/{REPO}/issues/{num_}/comments", "POST", {"body": text}); print("posted #", num_)
