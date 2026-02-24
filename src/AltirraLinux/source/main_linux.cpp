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
#include <at/atcore/serializable.h>
#include <at/ataudio/audiooutput.h>
#include <at/atio/image.h>

#include "simulator.h"
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
#include "uicommondialogs.h"
#include "uiqueue.h"
#include "inputmap.h"

#include <display_sdl2.h>
#include <input_sdl2.h>
#include <imgui_manager.h>
#include <debugger_imgui.h>
#include <emulator_imgui.h>
#include <imgui.h>

#include <SDL.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cxxabi.h>
#include <execinfo.h>
#include <getopt.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

// Forward declarations from debugger.cpp
void ATInitDebugger();
void ATShutdownDebugger();

// Forward declarations from uiregistry.cpp
void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);

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
	fprintf(stderr, "Altirra (Linux port) 4.40\n");
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

	// Enable vsync
	SDL_GL_SetSwapInterval(1);

	return true;
}

///////////////////////////////////////////////////////////////////////////
// Event handling
///////////////////////////////////////////////////////////////////////////

// Quick save state (in-memory, shared with emulator_imgui.cpp via extern)
static vdrefptr<IATSerializable> s_pQuickState;

static void HandleShortcuts(const SDL_Event& event) {
	if (event.type != SDL_KEYDOWN)
		return;

	switch (event.key.keysym.scancode) {
		// F7: quick save state
		case SDL_SCANCODE_F7: {
			try {
				s_pQuickState.clear();
				g_sim.CreateSnapshot(~s_pQuickState, nullptr);
				ATImGuiShowToast("State saved");
			} catch (...) {
				ATImGuiShowToast("Save state failed");
			}
			return;
		}

		// F8: quick load state
		case SDL_SCANCODE_F8: {
			if (s_pQuickState) {
				try {
					ATStateLoadContext ctx {};
					g_sim.ApplySnapshot(*s_pQuickState, &ctx);
					ATImGuiShowToast("State loaded");
				} catch (...) {
					ATImGuiShowToast("Load state failed");
				}
			}
			return;
		}

		// F9: save screenshot (auto-named)
		case SDL_SCANCODE_F9: {
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

		// F6: warm reset (Shift+F6: cold reset)
		case SDL_SCANCODE_F6: {
			bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
			if (shift)
				g_sim.ColdReset();
			else
				g_sim.WarmReset();
			return;
		}

		// F4: toggle mute
		case SDL_SCANCODE_F4: {
			IATAudioOutput *audioOut = g_sim.GetAudioOutput();
			if (audioOut) {
				bool mute = !audioOut->GetMute();
				audioOut->SetMute(mute);
				ATImGuiShowToast(mute ? "Audio muted" : "Audio unmuted");
			}
			return;
		}

		// Shift+F1: cycle quick maps
		case SDL_SCANCODE_F1: {
			if ((event.key.keysym.mod & KMOD_SHIFT) != 0) {
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
			}
			return;
		}

		// Pause key: toggle emulation pause
		case SDL_SCANCODE_PAUSE: {
			if (g_sim.IsPaused())
				g_sim.Resume();
			else
				g_sim.Pause();
			return;
		}

		default:
			break;
	}

	// Debug shortcuts (only when overlay visible)
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;

	switch (event.key.keysym.scancode) {
		case SDL_SCANCODE_F5:
			if (dbg->IsRunning())
				dbg->Break();
			else
				dbg->Run(kATDebugSrcMode_Disasm);
			break;

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

		// F1/F4/F6/F7/F8/F9/Pause always active (quick maps/mute/reset/save/load/screenshot/pause)
		if (event.type == SDL_KEYDOWN) {
			if (event.key.keysym.scancode == SDL_SCANCODE_F1
				|| event.key.keysym.scancode == SDL_SCANCODE_F4
				|| event.key.keysym.scancode == SDL_SCANCODE_F6
				|| event.key.keysym.scancode == SDL_SCANCODE_F7
				|| event.key.keysym.scancode == SDL_SCANCODE_F8
				|| event.key.keysym.scancode == SDL_SCANCODE_F9
				|| event.key.keysym.scancode == SDL_SCANCODE_PAUSE) {
				HandleShortcuts(event);
				continue;
			}

			// F11 = fullscreen toggle when overlay is hidden
			if (event.key.keysym.scancode == SDL_SCANCODE_F11
				&& !(g_pImGui && g_pImGui->IsVisible())) {
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

			// Ctrl+V = paste text to emulator
			if (event.key.keysym.scancode == SDL_SCANCODE_V
				&& (event.key.keysym.mod & KMOD_CTRL)) {
				ATImGuiPasteText();
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

			// If ImGui wants the input, don't pass to emulation
			if (g_pImGui->WantCaptureMouse() || g_pImGui->WantCaptureKeyboard())
				continue;
		}

		// Let input handler try
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
				}
				break;

			case SDL_DROPFILE: {
				char *dropped = event.drop.file;
				if (dropped) {
					VDStringW path = VDTextU8ToW(VDStringA(dropped));
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

	// Render emulation frame (upload texture + draw quad)
	g_pDisplay->RenderFrame();

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
	SDL_GL_SwapWindow(window);
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
	ATScanLinuxFirmwarePaths(configDir);

	// Add user-specified ROM path
	if (!opts.romPath.empty())
		s_extraRomPath = opts.romPath;

	// Init simulator (must happen before settings load, which calls
	// SetHardwareMode and other methods that access the device manager)
	g_sim.Init();
	g_sim.SetRandomSeed(rand() ^ (rand() << 15));

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

	fprintf(stderr, "SDL2/OpenGL initialized\n");

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
	if (!g_pImGui->Init(window, glContext)) {
		fprintf(stderr, "Warning: ImGui init failed (continuing without debugger UI)\n");
		delete g_pImGui;
		g_pImGui = nullptr;
	} else {
		fprintf(stderr, "ImGui initialized (F12 to toggle overlay)\n");
	}

	// Init debugger (must be after sim init)
	ATInitDebugger();
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

	// Load profiles and last-used settings (must be after simulator, joystick
	// manager, and audio init — settings load accesses GetJoystickManager(),
	// GetAudioOutput(), SetHardwareMode(), etc.)
	ATLoadDefaultProfiles();
	ATSettingsLoadLastProfile(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
	fprintf(stderr, "Settings loaded\n");

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

	// Main loop
	while (g_running) {
		ProcessEvents(window);

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
		ATSimulator::AdvanceResult result;
		try {
			result = g_sim.Advance(false);
		} catch (const MyError& e) {
			ATUIShowError(e);

			// Cold reset to recover from broken emulation state
			g_sim.ColdReset();
			g_sim.Resume();
			RenderAndSwap(window);
			SDL_Delay(16);
			continue;
		} catch (const std::exception& e) {
			ATUIShowError2(nullptr,
				VDTextU8ToW(VDStringA(e.what())).c_str(),
				L"Emulation Error");

			g_sim.ColdReset();
			g_sim.Resume();
			RenderAndSwap(window);
			SDL_Delay(16);
			continue;
		} catch (...) {
			ATUIShowError2(nullptr,
				L"An unknown error occurred during emulation.",
				L"Emulation Error");

			g_sim.ColdReset();
			g_sim.Resume();
			RenderAndSwap(window);
			SDL_Delay(16);
			continue;
		}

		if (result == ATSimulator::kAdvanceResult_WaitingForFrame) {
			RenderAndSwap(window);
		} else if (result == ATSimulator::kAdvanceResult_Stopped) {
			// Emulation stopped — still render but at reduced rate
			// Auto-show debugger on stop
			if (g_pImGui && !g_pImGui->IsVisible())
				g_pImGui->SetVisible(true);

			RenderAndSwap(window);
			SDL_Delay(16);
		}
		// kAdvanceResult_Running means more work to do — loop immediately
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
