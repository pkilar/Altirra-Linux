//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/thread.h>
#include <vd2/VDDisplay/display.h>

#include <at/atcore/device.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/media.h>
#include <at/atcore/profile.h>
#include <at/atcore/serializable.h>
#include <at/ataudio/audiooutput.h>
#include <at/atio/image.h>

#include "simulator.h"
#include "devicemanager.h"
#include "joystick.h"
#include "debugger.h"
#include "settings.h"
#include "firmwaremanager.h"
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include "oshelper.h"
#include "gtia.h"
#include "diskinterface.h"
#include "cassette.h"
#include "cartridge.h"
#include "inputmanager.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "uicommondialogs.h"
#include "uikeyboard.h"
#include "uiqueue.h"
#include "versioninfo.h"
#include "inputmap.h"

#include <display_sdl2.h>
#include <input_sdl2.h>
#include <imgui_manager.h>
#include <debugger_imgui.h>
#include <emulator_imgui.h>
#include <imgui.h>
#include "uienhancedtext.h"
#include <at/atcore/devicevideo.h>
#include <at/atui/uiwidget.h>

#include <SDL.h>
#include <GL/gl.h>

#include <vd2/system/time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cxxabi.h>
#include <execinfo.h>
#include <getopt.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

// Forward declarations from debugger.cpp
void ATInitDebugger();
void ATShutdownDebugger();

#include "compatengine.h"

// Forward declarations from uiregistry.cpp
void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);

// Enhanced text engine accessor (defined in stubs_linux.cpp)
IATUIEnhancedTextEngine *ATUIGetEnhancedTextEngine();

// Global simulator instance — matches Windows main.cpp
ATSimulator g_sim;

// Display
static ATDisplaySDL2 *g_pDisplay = nullptr;

// Input
static ATInputSDL2 *g_pInput = nullptr;

// ImGui manager (also referenced by console_linux.cpp)
ATImGuiManager *g_pImGui = nullptr;

// Joystick manager factory (defined in joystick_sdl2.cpp)
IATJoystickManager *ATCreateJoystickManagerSDL2();

// Fullscreen callback and state (defined in stubs_linux.cpp)
void ATSetFullscreenCallback(void (*pfn)(bool));
bool ATUIGetFullscreen();
void ATSetFullscreen(bool);

// Window resize callback (defined in stubs_linux.cpp)
void ATSetWindowSizeCallback(void (*pfn)(int, int));

// Settings path — declared early so crash handler can access it
static VDStringW g_settingsPath;

static volatile sig_atomic_t g_running = 1;

static void SignalHandler(int) {
	g_running = 0;
}

// Crash signal handler — print backtrace and exit
static void CrashSignalHandler(int sig) {
	// Prevent re-entry
	static volatile sig_atomic_t s_crashing = 0;
	if (s_crashing)
		_exit(128 + sig);
	s_crashing = 1;

	const char *signame = "Unknown";
	switch (sig) {
		case SIGSEGV: signame = "SIGSEGV (Segmentation fault)"; break;
		case SIGABRT: signame = "SIGABRT (Aborted)"; break;
		case SIGFPE:  signame = "SIGFPE (Floating point exception)"; break;
		case SIGBUS:  signame = "SIGBUS (Bus error)"; break;
		case SIGILL:  signame = "SIGILL (Illegal instruction)"; break;
	}

	// Use write() directly — async-signal-safe
	const char hdr[] = "\n=== Altirra crashed ===\nSignal: ";
	write(STDERR_FILENO, hdr, sizeof(hdr) - 1);
	write(STDERR_FILENO, signame, strlen(signame));
	write(STDERR_FILENO, "\n\nBacktrace:\n", 13);

	// Capture backtrace (backtrace() is not async-signal-safe on all
	// platforms, but it works on glibc and is our best option)
	void *frames[64];
	int nframes = backtrace(frames, 64);
	backtrace_symbols_fd(frames, nframes, STDERR_FILENO);

	write(STDERR_FILENO, "\nAttempting to save settings...\n", 31);

	// Try to save settings — this may fail if heap is corrupted
	// but it's worth attempting
	if (!g_settingsPath.empty()) {
		try {
			ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
			ATUISaveRegistry(g_settingsPath.c_str());
			write(STDERR_FILENO, "Settings saved.\n", 16);
		} catch (...) {
			write(STDERR_FILENO, "Settings save failed.\n", 22);
		}
	}

	// Re-raise with default handler to get core dump
	signal(sig, SIG_DFL);
	raise(sig);
}

// Mouse pointer auto-hide state
static Uint32 g_lastMouseMoveTime = 0;
static bool g_cursorHidden = false;
static const Uint32 kCursorHideDelayMs = 3000;

// Tracks whether we paused due to window losing focus (vs. user manual pause)
static bool g_pausedByInactive = false;

// Tracks whether we already auto-showed the overlay for the current stop event,
// so the user can dismiss it with F12/Escape without it re-appearing every frame.
static bool g_debuggerAutoShowed = false;

// Global accessor for the display backend — used by emulator_imgui.cpp
ATDisplaySDL2 *ATGetLinuxDisplay() { return g_pDisplay; }

// SDL window pointer for fullscreen toggle callback
static SDL_Window *g_pWindow = nullptr;

// Public accessor for SDL window — used by stubs_linux.cpp (mouse grab etc.)
SDL_Window *ATGetLinuxWindow() { return g_pWindow; }

