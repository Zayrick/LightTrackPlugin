# LightTrackPlugin

**English** | [简体中文](README.zh-CN.md)

[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue.svg)](LICENSE)
[![OpenRGB](https://img.shields.io/badge/OpenRGB-1.0%20(Plugin%20API%205)-brightgreen.svg)](https://openrgb.org)
[![Qt](https://img.shields.io/badge/Qt-5.10%2B%20%7C%206-41cd52.svg)](https://www.qt.io)

LightTrackPlugin is a timeline-based lighting sequencer for [OpenRGB](https://openrgb.org). It lets you arrange lighting effects as clips on per-device tracks, synchronize them with an audio track, and save the result as a reusable layout.

The plugin is written in C++17 and builds against both Qt 5 and Qt 6.

## Table of Contents

- [Features](#features)
- [Compatibility](#compatibility)
- [Installation](#installation)
- [Usage](#usage)
- [Building from Source](#building-from-source)
- [Running Tests](#running-tests)
- [Project Structure](#project-structure)
- [Contributing](#contributing)
- [License](#license)

## Features

- **Timeline editor** — place, move, resize, copy, and group effect clips on tracks for each device, zone, and segment.
- **OpenRGB effects** — uses the effect library from [OpenRGBEffectsPlugin](https://gitlab.com/OpenRGBDevelopers/OpenRGBEffectsPlugin), compiled directly into the plugin.
- **Audio synchronization** — load a music track (WAV, MP3, FLAC, OGG), view its waveform, and scrub or seek by clicking the audio track.
- **Layered rendering** — overlapping clips are composited by track priority; matrix devices are routed per segment.
- **Layout persistence** — save and reload layouts containing the audio track, effect clips, and per-device/zone/segment names and enabled states.
- **Undo / redo** — full editing history, including multi-clip operations.
- **Plays well with other plugins** — only devices that have clips on the timeline are driven, and idle regions are not continuously overwritten (see [Playback behavior](#playback-behavior)).

## Compatibility

| Component | Supported version |
| --- | --- |
| OpenRGB | 1.0 (Plugin API 5) |
| Qt | 5.10 or later, or Qt 6 |
| Platforms | Windows x64, Linux x64 |

> [!IMPORTANT]
> The plugin must be built with the same Qt major version and CPU architecture as the OpenRGB host that loads it. Official OpenRGB 1.0 Windows builds use **Qt 6.8.3 (MSVC 2022, x64)**.

The OpenRGB dependencies are pinned to their `release_1.0` branches:

| Submodule | Commit |
| --- | --- |
| `deps/OpenRGB` | `81bbe18a84c2e507006f19dd252e397e40a56bfe` |
| `deps/OpenRGBEffectsPlugin` | `0e0f1b4708c302bad4270ab62349b2efe5dcc439` |

## Installation

### Pre-built packages

Every push to `main` produces a pre-release build on the [Releases](https://github.com/Zayrick/LightTrackPlugin/releases) page:

| Platform | Package |
| --- | --- |
| Windows x64 | `LightTrackPlugin-Windows-x64.zip` (contains `LightTrackPlugin.dll`) |
| Linux x64 | `LightTrackPlugin-Linux-x64.tar.gz` (contains `libLightTrackPlugin.so`) |

> [!NOTE]
> Pre-release builds are generated automatically and may be unstable.

### Installing the plugin

1. Download and extract the package for your platform.
2. In OpenRGB, open **Settings → Plugins**, click **Install Plugin**, and select the extracted library file.
   Alternatively, copy the library into the OpenRGB plugins directory:
   - Windows: `%APPDATA%\OpenRGB\plugins`
   - Linux: `~/.config/OpenRGB/plugins`
3. Restart OpenRGB. A **LightTrack** tab will appear.

## Usage

### Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| <kbd>Space</kbd> | Play / pause |
| <kbd>Ctrl</kbd>+<kbd>N</kbd> | New layout |
| <kbd>Ctrl</kbd>+<kbd>O</kbd> | Open layout |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> | Save layout |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Undo |
| <kbd>Ctrl</kbd>+<kbd>Y</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | Redo |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> / <kbd>Ctrl</kbd>+<kbd>V</kbd> | Copy / paste selected clips |
| <kbd>Delete</kbd> | Delete selected clips |
| <kbd>Ctrl</kbd>+<kbd>G</kbd> | Group selected clips |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>G</kbd> | Ungroup selected clips |
| <kbd>Esc</kbd> | Cancel the current drag |

### Selecting and editing clips

- **Rubber-band selection** — drag on an empty area of the timeline to select every clip inside or intersecting the rectangle. Hold <kbd>Ctrl</kbd> while dragging to add to the current selection.
- **Toggle selection** — <kbd>Ctrl</kbd>+click a clip to add it to or remove it from the selection. Clicking an empty area clears the selection.
- **Group move** — drag the body of any selected clip to move the whole selection. Durations, relative timing, and track spacing are preserved; the group snaps as a unit and is clamped to time zero and the track bounds. Press <kbd>Esc</kbd> during a drag to cancel and restore the original positions. Each drag is a single undo step.
- **Resize** — drag the handles at the edges of a clip to change its duration.
- **Copy and paste** — use the keyboard shortcuts or the **Copy** / **Paste** context-menu items. Pasted clips keep their tracks, durations, effect settings, and relative spacing; the timeline extends automatically if needed.

### Layouts

- **Saving** — click **Save** or press <kbd>Ctrl</kbd>+<kbd>S</kbd>. Layouts are stored as `.lighttrack` files and include the audio track, all effect clips, and the name and enabled state of every device, zone, and segment (including rows without clips).
- **Loading** — older layout files remain supported. If an older file has no region configuration, the current region configuration is kept.
- **New layout** — click **New** or press <kbd>Ctrl</kbd>+<kbd>N</kbd>. If the current layout has unsaved changes, you will be prompted to save, discard, or cancel. Creating a new layout stops playback, clears the audio and clips, restores default names and enabled states, and resets the undo history.

> [!NOTE]
> Loading an OpenRGB host profile pauses LightTrack playback. LightTrack does not currently store layout data in OpenRGB profiles and does not expose SDK commands.

### Playback behavior

Each clip renders into its own virtual controller created through the OpenRGB host API; the results are then composited by track priority and submitted as a single frame per device.

To avoid flicker when other plugins (for example, Visual Map) drive the same LEDs:

- Only devices that have at least one clip on the timeline are written to.
- Idle regions receive a single black frame when they become idle (playback start, clip end, or stop) and are not overwritten afterwards.
- Idleness is evaluated at the smallest output unit — a segment if the zone has segments, otherwise the zone — so active regions keep their effects and enabled states.

## Building from Source

### Prerequisites

- A C++17 compiler (MSVC 2022 recommended on Windows)
- CMake 3.16 or later (3.21 or later to use `CMakePresets.json`)
- Qt 5.10+ or Qt 6 with the following modules:
  - Qt 5: Core, Gui, Widgets, OpenGL
  - Qt 6: Core, Gui, Widgets, OpenGL, OpenGLWidgets, Core5Compat

On Debian/Ubuntu, the Qt 6 build dependencies can be installed with:

```sh
sudo apt-get install ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools libqt6core5compat6-dev libqt6opengl6-dev \
  libgl-dev libvulkan-dev libxkbcommon-dev libopenal-dev libpipewire-0.3-dev
```

### Getting the source

OpenRGB, OpenRGBEffectsPlugin, and [miniaudio](https://github.com/mackron/miniaudio) are included as Git submodules. Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/Zayrick/LightTrackPlugin.git
cd LightTrackPlugin
```

If you already cloned the repository without submodules:

```sh
git submodule update --init --recursive
```

### Building with CMake presets

The following presets are available:

| Preset | Qt | Configuration | Default `CMAKE_PREFIX_PATH` |
| --- | --- | --- | --- |
| `qt6-debug` | 6 | Debug | `C:/Qt/6.8.3/msvc2022_64` |
| `qt6-release` | 6 | Release | `C:/Qt/6.8.3/msvc2022_64` |
| `qt5-debug` | 5 | Debug | `C:/Qt/5.15.2/msvc2019_64` |
| `qt5-release` | 5 | Release | `C:/Qt/5.15.2/msvc2019_64` |

```sh
cmake --preset qt6-release
cmake --build --preset qt6-release
```

If Qt is installed elsewhere, override the prefix path at configure time:

```sh
cmake --preset qt6-release -D CMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64
cmake --build --preset qt6-release
```

Build output is written to `build/<preset-name>/`.

### Building without presets

```sh
cmake -S . -B build/qt6 -D CMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64
cmake --build build/qt6 --config Release
```

For Qt 5, point `CMAKE_PREFIX_PATH` at your Qt 5 installation. On Linux with system Qt packages, `CMAKE_PREFIX_PATH` can usually be omitted.

## Running Tests

Tests are disabled by default. Enable them with `BUILD_TESTING=ON`:

```sh
cmake --preset qt6-release -D BUILD_TESTING=ON
cmake --build --preset qt6-release
ctest --test-dir build/qt6-release -C Release --output-on-failure
```

On headless systems, set `QT_QPA_PLATFORM=offscreen` before running `ctest`.

The test suite covers:

- Layout persistence, including device/zone/segment names and enabled states, with undo/redo
- Edit history
- Timeline copy/paste, dragging, and waveform rendering
- Overlapping effects and matrix segment routing
- Loading the built plugin library through Plugin API 5, device rescan notifications, and repeated unloading

Effect tests use mock controllers and do not access real RGB hardware or play audio.

## Project Structure

```text
src/
├── plugin/           OpenRGB plugin entry point
├── core/             Timeline and layout data model
├── ui/               Pages and timeline widgets
├── infrastructure/   Layout file I/O
└── integrations/     OpenRGB and audio backends
cmake/                OpenRGB Effects source configuration
tests/                Regression tests
resource/             Static assets referenced by the Qt resource file
deps/                 Third-party Git submodules (do not modify)
```

## Contributing

Contributions are welcome. Please keep the following in mind:

- **Do not modify third-party code in `deps/`.** To upgrade a dependency, update the submodule commit and verify compatibility in a separate change.
- **Keep generated files out of the repository.** All build output belongs in `build/`.
- **Keep `CMakeLists.txt` in sync** when adding or removing source files.
- Run the test suite before opening a pull request.
- Commit messages follow the [Conventional Commits](https://www.conventionalcommits.org/) style (e.g. `feat(timeline): ...`, `fix: ...`).

## License

LightTrackPlugin is licensed under the [GNU General Public License v2.0 or later](LICENSE), the same license as OpenRGB.

Third-party submodules in `deps/` are distributed under their own licenses.
