# Architecture

Tonearm is a C++20 OBS module with a platform-neutral core. The core has no dependency on OBS or Windows, which keeps it testable on any machine and lets the documentation renderer use exactly the same code as the plugin.

```
src/
  core/    model, design tokens, layout, color, imaging, rasterizer, scene, animation
  text/    text backends: DirectWrite (Windows), Pango (Linux, tests, docs)
  media/   media providers: SMTC (Windows), scripted test player, sample player; WIC decoding
  obs/     OBS source, GPU compositor, raster worker thread, settings UI, shared media service
data/      effect file, locale, bundled fonts
tools/     tonearm-render, a headless renderer for screenshots and animations
tests/     unit tests (no framework dependency)
```

## Threads

| Thread | Owns | Never does |
|---|---|---|
| `tonearm-smtc` (one per OBS process) | All WinRT calls: session manager, per-session event handlers, metadata, thumbnail streams, WIC decoding | Touch OBS or GPU objects |
| `tonearm-raster` (one per source) | Text shaping and all CPU rasterization of layers | Read live media state or touch the GPU |
| OBS graphics thread | `video_tick` and `video_render`: picks a session, advances the scene, uploads finished layers, draws | Block on Windows or on rasterization |
| OBS UI thread | Properties, `update()` | Rasterize |

Data moves between threads as immutable snapshots behind a mutex that is held only long enough to swap a `shared_ptr`:

- The SMTC thread publishes a `MediaSnapshot` (every session with its track, artwork and timeline). Readers take the latest pointer.
- The graphics thread posts a `LayerRequest` (style + content) to the raster worker. If a newer request arrives before the worker starts, the older one is dropped. Finished `LayerSet`s come back the same way and are uploaded on the next tick.
- `update()` stores a pending `Style` that the graphics thread adopts at the start of its next tick.

## Media

`SmtcProvider` uses `GlobalSystemMediaTransportControlsSessionManager`. It subscribes to `SessionsChanged`, `CurrentSessionChanged` and, per session, `MediaPropertiesChanged`, `PlaybackInfoChanged` and `TimelinePropertiesChanged`. Handlers only mark what changed; the thread waits 60 ms to coalesce the bursts players send on a track change, then queries once. Metadata and artwork are only re-read after a property event. A slow safety refresh every four seconds re-reads playback state for players that forget to send events. Every asynchronous WinRT call has a two-second deadline, so a hung player can delay updates but can't stall OBS shutdown.

Event handlers capture a `weak_ptr` to a small signal object rather than the provider, so a callback arriving during shutdown finds nothing to touch.

Timelines are stored as *position at a sample time*. The sample time comes from SMTC's `LastUpdatedTime`, translated to the steady clock, and the core extrapolates from there. If a player resumes without refreshing its timeline, the stale sample would make the bar jump forward, so a resume with an old sample is treated as current.

`selectSession` scores sessions: playing beats paused, then the user's priority list, then the session shown last (to avoid flicker between two paused players), then the one Windows considers current. Browsers are excluded unless allowed or listed.

`normalizeMetadata` trims fields, splits Apple Music's combined `Artist — Album` field and falls back to the album artist when the artist is empty.

## Design system

`style.hpp` defines the tokens every preset draws from: a 4 px spacing scale (`space1`–`space10`), a type scale from 11 to 40 px, radii from 4 to 24 px, and durations (160, 280, 460, 640 ms and a 50 ms stagger). A `Style` is a full description of one look; `makePreset(id, layout)` returns each preset's values for each layout. All logical sizes are multiplied by a *unit* derived from the source size, so a source at twice the recommended size renders at twice the resolution rather than looking stretched.

## Layout

`computeLayout` turns a style and the available content into rectangles: card, artwork, scrim, text lines, progress bar and time labels. It handles horizontal layouts (artwork beside text, or full-bleed for Editorial), stacked layouts (artwork above text, centered for Neon, overlay with a scrim for Glass square, full-bleed for Editorial), and degrades in a fixed order when space is short: the player name goes first, then the album line. Presets that show a single time label place it inline with the bar instead of on its own row. The unit tests check, for every preset, layout, content combination and a range of sizes, that nothing leaves the card and nothing overlaps.

## Rasterization

`rasterizeLayers` produces a `LayerSet` when the track, artwork or style changes:

- **surface**: drop shadows and the card. Glass composites a heavily blurred, desaturated copy of the artwork (computed at 1/16 scale), tinted toward the artwork's deep tone, with a top sheen and bottom vignette. Editorial bakes a feathered full-bleed cover into a gradient that starts from the cover's own edge color. Neon is a dark surface lit by the accent. Minimal is a flat color. A light grain prevents gradient banding on stream encoders.
- **art**: the cover with its own shadow, rounded mask and edge stroke.
- **text**: one image per line, with tracking, tabular figures and uppercase where the preset asks. If a line is wider than its box and scrolling is on, the full line is rendered and the compositor scrolls it.
- **glow ring** (Neon): drawn as its own layer so it can animate.