static void SetFullscreenImpl(bool fs) {
	if (g_pWindow) {
		SDL_SetWindowFullscreen(g_pWindow, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		SDL_SetWindowGrab(g_pWindow, (fs && ATUIGetConstrainMouseFullScreen()) ? SDL_TRUE : SDL_FALSE);
	}
}

static void SetWindowSizeImpl(int w, int h) {
	if (g_pWindow)
		SDL_SetWindowSize(g_pWindow, w, h);
}

// Registry provider (owned, freed at shutdown)
static VDRegistryProviderMemory *g_pRegistryMemory = nullptr;

// Save settings — used by emulator_imgui.cpp and Ctrl+S shortcut
void ATLinuxSaveSettings() {
	ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
	if (!g_settingsPath.empty())
		ATUISaveRegistry(g_settingsPath.c_str());
}

///////////////////////////////////////////////////////////////////////////
// Settings path helpers
///////////////////////////////////////////////////////////////////////////

static void EnsureDirectoryExists(const VDStringW& path) {
	VDStringA u8 = VDTextWToU8(path);
	struct stat st;
	if (stat(u8.c_str(), &st) != 0) {
		mkdir(u8.c_str(), 0755);
	}
}

static VDStringW ATGetLinuxConfigDir() {
	const char *xdgConfig = getenv("XDG_CONFIG_HOME");
	VDStringW configDir;

	if (xdgConfig && xdgConfig[0]) {
		configDir = VDTextU8ToW(VDStringA(xdgConfig));
	} else {
		const char *home = getenv("HOME");
		if (!home)
			home = "/tmp";
		VDStringW homeW = VDTextU8ToW(VDStringA(home));
		configDir = VDMakePath(homeW.c_str(), L".config");
	}

	VDStringW altirraDir = VDMakePath(configDir.c_str(), L"altirra");
	EnsureDirectoryExists(configDir);
	EnsureDirectoryExists(altirraDir);
	return altirraDir;
}

static VDStringW ATGetLinuxSettingsPath() {
	return VDMakePath(ATGetLinuxConfigDir().c_str(), L"Altirra.ini");
}

///////////////////////////////////////////////////////////////////////////
// Firmware path scanning
///////////////////////////////////////////////////////////////////////////

static VDStringW s_extraRomPath;

static void ATScanLinuxFirmwarePaths(const VDStringW& configDir) {
	// Build list of firmware search directories:
	// 1. ~/.config/altirra/firmware/
	// 2. <program-dir>/firmware/
	// 3. /usr/share/altirra/firmware/
	// 4. /usr/local/share/altirra/firmware/

	vdvector<VDStringW> paths;
	paths.push_back(VDMakePath(configDir.c_str(), L"firmware"));
	paths.push_back(VDMakePath(VDGetProgramPath().c_str(), L"firmware"));
	paths.push_back(VDStringW(L"/usr/share/altirra/firmware"));
	paths.push_back(VDStringW(L"/usr/local/share/altirra/firmware"));

	if (!s_extraRomPath.empty())
		paths.push_back(s_extraRomPath);

	// Ensure user firmware directory exists
	EnsureDirectoryExists(paths[0]);

	// Set the primary firmware path in registry so the firmware manager
	// can resolve relative paths. Use the user config firmware dir.
	{
		VDRegistryAppKey key("Firmware", true);
		key.setString("Firmware base path", paths[0].c_str());
	}

	// Log discovered firmware directories
	for (const auto& p : paths) {
		VDStringA u8 = VDTextWToU8(p);
		struct stat st;
		if (stat(u8.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
			fprintf(stderr, "Firmware search path: %s\n", u8.c_str());
		}
	}
}

void ATGetFirmwareSearchPaths(vdvector<VDStringW>& outPaths) {
	VDStringW configDir = ATGetLinuxConfigDir();
	outPaths.clear();
	outPaths.push_back(VDMakePath(configDir.c_str(), L"firmware"));
	outPaths.push_back(VDMakePath(VDGetProgramPath().c_str(), L"firmware"));
	outPaths.push_back(VDStringW(L"/usr/share/altirra/firmware"));
	outPaths.push_back(VDStringW(L"/usr/local/share/altirra/firmware"));

	if (!s_extraRomPath.empty())
		outPaths.push_back(s_extraRomPath);
}

///////////////////////////////////////////////////////////////////////////
// CLI argument parsing
///////////////////////////////////////////////////////////////////////////

struct ATLinuxOptions {
	bool portable = false;
	bool fullscreen = false;
	bool showHelp = false;
	bool showVersion = false;
	VDStringW configPath;
	VDStringW romPath;
	VDStringW imagePath;
};

static void PrintUsage(const char *progname) {
	fprintf(stderr,
		"Usage: %s [options] [file]\n"
		"\n"
		"Options:\n"
		"  <file>              Load disk/cart/tape image\n"
		"  --portable          Use settings from program directory\n"
		"  --config <path>     Use alternate settings file\n"
		"  --rom-path <path>   Add firmware ROM search path\n"
		"  --fullscreen        Start in fullscreen mode\n"
		"  --help              Show this help\n"
		"  --version           Show version\n",
		progname
	);
}

static void PrintVersion() {
	fprintf(stderr, "Altirra (Linux port) " AT_VERSION "\n");
}

static ATLinuxOptions ParseArguments(int argc, char *argv[]) {
	ATLinuxOptions opts;

	static const struct option long_options[] = {
		{"portable",    no_argument,       nullptr, 'p'},
		{"config",      required_argument, nullptr, 'c'},
		{"rom-path",    required_argument, nullptr, 'r'},
		{"fullscreen",  no_argument,       nullptr, 'f'},
		{"help",        no_argument,       nullptr, 'h'},
		{"version",     no_argument,       nullptr, 'v'},
		{nullptr, 0, nullptr, 0}
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "pc:r:fhv", long_options, nullptr)) != -1) {
		switch (opt) {
			case 'p':
				opts.portable = true;
				break;
			case 'c':
				opts.configPath = VDTextU8ToW(VDStringA(optarg));
				break;
			case 'r':
				opts.romPath = VDTextU8ToW(VDStringA(optarg));
				break;
			case 'f':
				opts.fullscreen = true;
				break;
			case 'h':
				opts.showHelp = true;
				break;
			case 'v':
				opts.showVersion = true;
				break;
			default:
				break;
		}
	}

	// Remaining non-option argument is the image file
	if (optind < argc) {
		opts.imagePath = VDTextU8ToW(VDStringA(argv[optind]));
	}

	return opts;
}

///////////////////////////////////////////////////////////////////////////
// SDL init
///////////////////////////////////////////////////////////////////////////

static bool InitSDL(SDL_Window *&window, SDL_GLContext &glContext, bool fullscreen) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) < 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
	if (fullscreen)
		windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	// Restore saved window geometry, or use defaults (2x NTSC resolution)
	int winX = SDL_WINDOWPOS_CENTERED;
	int winY = SDL_WINDOWPOS_CENTERED;
	int winW = 912;
	int winH = 524;
	bool winMaximized = false;

	VDRegistryAppKey key("Window", false);
	if (key.getInt("Width", 0) > 0) {
		winW = key.getInt("Width", 912);
		winH = key.getInt("Height", 524);
		winX = key.getInt("X", SDL_WINDOWPOS_CENTERED);
		winY = key.getInt("Y", SDL_WINDOWPOS_CENTERED);
		winMaximized = key.getBool("Maximized", false);

		// Sanity check: ensure window is at least partially visible
		int numDisplays = SDL_GetNumVideoDisplays();
		if (numDisplays > 0 && winX != SDL_WINDOWPOS_CENTERED) {
			SDL_Rect bounds;
			bool onScreen = false;
			for (int i = 0; i < numDisplays; ++i) {
				if (SDL_GetDisplayBounds(i, &bounds) == 0) {
					if (winX + winW > bounds.x && winX < bounds.x + bounds.w &&
						winY + winH > bounds.y && winY < bounds.y + bounds.h) {
						onScreen = true;
						break;
					}
				}
			}
			if (!onScreen) {
				winX = SDL_WINDOWPOS_CENTERED;
				winY = SDL_WINDOWPOS_CENTERED;
			}
		}
	}

	if (winMaximized && !fullscreen)
		windowFlags |= SDL_WINDOW_MAXIMIZED;

	window = SDL_CreateWindow(
		"Altirra (Linux)",
		winX,
		winY,
		winW, winH,
		windowFlags
	);

	if (!window) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return false;
	}

	glContext = SDL_GL_CreateContext(window);
	if (!glContext) {
		fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		return false;
	}

	// Enable vsync (adaptive if supported and configured)
	if (ATUIGetFrameRateVSyncAdaptive()) {
		if (SDL_GL_SetSwapInterval(-1) < 0)
			SDL_GL_SetSwapInterval(1);
	} else {
		SDL_GL_SetSwapInterval(1);
	}

	// On Wayland, the window may not be visible until the first buffer is
	// committed. Do a clear + swap so the compositor shows the window
	// immediately, and raise the window to request focus.
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	SDL_GL_SwapWindow(window);
	SDL_RaiseWindow(window);

	return true;
}

