# Animated artwork (experimental)

Tonearm can play a looping video in place of the still album cover. The video sits in the same place as the cover, with the same mask, in every preset and layout. When no video is available, or anything goes wrong, Tonearm shows the still cover as it always has.

The feature is **off by default**. While it is off, Tonearm creates no media decoder, starts no extra thread and reads no extra files.

![Animated artwork in all four presets and layouts (test videos)](media/animated-gallery.webp)

*The covers in this image are FFmpeg test patterns, not album artwork. They were captured from OBS Studio 30.0.2 on Linux while the scripted test player was running.*

## Automatic Apple Music artwork (experimental, Windows)

When **Animated artwork when available** is selected and **Local artwork folder** is blank, Tonearm looks up the current artist/album/track in the independent public API at [artwork.m8tec.top](https://artwork.m8tec.top). Its response provides genuine Apple Music HLS artwork streams. OBS FFmpeg plays the HLS media directly—Tonearm doesn't synthesize movement or fetch/store MP4 videos.

The lookup sends **artist, album, and track title** to that third party. It does not transmit your Apple ID, Apple Music credentials, or listening history beyond the current lookup. Requests run off the graphics thread; a failed lookup, ambiguous album, unavailable service, or unsupported song falls back to the ordinary still artwork.

This service is **unofficial**, can change, and may be rate-limited. It is not owned or operated by Apple or Tonearm. Only an exact normalized artist and album edition match is accepted, and video locations are limited to Apple CDN HTTPS hosts. Tonearm doesn't extract or store Apple web-player tokens.

### Use your own local motion library instead

To avoid network lookups, choose a folder. Name each file after the release:

```
Phoebe Bridgers - Punisher.mp4          square artwork
Phoebe Bridgers - Punisher [tall].mp4   portrait (3:4) artwork
Fleetwood Mac - Rumours (Super Deluxe).mp4
Phoebe Bridgers - Kyoto - Single.mp4
```

Supported files: `.mp4`, `.m4v`, `.mov`, `.webm`, `.mkv`, and `.m3u8`. New files are picked up within about ten seconds. When a local folder is set, Tonearm never queries the remote API.

### About CoverPy

[CoverPy](https://github.com/matteing/coverpy) established a working approach for locating Apple Music's genuine editorial motion HLS streams, including square and portrait artwork. Its experimental approach extracts the Apple Music web-player token to call Apple's undocumented `editorialVideo` catalog field. Tonearm's online mode instead uses a separately published third-party artwork lookup endpoint that returns the HLS URLs, avoiding web-player credential extraction inside the plugin. Both approaches use actual Apple motion streams rather than simulated animation.

The Apple Music API does not officially document `editorialVideo` for third parties. The experimental online mode is subject to service availability and content terms. It is intentionally opt-in and disabled by default.

## Settings

The settings are in **Animated artwork (Experimental)** in the source properties.

| Setting | What it does |
|---|---|
| **Artwork** | *Static artwork* (default) or *Animated artwork when available*. |
| **Preferred orientation** | *Automatic* uses portrait videos for tall artwork areas and square ones otherwise. *Square* and *Portrait* force a choice. If only one shape exists, it is used, center-cropped to fit. |
| **When music pauses** | *Pause the animation* (default) freezes the current frame. *Keep looping* doesn't. |
| **Local artwork folder** | Optional. Blank enables online automatic artwork lookup on Windows; a folder selects local-only video files. |

**Reduce motion** in the Motion group always shows the still cover. There is no quality setting: Tonearm copies frames at the size the artwork is drawn, and never above the video's own resolution.

## How it behaves

| Situation | Result |
|---|---|
| A video matches the playing release | The video fades in over the still cover (460 ms) and loops |
| No matching file, or the match is uncertain | Still cover |
| File can't be opened or decoded | Still cover (OBS logs `MP: Failed to open media`) |
| Track changes | The old video is dropped at once (the still cover underneath matches it), then the new one fades in when its first frame is decoded. A stale frame from the previous video is never shown |
| Next track from the same album | The same video keeps playing without restarting |
| Music paused | The video is dimmed like the still cover, using a tint so the cover underneath never shows through, and freezes unless *Keep looping* is on |
| Source not visible in any scene | Decoding is paused |
| Feature switched off | The decoder and its thread are destroyed, and the open file is closed |
| Sample tracks (*Show sample tracks when nothing is playing*) | Still covers only |

### Matching rules

A video is used only when the artist **and** the album match exactly after normalization. Normalization ignores case, accents, apostrophes and punctuation, and treats `&` as "and". Edition qualifiers must match too:

- *Midnights* and *Midnights (3am Edition)* are different releases.
- *Rumours* and *Rumours (Super Deluxe)* are different releases.
- *Kyoto - Single* and *Kyoto* are different releases.
- *Wish You Were Here* and *Wish You Were Here (2011 Remastered Version)* are different releases.
- "Deluxe" and "Deluxe Edition" count as the same qualifier.

Artist matching also accepts the album artist and the first artist of a credit like "A & B" or "A feat. B". If two different files claim the same release and shape, neither is used.

## Performance

These numbers come from OBS Studio 30.0.2 on a 4-core Linux container. It has **no GPU**, so all rendering is software OpenGL and decoding is software only. Rendering work that a graphics card would do shows up here as CPU time, so read the numbers as an upper bound for this machine, not a prediction for Windows. Each figure is OBS's whole-process CPU over 48 seconds of the scripted player, at 1280×720 and 30 fps, with a 720×720 H.264 test video at 30 fps.

| Configuration | OBS process CPU (% of one core) |
|---|---|
| No Tonearm source | 21.1 |
| Tonearm, static artwork | 30.3 |
| Tonearm, animated artwork | 58.1 |
| Animated artwork switched back off | 30.8 |
| Animated artwork on, Tonearm not in the live scene | 20.6 |

Decoding that video alone takes about 7% of one core. The rest of the difference is software rendering: OBS's color conversion of each new frame, Tonearm's copy into its artwork texture, and the masked draw. On a graphics card these are small GPU passes, and OBS's media source uses hardware decoding where it's available (D3D11VA on Windows). Memory rose by about 2 MB.

The design choices that keep the cost down:

- Each video is decoded once, by OBS's own media source, however many times Tonearm is drawn.
- Tonearm copies at most 30 frames a second, at the artwork's on-screen size, into one reused texture. Only the cropped part of the frame is copied.
- No copies happen while the still cover is shown, and decoding is paused while the source is hidden.
- File lookups run on their own thread, never on OBS's graphics or UI threads.

GPU load, encoder impact and hardware decoding on Windows still need measuring on a real PC (see [Testing](testing.md#animated-artwork)).

## Privacy and network

With static artwork selected, Tonearm makes no motion-artwork network requests. With animated artwork selected and no local folder, Windows Tonearm sends the playing artist, album and track title to artwork.m8tec.top for HLS URL discovery. With a local folder set, Tonearm uses only local files.
