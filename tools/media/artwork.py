#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Animated-artwork showcase, cut from a real OBS recording of Tonearm.

Nothing here synthesizes motion. The input is an OBS recording (2560x1440,
black canvas) of four Tonearm sources playing genuine Apple Music motion
artwork; this script only crops each card, masks its rounded corners, and
places it on the documentation stage. The recording itself is not kept in the
repository.

    python3 tools/media/artwork.py RECORDING.mp4 OUTDIR

Canvas positions of the four sources in the recording:
  glass-square       Glass, square,    760x760 at (60, 60)
  editorial-square   Editorial, square, 760x760 at (880, 60)
  editorial-vertical Editorial, vertical, 600x1000 at (1700, 60)
  glass-expanded     Glass, expanded,  1200x392 at (60, 920)
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import GROOVE, MUTED, PAPER, WIDE, encode_animation, font, stage  # noqa: E402

FPS = 30
LOOP = 6.0     # seconds per loop
FADE = 0.6     # crossfade that hides the loop seam
RADIUS = 33    # card corner radius in the recording, measured

# Visible card rectangles in the recording (inset by a pixel from the measured edge).
CARDS = {
    "glass-square": (105, 90, 774, 759),
    "editorial-square": (927, 91, 1592, 756),
    "editorial-vertical": (1744, 89, 2256, 1000),
    "glass-expanded": (103, 948, 1216, 1254),
}

# Where each album plays in the recording, and a clean stretch of its motion.
ALBUMS = {
    "sos": dict(title="SOS", artist="SZA", start=134.0),
    "wonder": dict(title="Wonder", artist="Shawn Mendes", start=101.2),
    "merry-christmas": dict(title="Merry Christmas", artist="Mariah Carey", start=60.0),
    "positions": dict(title="Positions", artist="Ariana Grande", start=20.0),
    "143": dict(title="143", artist="Katy Perry", start=114.0),
}


def extract(rec: Path, album: str, card: str, width: int, work: Path) -> list[Image.Image]:
    """Frames of one card for one album, scaled to `width`, with a seamless loop."""
    x0, y0, x1, y1 = CARDS[card]
    w, h = x1 - x0, y1 - y0
    height = round(h * width / w / 2) * 2
    out = work / f"{album}-{card}-{width}"
    out.mkdir(parents=True, exist_ok=True)
    total = LOOP + FADE
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", f"{ALBUMS[album]['start']:.3f}", "-t", f"{total:.3f}",
                    "-i", str(rec), "-an", "-vf",
                    f"crop={w}:{h}:{x0}:{y0},fps={FPS},scale={width}:{height}:flags=lanczos,format=rgb24",
                    str(out / "f%04d.png")], check=True)
    src = [Image.open(p).convert("RGB") for p in sorted(out.glob("f*.png"))]
    n, f = round(LOOP * FPS), round(FADE * FPS)
    if len(src) < n + f:
        raise SystemExit(f"{album}/{card}: only {len(src)} frames")
    frames = []
    for i in range(n):
        if i < f:
            a = (i + 1) / (f + 1)
            frames.append(Image.blend(src[n + i], src[i], a))
        else:
            frames.append(src[i])
    mask = card_mask(frames[0].size, RADIUS * width / w)
    for im in frames:
        im.putalpha(mask)
    return frames


def card_mask(size: tuple[int, int], radius: float) -> Image.Image:
    s = 4
    big = Image.new("L", (size[0] * s, size[1] * s), 0)
    ImageDraw.Draw(big).rounded_rectangle((0, 0, size[0] * s - 1, size[1] * s - 1), radius=radius * s, fill=255)
    return big.resize(size, Image.LANCZOS)


