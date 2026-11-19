# Changelog

All notable changes to Tonearm are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

Planned as 0.2.0. Not released.

### Added

- Animated artwork (experimental, off by default). It plays a looping video in place of the still cover, masked into all four presets and every layout. Videos come from a local folder named `Artist - Album.mp4` (add ` [tall]` for portrait). MP4, MOV, WebM, MKV and HLS, including fragmented-MP4 HLS, all decode through OBS's own media source. It falls back to the still cover whenever no video matches, the match is uncertain, or decoding fails. Reduce motion turns it off. See [docs/animated-artwork.md](docs/animated-artwork.md).
- Settings: Artwork (static or animated), preferred orientation, behavior when music pauses, and the animated artwork folder.

### Not included

- Fetching Apple Music's own motion artwork. The only known method reuses the Apple Music web player's private sign-in token, so it isn't shipped. The reasons are in [docs/animated-artwork.md](docs/animated-artwork.md#why-tonearm-doesnt-fetch-apple-musics-videos).

## [0.1.0] - 2026-11-19

First preview release. The Windows media connection has not yet been tested on real hardware; see [docs/testing.md](docs/testing.md).

### Added

- Tonearm Now Playing source for OBS Studio 30.2 and newer.
- Media from the Windows System Media Transport Controls: Apple Music, Spotify, TIDAL, Deezer, Amazon Music, foobar2000, MusicBee, VLC, Media Player and other players that publish there. Browsers are opt-in.
- Player selection: automatic, preferred order, or allow list.
- Four presets (Glass, Minimal, Neon, Editorial), each with compact, expanded, square and vertical layouts.
- Accent and surface colors from the artwork, with automatic contrast adjustment to WCAG AA.
- Extrapolated progress with smooth seeking, elapsed and remaining time.
- Per-preset transitions for track changes, late artwork and style changes; scrolling for long text; reduced-motion mode.
- Paused, idle and closed-player behaviors, including hide-after-delay and an idle message.
- Sample tracks for designing scenes without music playing.
- Preset export and import as JSON.
- Bundled Inter, IBM Plex and EB Garamond fonts (SIL OFL 1.1).
- `tonearm-render`, a headless renderer for documentation images, and a scripted test player (`TONEARM_SCRIPTED_MEDIA=1`).

[Unreleased]: https://github.com/cxrlosxk/tonearm/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/cxrlosxk/tonearm/releases/tag/v0.1.0