///////////////////////////////////////////////////////////////////////////
// Keyboard mapping → POKEY
///////////////////////////////////////////////////////////////////////////

extern ATUIKeyboardOptions g_kbdOpts;

// Track active special keys (Start/Select/Option/Break) by their VK code
// so we can release them correctly on key-up even if modifiers changed.
struct ActiveSpecialKey {
	uint32 vk;
	uint32 scanCode;
};
static std::vector<ActiveSpecialKey> s_activeSpecialKeys;

static bool IsExtendedKey(SDL_Scancode sc) {
	switch (sc) {
		case SDL_SCANCODE_INSERT:
		case SDL_SCANCODE_DELETE:
		case SDL_SCANCODE_HOME:
		case SDL_SCANCODE_END:
		case SDL_SCANCODE_PAGEUP:
		case SDL_SCANCODE_PAGEDOWN:
		case SDL_SCANCODE_LEFT:
		case SDL_SCANCODE_RIGHT:
		case SDL_SCANCODE_UP:
		case SDL_SCANCODE_DOWN:
		case SDL_SCANCODE_KP_ENTER:
			return true;
		default:
			return false;
	}
}

static void HandleSpecialKey(uint32 scanCode, bool state) {
	switch(scanCode) {
		case kATUIKeyScanCode_Start:
			g_sim.GetGTIA().SetConsoleSwitch(0x01, state);
			break;
		case kATUIKeyScanCode_Select:
			g_sim.GetGTIA().SetConsoleSwitch(0x02, state);
			break;
		case kATUIKeyScanCode_Option:
			g_sim.GetGTIA().SetConsoleSwitch(0x04, state);
			break;
		case kATUIKeyScanCode_Break:
			g_sim.GetPokey().SetBreakKeyState(state, !g_kbdOpts.mbFullRawKeys);
			break;
	}
}

static void ProcessAtariKeyboard(const SDL_Event& event) {
	if (event.type == SDL_KEYDOWN) {
		uint32 vk = ATInputSDL2::TranslateSDLScancode(event.key.keysym.scancode);
		if (vk == kATInputCode_None)
			return;

		SDL_Keymod mod = SDL_GetModState();
		bool alt   = (mod & KMOD_ALT) != 0;
		bool ctrl  = (mod & KMOD_CTRL) != 0;
		bool shift = (mod & KMOD_SHIFT) != 0;
		bool ext   = IsExtendedKey(event.key.keysym.scancode);

		uint32 scanCode;
		if (!ATUIGetScanCodeForVirtualKey(vk, alt, ctrl, shift, ext, scanCode))
			return;

		if (scanCode >= kATUIKeyScanCodeFirst) {
			// Special key (Start/Select/Option/Break)
			HandleSpecialKey(scanCode, true);

			// Track for release on key-up
			bool found = false;
			for (auto& k : s_activeSpecialKeys) {
				if (k.vk == vk) {
					k.scanCode = scanCode;
					found = true;
					break;
				}
			}
			if (!found)
				s_activeSpecialKeys.push_back({vk, scanCode});
		} else if (g_kbdOpts.mbRawKeys) {
			g_sim.GetPokey().PushRawKey(scanCode, !g_kbdOpts.mbFullRawKeys);
		} else {
			g_sim.GetPokey().PushKey(scanCode, event.key.repeat != 0);
		}
	} else if (event.type == SDL_KEYUP) {
		uint32 vk = ATInputSDL2::TranslateSDLScancode(event.key.keysym.scancode);
		if (vk == kATInputCode_None)
			return;

		// Release any special key tracked for this VK
		for (auto it = s_activeSpecialKeys.begin(); it != s_activeSpecialKeys.end(); ++it) {
			if (it->vk == vk) {
				HandleSpecialKey(it->scanCode, false);
				s_activeSpecialKeys.erase(it);
				break;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// Event handling
///////////////////////////////////////////////////////////////////////////

// Quick save state (in-memory, shared with emulator_imgui.cpp via extern)
static vdrefptr<IATSerializable> s_pQuickState;

static void HandleShortcuts(const SDL_Event& event) {
	if (event.type != SDL_KEYDOWN)
		return;

	bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
	bool ctrl  = (event.key.keysym.mod & KMOD_CTRL) != 0;
	bool alt   = (event.key.keysym.mod & KMOD_ALT) != 0;

	switch (event.key.keysym.scancode) {
		// Shift+F1: cycle quick maps
		case SDL_SCANCODE_F1: {
			if (shift) {
				ATInputManager *inputMgr = g_sim.GetInputManager();
				if (inputMgr) {
					ATInputMap *pMap = inputMgr->CycleQuickMaps();
					if (pMap) {
						VDStringA name = VDTextWToU8(VDStringW(pMap->GetName()));
						char msg[128];
						snprintf(msg, sizeof(msg), "Quick map: %s", name.c_str());
						ATImGuiShowToast(msg);
					} else {
						ATImGuiShowToast("Quick maps disabled");
					}
				}
			} else if (ctrl) {
				// Ctrl+F1: cycle display filter mode
				ATDisplayFilterMode fm = ATUIGetDisplayFilterMode();
				fm = (ATDisplayFilterMode)(((int)fm + 1) % kATDisplayFilterModeCount);
				ATUISetDisplayFilterMode(fm);
				ATDisplaySDL2 *disp = ATGetLinuxDisplay();
				if (disp) {
					disp->SetFilterMode(
						(fm == kATDisplayFilterMode_Point)
						? IVDVideoDisplay::kFilterPoint
						: IVDVideoDisplay::kFilterBilinear);
				}
			}
			return;
		}

		// F5: warm reset / Shift+F5: cold reset (non-debugger)
		// In debugger context: F5 = run
		case SDL_SCANCODE_F5: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg && g_pImGui && g_pImGui->IsVisible() && ATImGuiDebuggerIsVisible()) {
				if (dbg->IsRunning())
					dbg->Break();
				else
					dbg->Run(kATDebugSrcMode_Disasm);
			} else if (shift) {
				g_sim.ColdReset();
				ATImGuiShowToast("Cold reset");
			} else {
				g_sim.WarmReset();
				ATImGuiShowToast("Warm reset");
			}
			return;
		}

		// F8: debugger run/stop (global, always available)
		case SDL_SCANCODE_F8: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg) {
				if (dbg->IsRunning())
					dbg->Break();
				else
					dbg->Run(kATDebugSrcMode_Disasm);
			}
			return;
		}

		// F9: toggle pause (display) / toggle breakpoint (debugger)
		case SDL_SCANCODE_F9: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg && g_pImGui && g_pImGui->IsVisible() && ATImGuiDebuggerIsVisible()) {
				// Toggle breakpoint at current disassembly address
				uint16 addr = ATImGuiDebuggerGetDisasmAddr();
				dbg->ToggleBreakpoint(addr);
			} else {
				if (g_sim.IsPaused()) {
					g_sim.Resume();
					ATImGuiShowToast("Resumed");
				} else {
					g_sim.Pause();
					// Suppress the auto-show overlay that normally activates
					// when emulation enters stopped state (for debugger
					// breakpoints).  User-initiated pause via F9 should NOT
					// pop up the overlay.
					g_debuggerAutoShowed = true;
					ATImGuiDebuggerDidBreak();
					ATImGuiShowToast("Paused");
				}
			}
			return;
		}

		// Alt+F10: save screenshot
		case SDL_SCANCODE_F10: {
			if (alt) {
				VDPixmapBuffer pxbuf;
				VDPixmap px;
				if (g_sim.GetGTIA().GetLastFrameBuffer(pxbuf, px)) {
					char fname[256];
					time_t now = time(nullptr);
					struct tm tm;
					localtime_r(&now, &tm);
					const char *home = getenv("HOME");
					snprintf(fname, sizeof(fname), "%s/altirra_%04d%02d%02d_%02d%02d%02d.png",
						home ? home : "/tmp",
						tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
						tm.tm_hour, tm.tm_min, tm.tm_sec);
					try {
						VDStringW wpath = VDTextU8ToW(VDStringA(fname));
						ATSaveFrame(px, wpath.c_str());
						ATImGuiShowToast("Screenshot saved");
					} catch (...) {
						ATImGuiShowToast("Screenshot failed");
					}
				}
				return;
			}
			break;
		}

		default:
			break;
	}

	// Debug shortcuts: F10 step over, F11 step into/out
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	switch (event.key.keysym.scancode) {
		case SDL_SCANCODE_F10:
			if (!dbg->IsRunning())
				dbg->StepOver(kATDebugSrcMode_Disasm);
			break;

		case SDL_SCANCODE_F11:
			if (!dbg->IsRunning()) {
				if (shift)
					dbg->StepOut(kATDebugSrcMode_Disasm);
				else
					dbg->StepInto(kATDebugSrcMode_Disasm);
			}
			break;

		default:
			break;
	}
}

