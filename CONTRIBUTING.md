# Contributing

Thanks for helping. A few notes to make that easy.

## Reporting problems

Include your OBS version, Windows version, the music player and its version, which preset and layout you use, and the `[tonearm]` lines from the OBS log (**Help → Log Files**). A screenshot helps for visual problems.

## Development setup

See [docs/building.md](docs/building.md). The core builds and tests on Linux without Windows, and `TONEARM_SCRIPTED_MEDIA=1` lets you exercise the plugin inside OBS without a music app. Develop against a portable OBS so your regular installation stays untouched.

## Pull requests

- Keep OBS and Windows types out of `src/core`. Anything that can be expressed without them belongs there and should have a test.
- Nothing slow on the OBS graphics thread: no WinRT calls, no text shaping, no file I/O. Work goes to the media or raster thread and comes back as an immutable snapshot.
- New visual options should use the tokens in `src/core/style.hpp` rather than new magic numbers.
- Run `ctest` before sending. For visual changes, attach before and after images from `tonearm-render` or OBS.
- User-facing text goes in `data/locale/en-US.ini`. Write it the way you'd explain the option to a friend: short, specific, no exclamation marks.
- Don't fake data. If the player doesn't report something, Tonearm hides it rather than inventing it.

Code style is what's already there: tabs for indentation and lines up to about 120 columns.

By contributing you agree that your contributions are licensed under GPL-2.0-or-later.
