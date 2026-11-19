# Troubleshooting and FAQ

Most answers start with the OBS log: **Help → Log Files → View Current Log**. Tonearm's lines start with `[tonearm]`.

## The source isn't in the Add Source menu

- Check the log for `[tonearm] loaded version …`. If it's missing, OBS didn't find the plugin. Make sure `tonearm.dll` is in `obs-plugins\64bit` (not `obs-plugins\64bit\tonearm\`) and the data folder is `data\obs-plugins\tonearm`.
- Look for a line mentioning `tonearm.dll` with an error. *Module compiled with a newer libobs* means your OBS is older than 30.2; update OBS.
- Tonearm is 64-bit only.

## The overlay stays empty while music plays

1. Open the source's properties and look at **Detected now** under Music sources, then press **Refresh**.
   - *Windows media controls are not available on this system*: Windows didn't provide the media session service. This happens on Windows N editions without the Media Feature Pack, on Windows versions before 10 1809, and in some locked-down environments.
   - *No media players are reporting playback right now*: the player isn't publishing to Windows. Check whether it appears in the media flyout (press a media key or the volume key). Some players, such as foobar2000 and MusicBee, publish to Windows only through an optional component or setting; enable it in the player.
   - The player is listed but the overlay is empty: check **Choose a player**. With *Only the apps below*, the app must be on the list. Browsers need **Include web browsers**.
2. If **When nothing is playing** is *Hide the overlay*, the card only appears while something plays or is paused.

## It shows the wrong player

Tonearm prefers a playing player over a paused one. If two players are both playing (a game's music and Spotify, say), set **Choose a player** to *Prefer the apps below, in order* and list your music app first, or use *Only the apps below*.

## The artist line shows "Artist — Album" or the album is wrong

Apple Music for Windows reports artist and album in one field. Tonearm splits it at the last ` — `. If an artist's name itself contains that sequence, the split lands in the wrong place. Other players report the fields separately and are not affected.

## The progress bar is a second or two off, or jumps

Players only report their position on seeks, pauses and track changes. Tonearm counts forward in between, which keeps it within about a second. A small correction when the player next reports is normal. If the bar drifts steadily, the player is reporting a playback rate or start time incorrectly; please report it with the player's name and version.

## There is no progress bar or time

The player didn't report a duration (common for radio streams, some podcasts and browsers). Tonearm hides the bar instead of guessing.

## The artwork looks blurry

The player supplies the image. Some send small thumbnails (often 300 px or less). Tonearm decodes up to 600 px and scales smoothly, but a small source image will look soft on a large square or vertical layout. Using a smaller layout, or the compact and expanded layouts, hides this.

## The artwork is a pattern of circles instead of the cover

That is Tonearm's generated cover, shown when the player supplies no artwork. It is colored from the track's identity so it changes between tracks.

## Is there an audio visualizer?

No. Windows media controls carry metadata (title, artist, artwork, position) but no audio. Any visualizer driven by that data would be animation that looks like audio but isn't. A real one would need to read an OBS audio source and is not part of this release.

## The sample tracks showed up on stream

**Show sample tracks when nothing is playing** is on. Turn it off in Playback states. The sample songs and artists are invented by Tonearm; they are not real releases.

## Text looks blurry in the canvas

The source was scaled in the OBS canvas. Reset its transform (right-click → **Transform → Reset Transform**) and change **Width** and **Height** in the properties instead. Tonearm renders text at the source's real size.

## Long titles scroll and I'd rather they didn't

Turn off **Scroll long titles**, or turn on **Reduce motion**. Both shorten long text with an ellipsis instead.

## Colors in my custom text don't match what I picked

**Keep text readable** adjusts colors that would be hard to read against the current background. Turn it off to use your colors exactly.

## Does Tonearm send data anywhere?

No. It makes no network connections, needs no account and stores nothing outside OBS's own scene files.

## Does it work on macOS or Linux?

The plugin builds and runs, but only shows sample tracks there because the media connections for those systems (MPRIS on Linux, MediaRemote on macOS) are not written yet.

## How do I report a problem?

Open an issue with your OBS version, Windows version, the player and its version, and the relevant part of the OBS log. For crashes, the matching `-pdb` zip from the release lets the crash report show function names.