static void ProcessEvents(SDL_Window *window) {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		// Always let ImGui see the event (for input state tracking)
		if (g_pImGui)
			g_pImGui->ProcessEvent(event);

		// Input capture mode: intercept keyboard/controller events for binding editor
		if (ATImGuiIsCapturingInput()) {
			if (event.type == SDL_KEYDOWN && !event.key.repeat) {
				SDL_Scancode sc = event.key.keysym.scancode;

				// Shift+Escape = cancel capture
				if (sc == SDL_SCANCODE_ESCAPE && (event.key.keysym.mod & KMOD_SHIFT)) {
					ATImGuiOnCapturedInput(kATInputCode_None);
					continue;
				}

				// Skip bare shift press — wait for key-up to distinguish L/R
				if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT)
					continue;

				uint32 code = ATInputSDL2::TranslateSDLScancode(sc);
				if (code != kATInputCode_None) {
					ATImGuiOnCapturedInput(code);
					continue;
				}
			} else if (event.type == SDL_KEYUP) {
				// Resolve L/R shift on key-up (user pressed and released shift alone)
				if (event.key.keysym.scancode == SDL_SCANCODE_LSHIFT) {
					ATImGuiOnCapturedInput(kATInputCode_KeyLShift);
					continue;
				} else if (event.key.keysym.scancode == SDL_SCANCODE_RSHIFT) {
					ATImGuiOnCapturedInput(kATInputCode_KeyRShift);
					continue;
				}
			} else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
				uint32 code = kATInputCode_JoyButton0 + event.cbutton.button;
				ATImGuiOnCapturedInput(code);
				continue;
			} else if (event.type == SDL_CONTROLLERAXISMOTION) {
				sint32 rawValue = event.caxis.value;
				if (rawValue > 24000 || rawValue < -24000) {
					uint32 axisCode;
					switch (event.caxis.axis) {
						case SDL_CONTROLLER_AXIS_LEFTX:       axisCode = kATInputCode_JoyHoriz1; break;
						case SDL_CONTROLLER_AXIS_LEFTY:        axisCode = kATInputCode_JoyVert1; break;
						case SDL_CONTROLLER_AXIS_RIGHTX:       axisCode = kATInputCode_JoyHoriz3; break;
						case SDL_CONTROLLER_AXIS_RIGHTY:       axisCode = kATInputCode_JoyVert3; break;
						case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  axisCode = kATInputCode_JoyVert2; break;
						case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:  axisCode = kATInputCode_JoyVert4; break;
						default: goto not_captured;
					}
					ATImGuiOnCapturedInput(axisCode);
					continue;
				}
			}
			not_captured:;
		}

		// Track mouse movement for auto-hide cursor
		if (event.type == SDL_MOUSEMOTION) {
			g_lastMouseMoveTime = SDL_GetTicks();
			if (g_cursorHidden) {
				SDL_ShowCursor(SDL_ENABLE);
				g_cursorHidden = false;
			}
		}

		// F12 toggles debugger overlay
		if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_F12) {
			if (g_pImGui) {
				g_pImGui->ToggleVisible();
				// Show cursor when overlay becomes visible
				if (g_pImGui->IsVisible() && g_cursorHidden) {
					SDL_ShowCursor(SDL_ENABLE);
					g_cursorHidden = false;
				}
			}
			continue;
		}

		// Escape closes overlay (when visible and no popup active)
		if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE
			&& g_pImGui && g_pImGui->IsVisible()
			&& !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
			g_pImGui->SetVisible(false);
			continue;
		}

		// F1 warp: hold to enable, release to disable
		if (event.key.keysym.scancode == SDL_SCANCODE_F1
			&& !(event.key.keysym.mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT))) {
			if (event.type == SDL_KEYDOWN && !event.key.repeat) {
				ATUISetTurbo(true);
			} else if (event.type == SDL_KEYUP) {
				ATUISetTurbo(false);
			}
			continue;
		}

		// F1/F5/F8/F9 shortcuts always active
		if (event.type == SDL_KEYDOWN) {
			if (event.key.keysym.scancode == SDL_SCANCODE_F1
				|| event.key.keysym.scancode == SDL_SCANCODE_F5
				|| event.key.keysym.scancode == SDL_SCANCODE_F8
				|| event.key.keysym.scancode == SDL_SCANCODE_F9) {
				HandleShortcuts(event);
				continue;
			}

			// Alt+Enter = fullscreen toggle (always)
			// F11 = fullscreen toggle (when overlay hidden)
			if ((event.key.keysym.scancode == SDL_SCANCODE_RETURN
					&& (event.key.keysym.mod & KMOD_ALT))
				|| (event.key.keysym.scancode == SDL_SCANCODE_F11
					&& !(g_pImGui && g_pImGui->IsVisible()))) {
				ATSetFullscreen(!ATUIGetFullscreen());
				continue;
			}

			// Ctrl+O = open image, Ctrl+Shift+O = boot image
			if (event.key.keysym.scancode == SDL_SCANCODE_O
				&& (event.key.keysym.mod & KMOD_CTRL)) {
				if (event.key.keysym.mod & KMOD_SHIFT)
					ATImGuiBootImage();
				else
					ATImGuiOpenImage();
				continue;
			}

			// Ctrl+S = save settings
			if (event.key.keysym.scancode == SDL_SCANCODE_S
				&& (event.key.keysym.mod & KMOD_CTRL)) {
				ATLinuxSaveSettings();
				ATImGuiShowToast("Settings saved");
				continue;
			}

			// Alt+Shift+V = paste text to emulator
			if (event.key.keysym.scancode == SDL_SCANCODE_V
				&& (event.key.keysym.mod & KMOD_ALT)
				&& (event.key.keysym.mod & KMOD_SHIFT)) {
				ATImGuiPasteText();
				continue;
			}

			// Alt+Shift+C = copy frame to clipboard
			if (event.key.keysym.scancode == SDL_SCANCODE_C
				&& (event.key.keysym.mod & KMOD_ALT)
				&& (event.key.keysym.mod & KMOD_SHIFT)) {
				VDPixmapBuffer pxbuf;
				VDPixmap px;
				if (g_sim.GetGTIA().GetLastFrameBuffer(pxbuf, px)) {
					ATCopyFrameToClipboard(px);
					ATImGuiShowToast("Frame copied");
				}
				continue;
			}

			// Alt+F10 = save screenshot
			if (event.key.keysym.scancode == SDL_SCANCODE_F10
				&& (event.key.keysym.mod & KMOD_ALT)) {
				HandleShortcuts(event);
				continue;
			}

			// Alt+Backspace = toggle slow motion
			if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE
				&& (event.key.keysym.mod & KMOD_ALT)) {
				ATUISetSlowMotion(!ATUIGetSlowMotion());
				ATImGuiShowToast(ATUIGetSlowMotion() ? "Slow motion" : "Normal speed");
				continue;
			}

			// Ctrl+Q = quit
			if (event.key.keysym.scancode == SDL_SCANCODE_Q
				&& (event.key.keysym.mod & KMOD_CTRL)) {
				ATImGuiRequestQuit();
				if (g_pImGui && !g_pImGui->IsVisible())
					g_pImGui->ToggleVisible();
				continue;
			}
		}

		// When overlay is visible, handle debug shortcuts
		if (g_pImGui && g_pImGui->IsVisible()) {
			HandleShortcuts(event);

			// If ImGui wants the input, don't pass to emulation — but always
			// let system events (quit, window close/focus) through.
			if (event.type != SDL_QUIT && event.type != SDL_WINDOWEVENT
				&& (g_pImGui->WantCaptureMouse() || g_pImGui->WantCaptureKeyboard()))
				continue;
		}

		// Enhanced text engine input routing — when active and not in raw
		// input mode, the engine handles keyboard events instead of POKEY.
		{
			IATUIEnhancedTextEngine *enhText = ATUIGetEnhancedTextEngine();
			if (enhText && !enhText->IsRawInputEnabled()) {
				if (event.type == SDL_KEYDOWN) {
					uint32 vk = 0;
					switch (event.key.keysym.scancode) {
						case SDL_SCANCODE_LEFT:      vk = kATUIVK_Left; break;
						case SDL_SCANCODE_RIGHT:     vk = kATUIVK_Right; break;
						case SDL_SCANCODE_UP:        vk = kATUIVK_Up; break;
						case SDL_SCANCODE_DOWN:      vk = kATUIVK_Down; break;
						case SDL_SCANCODE_BACKSPACE: vk = kATUIVK_Back; break;
						case SDL_SCANCODE_DELETE:    vk = kATUIVK_Delete; break;
						case SDL_SCANCODE_RETURN:
						case SDL_SCANCODE_KP_ENTER:  vk = kATUIVK_Return; break;
						case SDL_SCANCODE_CAPSLOCK:  vk = kATUIVK_CapsLock; break;
						default: break;
					}
					if (vk && enhText->OnKeyDown(vk))
						continue;
				} else if (event.type == SDL_KEYUP) {
					uint32 vk = 0;
					switch (event.key.keysym.scancode) {
						case SDL_SCANCODE_LEFT:      vk = kATUIVK_Left; break;
						case SDL_SCANCODE_RIGHT:     vk = kATUIVK_Right; break;
						case SDL_SCANCODE_UP:        vk = kATUIVK_Up; break;
						case SDL_SCANCODE_DOWN:      vk = kATUIVK_Down; break;
						case SDL_SCANCODE_BACKSPACE: vk = kATUIVK_Back; break;
						case SDL_SCANCODE_DELETE:    vk = kATUIVK_Delete; break;
						case SDL_SCANCODE_RETURN:
						case SDL_SCANCODE_KP_ENTER:  vk = kATUIVK_Return; break;
						case SDL_SCANCODE_CAPSLOCK:  vk = kATUIVK_CapsLock; break;
						default: break;
					}
					if (vk && enhText->OnKeyUp(vk))
						continue;
				} else if (event.type == SDL_TEXTINPUT) {
					// SDL_TEXTINPUT gives us UTF-8 text — forward printable ASCII chars
					for (const char *p = event.text.text; *p; ++p) {
						uint8 ch = (uint8)*p;
						if (ch >= 0x20 && ch < 0x7F)
							enhText->OnChar(ch);
					}
					continue;
				}
			}
		}

		// Keyboard mapping → POKEY (type characters into Atari emulation)
		ProcessAtariKeyboard(event);

		// Let input handler try (joystick/paddle/etc. input maps)
		if (g_pInput && g_pInput->ProcessEvent(event))
			continue;

		switch (event.type) {
			case SDL_QUIT:
				ATImGuiRequestQuit();
				if (g_pImGui && !g_pImGui->IsVisible())
					g_pImGui->ToggleVisible();
				break;

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
					ATImGuiRequestQuit();
					if (g_pImGui && !g_pImGui->IsVisible())
						g_pImGui->ToggleVisible();
				} else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
					ATUISetAppActive(false);
					if (ATUIGetPauseWhenInactive() && !g_sim.IsPaused()) {
						g_sim.Pause();
						g_pausedByInactive = true;
					}
				} else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
					ATUISetAppActive(true);
					if (g_pausedByInactive) {
						g_pausedByInactive = false;
						g_sim.Resume();
					}
				} else if (event.window.event == SDL_WINDOWEVENT_RESIZED
					|| event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					IATUIEnhancedTextEngine *enhText = ATUIGetEnhancedTextEngine();
					if (enhText)
						enhText->OnSize(event.window.data1, event.window.data2);
				}
				break;

			case SDL_DROPFILE: {
				char *dropped = event.drop.file;
				if (dropped) {
					VDStringW path = VDTextU8ToW(VDStringA(dropped));
					if (ATImGuiIsDiskExplorerActive()) {
						ATImGuiDiskExplorerImportFile(path.c_str());
					} else {
						try {
							g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
							char msg[256];
							const char *fname = strrchr(dropped, '/');
							snprintf(msg, sizeof(msg), "Loaded: %s", fname ? fname + 1 : dropped);
							ATImGuiShowToast(msg);
						} catch (const std::exception& e) {
							char msg[512];
							snprintf(msg, sizeof(msg), "Drop failed: %s", e.what());
							ATImGuiShowToast(msg);
						} catch (...) {
							ATImGuiShowToast("Failed to load dropped file");
						}
					}
					SDL_free(dropped);
				}
				break;
			}
		}
	}
}

