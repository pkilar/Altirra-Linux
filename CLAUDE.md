# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Altirra is an Atari 8-bit computer emulator (800/XL/XE/5200) written in C++ by Avery Lee. It features cycle-accurate emulation of multiple CPUs (6502, 65C02, 65C816, 6809, Z80, 8048, 8051), ANTIC/GTIA/POKEY/PIA chip emulation, an integrated debugger, networking, and extensive peripheral device support. Licensed under GPL v2+.

## Build System

### Linux (CMake)

**Toolchain**: CMake 3.20+, Ninja, GCC 13+ (tested with GCC 15.2.1), C++23

**Dependencies**: libsdl3-dev, libgl-dev, zlib1g-dev, xsltproc, MADS 2.1.0+ (6502 assembler)

**Building**:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Testing**: `cd build/src && ATTest/attest all` (25 tests, 0 failures)

**MADS**: Not in standard package repos. Build from source:
```bash
git clone https://github.com/tebe6502/Mad-Assembler.git
cd Mad-Assembler && fpc -Mdelphi -vh -O3 mads.pas
sudo install -m 755 mads /usr/local/bin/mads
```

**Embedded resources**: `cmake/EmbedResources.cmake` embeds 29 STUFF + 4 PNG resources at configure time. MADS assembles 10 firmware ROMs at configure time. Dear ImGui is fetched via FetchContent.

**Linux packaging**: Specs in `dist/` for Arch (`PKGBUILD`), RPM (`altirra.spec`), and Debian (`debian/`).

### Windows (Visual Studio)

**Toolchain**: Visual Studio 2022 (v17.14+), MSVC v143, Windows 11 SDK (10.0.26100.0+)

**Solutions** (all under `src/`):
- `Altirra.sln` — Main emulator (32 projects)
- `AltirraRMT.sln` — Raster Music Tracker plugins
- `ATHelpFile.sln` — Help documentation

**Build configurations**: Debug, Profile (optimized), ProfileClang (clang-cl), Analysis, Release (LTCG)
**Platforms**: x64 (primary), ARM64, Win32

**Building**:
1. Open `src/Altirra.sln` in VS2022
2. Set startup project to `Altirra`
3. Build Release x64 first (builds required build tools like ATCompiler)
4. Then build other configurations as needed
5. Output goes to `/out`, intermediates to `/obj`, libraries to `/lib`

**MADS path**: Configure via `localconfig/active/Altirra.local.props`:
```xml
<ATMadsPath>c:\path\to\mads.exe</ATMadsPath>
```

**Release packaging**: `python release.py` (requires Python 3.10+, 7-zip, AdvanceCOMP)

## Code Style

- Tabs for indentation in C++/headers (4-space width), spaces for .html/.txt files
- 80-character guideline
- Precompiled header: `stdafx.h` (included in every .cpp)
- C++ standard: latest (`/std:c++latest`)
- Warning level 4, with specific warnings disabled (4100, 4127, 4245, 4250, 4310, 4324, 4389, 4456, 4457, 4701, 4702, 4706)
- `__vectorcall` calling convention (except ARM64)
- Buffer security checks disabled for performance

## Architecture

### Solution Structure

The 32 projects form a layered architecture:

**Foundation layer** (`vd2/` headers — VirtualDub-derived utilities):
- `system` — Low-level: threading, file I/O, math, SIMD, memory, registry, string utilities
- `vdjson` — JSON parser
- `Kasumi` — Pixel/image manipulation
- `Riza` — AVI/bitmap handling
- `Tessa` — Graphics formatting
- `VDDisplay` — Display driver (Direct3D), rendering pipeline
- `Dita` — Accelerator/keyboard framework
- `Asuka` — Graphics tools

**Core emulation layer** (`at/` headers):
- `ATCore` — Device framework, schedulers, VFS, serialization, profiling, property sets
- `ATCPU` — CPU emulators (6502 family, Z80, 6809, 8048, 8051)
- `ATAudio` — POKEY sound chip emulation, audio pipeline
- `ATEmulation` — Common chip emulations (ACIA, CRTC, EEPROM, Flash, RTC, RIOT, SCSI, VIA)
- `ATIO` — I/O subsystem (disk/cassette/cartridge image formats, WAV, FLAC, Vorbis)
- `ATDevices` — Peripheral device implementations
- `ATNetwork` / `ATNetworkSockets` — Ethernet emulation, TCP/IP stack
- `ATDebugger` — Debugging infrastructure (breakpoints, symbols, disassembly)
- `ATVM` — Virtual machine for custom devices
- `ATBasic` — Built-in BASIC compiler/interpreter
- `ATCompiler` — Build tool (lzpack, makereloc, mkfsdos2)

**UI layer (Windows)**:
- `ATUI` — Abstract UI layer
- `ATUIControls` — Custom controls (buttons, lists, sliders, text editor)
- `ATNativeUI` — Windows native UI wrapper (dialogs, themes, message loop)
- `ATAppBase` — Application base (exception filter, CRT hooks)

**UI layer (Linux)**:
- `AltirraShell` — ImGui-based UI: menus, config dialogs, disk explorer, status bar, 13-window debugger
- `AltirraLinux` — Linux application: SDL3 window management, main loop, settings (INI), stubs for Windows-only symbols

