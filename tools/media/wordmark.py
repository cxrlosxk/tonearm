# SPDX-License-Identifier: GPL-2.0-or-later
"""Builds the Tonearm wordmark as outlined SVG (no font dependency on GitHub).

The word is set in EB Garamond 12 (bundled, SIL OFL 1.1) and converted to paths.
The mark is a record seen from above with the arm resting in the outer groove.
"""
import sys
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "media" / "brand"

font = TTFont(str(ROOT / "data" / "fonts" / "EBGaramond12-Regular.otf"))
gs = font.getGlyphSet()
cmap = font.getBestCmap()
upm = font["head"].unitsPerEm
hmtx = font["hmtx"]

TEXT = "tonearm"
SIZE = 96.0                  # cap size in SVG units
TRACK = -0.012 * upm         # slight negative tracking for display size
scale = SIZE / upm
ascent = 0.72 * upm          # baseline placement inside the box

pen = SVGPathPen(gs)
x = 0.0
for ch in TEXT:
    g = cmap[ord(ch)]
    tp = TransformPen(pen, (scale, 0, 0, -scale, x * scale, ascent * scale))
    gs[g].draw(tp)
    x += hmtx[g][0] + TRACK
word_path = pen.getCommands()
word_w = (x - TRACK) * scale
xh = 0.43 * upm * scale      # approximate x-height of EB Garamond

def svg(color: str, mark: str) -> str:
    # Mark geometry: the record sits on the baseline, its height a little over x-height.
    r = 34.0
    cx, cy = r + 2, ascent * scale - r + 0.5
    gap = 24.0
    tx = cx + r + gap
    import math
    px, py = cx + 1.22 * r, cy - 1.18 * r          # pivot, outside the record
    ex, ey = cx + 0.30 * r, cy - 0.70 * r          # stylus, resting in the outer grooves
    ang = math.atan2(ey - py, ex - px)
    hx, hy = ex + math.cos(ang + 0.9) * 9, ey + math.sin(ang + 0.9) * 9   # short headshell
    arm = (
        f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="none" stroke="{mark}" stroke-width="3"/>'
        f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r*0.62:.1f}" fill="none" stroke="{mark}" stroke-width="1.2" opacity=".45"/>'
        f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="3" fill="{mark}"/>'
        f'<path d="M{px:.1f} {py:.1f} L{ex:.1f} {ey:.1f} L{hx:.1f} {hy:.1f}" stroke="{mark}" stroke-width="3" '
        f'stroke-linecap="round" stroke-linejoin="round" fill="none"/>'
        f'<circle cx="{px:.1f}" cy="{py:.1f}" r="5" fill="{mark}"/>'
    )
    top = cy - r * 1.18 - 8
    height = ascent * scale + 0.24 * upm * scale - top
    width = tx + word_w + 2
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 {top:.1f} {width:.1f} {height:.1f}" '
        f'width="{width/1.0:.0f}" height="{height/1.0:.0f}" role="img" aria-labelledby="t">'
        f'<title id="t">Tonearm</title>'
        f'{arm}<path transform="translate({tx:.1f} 0)" fill="{color}" d="{word_path}"/></svg>\n'
    )

OUT.mkdir(parents=True, exist_ok=True)
(OUT / "wordmark-light.svg").write_text(svg("#1f1d24", "#1f1d24"))   # for GitHub light theme
(OUT / "wordmark-dark.svg").write_text(svg("#ece8f0", "#ece8f0"))    # for GitHub dark theme
print("ok", round(word_w), round(xh))