def shadow(size: tuple[int, int], radius: float, blur: int = 28, alpha: int = 120) -> tuple[Image.Image, int]:
    pad = blur * 2
    m = Image.new("L", (size[0] + pad * 2, size[1] + pad * 2), 0)
    ImageDraw.Draw(m).rounded_rectangle((pad, pad + blur // 3, pad + size[0], pad + size[1] + blur // 3),
                                        radius=radius, fill=alpha)
    return m.filter(ImageFilter.GaussianBlur(blur)), pad


def compose(bg: Image.Image, layers: list[tuple[list[Image.Image], tuple[int, int]]], out: Path,
            captions: list[tuple[str, str, tuple[int, int], str]] = ()) -> list[Path]:
    """Composite looping card layers over a static stage, frame by frame."""
    base = bg.copy()
    for frames, (x, y) in layers:
        sh, pad = shadow(frames[0].size, RADIUS * 0.8)
        dark = Image.new("RGBA", sh.size, (0, 0, 0, 0))
        dark.putalpha(sh)
        base.alpha_composite(dark, (x - pad, y - pad))
    d = ImageDraw.Draw(base)
    for title, artist, (x, y), anchor in captions:
        d.text((x, y), title, font=font("Inter-SemiBold.otf", 26), fill=PAPER, anchor=anchor)
        d.text((x, y + 34), artist, font=font("Inter-Regular.otf", 22), fill=MUTED, anchor=anchor)
    out.mkdir(parents=True, exist_ok=True)
    paths = []
    for i in range(len(layers[0][0])):
        im = base.copy()
        for frames, pos in layers:
            im.alpha_composite(frames[i], pos)
        p = out / f"c{i:04d}.png"
        im.save(p)
        paths.append(p)
    return paths


def flatten(paths: list[Path], out: Path) -> list[Path]:
    """Opaque copies for MP4, which has no alpha channel."""
    out.mkdir(parents=True, exist_ok=True)
    res = []
    for p in paths:
        im = Image.open(p)
        flat = Image.new("RGB", im.size, GROOVE)
        flat.paste(im, mask=im.getchannel("A"))
        q = out / p.name
        flat.save(q)
        res.append(q)
    return res


def gif_from_mp4(mp4: Path, gif: Path, width: int) -> None:
    """A lighter GIF fallback: 20 fps, ordered dither, so busy motion art stays small."""
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(mp4), "-vf",
                    f"fps=20,scale={width}:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=200:stats_mode=diff[p];"
                    "[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
                    "-loop", "0", str(gif)], check=True)


def collection(rec: Path, outdir: Path, work: Path) -> None:
    """All five albums at once, each in a different Tonearm look."""
    W, H = WIDE, 860
    sq = 400                       # square cards
    vw = 330                       # vertical cards
    vh = round(vw * 911 / 512)
    ew = sq * 2 + 40               # expanded card spans both squares
    gap = 40
    content = vw * 2 + sq * 2 + gap * 3 + 40
    x = (W - content) // 2
    mid_h = sq + gap + round(ew * 306 / 1113)
    ytop = (H - mid_h) // 2
    yv = (H - vh) // 2
    sos = extract(rec, "sos", "editorial-vertical", vw, work)
    mc = extract(rec, "merry-christmas", "glass-square", sq, work)
    pos = extract(rec, "positions", "editorial-square", sq, work)
    won = extract(rec, "wonder", "glass-expanded", ew, work)
    k143 = extract(rec, "143", "editorial-vertical", vw, work)
    xm = x + vw + gap + 20
    layers = [
        (sos, (x, yv)),
        (mc, (xm, ytop)),
        (pos, (xm + sq + gap, ytop)),
        (won, (xm, ytop + sq + gap)),
        (k143, (xm + sq * 2 + gap * 2 + 20, yv)),
    ]
    bg = stage((W, H), tint=(120, 132, 170), glow_at=(0.5, -0.1)).convert("RGBA")
    frames = compose(bg, layers, work / "collection")
    encode_animation(frames, out_webp=outdir / "artwork.webp", fps=FPS, webp_quality=65)
    encode_animation(flatten(frames, work / "collection-flat"), out_mp4=outdir / "artwork.mp4", fps=FPS)
    gif_from_mp4(outdir / "artwork.mp4", outdir / "artwork.gif", 800)
    shutil.copy(frames[90], outdir / "artwork-poster.png")


def album_clip(rec: Path, album: str, outdir: Path, work: Path) -> None:
    """One album: Editorial vertical beside Glass square, lit by its own artwork."""
    vw, sq, gap, mx, my = 300, 380, 40, 64, 56
    vh = round(vw * 911 / 512)
    W = vw + sq + gap + 2 * mx
    W += W % 2
    H = vh + 2 * my
    H += H % 2
    v = extract(rec, album, "editorial-vertical", vw, work)
    s = extract(rec, album, "glass-square", sq, work)
    arr = np.asarray(s[0].convert("RGB").resize((32, 32))).reshape(-1, 3)
    tint = tuple(int(c) for c in np.median(arr[arr.sum(1) > 120], axis=0)) if (arr.sum(1) > 120).any() else None
    bg = stage((W, H), tint=tint, glow_at=(0.3, -0.1)).convert("RGBA")
    frames = compose(bg, [(v, (mx, my)), (s, (mx + vw + gap, my))], work / f"clip-{album}")
    encode_animation(frames, out_webp=outdir / f"{album}.webp", fps=FPS, webp_quality=60)
    encode_animation(flatten(frames, work / f"clip-{album}-flat"), out_mp4=outdir / f"{album}.mp4", fps=FPS)


def main() -> None:
    rec, outdir = Path(sys.argv[1]), Path(sys.argv[2])
    only = sys.argv[3:] or ["collection", *ALBUMS]
    outdir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="tonearm-artwork-"))
    try:
        for job in only:
            if job == "collection":
                collection(rec, outdir, work)
            else:
                album_clip(rec, job, outdir, work)
            print("done", job, flush=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
