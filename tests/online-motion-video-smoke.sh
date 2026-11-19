#!/usr/bin/env bash
set -euo pipefail
# Diagnostic only: fetch a published artwork video via the third-party public API.
# Verify that it is genuine moving HLS content, not a static thumbnail.
url="$(python3 - <<'PY'
import json
from urllib.parse import urlencode, urlparse
from urllib.request import Request, urlopen

qs = urlencode({"artist": "Gorillaz", "album": "Plastic Beach"})
req = Request("https://artwork.m8tec.top/api/v1/artwork/search?" + qs, headers={"Accept": "application/json", "User-Agent": "Tonearm CI"})
with urlopen(req, timeout=20) as resp:
    data = json.load(resp)
url = data.get("url") or data.get("url_tall") or ""
host = (urlparse(url).hostname or "").lower()
if not url.startswith("https://") or not (host.endswith(".apple.com") or host.endswith(".mzstatic.com")):
    raise SystemExit("No valid Apple CDN HLS URL returned")
print(url)
PY
)"
workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
timeout 90 ffmpeg -nostdin -hide_banner -loglevel error -y -i "$url" -vf "fps=1,scale=160:-2" -frames:v 5 "$workdir/frame-%02d.png"
python3 - "$workdir" <<'PY'
import glob, hashlib, os, sys
frames = sorted(glob.glob(os.path.join(sys.argv[1], "frame-*.png")))
distinct = len({hashlib.sha256(open(p, "rb").read()).hexdigest() for p in frames})
print(f"Decoded video samples: total={len(frames)}, distinct={distinct}")
if len(frames) < 3 or distinct < 2:
    raise SystemExit("Retrieved HLS didn't yield moving frames")
PY
