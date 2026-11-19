#!/usr/bin/env python3
"""Resolve genuine animated album artwork via an installed CoverPy package.

Intended to be called by Tonearm on demand. Prints one HTTPS HLS URL on success,
and nothing on an unavailable / ambiguous match. Does not persist music history.
Requires: Python 3.10+ and "python -m pip install coverpy".
"""
from __future__ import annotations

import argparse
import re
import sys
import unicodedata
from urllib.parse import urlparse


def normalize(text: str) -> str:
    text = unicodedata.normalize("NFKD", text.casefold()).replace("&", " and ")
    text = "".join(ch for ch in text if not unicodedata.combining(ch))
    text = re.sub(r"[^\w]+", " ", text, flags=re.UNICODE)
    return " ".join(text.split())


def valid_hls_url(value: str | None) -> bool:
    if not value or len(value) > 4096:
        return False
    parsed = urlparse(value)
    host = (parsed.hostname or "").lower()
    # Avoid passing arbitrary network locations into OBS's FFmpeg source.
    return parsed.scheme == "https" and (
        host == "mzstatic.com" or host.endswith(".mzstatic.com")
        or host == "apple.com" or host.endswith(".apple.com")
    ) and not parsed.username and not parsed.password


def resolve(client: object, artist: str, album: str, album_artist: str, orientation: str) -> str | None:
    """Use CoverPy's public-catalog search and experimental get_motion_artwork()."""
    from coverpy import Entity

    artist_names = {normalize(x) for x in [artist, album_artist] if x}
    if not album or not artist_names:
        return None
    results = client.search(f"{album_artist or artist} {album}", entity=Entity.ALBUM, limit=50)
    by_id: dict[int, object] = {}
    for item in results:
        album_id = item.collection_id
        if not album_id or normalize(item.collection_name or "") != normalize(album):
            continue
        if normalize(item.artist_name or "") not in artist_names:
            continue
        by_id[int(album_id)] = item
    if len(by_id) != 1:
        return None

    album_id = next(iter(by_id))
    motion = client.get_motion_artwork(album_id)
    if motion is None:
        return None

    preferred = motion.tall_hls_url if orientation == "portrait" else motion.hls_url
    alternative = motion.hls_url if orientation == "portrait" else motion.tall_hls_url
    for value in (preferred, alternative):
        if valid_hls_url(value):
            return value
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artist", required=True)
    parser.add_argument("--album", required=True)
    parser.add_argument("--album-artist", default="")
    parser.add_argument("--orientation", choices=("square", "portrait"), default="square")
    args = parser.parse_args()

    try:
        from coverpy import CoverPy
        with CoverPy(country="US", timeout=5.0) as client:
            result = resolve(client, args.artist, args.album, args.album_artist, args.orientation)
        if result:
            sys.stdout.write(result + "\n")
    except Exception:
        # The native plugin retains its static cover on any failure.
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