static uint32 s_titleUpdateCounter = 0;

static void UpdateWindowTitle(SDL_Window *window) {
	// Update every ~60 frames (roughly once per second)
	if (++s_titleUpdateCounter < 60)
		return;
	s_titleUpdateCounter = 0;

	const char *hwName = "";
	switch (g_sim.GetHardwareMode()) {
		case kATHardwareMode_800:    hwName = "800"; break;
		case kATHardwareMode_800XL:  hwName = "800XL"; break;
		case kATHardwareMode_5200:   hwName = "5200"; break;
		case kATHardwareMode_XEGS:   hwName = "XEGS"; break;
		case kATHardwareMode_1200XL: hwName = "1200XL"; break;
		case kATHardwareMode_130XE:  hwName = "130XE"; break;
		default: hwName = "Atari"; break;
	}

	char title[256];
	int off = snprintf(title, sizeof(title), "Altirra %s", hwName);
	int baseOff = off;

	// Show first loaded disk
	for (int i = 0; i < 4; ++i) {
		ATDiskInterface& di = g_sim.GetDiskInterface(i);
		if (di.IsDiskLoaded()) {
			VDStringA u8 = VDTextWToU8(VDStringW(VDFileSplitPath(di.GetPath())));
			off += snprintf(title + off, sizeof(title) - off, " - %s", u8.c_str());
			break;
		}
	}

	// Show cartridge (if no disk shown)
	if (off == baseOff && g_sim.IsCartridgeAttached(0)) {
		ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
		if (cart && cart->GetPath() && *cart->GetPath()) {
			VDStringA u8 = VDTextWToU8(VDStringW(VDFileSplitPath(cart->GetPath())));
			off += snprintf(title + off, sizeof(title) - off, " - %s", u8.c_str());
		}
	}

	// Show cassette (if no disk or cartridge shown)
	if (off == baseOff && g_sim.GetCassette().IsLoaded()) {
		const wchar_t *tapePath = g_sim.GetCassette().GetPath();
		if (tapePath && *tapePath) {
			VDStringA u8 = VDTextWToU8(VDStringW(VDFileSplitPath(tapePath)));
			off += snprintf(title + off, sizeof(title) - off, " - %s", u8.c_str());
		}
	}

	// Show turbo/paused indicator
	if (ATUIGetTurbo())
		off += snprintf(title + off, sizeof(title) - off, " [TURBO]");
	else if (g_sim.IsPaused())
		off += snprintf(title + off, sizeof(title) - off, " [PAUSED]");

	SDL_SetWindowTitle(window, title);
}