Palette extraction runs k-means (k = 5) in Oklab on a 24 × 24 thumbnail and picks a dominant, an accent (the most vivid usable cluster), a secondary and a deep tone. After the surface is rendered, Tonearm measures the average color of the pixels behind the text column and adjusts text and accent colors to reach the contrast targets, first by raising opacity and then lightness, so presets keep their character where possible.

All images are premultiplied BGRA, matching the GPU textures.

## Scene and motion

`Scene` holds two decks, current and previous, and builds a list of draw operations every frame: textured quads with opacity, offset, scale and an optional soft-edged clip (for scrolling text), plus rounded rectangles evaluated by a signed-distance shader (progress track, fill and glow). Track changes, artwork arriving late and style changes are different transition kinds with their own timing. Each preset has its own choreography: Glass lifts text vertically and grows the new cover from 94%, Minimal uses barely visible 3–4 px offsets and a tighter stagger, Neon slides sideways and its glow overshoots briefly as it powers on, and Editorial slides lines in from the right with the widest stagger, like a column of type being set.

The progress bar follows the extrapolated position exactly and eases across jumps (half-life 85 ms), so seeks glide rather than snap. Time labels change once per second and are rasterized by the worker, not every frame.

Reduced motion replaces travel and scaling with short crossfades, makes the progress bar jump, and turns scrolling text into ellipsized text.

## GPU

`GpuCompositor` keeps a texture per layer image, keyed by a unique id, and frees textures that haven't been drawn for three frames. Drawing uses `data/effects/tonearm.effect` with two techniques, `Layer` and `RoundRect`, premultiplied blending (`ONE`, `INVSRCALPHA`), and sRGB framebuffer conversion disabled while drawing so colors match the CPU rasterizer. GPU objects are created and destroyed inside the graphics context. The raster worker is joined before the compositor is destroyed.

When the source isn't shown anywhere and has finished fading out, `video_tick` returns early: no scene updates, no rasterization, no draws.

## Animated artwork

Off by default, and nothing below exists until it is switched on.

```
SMTC track ──► MotionPlayer::tick (graphics thread, on track change)
                 └─► MotionResolver thread ──► MotionSource::resolve()   (LocalFolderSource today)
                        └─► deliver(): obs_source_update(child, file)    (opening media happens here)
child = private "ffmpeg_source" (OBS's FFmpeg, HW decode, HLS, looping)
video_render: MotionPlayer::prepare ──► copy the cropped frame into one texrender (≤ 30/s)
              GpuCompositor ──► Video technique: cover crop, rounded mask, card clip, Editorial feather
```

- **Matching** (`core/motion.cpp`) normalizes names and splits album titles into a base and edition qualifiers, so Deluxe, remastered, single and anniversary releases never stand in for each other. Ambiguity means no match.
- **Resolver** (`media/motion_resolver.cpp`): one thread per source. The newest request wins and running lookups are cancelled. Answers are cached, negative ones for 30 seconds. The same release is never looked up twice at once, and a throwing source counts as "no artwork". `MotionSource` is the extension point for other origins.
- **Playback** (`obs/motion-player.cpp`) uses OBS's media source as a private, muted child. Before a different file is opened, the child's current frame is cleared, so a frame from the previous album can never be shown. Another track from the same album keeps the same video running. The child is paused when music pauses (optional) and whenever the Tonearm source isn't shown.
- **Scene** (`core/scene.cpp`): the host reports which track its decoded video belongs to (`MotionFrame`). The scene fades the video in (460 ms) only when that is exactly the track on screen, and drops it at once on a change. The draw order is still cover, then video, then `artOverlay` (the Glass scrim and the Glass and Neon edge strokes), so text contrast and edges survive. The paused dim is a tint on the video instead of transparency.
- **Masking** is defined once in `videoCoverage()` (CPU reference) and mirrored in `PSVideo`. Unit tests render every preset and layout and check that no pixel outside the artwork changes, that corners are masked and that the Editorial feather matches.

## Testing tools

- `tonearm-render` (Linux) drives the real scene and rasterizer with the CPU reference compositor, which mirrors the shader's math, and writes PNG stills or frame sequences.
- `TONEARM_SCRIPTED_MEDIA=1` makes the plugin use a scripted player instead of SMTC. It loops through playing, seeking, pausing, resuming, a long title, missing artwork, a second player taking over and both players closing, so behavior can be checked inside a real OBS without a music app.
