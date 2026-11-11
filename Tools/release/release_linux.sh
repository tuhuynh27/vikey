#!/bin/zsh
# Publishes the Linux packages of a release the Mac lane already made:
#   Tools/release/release.sh --linux 1.0.14
# The tag v<version> (or a manual run of the linux workflow with release_tag) puts vikey_<ver>_amd64.deb,
# vikey_<ver>_arm64.deb, the .rpm files and the Arch .pkg.tar.zst files on the GitHub release. This script
# waits for them, copies them to vikey.app/downloads, writes release.json "linux" and deploys the site.
set -e
VER=${1:?usage: release.sh --linux <version>}
cd "$(dirname "$0")/../.."
# the version must be the one main is at: the packages on the release were built from it, and the prune below keeps only $VER files
PV=$(grep MARKETING_VERSION project.yml | grep -o '"[^"]*"' | tr -d '"'); [ "$PV" = "$VER" ] || { echo "project.yml is at $PV, not $VER"; exit 1; }
REPO=tonyminibot/Vikey
echo "==> waiting for the Linux packages on v$VER"
for i in $(seq 1 60); do
  n=$(gh release view "v$VER" -R $REPO --json assets -q '[.assets[].name | select(endswith(".deb") or endswith(".rpm") or endswith(".pkg.tar.zst"))] | length' 2>/dev/null || echo 0)
  [ "$n" -ge 6 ] && break; sleep 30
done
[ "$n" -ge 6 ] || { echo "packages not on the release after 30 min: gh run list -R $REPO -w linux"; exit 1; }
rm -rf dist/linux && mkdir -p dist/linux site/downloads
gh release download "v$VER" -R $REPO -p "*.deb" -p "*.rpm" -p "*.pkg.tar.zst" -D dist/linux --clobber
MACDMG=$(python3 -c "import json;print(json.load(open('site/release.json'))['download_url'].split('/')[-1])")
[ -f "site/downloads/$MACDMG" ] || gh release download "v$(echo "$MACDMG" | sed -E 's/^Vikey-(.*)\.dmg$/\1/')" -R $REPO -p "$MACDMG" -D site/downloads --clobber
WIN=$(python3 -c "import json;d=json.load(open('site/release.json')).get('windows',{});print(' '.join(v['download_url'].split('/')[-1] for k,v in d.items() if isinstance(v,dict)))")
for w in ${=WIN}; do [ -f "site/downloads/$w" ] || gh release download "v$VER" -R $REPO -p "$w" -D site/downloads --clobber || true; done
cp dist/linux/*.deb dist/linux/*.rpm dist/linux/*.pkg.tar.zst site/downloads/
python3 - "$VER" <<'PY'
import json, hashlib, sys, glob, os
ver = sys.argv[1]; p = "site/release.json"; rel = json.load(open(p))
sha = lambda f: hashlib.sha256(open(f, "rb").read()).hexdigest()
lin = {"version": ver}
for f in sorted(glob.glob(f"dist/linux/*")):
    n = os.path.basename(f)
    kind = "deb" if n.endswith(".deb") else "rpm" if n.endswith(".rpm") else "pacman"
    arch = "arm64" if ("arm64" in n or "aarch64" in n) else "x64"
    lin[f"{kind}_{arch}"] = {"download_url": f"/downloads/{n}", "sha256": sha(f)}
rel["linux"] = lin
json.dump(rel, open(p, "w"), indent=0); open(p, "a").write("\n")
PY
setopt null_glob; for old in site/downloads/vikey*.deb site/downloads/vikey*.rpm site/downloads/vikey*.pkg.tar.zst; do case "$old" in *"$VER"*) ;; *) rm "$old";; esac; done
git add site/release.json && git commit -q -m "Release $VER (Linux)" && git push -q origin main
Tools/release/deploy_site.sh
echo "==> done: https://vikey.app/download"
