# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared helpers for building Tonearm's documentation media.

Every overlay pixel comes from tools/tonearm-render, which drives the plugin's
own scene, rasterizer and compositor code. This module only arranges those
renders on a stage, adds captions and encodes the results.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "data" / "fonts"
RENDER = Path(os.environ.get("TONEARM_RENDER", ROOT / "build" / "tonearm-render"))

# Design tokens (see docs/media/DESIGN.md). Sizes are at 2x, the export density.
GROOVE = (24, 22, 29)       # stage base
LACQUER = (36, 33, 43)      # stage highlight
PAPER = (242, 238, 232)     # text on stage
MUTED = (164, 158, 172)     # secondary text on stage
RADIUS = 24                 # stage corner radius (12 px at 1x)
WIDE = 1760                 # width of full-column images (about 2.1x of GitHub's column)


def font(name: str, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(FONTS / name), size)


@dataclass
class Shot:
    preset: str
    layout: str
    track: int = 0
    scale: float = 1.0
    pos: float = 74.0
    status: str = "playing"
    at: float = 1.6
    margin: int = 40


def actual_margin(shot: Shot) -> int:
    """tonearm-render scales its integer --margin by --scale."""
    return int(round(shot.margin * shot.scale + 1e-9)) if shot.scale else shot.margin


def render_still(shot: Shot) -> Image.Image:
    """Render one overlay with transparent surroundings (margin keeps the shadow).

    `shot.margin` is in unscaled pixels; the returned image's margin is actual_margin(shot).
    """
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "s.png"
        subprocess.run(
            [str(RENDER), "still", "--preset", shot.preset, "--layout", shot.layout,
             "--track", str(shot.track), "--pos", str(shot.pos), "--status", shot.status,
             "--scale", str(shot.scale), "--margin", str(shot.margin), "--at", str(shot.at),
             "--bg", "none", "--out", str(out)],
            check=True, stdout=subprocess.DEVNULL)
        return Image.open(out).convert("RGBA").copy()


def render_frames(mode: str, shot: Shot, outdir: Path, *, to: int = 1, seconds: float = 2.4,
                  fps: int = 30) -> list[Path]:
    outdir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [str(RENDER), mode, "--preset", shot.preset, "--layout", shot.layout,
         "--track", str(shot.track), "--to", str(to), "--pos", str(shot.pos),
         "--scale", str(shot.scale), "--margin", str(shot.margin), "--fps", str(fps),
         "--seconds", str(seconds), "--bg", "none", "--outdir", str(outdir)],
        check=True, stdout=subprocess.DEVNULL)
    return sorted(outdir.glob("f*.png"))


def rounded_mask(size: tuple[int, int], radius: int) -> Image.Image:
    w, h = size
    big = Image.new("L", (w * 4, h * 4), 0)
    ImageDraw.Draw(big).rounded_rectangle((0, 0, w * 4 - 1, h * 4 - 1), radius * 4, fill=255)
    return big.resize((w, h), Image.LANCZOS)


def stage(size: tuple[int, int], tint: tuple[int, int, int] | None = None, *,
          glow_at: tuple[float, float] = (0.78, 0.0), grooves: bool = True,
          radius: int = RADIUS) -> Image.Image:
    """The neutral stage every overlay is shown on: graphite with a soft sheen.

    `tint` is a color taken from the artwork shown on the stage, so each image is
    lit by the record it shows. `grooves` adds the faint concentric rings that are
    the only literal nod to a record player.
    """
    w, h = size
    img = Image.new("RGB", size, GROOVE)
    # Vertical falloff from lacquer to groove.
    grad = Image.linear_gradient("L").resize((w, h))
    img = Image.composite(Image.new("RGB", size, GROOVE), Image.new("RGB", size, LACQUER), grad)
    # Sheen from one corner, tinted by the artwork when given.
    sheen_color = tint or (255, 255, 255)
    sheen = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(sheen)
    cx, cy = glow_at[0] * w, glow_at[1] * h
    r = max(w, h) * 0.75
    steps = 60
    for i in range(steps, 0, -1):
        rr = r * i / steps
        v = int(255 * (1 - i / steps) ** 2.2)
        d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=v)
    sheen = sheen.filter(ImageFilter.GaussianBlur(24))
    strength = 0.22 if tint else 0.06
    img = Image.composite(Image.new("RGB", size, sheen_color), img,
                          sheen.point(lambda v: int(v * strength)))
    if grooves:
        # A few wide, faint rings centred beyond the bottom-right corner.
        rings = Image.new("L", (w * 2, h * 2), 0)
        rd = ImageDraw.Draw(rings)
        gx, gy = w * 2 * 1.08, h * 2 * 1.25
        for i in range(1, 60):
            rr = 220 + i * 46
            rd.ellipse((gx - rr, gy - rr, gx + rr, gy + rr), outline=255, width=3)
        rings = rings.resize((w, h), Image.LANCZOS)
        # Fade the rings out towards the upper left so they read as light, not pattern.
        fade = Image.new("L", (w, h), 0)
        fd = ImageDraw.Draw(fade)
        fx, fy = w * 1.08, h * 1.25
        R = max(w, h) * 1.1
        for i in range(40, 0, -1):
            rr = R * i / 40
            fd.ellipse((fx - rr, fy - rr, fx + rr, fy + rr), fill=int(255 * (1 - i / 40)))
        fade = fade.filter(ImageFilter.GaussianBlur(30))
        rings = ImageChops.multiply(rings, fade)
        img = Image.composite(Image.new("RGB", size, (255, 255, 255)), img, rings.point(lambda v: int(v * 0.07)))
    # Fine grain so gradients never band after encoding.
    noise = Image.effect_noise(size, 10).point(lambda v: int((v - 128) * 0.25 + 128))
    img = ImageChops.add(img, Image.merge("RGB", (noise, noise, noise)), 1.0, -128)
    out = img.convert("RGBA")
    out.putalpha(rounded_mask(size, radius))
    return out


