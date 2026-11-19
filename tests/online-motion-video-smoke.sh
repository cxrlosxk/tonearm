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

echo "Comparing original Kelly Clarkson video color tags across rendition codecs..."
python3 - <<'PY'
import json, re, subprocess
from urllib.parse import urlencode, urljoin
from urllib.request import Request, urlopen
req = Request("https://artwork.m8tec.top/api/v1/artwork/search?" + urlencode({"artist":"Kelly Clarkson","album":"Wrapped in Red"}), headers={"Accept":"application/json", "User-Agent":"Tonearm CI"})
with urlopen(req, timeout=20) as r:
    source=json.load(r)
url=source.get("url")
with urlopen(url, timeout=20) as r:
    master=r.read().decode()
rows=master.splitlines()
variants=[]
for index, row in enumerate(rows[:-1]):
    if not row.startswith("#EXT-X-STREAM-INF"):
        continue
    codec=re.search(r'CODECS="([^"]+)"',row)
    resolution=re.search(r'RESOLUTION=(\d+)x(\d+)',row)
    if not codec or not resolution:
        continue
    width=int(resolution.group(1))
    if (width==2160 and 'hvc1' in codec.group(1)) or (width==1080 and 'avc1' in codec.group(1)):
        variants.append((width,codec.group(1),urljoin(url,rows[index+1])))
done=set()
for w,codec,variant in sorted(variants,reverse=True):
    if w in done: continue
    done.add(w)
    try:
        probe=subprocess.run(["ffprobe","-v","error","-show_entries","stream=codec_name,pix_fmt,color_space,color_primaries,color_transfer,color_range,width,height:side_data=side_data_type","-of","json",variant],capture_output=True,text=True,timeout=45,check=True)
        details=json.loads(probe.stdout).get("streams",[])
        print(f"Remote Apple {w}p {codec} video tags: {details[:1]}")
    except Exception as exc:
        print("Variant color probe unavailable:", str(exc)[:160])
PY