**Application (Windows)** (`Altirra`):
- `ATSimulator` (simulator.h) — Central orchestrator connecting all emulation components
- `cmd*.cpp` files — Command handlers organized by domain (audio, cart, cassette, cpu, debug, input, options, system, tools, view, window)
- `devicemanager.h` — Device tree management
- `main.cpp` — WinMain entry point, initialization, message pump

**Application (Linux)**:
- Shares `ATSimulator` and all core emulation with Windows
- `src/AltirraShell/source/emulator_imgui.cpp` — ImGui UI (menus, dialogs, status bar)
- `src/AltirraShell/source/debugger_imgui.cpp` — ImGui debugger (13 windows + toolbar)
- `src/AltirraShell/source/display_sdl3.cpp` — SDL3+OpenGL display backend
- `src/AltirraShell/source/commands_linux.cpp` — Linux UI command handlers
- `src/AltirraLinux/source/main_linux.cpp` — Main loop, window management, settings

**Kernel** (`src/Kernel/`):
- 6502 assembly source for Atari OS ROM replacements
- Built with MADS assembler via NMake (`src/Kernel/Makefile`)
- Produces kernel.rom, kernelxl.rom, kernel816.rom, and various firmware images

### Key Design Patterns

**Device Framework**: Component-based with `IATDevice` as the root interface. Devices acquire capabilities by implementing additional interfaces (e.g., `IATDeviceMemMap`, `IATDeviceFirmware`, `IATDeviceScheduling`, `IATDeviceAudioOutput`). Each interface has a `kTypeID` for runtime type discovery via `IVDUnknown::AsInterface()`. Device lifecycle: factory creates → `SetManager` → `Init` → operational → `Shutdown`.

**Scheduler System**: Two schedulers — a fast scheduler (cycle-granularity) for timing-critical events, and a slow scheduler (scanline-rate, 114 cycles) for lower-priority tasks. Devices receive both via `IATDeviceScheduling::InitScheduling()`.

**Service Locator**: `IATDeviceManager::GetService<T>()` provides access to shared services (scheduling, memory, firmware manager, etc.) using type IDs.

**Save States**: Serialization framework (`IATDeviceSnapshot`) for capturing/restoring emulator state. Devices declare `IsSaveStateAgnostic()` if they have no state.

**Command System**: Commands registered through `ATUICommandManager`, handlers in `cmd*.cpp` files access the global `g_sim` simulator instance.

**Property Sets**: `ATPropertySet` for generic device configuration, used for settings persistence and device creation.

### Header Organization

Two parallel header trees under `src/h/`:
- `at/` — Altirra-specific: `atcore/`, `ataudio/`, `atcpu/`, `atdebugger/`, `atdevices/`, `atemulation/`, `atio/`, `atnativeui/`, `atnetwork/`, `attest/`, `atui/`, `atuicontrols/`, `atvm/`
- `vd2/` — VirtualDub-derived: `system/`, `Dita/`, `Kasumi/`, `Riza/`, `Tessa/`, `VDDisplay/`, `vdjson/`, `external/`

## Testing

**ATTest project** (`src/ATTest/`) — Test executable covering:
- CPU emulation (`TestCoProc_6502`)
- POKEY timers/pots (`TestEmu_PokeyTimers`, `TestEmu_PokeyPots`)
- I/O formats (disk images, FLAC, Vorbis, tape write, VirtFAT32)
- System library (vectors, hash maps, math, CRC, zip, filesys)
- Display/graphics (Kasumi pixel ops, resampler, uberblit)
- Debugger (history tree, symbol I/O)
- Core (VFS, checksum, MD5, FFT)

**State testing**: `scripts/statetest.py`

## Scripts

- `release.py` — Full release automation (build, package, compress)
- `scripts/makefeed.py` — Update feed generation
- `scripts/makeicon.py` — Icon creation
- `scripts/hashfont.py` — Font hashing utility
- `scripts/dumpexports.py` — Export table dumping

## Local Configuration

Copy examples from `localconfig/example/` to `localconfig/active/`:
- `Altirra.local.props` — MADS path override
- `PlatformSetup.local.props` — Compiler/toolset override

## CI/CD

**GitHub Actions** (`.github/workflows/`):
- `ci.yml` — Build and test on every push/PR to `main` (Ubuntu 24.04, g++-14, MADS from source)
- `release.yml` — On `v*` tag push: build Arch/RPM/DEB packages in native containers, create draft GitHub release

## Platform Notes

- **Windows**: Win32 API, Direct3D, COM throughout. VS2022 .sln/.vcxproj build.
- **Linux**: SDL3+OpenGL display, Dear ImGui UI, POSIX sockets, inotify. CMake/Ninja build. ~99.9% feature-complete.
- Linux porting uses separate `*_linux.cpp` files and `#ifdef VD_PLATFORM_LINUX` guards to keep platform code isolated
- The `vd2/system` library provides platform abstraction (threading, file I/O, registry) with Linux implementations in `*_linux.cpp` files
- SIMD: separate code paths for SSE2 (x86/x64) and NEON (ARM64)
- Settings: Windows uses registry, Linux uses portable INI at `~/.config/altirra/Altirra.ini`
- File dialogs on Linux: zenity (GTK) -> kdialog (KDE) -> ImGui fallback