static void RenderAndSwap(SDL_Window *window) {
	// Update window title periodically
	UpdateWindowTitle(window);

	// Update pixel aspect ratio from GTIA for correct display scaling
	g_pDisplay->SetPixelAspectRatio(g_sim.GetGTIA().GetPixelAspectRatio());

	// Update enhanced text engine if active — this produces the framebuffer
	// and pushes it to the display instead of the GTIA's normal output.
	IATUIEnhancedTextEngine *enhText = ATUIGetEnhancedTextEngine();
	if (enhText) {
		enhText->Update(false);
		IATDeviceVideoOutput *vo = enhText->GetVideoOutput();
		if (vo) {
			const VDPixmap& fb = vo->GetFrameBuffer();
			if (fb.data && fb.w > 0 && fb.h > 0) {
				g_pDisplay->SetSourcePersistent(true, fb, true, nullptr, nullptr);
			}
		}
	}

	// Render emulation frame (upload texture + draw quad)
	ATProfileBeginRegion(kATProfileRegion_DisplayTick);
	g_pDisplay->RenderFrame();
	ATProfileEndRegion(kATProfileRegion_DisplayTick);

	// Auto-show overlay when debugger hits a breakpoint
	if (g_pImGui && !g_pImGui->IsVisible() && ATImGuiDebuggerDidBreak()) {
		g_pImGui->SetVisible(true);
		SDL_ShowCursor(SDL_ENABLE);
		g_cursorHidden = false;
	}

	// Render ImGui overlay on top
	if (g_pImGui && g_pImGui->IsVisible()) {
		g_pImGui->NewFrame();
		ATImGuiEmulatorDraw();
		g_pImGui->Render();
	} else if (g_pImGui) {
		// Minimal frame for toast notifications when overlay is hidden
		g_pImGui->NewFrame();
		extern void ATImGuiDrawToastsOnly();
		ATImGuiDrawToastsOnly();
		g_pImGui->Render();
	}

	// Single swap
	ATProfileBeginRegion(kATProfileRegion_DisplayPresent);
	SDL_GL_SwapWindow(window);
	ATProfileEndRegion(kATProfileRegion_DisplayPresent);
}