def place(canvas: Image.Image, shot_img: Image.Image, x: int, y: int, shot: Shot) -> None:
    """Paste a render so that its card (not its margin) lands at x, y."""
    m = actual_margin(shot)
    canvas.alpha_composite(shot_img, (int(round(x)) - m, int(round(y)) - m))


def dominant(img: Image.Image) -> tuple[int, int, int]:
    small = img.convert("RGB").resize((32, 32))
    q = small.quantize(6)
    pal = q.getpalette()
    counts = sorted(q.getcolors(), reverse=True)
    for _, idx in counts:
        r, g, b = pal[idx * 3: idx * 3 + 3]
        if max(r, g, b) - min(r, g, b) > 40 and max(r, g, b) > 70:
            return (r, g, b)
    r, g, b = pal[counts[0][1] * 3: counts[0][1] * 3 + 3]
    return (r, g, b)


def save_webp(img: Image.Image, path: Path, quality: int = 90) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path, "WEBP", quality=quality, method=6)


def save_png(img: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path, "PNG", optimize=True)


def encode_animation(frames: list[Path], out_webp: Path | None = None, out_gif: Path | None = None,
                     out_mp4: Path | None = None, fps: int = 30, gif_width: int | None = None,
                     webp_quality: int = 80) -> None:
    """Encode a frame sequence. WebP for the README, GIF fallback, MP4 for download."""
    tmp = Path(tempfile.mkdtemp())
    try:
        for i, f in enumerate(frames):
            os.symlink(f, tmp / f"f{i:05d}.png")
        pattern = str(tmp / "f%05d.png")
        if out_webp:
            out_webp.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps), "-i", pattern,
                            "-c:v", "libwebp_anim", "-lossless", "0", "-quality", str(webp_quality),
                            "-compression_level", "6", "-loop", "0", "-pix_fmt", "yuva420p", str(out_webp)],
                           check=True)
        if out_gif:
            out_gif.parent.mkdir(parents=True, exist_ok=True)
            scale = f"scale={gif_width}:-1:flags=lanczos," if gif_width else ""
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps), "-i", pattern,
                            "-vf", f"{scale}split[a][b];[a]palettegen=max_colors=256:stats_mode=diff[p];"
                                   f"[b][p]paletteuse=dither=sierra2_4a:diff_mode=rectangle",
                            "-loop", "0", str(out_gif)], check=True)
        if out_mp4:
            out_mp4.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps), "-i", pattern,
                            "-vf", "format=yuv420p", "-c:v", "libx264", "-preset", "slow", "-crf", "18",
                            "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709",
                            "-movflags", "+faststart", str(out_mp4)], check=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def card_box(img: Image.Image, threshold: int = 200) -> tuple[int, int, int, int]:
    """Bounding box of the card itself (opaque pixels), excluding soft shadow and glow."""
    a = img.getchannel("A").point(lambda v: 255 if v >= threshold else 0)
    return a.getbbox()


def place_card(canvas: Image.Image, shot_img: Image.Image, x: float, y: float) -> tuple[int, int, int, int]:
    """Paste a render so the visible card's top-left lands at x, y. Returns the card box on canvas."""
    l, t, r, b = card_box(shot_img)
    ox, oy = int(round(x)) - l, int(round(y)) - t
    canvas.alpha_composite(shot_img, (ox, oy))
    return (ox + l, oy + t, ox + r, oy + b)


_size_cache: dict = {}


def card_size(preset: str, layout: str) -> tuple[float, float]:
    """Visible card size at scale 1."""
    key = (preset, layout)
    if key not in _size_cache:
        l, t, r, b = card_box(render_still(Shot(preset, layout, 0, scale=1.0)))
        _size_cache[key] = (r - l, b - t)
    return _size_cache[key]


SEQUENCE = Path(os.environ.get("TONEARM_SEQUENCE", RENDER.parent / "tonearm-sequence"))


def run_sequence(script: str, outdir: Path) -> list[Path]:
    """Run tonearm-sequence with an inline script; returns the frame paths in order."""
    if outdir.exists():
        shutil.rmtree(outdir)
    outdir.mkdir(parents=True)
    script_path = outdir / "script.txt"
    script_path.write_text(script.replace("{OUT}", str(outdir / "raw")))
    subprocess.run([str(SEQUENCE), str(script_path)], check=True, stdout=subprocess.DEVNULL)
    return sorted((outdir / "raw").glob("f*.png"))


def composite_frames(background: Image.Image, layers: list[tuple[list[Path], tuple[int, int]]],
                     outdir: Path, foreground: Image.Image | None = None) -> list[Path]:
    """Lay animated frame sequences over a static background. Each layer is (frames, offset)."""
    outdir.mkdir(parents=True, exist_ok=True)
    n = max(len(f) for f, _ in layers)
    out = []
    for i in range(n):
        img = background.copy()
        for frames, off in layers:
            fr = Image.open(frames[min(i, len(frames) - 1)]).convert("RGBA")
            img.alpha_composite(fr, off)
        if foreground is not None:
            img.alpha_composite(foreground)
        # Keep the stage's rounded corners: re-apply the background's alpha.
        img.putalpha(ImageChops.multiply(img.getchannel("A"), background.getchannel("A")))
        p = outdir / f"c{i:05d}.png"
        img.save(p, compress_level=1)
        out.append(p)
    return out
