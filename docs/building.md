# Building

Tonearm uses CMake (3.22 or newer) and C++20.

| Option | Default | |
|---|---|---|
| `TONEARM_BUILD_PLUGIN` | `ON` | The OBS module. Needs libobs. |
| `TONEARM_BUILD_TESTS` | `ON` | `tonearm-tests`, run with `ctest`. |
| `TONEARM_BUILD_TOOLS` | `ON` on Linux | `tonearm-render`, the headless renderer used for documentation. |

## Windows

You need:

- Visual Studio 2022 with the *Desktop development with C++* workload and a Windows 10 or 11 SDK. The SDK includes the C++/WinRT headers used for the media connection.
- A libobs SDK: headers, `obs.lib` and CMake package files. The simplest way is to build it from the OBS Studio source, which is what CI does:

```powershell
git clone --depth 1 --branch 30.2.3 https://github.com/obsproject/obs-studio.git
cd obs-studio
git submodule update --init --recursive --depth 1 plugins/win-dshow/libdshowcapture
cmake --preset windows-x64 -DENABLE_UI=OFF -DENABLE_PLUGINS=OFF -DENABLE_SCRIPTING=OFF `
  -DENABLE_BROWSER=OFF -DVIRTUALCAM_GUID= -DOBS_VERSION_OVERRIDE=30.2.3
cmake --build build_x64 --target obs-frontend-api --config RelWithDebInfo
cmake --install build_x64 --component Development --config RelWithDebInfo --prefix C:\obs-sdk
```

The `windows-x64` preset downloads OBS's prebuilt dependencies automatically. OBS always configures a 32-bit slice alongside the 64-bit build, which is why the `libdshowcapture` submodule is needed even with plugins turned off. Building `obs-frontend-api` also builds libobs and `w32-pthreads`, so the install step finds every file it expects. Building against an older libobs is deliberate: plugins compiled against 30.2 load in newer OBS releases, but not the other way around.

Then build Tonearm:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\obs-sdk `
  -Dlibobs_DIR=C:\obs-sdk\cmake\libobs -Dw32-pthreads_DIR=C:\obs-sdk\cmake\w32-pthreads
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
cmake --install build --config RelWithDebInfo --prefix stage
```

`stage` then contains `obs-plugins\64bit\tonearm.dll` and `data\obs-plugins\tonearm\`, ready to copy into an OBS installation. For development, a portable OBS (extract the OBS zip and create an empty `portable_mode.txt` next to `bin`) keeps your normal OBS installation and settings untouched.

## Linux

On Debian or Ubuntu:

```sh
sudo apt install cmake ninja-build pkg-config libobs-dev libpango1.0-dev libfontconfig-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The test that renders Japanese text needs a CJK font such as `fonts-noto-cjk`.

To try the plugin in OBS, copy it to the per-user plugin folder:

```sh
mkdir -p ~/.config/obs-studio/plugins/tonearm/bin/64bit
cp build/tonearm.so ~/.config/obs-studio/plugins/tonearm/bin/64bit/
cp -r data ~/.config/obs-studio/plugins/tonearm/data
```

Linux has no media connection yet; use the sample tracks, or start OBS with `TONEARM_SCRIPTED_MEDIA=1` to run the scripted test player.

## Rendering documentation images

```sh
./build/tonearm-render still --preset glass --layout expanded --track 0 --scale 2 --out glass.png
./build/tonearm-render transition --preset neon --layout square --track 2 --to 4 --fps 30 --seconds 3 --outdir frames/
./build/tonearm-render marquee --preset editorial --layout vertical --outdir frames/
./build/tonearm-render pause --preset minimal --layout expanded --outdir frames/
```

Options: `--preset`, `--layout`, `--track N` (0–4), `--to N`, `--pos SECONDS`, `--status playing|paused`, `--size WxH`, `--scale F`, `--bg stage|none`, `--no-art`, `--reduced-motion`, `--fps`, `--seconds`. Frame sequences can be turned into a GIF with ffmpeg:

```sh
ffmpeg -framerate 30 -i frames/f%04d.png -filter_complex \
  "[0:v]format=rgb24,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=sierra2_4a" demo.gif
```

The README's images are built by the scripts in `tools/media`, which add a timeline tool (`tonearm-sequence`, built by `tools/media/build-sequence.sh`) for longer scripted runs. [Media design](media/DESIGN.md) lists the scripts and the visual rules they follow.

## Continuous integration

`.github/workflows/build.yml` runs on every push and pull request:

- **Linux**: builds the core, plugin and tools against Ubuntu's libobs, and runs the unit tests.
- **Windows**: builds libobs 30.2.3 (cached between runs), builds Tonearm with MSVC, runs the unit tests with the DirectWrite backend, and uploads the plugin zip and a separate debug-symbol zip.
- **Release**: pushing a tag such as `v0.2.0` builds both platforms, then publishes a GitHub release with the plugin zip and the symbols zip. The notes come from `docs/release-notes/<tag>.md` when that file exists, otherwise from the matching section of `CHANGELOG.md`. The Package step fails if the DLL or locale files are missing and prints every entry of the zip, so the layout can be checked in the job log.

## Versioning

The version lives in `project(tonearm VERSION …)` in `CMakeLists.txt`. It is compiled into the plugin (logged at load) and the Windows file version resource. Update `CHANGELOG.md` and tag the commit `v<version>`.