///////////////////////////////////////////////////////////////////////////
// Main
///////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
	// Parse command-line arguments
	ATLinuxOptions opts = ParseArguments(argc, argv);

	if (opts.showHelp) {
		PrintUsage(argv[0]);
		return 0;
	}

	if (opts.showVersion) {
		PrintVersion();
		return 0;
	}

	fprintf(stderr, "Altirra Linux - starting up\n");

	// Detect CPU features (SSE2, AVX, etc.)
	CPUCheckForExtensions();

	// Install signal handlers
	signal(SIGINT, SignalHandler);
	signal(SIGTERM, SignalHandler);

	// Install crash signal handlers for diagnostics
	{
		struct sigaction sa {};
		sa.sa_handler = CrashSignalHandler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESETHAND;  // one-shot: re-raise gets default handler
		sigaction(SIGSEGV, &sa, nullptr);
		sigaction(SIGABRT, &sa, nullptr);
		sigaction(SIGFPE, &sa, nullptr);
		sigaction(SIGBUS, &sa, nullptr);
		sigaction(SIGILL, &sa, nullptr);
	}

	// Init registry (in-memory, backed by INI file)
	g_pRegistryMemory = new VDRegistryProviderMemory;
	VDSetRegistryProvider(g_pRegistryMemory);
	VDRegistryAppKey::setDefaultKey("Software\\virtualdub.org\\Altirra\\");

	// Always use portable mode on Linux (INI file instead of Windows registry)
	ATSettingsSetInPortableMode(true);
	ATSetFirmwarePathPortabilityMode(true);

	// Determine settings file path
	if (!opts.configPath.empty()) {
		g_settingsPath = opts.configPath;
	} else if (opts.portable) {
		g_settingsPath = VDMakePath(VDGetProgramPath().c_str(), L"Altirra.ini");
	} else {
		g_settingsPath = ATGetLinuxSettingsPath();
	}

	// Load existing settings from INI file
	if (VDDoesPathExist(g_settingsPath.c_str())) {
		try {
			ATUILoadRegistry(g_settingsPath.c_str());
			fprintf(stderr, "Settings loaded from %s\n", VDTextWToU8(g_settingsPath).c_str());
		} catch (...) {
			fprintf(stderr, "Warning: Failed to load settings file, using defaults\n");
		}
	} else {
		fprintf(stderr, "No settings file found, using defaults\n");
	}

	// Set up firmware search paths
	VDStringW configDir = ATGetLinuxConfigDir();
	ATSetFirmwareBasePath(configDir.c_str());
	ATScanLinuxFirmwarePaths(configDir);

	// Add user-specified ROM path
	if (!opts.romPath.empty())
		s_extraRomPath = opts.romPath;

	// Register save state type deserializers (must be before any save state load)
	extern void ATInitSaveStateDeserializer();
	ATInitSaveStateDeserializer();

	// Init simulator (must happen before settings load, which calls
	// SetHardwareMode and other methods that access the device manager)
	g_sim.Init();
	g_sim.SetRandomSeed(rand() ^ (rand() << 15));

	// Register all device definitions (must be before settings load)
	extern void ATRegisterDevices(ATDeviceManager& dm);
	extern void ATRegisterDeviceXCmds(ATDeviceManager& dm);
	ATRegisterDevices(*g_sim.GetDeviceManager());
	ATRegisterDeviceXCmds(*g_sim.GetDeviceManager());

	// Register UI command handlers (enables custom device VM scripts)
	extern void ATLinuxInitCommands();
	ATLinuxInitCommands();

	fprintf(stderr, "Simulator initialized\n");

	// Init SDL2
	SDL_Window *window = nullptr;
	SDL_GLContext glContext = nullptr;

	if (!InitSDL(window, glContext, opts.fullscreen)) {
		fprintf(stderr, "Failed to initialize SDL2/OpenGL\n");
		return 1;
	}

	// Store window pointer and register fullscreen callback
	g_pWindow = window;
	ATSetFullscreenCallback(SetFullscreenImpl);
	ATSetWindowSizeCallback(SetWindowSizeImpl);

	const char *videoDriver = SDL_GetCurrentVideoDriver();
	fprintf(stderr, "SDL2/OpenGL initialized (video driver: %s)\n", videoDriver ? videoDriver : "unknown");

	// Init display backend
	g_pDisplay = new ATDisplaySDL2;
	if (!g_pDisplay->Init(window, glContext)) {
		fprintf(stderr, "Failed to initialize display backend\n");
		return 1;
	}

	// Apply display settings that were loaded before the display was created.
	// Settings load runs ATUISet* stubs which store values but can't update
	// the display (not created yet). Re-apply them now.
	{
		extern ATDisplayFilterMode ATUIGetDisplayFilterMode();
		extern ATDisplayStretchMode ATUIGetDisplayStretchMode();
		ATDisplayFilterMode fm = ATUIGetDisplayFilterMode();
		g_pDisplay->SetFilterMode(
			fm == kATDisplayFilterMode_Point
				? IVDVideoDisplay::kFilterPoint
				: IVDVideoDisplay::kFilterBilinear);
		g_pDisplay->SetStretchMode(ATUIGetDisplayStretchMode());
	}

	fprintf(stderr, "Display backend initialized\n");

	// Init ImGui
	g_pImGui = new ATImGuiManager;
	VDStringA imguiConfigDir = VDTextWToU8(VDFileSplitPathLeft(g_settingsPath));
	if (!g_pImGui->Init(window, glContext, imguiConfigDir.c_str())) {
		fprintf(stderr, "Warning: ImGui init failed (continuing without debugger UI)\n");
		delete g_pImGui;
		g_pImGui = nullptr;
	} else {
		fprintf(stderr, "ImGui initialized (F12 to toggle overlay)\n");
	}

	// Init debugger and compatibility database (must be after sim init)
	ATInitDebugger();
	ATCompatInit();
	ATImGuiDebuggerInit();
	ATImGuiEmulatorInit();
	fprintf(stderr, "Debugger initialized\n");

	// Connect display to GTIA
	g_sim.GetGTIA().SetVideoOutput(g_pDisplay);

	// Init audio
	IATAudioOutput *audioOutput = g_sim.GetAudioOutput();
	audioOutput->InitNativeAudio();

	fprintf(stderr, "Audio initialized\n");

	// Init input
	g_pInput = new ATInputSDL2;
	g_pInput->Init(g_sim.GetInputManager());

	// Init joystick manager
	IATJoystickManager *jm = ATCreateJoystickManagerSDL2();
	if (jm->Init(nullptr, g_sim.GetInputManager())) {
		g_sim.SetJoystickManager(jm);
		fprintf(stderr, "Joystick manager initialized\n");
	} else {
		delete jm;
		jm = nullptr;
		fprintf(stderr, "Joystick manager init failed (continuing without joystick support)\n");
	}

	// Load config variable overrides from persistent storage
	extern void ATLoadConfigVars();
	ATLoadConfigVars();

	// Load profiles and last-used settings (must be after simulator, joystick
	// manager, and audio init — settings load accesses GetJoystickManager(),
	// GetAudioOutput(), SetHardwareMode(), etc.)
	ATLoadDefaultProfiles();
	ATSettingsLoadLastProfile(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));

	// Always show real sector numbers in the status bar on Linux
	g_sim.SetDiskSectorCounterEnabled(true);

	fprintf(stderr, "Settings loaded\n");

	// Initialize keyboard mapping (must be after settings load which sets g_kbdOpts)
	ATUIInitVirtualKeyMap(g_kbdOpts);
	fprintf(stderr, "Keyboard mapping initialized\n");

	// Load ROMs (will use HLE kernel if no external ROMs found)
	try {
		g_sim.LoadROMs();
		fprintf(stderr, "ROMs loaded\n");
	} catch (...) {
		fprintf(stderr, "Warning: ROM loading failed, will use HLE kernel\n");
	}

	// Load image from command line if specified
	if (!opts.imagePath.empty()) {
		try {
			g_sim.Load(opts.imagePath.c_str(), kATMediaWriteMode_RO, nullptr);
			fprintf(stderr, "Loaded image: %s\n", VDTextWToU8(opts.imagePath).c_str());
		} catch (...) {
			fprintf(stderr, "Warning: Failed to load image: %s\n", VDTextWToU8(opts.imagePath).c_str());
		}
	}

	// Cold reset and start emulation
	g_sim.ColdReset();
	g_sim.Resume();
	fprintf(stderr, "Emulation started\n");

	// Frame pacing state — error accumulation feedback loop (matches Windows main.cpp)
	extern sint64 g_frameTicks;
	extern uint32 g_frameSubTicks;
	extern sint64 g_frameErrorBound;
	extern sint64 g_frameTimeout;

	sint64 frameError = 0;
	uint32 frameTimeErrorAccum = 0;
	uint64 lastFrameTime = VDGetPreciseTick();

	// Main loop
	while (g_running) {
		ATProfileBeginRegion(kATProfileRegion_NativeEvents);
		ProcessEvents(window);
		ATProfileEndRegion(kATProfileRegion_NativeEvents);

		// Auto-hide mouse cursor after idle period
		if (ATUIGetPointerAutoHide() && !g_cursorHidden
			&& !(g_pImGui && g_pImGui->IsVisible())) {
			Uint32 now = SDL_GetTicks();
			if (now - g_lastMouseMoveTime > kCursorHideDelayMs) {
				SDL_ShowCursor(SDL_DISABLE);
				g_cursorHidden = true;
			}
		}

		// Process UI step queue (custom device scripts, deferred actions)
		while (ATUIGetQueue().Run()) {}

		// Check if quit was confirmed (after dirty disk dialog)
		if (ATImGuiIsQuitConfirmed()) {
			g_running = 0;
			break;
		}

		// Tick debugger (processes queued commands)
		IATDebugger *dbg = ATGetDebugger();
		if (dbg)
			dbg->Tick();

		// Advance emulation with exception recovery
		ATProfileBeginRegion(kATProfileRegion_Simulation);
		ATSimulator::AdvanceResult result;
		try {
			result = g_sim.Advance(false);
		} catch (const MyError& e) {
			ATProfileEndRegion(kATProfileRegion_Simulation);
			ATUIShowError(e);

			// Cold reset to recover from broken emulation state
			g_sim.ColdReset();
			g_sim.Resume();
			RenderAndSwap(window);
			SDL_Delay(16);
			continue;
		} catch (const std::exception& e) {
			ATProfileEndRegion(kATProfileRegion_Simulation);
			ATUIShowError2(nullptr,
				VDTextU8ToW(VDStringA(e.what())).c_str(),
				L"Emulation Error");

			g_sim.ColdReset();
			g_sim.Resume();
			RenderAndSwap(window);
			SDL_Delay(16);
			continue;
		} catch (...) {
			ATProfileEndRegion(kATProfileRegion_Simulation);
			ATUIShowError2(nullptr,
				L"An unknown error occurred during emulation.",
				L"Emulation Error");

			g_sim.ColdReset();
			g_sim.Resume();
			RenderAndSwap(window);
			SDL_Delay(16);
			continue;
		}
		ATProfileEndRegion(kATProfileRegion_Simulation);

		// Determine if a frame was rendered this iteration
		bool frameRendered = false;

		if (result == ATSimulator::kAdvanceResult_WaitingForFrame) {
			ATProfileMarkEvent(kATProfileEvent_BeginFrame);
			RenderAndSwap(window);
			frameRendered = true;
			g_debuggerAutoShowed = false;
		} else if (result == ATSimulator::kAdvanceResult_Stopped) {
			// Emulation stopped — still render but at reduced rate.
			// Auto-show debugger once on the transition to stopped state,
			// so the user can dismiss it with F12/Escape without it
			// reappearing every frame.
			if (g_pImGui && !g_pImGui->IsVisible() && !g_debuggerAutoShowed) {
				g_pImGui->SetVisible(true);
				g_debuggerAutoShowed = true;
			}

			ATProfileMarkEvent(kATProfileEvent_BeginFrame);
			RenderAndSwap(window);

			ATProfileBeginRegion(kATProfileRegion_IdleFrameDelay);
			SDL_Delay(16);
			ATProfileEndRegion(kATProfileRegion_IdleFrameDelay);

			// Reset pacing state while stopped
			lastFrameTime = VDGetPreciseTick();
			frameError = 0;
			frameTimeErrorAccum = 0;
		} else if (result == ATSimulator::kAdvanceResult_Running) {
			// Our display copies frame data in PostBuffer and immediately
			// releases the frame, so the GTIA frame tracker never fills up
			// and Advance() never yields WaitingForFrame. Render whenever
			// the display has a new frame ready.
			if (g_pDisplay && g_pDisplay->IsFramePending()) {
				ATProfileMarkEvent(kATProfileEvent_BeginFrame);
				RenderAndSwap(window);
				frameRendered = true;
			}
			g_debuggerAutoShowed = false;
		}

		// Frame pacing — error accumulation feedback loop
		// Matches the Windows main.cpp precision timing algorithm.
		if (frameRendered) {
			uint64 curTime = VDGetPreciseTick();
			sint64 lastFrameDuration = curTime - lastFrameTime;
			lastFrameTime = curTime;

			// Accumulate timing error (actual - target)
			frameError += lastFrameDuration - g_frameTicks;
			frameTimeErrorAccum += g_frameSubTicks;

			if (frameTimeErrorAccum >= 0x10000) {
				frameTimeErrorAccum &= 0xFFFF;
				--frameError;
			}

			// Reset if error is too large (catch-up/behind scenarios)
			if (frameError > g_frameErrorBound || frameError < -g_frameErrorBound)
				frameError = -g_frameTicks;

			// In turbo mode, don't pace
			if (g_sim.IsTurboModeEnabled()) {
				frameError = 0;
			} else if (frameError < 0) {
				// We're ahead of schedule — sleep to maintain target frame rate.
				// Convert ticks-ahead to nanoseconds and use clock_nanosleep
				// for sub-millisecond precision.
				sint64 nsToSleep = -frameError;  // ticks are nanoseconds on Linux

				if (nsToSleep > 0 && nsToSleep < (sint64)g_frameTimeout) {
					struct timespec ts;
					ts.tv_sec = nsToSleep / 1000000000LL;
					ts.tv_nsec = nsToSleep % 1000000000LL;

					ATProfileBeginRegion(kATProfileRegion_Idle);
					clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
					ATProfileEndRegion(kATProfileRegion_Idle);
				}
			}
		}
	}

	fprintf(stderr, "Shutting down...\n");

	// Save window geometry before shutdown
	if (window) {
		VDRegistryAppKey wkey("Window", true);
		uint32 flags = SDL_GetWindowFlags(window);
		wkey.setBool("Maximized", (flags & SDL_WINDOW_MAXIMIZED) != 0);

		if (!(flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN))) {
			int x, y, w, h;
			SDL_GetWindowPosition(window, &x, &y);
			SDL_GetWindowSize(window, &w, &h);
			wkey.setInt("X", x);
			wkey.setInt("Y", y);
			wkey.setInt("Width", w);
			wkey.setInt("Height", h);
		}
	}

	// Save settings before shutdown
	ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));

	try {
		ATUISaveRegistry(g_settingsPath.c_str());
	} catch (...) {
		fprintf(stderr, "Warning: Failed to save settings\n");
	}

	// Disconnect display from GTIA before destroying it
	g_sim.GetGTIA().SetVideoOutput(nullptr);

	// Shutdown joystick
	if (g_sim.GetJoystickManager()) {
		IATJoystickManager *jm2 = g_sim.GetJoystickManager();
		g_sim.SetJoystickManager(nullptr);
		jm2->Shutdown();
		delete jm2;
	}

	// Shutdown input
	if (g_pInput) {
		g_pInput->Shutdown();
		delete g_pInput;
		g_pInput = nullptr;
	}

	// Shutdown debugger and emulator UI
	ATImGuiEmulatorShutdown();
	ATImGuiDebuggerShutdown();
	ATShutdownDebugger();
	ATCompatShutdown();

	// Shutdown simulator
	g_sim.Shutdown();

	// Shutdown ImGui
	if (g_pImGui) {
		g_pImGui->Shutdown();
		delete g_pImGui;
		g_pImGui = nullptr;
	}

	// Shutdown display
	if (g_pDisplay) {
		g_pDisplay->Shutdown();
		delete g_pDisplay;
		g_pDisplay = nullptr;
	}

	// Shutdown SDL
	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();

	// Cleanup registry provider
	VDSetRegistryProvider(nullptr);
	delete g_pRegistryMemory;
	g_pRegistryMemory = nullptr;

	fprintf(stderr, "Shutdown complete\n");
	return 0;
}
