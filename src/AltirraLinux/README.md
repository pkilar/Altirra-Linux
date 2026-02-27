# Altirra Linux Port

Linux port of [Altirra](http://www.virtualdub.org/altirra.html), Avery Lee's
cycle-accurate Atari 8-bit (800/XL/XE/5200) emulator. This port uses SDL2 for
display, audio, and input, OpenGL 2.1 for rendering, and Dear ImGui for the
configuration and debugger overlay.

> **Status**: ~99.8% feature-complete. Core emulation, UI, debugger, recording,
> disk explorer, and all configuration dialogs are fully functional. The generic
> device config system covers 66 of 67 Windows device tags.

## Prerequisites

| Dependency | Minimum version | Notes                                          |
| ---------- | --------------- | ---------------------------------------------- |
| GCC        | 13+             | Must support C++23 (`if consteval`, etc.)      |
| CMake      | 3.20            | Ninja generator recommended                    |
| SDL2       | 2.0.20+         | `libsdl2-dev` on Debian/Ubuntu, `sdl2` on Arch |
| OpenGL     | 2.1             | `libgl-dev` / `mesa-libGL-devel`               |
| FFmpeg     | 5.0+ (optional) | Enables H.264+AAC video recording to MP4       |

Dear ImGui v1.91.8 is fetched automatically via CMake `FetchContent` — no
manual download needed.

### Installing dependencies

**Debian / Ubuntu:**
```sh
sudo apt install build-essential cmake ninja-build libsdl2-dev libgl-dev

# Optional: H.264 video recording
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
```

**Arch Linux:**
```sh
sudo pacman -S base-devel cmake ninja sdl2 mesa

# Optional: H.264 video recording
sudo pacman -S ffmpeg
```

**Fedora:**
```sh
sudo dnf install gcc-c++ cmake ninja-build SDL2-devel mesa-libGL-devel

# Optional: H.264 video recording
sudo dnf install ffmpeg-free-devel
```

## Building

From the repository root:

```sh
# Configure (Debug build)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# The binary is at:
#   build/src/AltirraLinux/altirra
```

Available build types: `Debug`, `Release`.

To install system-wide:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build
```

This installs:
- `/usr/local/bin/altirra`
- `/usr/local/share/applications/altirra.desktop`
- `/usr/local/share/altirra/firmware/` (empty directory for ROM files)

## Running

```sh
# Run with no arguments (uses HLE kernel if no ROMs found)
./build/src/AltirraLinux/altirra

# Load a disk image
./build/src/AltirraLinux/altirra game.atr

# Start in fullscreen
./build/src/AltirraLinux/altirra --fullscreen game.atr
```

### Command-line options

| Option              | Description                                                   |
| ------------------- | ------------------------------------------------------------- |
| `<file>`            | Load a disk, cartridge, tape, or save state image             |
| `--portable`        | Store settings alongside the binary instead of in `~/.config` |
| `--config <path>`   | Use a specific settings (INI) file                            |
| `--rom-path <path>` | Add an additional firmware ROM search directory               |
| `--fullscreen`      | Start in fullscreen mode                                      |
| `--help`            | Show usage information                                        |
| `--version`         | Print version and exit                                        |

### Drag and drop

You can drag and drop `.atr`, `.xfd`, `.bin`, `.car`, `.cas`, or `.atstate2`
files onto the emulator window to load them.

## Firmware / ROM files

Altirra includes a built-in HLE (high-level emulation) kernel, so it runs
without any external ROM files. For better compatibility, you can supply
original Atari OS ROMs.

Place ROM files in any of these directories (searched in order):

1. `~/.config/altirra/firmware/`
2. `<program-directory>/firmware/`
3. `/usr/share/altirra/firmware/`
4. `/usr/local/share/altirra/firmware/`

The firmware manager will auto-detect standard ROM files placed in these
directories.

## Settings

Settings are stored as an INI file in portable mode:

- **Default**: `~/.config/altirra/Altirra.ini`
- **Portable mode** (`--portable`): `<program-directory>/Altirra.ini`
- **Custom** (`--config`): user-specified path

Settings are loaded at startup and saved on clean exit. The format is the same
INI format used by Altirra's Windows portable mode.

## User Interface

Press **F12** to toggle the ImGui overlay. When visible, the overlay provides:

### Menu bar

| Menu       | Contents                                                                                                                                 |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| **System** | Hardware mode (800/XL/XE/5200/XEGS), memory size, video standard (NTSC/PAL/SECAM), BASIC toggle, firmware/audio/video/keyboard/input/device/boot config, cold/warm reset |
| **File**   | Open/boot image, recent files, quick/file save/load state, cassette control, disk drives (D1-D8) with mount/unmount/save/rotate/new, disk explorer, screenshots, video/audio recording |
| **Edit**   | Paste text to emulator |
| **View**   | FPS, display filter (Point/Bilinear), stretch mode, window size, fullscreen, status bar toggle, cursor |
| **Speed**  | Pause, turbo, speed slider (50-800%), mute, pause-when-inactive |
| **Debug**  | Break/Run, step into/over/out, debugger window visibility (13 windows), symbol loading |
| **Help**   | Keyboard shortcuts, config directory, about |

### Status bar

The bottom bar shows: hardware mode, video standard, disk drive activity (D1-D8
with track/sector numbers, SIO transfer, dirty indicator), H:/PCLink/IDE/Flash
indicators, cartridge, cassette position, pause/turbo/speed, recording, mute,
FPS, held console buttons, LED status, tracing size, debugger watch values,
and auto-expiring status messages.

### File dialogs

The emulator tries to use native file dialogs:
1. **zenity** (GTK environments)
2. **kdialog** (KDE environments)
3. **ImGui text input** fallback (if neither is installed)

## Keyboard shortcuts

These shortcuts work regardless of overlay visibility:

| Key            | Action                       |
| -------------- | ---------------------------- |
| **F7**         | Quick save state             |
| **F8**         | Quick load state             |
| **F12**        | Toggle ImGui overlay         |
| **Escape**     | Close overlay                |
| **Alt+Return** | Toggle fullscreen            |
| **F1 (hold)**  | Warp speed while held        |
| **Ctrl+S**     | Save settings                |
| **Ctrl+V**     | Paste text to emulator       |

When the overlay is visible, debug shortcuts are also active:

| Key           | Action                   |
| ------------- | ------------------------ |
| **F5**        | Break / Resume execution |
| **F10**       | Step over                |
| **F11**       | Step into                |
| **Shift+F11** | Step out                 |

## Display

- **Filters**: Point (nearest-neighbor) and Bilinear (linear interpolation)
- **Stretch modes**: Unconstrained, preserve aspect ratio, square pixels, integral scaling
- **Overscan**: Normal, extended, full, OS screen, widescreen
- **Video**: Artifact modes (NTSC/PAL/Auto), monitor modes (Color/Peritel/Mono variants), scanlines, interlace, deinterlace
- **Color**: Brightness, contrast, hue, saturation, gamma sliders
- **HiDPI**: Automatic high-DPI scaling via SDL2
- **PAR correction**: Pixel aspect ratio correction each frame
- **Adaptive vsync**: Tries adaptive (-1), falls back to standard (1)

## Video recording

Record emulator video and audio via **File > Record Video**. Available codecs:

| Codec                  | Container | Notes                                       |
| ---------------------- | --------- | ------------------------------------------- |
| ZMBV (Lossless)        | AVI       | Best quality, large files                   |
| Raw (Uncompressed)     | AVI       | Largest files                               |
| RLE (Palette only)     | AVI       | 8-bit palette modes only                    |
| H.264+AAC (MP4)        | MP4       | Requires FFmpeg dev libraries at build time |

The H.264 option only appears if FFmpeg was detected during the CMake configure
step. CMake prints `FFmpeg found - H.264 recording enabled` when detection
succeeds.

## Debugger

13 tool windows accessible via Debug > View menu:

- **Registers** — CPU state with double-click editing, stack peek, hardware registers (ANTIC/POKEY/PIA/GTIA)
- **Disassembly** — Symbol labels, follow operand, Run to Cursor, Set PC, copy address
- **Memory** — Hex editing, break on access, follow pointer, quick-nav, byte search
- **Console** — Command input with search/filter, copy/clear, auto-scroll
- **Breakpoints** — PC/Read/Write types with symbol names, conditions, click-to-navigate
- **Watch** — Expression evaluator with symbol names, values shown in status bar
- **Call Stack** — Frame navigation with symbol names
- **History** — 256-entry instruction history with optional register columns
- **Source Code** — Source-level debugging with file search
- **Printer Output** — Captured printer text
- **Profiler** — Performance overlay with timeline, call graph, function detail
- **Trace Viewer** — CPU/Video/BASIC trace recording with timeline visualization
- **Debug Display** — Auxiliary display output

## Known limitations

- No DragonCart Ethernet emulation (modem TCP works via POSIX sockets)
- No physical disk access (intentionally disabled for security)
- A few complex device dialogs use simplified controls (e.g., DIP switches as integer input, compound dropdowns as separate controls)

## Running the test suite

```sh
# Build includes ATTest automatically
build/src/ATTest/attest all
```

All 25 tests pass (0 failures).

## Project structure

```
src/AltirraLinux/
  CMakeLists.txt          Build config (GLOBs Altirra sources, excludes ~90 Win32 files)
  h/
    stdafx.h              Linux precompiled header (shadows Windows version)
    tchar.h               Minimal TCHAR stub
    at/atnativeui/
      uiframe.h           Linux shim (shadows Win32 uiframe.h)
  source/
    main_linux.cpp         SDL2+OpenGL main loop, settings, CLI, event handling
    stubs_linux.cpp        Stub/shim implementations for ~120 Win32-only symbols
    oshelper_linux.cpp     xdg-open, clipboard, process helpers
    console_linux.cpp      Debugger console and source window integration

src/AltirraShell/
  CMakeLists.txt          SDL2/OpenGL frontend library
  h/
    display_sdl2.h        IVDVideoDisplay SDL2+GL implementation
    input_sdl2.h          Keyboard/mouse/controller input
    imgui_manager.h       ImGui lifecycle management
    debugger_imgui.h      Debugger windows (13 windows + toolbar)
    emulator_imgui.h      Emulator config UI (menus, dialogs, status bar)
    filedialog_linux.h    Native file dialog (zenity/kdialog/fallback)
    error_imgui.h         Thread-safe error queue for ImGui popups
  source/
    display_sdl2.cpp      Double-buffered staging, GL texture upload, aspect-correct rendering
    input_sdl2.cpp        SDL scancode -> ATInputCode mapping
    joystick_sdl2.cpp     IATJoystickManager SDL2 implementation
    imgui_manager.cpp     ImGui frame management
    debugger_imgui.cpp    13 debugger windows + IATDebuggerClient
    emulator_imgui.cpp    Menu bar, all config dialogs, disk explorer, status bar
    filedialog_linux.cpp  Fork+exec zenity/kdialog, ImGui fallback
    commands_linux.cpp    67 UI command handlers + ATUICommandManager
```

## License

Altirra is licensed under the GNU General Public License v2 or later.
See the top-level repository for the full license text.
