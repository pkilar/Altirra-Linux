# Altirra Linux Port

An unofficial Linux port of [Altirra](https://virtualdub.org/altirra.html), a cycle-accurate Atari 8-bit computer emulator (800/XL/XE/5200) by Avery Lee.

This port brings Altirra's full emulation core to Linux using SDL2, OpenGL, and Dear ImGui, replacing the Windows-native UI and display layers while keeping the emulation engine unchanged.

## Features

### Emulation (from upstream Altirra)

- Cycle-accurate CPU emulation: 6502, 65C02, 65C816, 6809, Z80, 8048, 8051
- ANTIC, GTIA, POKEY, and PIA chip emulation
- Disk drive emulation (D1-D15) with SIO bus timing
- Cassette tape emulation with playback, recording, and turbo modes
- Cartridge support (100+ mapper types)
- H: host device and PCLink file sharing
- IDE hard disk emulation
- Modem emulation via TCP sockets
- Save states
- Built-in BASIC compiler/interpreter

### Linux Frontend

- **Display**: SDL2 + OpenGL with bilinear/point filtering, multiple stretch modes (aspect-preserving, square pixels, integral scaling), PAR correction, HiDPI support, adaptive vsync
- **Audio**: SDL2 audio output with POKEY emulation
- **Input**: SDL2 keyboard + joystick/gamepad mapping with configurable bindings and key/button capture editor
- **UI**: Full Dear ImGui interface with menus, dialogs, and configuration
- **Debugger**: Integrated ImGui debugger with registers, disassembly, memory viewer, watch expressions, call stack, history, breakpoints, source-level debugging, console, hardware register inspection, CPU target switching, CPU profiler with timeline/call graph/function detail, trace viewer, and runtime performance overlay
- **Status bar**: Always-visible status bar showing hardware mode, video standard, disk activity with track/sector numbers, H:/PCLink/IDE/Flash indicators, cartridge, cassette position, speed, recording, and FPS
- **Disk explorer**: Browse and modify Atari disk images (ATR/XFD/ATX) with extract, import, rename, delete, bulk import, drag-and-drop, and text EOL conversion
- **Profiles**: Hardware profile system with 5 built-in profiles (800, 1200XL, XL/XE, XEGS, 5200), profile manager dialog, and automatic profile switching when changing hardware modes
- **Settings**: Portable INI-based configuration at `~/.config/altirra/Altirra.ini`
- **File dialogs**: Native dialogs via zenity (GTK) or kdialog (KDE) with ImGui fallback
- **Firmware discovery**: Automatic ROM scanning from multiple paths
- **Screenshots**: PNG screenshot capture
- **Video recording**: AVI (ZMBV lossless, Raw, RLE) and H.264+AAC MP4 (requires optional FFmpeg libraries)
- **Audio recording**: WAV, raw PCM, SAP, VGM
- **Audio visualization**: Audio Monitor (4-channel POKEY waveform display with frequency, mode, and volume) and Audio Scope (oscilloscope with adjustable time base), both supporting dual POKEY (stereo)
- **Speed control**: Precision frame pacing with adjustable speed (50%-800%), turbo mode, slow motion

### Keyboard Shortcuts

| Key        | Action                                                    |
| ---------- | --------------------------------------------------------- |
| F12        | Toggle ImGui overlay (menus/dialogs)                      |
| Escape     | Close overlay                                             |
| F5         | Continue (debugger)                                       |
| F10        | Step over (debugger)                                      |
| F11        | Step into (debugger) / Fullscreen toggle (overlay hidden) |
| Alt+Return | Toggle fullscreen                                         |
| F1 (hold)  | Warp speed while held                                     |
| F7         | Quick save state                                          |
| F8         | Quick load state                                          |
| Ctrl+S     | Save settings                                             |
| Ctrl+V     | Paste text to emulator                                    |

## Building

### Requirements

- CMake 3.20+
- GCC 13+ or Clang 16+ (C++23 required)
- Ninja (recommended) or Make
- SDL2 development libraries
- OpenGL development libraries
- Git (for Dear ImGui fetch)
- FFmpeg development libraries (optional, enables H.264+AAC video recording)

#### Debian/Ubuntu

```bash
sudo apt install build-essential cmake ninja-build libsdl2-dev libgl-dev git

# Optional: H.264 video recording
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
```

#### Arch Linux

```bash
sudo pacman -S base-devel cmake ninja sdl2 mesa git

# Optional: H.264 video recording
sudo pacman -S ffmpeg
```

#### Fedora

```bash
sudo dnf install gcc-c++ cmake ninja-build SDL2-devel mesa-libGL-devel git

# Optional: H.264 video recording
sudo dnf install ffmpeg-free-devel
```

### Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is produced at `build/src/AltirraLinux/altirra`.

Dear ImGui v1.91.8 is automatically fetched via CMake FetchContent during the first build.

#### Build Types

| Type      | Description                                      |
| --------- | ------------------------------------------------ |
| `Release` | Optimized (`-O2`), recommended for regular use   |
| `Debug`   | Debug symbols, no optimization, `_DEBUG` defined |

#### Address Sanitizer (development)

```bash
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan
```

### Running Tests

```bash
build/src/ATTest/attest CoProc_6502 Emu_PokeyTimers Core_MD5 System_Vector System_CRC
```

## Usage

```bash
# Run with no arguments (starts with built-in kernel)
./build/src/AltirraLinux/altirra

# Load a disk/cartridge/tape image directly
./build/src/AltirraLinux/altirra game.atr
./build/src/AltirraLinux/altirra cart.car

# Start in fullscreen
./build/src/AltirraLinux/altirra --fullscreen game.atr

# Specify firmware ROM search path
./build/src/AltirraLinux/altirra --rom-path /path/to/roms

# Use alternate settings file
./build/src/AltirraLinux/altirra --config ~/my-altirra.ini
```

### Command-Line Options

| Option              | Description                               |
| ------------------- | ----------------------------------------- |
| `<file>`            | Load disk/cartridge/tape image at startup |
| `--portable`        | Store settings next to the executable     |
| `--config <path>`   | Use alternate settings file               |
| `--rom-path <path>` | Add firmware ROM search path              |
| `--fullscreen`      | Start in fullscreen mode                  |
| `--help`            | Show usage information                    |
| `--version`         | Show version                              |

### Firmware ROMs

Altirra includes a built-in replacement kernel, but for best compatibility you can provide original Atari OS ROMs. Place ROM files in any of these directories:

1. `~/.config/altirra/firmware/`
2. `<program-directory>/firmware/`
3. `/usr/share/altirra/firmware/`
4. `/usr/local/share/altirra/firmware/`

Use **System > Firmware Manager** (F12 to open overlay) to scan, identify, and assign firmware images.

### Configuration

Settings are stored in `~/.config/altirra/Altirra.ini` by default. All emulator settings (hardware mode, memory, video standard, input mappings, display options) are saved here. Use Ctrl+S or **System > Save Settings** to save, or settings are automatically saved on exit.

## Port Status

The Linux port is approximately **99.9% complete** relative to the Windows version.

### Fully Functional

- Complete emulation core (all CPUs, chips, peripherals, disk/cassette/cartridge)
- Full ImGui UI with all configuration dialogs
- Hardware profile system (5 built-in profiles, profile manager with create/rename/delete)
- Compatibility database for auto-suggesting configuration adjustments
- Integrated debugger with 13 tool windows (registers, disassembly, memory, console, breakpoints, watch, call stack, history, source code, printer output, profiler, trace viewer, debug display)
- Disk explorer with filesystem operations (browse, extract, import, rename, delete, drag-and-drop)
- Input mapping with binding editor and key/button capture
- Save states (quick save/load and file-based)
- Audio recording (WAV/PCM/SAP/VGM) and video recording (AVI + H.264/MP4)
- Audio visualization (Audio Monitor with per-channel waveforms, Audio Scope oscilloscope)
- Network emulation (modem TCP via POSIX sockets, socket layer with epoll)
- Firmware discovery and management
- Embedded kernel ROMs (10 firmware images assembled from 6502 source at build time)
- Embedded resources (audio samples, debugger help, diskloader128)
- Source-level debugging with symbol file support
- Precision frame pacing with speed control
- inotify-based directory watcher with polling fallback
- HiDPI display, PAR correction, adaptive vsync

### Intentionally Not Ported

- **DragonCart Ethernet** — niche Atari networking device, stubbed (modem TCP works via POSIX sockets)
- **Raw disk access** (ATIDEPhysicalDisk) — disabled for security
- **ETW tracing** (ATCreateNativeTracer) — Windows-specific; Linux has ImGui trace viewer instead
- **Named Pipe Serial / MidiMate / Browser** — Windows-specific peripherals with no Linux equivalent

## Project Structure

```
CMakeLists.txt                          # Top-level build
cmake/
  AltirraCompilerFlags.cmake            # Shared compiler settings (C++23, warnings)
  FetchImGui.cmake                      # Dear ImGui v1.91.8 auto-download
src/
  AltirraLinux/source/
    main_linux.cpp                      # Entry point, SDL2 window, main loop, settings
    stubs_linux.cpp                     # Implementations for Windows-only interfaces
    console_linux.cpp                   # Debug console, source window bridge
  AltirraShell/source/
    emulator_imgui.cpp                  # ImGui UI (menus, dialogs, status bar)
    debugger_imgui.cpp                  # ImGui debugger windows
    display_sdl2.cpp                    # SDL2+OpenGL display backend
  system/                               # VirtualDub-derived platform abstraction
  ATCore/                               # Device framework, schedulers, VFS
  ATCPU/                                # CPU emulators (6502, Z80, 6809, etc.)
  ATAudio/                              # POKEY sound emulation, audio pipeline
  ATEmulation/                          # Common chip emulations
  ATIO/                                 # Disk/cassette/cartridge image formats
  ATDevices/                            # Peripheral device implementations
  ATNetwork/                            # Ethernet emulation
  ATDebugger/                           # Debug infrastructure
  ATTest/                               # Test suite
  h/                                    # Headers (at/ and vd2/ trees)
```

## License

Altirra is licensed under the GNU General Public License v2 or later. See the original [Altirra website](https://virtualdub.org/altirra.html) for details.

This Linux port maintains the same license. The port modifies no emulation core code; it provides alternative frontend, display, audio, and input implementations for the Linux platform.

## Credits

- **Avery Lee** — Altirra emulator author and all emulation core code
- **Dear ImGui** — Immediate-mode GUI library by Omar Cornut ([ocornut/imgui](https://github.com/ocornut/imgui))
- **SDL2** — Cross-platform multimedia library ([libsdl.org](https://www.libsdl.org/))
