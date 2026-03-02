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

#include <wx/wx.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/cpuaccel.h>

#include <at/atcore/device.h>
#include <at/atcore/media.h>
#include <at/atcore/profile.h>
#include <at/ataudio/audiooutput.h>
#include <at/atio/image.h>

#include "simulator.h"
#include "devicemanager.h"
#include "inputmap.h"
#include "inputmanager.h"
#include "joystick.h"
#include "debugger.h"
#include "settings.h"
#include "firmwaremanager.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "uikeyboard.h"
#include "uiqueue.h"
#include "versioninfo.h"

#include "mainframe.h"
#include "display_sdl3.h"
#include "imgui_manager.h"
#include <debugger_wx.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cxxabi.h>
#include <execinfo.h>
#include <getopt.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Forward declarations from debugger.cpp
void ATInitDebugger();
void ATShutdownDebugger();

#include "compatengine.h"

// Forward declarations from uiregistry.cpp
void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);

// Global simulator instance — matches Windows main.cpp
ATSimulator g_sim;

// Display pointer — stubs_linux.cpp calls ATGetLinuxDisplay() to access display settings.
// Points to the ATDisplayWx (wxGLCanvas) owned by ATMainFrame.
static ATDisplayWx *g_pDisplay = nullptr;
ATDisplaySDL3 *ATGetLinuxDisplay() { return g_pDisplay; }

// No SDL window in the wxWidgets build (wxWidgets manages the window)
SDL_Window *ATGetLinuxWindow() { return nullptr; }

// ImGui manager pointer — console_linux.cpp references this as extern
ATImGuiManager *g_pImGui = nullptr;

// Toast notification stub — will be replaced with wxWidgets implementation
void ATImGuiShowToast(const char *message) {
	fprintf(stderr, "[Toast] %s\n", message);
}

// Joystick manager factory (defined in joystick_sdl3.cpp)
IATJoystickManager *ATCreateJoystickManagerSDL3();

// Fullscreen callback and state (defined in stubs_linux.cpp)
void ATSetFullscreenCallback(void (*pfn)(bool));
bool ATUIGetFullscreen();
void ATSetFullscreen(bool);

// Window resize callback (defined in stubs_linux.cpp)
void ATSetWindowSizeCallback(void (*pfn)(int, int));

// Settings path — declared early so crash handler can access it
static VDStringW g_settingsPath;

// Registry provider (owned, freed at shutdown)
static VDRegistryProviderMemory *g_pRegistryMemory = nullptr;

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
	vdvector<VDStringW> paths;
	paths.push_back(VDMakePath(configDir.c_str(), L"firmware"));
	paths.push_back(VDMakePath(VDGetProgramPath().c_str(), L"firmware"));
	paths.push_back(VDStringW(L"/usr/share/altirra/firmware"));
	paths.push_back(VDStringW(L"/usr/local/share/altirra/firmware"));

	if (!s_extraRomPath.empty())
		paths.push_back(s_extraRomPath);

	// Ensure user firmware directory exists
	EnsureDirectoryExists(paths[0]);

	// Set the primary firmware path in registry
	{
		VDRegistryAppKey key("Firmware", true);
		key.setString("Firmware base path", paths[0].c_str());
	}

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
// Save settings — reused by UI code
///////////////////////////////////////////////////////////////////////////

void ATLinuxSaveSettings() {
	ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
	if (!g_settingsPath.empty())
		ATUISaveRegistry(g_settingsPath.c_str());
}

///////////////////////////////////////////////////////////////////////////
// Signal handlers
///////////////////////////////////////////////////////////////////////////

static volatile sig_atomic_t g_running = 1;

static void ATSignalHandler(int) {
	g_running = 0;
}

static void CrashATSignalHandler(int sig) {
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

	const char hdr[] = "\n=== Altirra crashed ===\nSignal: ";
	write(STDERR_FILENO, hdr, sizeof(hdr) - 1);
	write(STDERR_FILENO, signame, strlen(signame));
	write(STDERR_FILENO, "\n\nBacktrace:\n", 13);

	void *frames[64];
	int nframes = backtrace(frames, 64);
	backtrace_symbols_fd(frames, nframes, STDERR_FILENO);

	write(STDERR_FILENO, "\nAttempting to save settings...\n", 31);

	if (!g_settingsPath.empty()) {
		try {
			ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
			ATUISaveRegistry(g_settingsPath.c_str());
			write(STDERR_FILENO, "Settings saved.\n", 16);
		} catch (...) {
			write(STDERR_FILENO, "Settings save failed.\n", 22);
		}
	}

	signal(sig, SIG_DFL);
	raise(sig);
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

	// Reset getopt state in case wxWidgets has already parsed argv
	optind = 1;

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

	if (optind < argc) {
		opts.imagePath = VDTextU8ToW(VDStringA(argv[optind]));
	}

	return opts;
}

///////////////////////////////////////////////////////////////////////////
// wxApp
///////////////////////////////////////////////////////////////////////////

class ATApp : public wxApp {
public:
	bool OnInit() override;
	int OnExit() override;

	// Global event filter — handles hotkeys before any window sees them
	int FilterEvent(wxEvent& event) override;

private:
	void InitATSignalHandlers();
	bool InitRegistry(const ATLinuxOptions& opts);
	bool InitSimulator();
	bool InitSDL3AudioGamepad();
	bool InitAudio();
	bool InitJoystick();
	bool LoadSettingsAndROMs(const ATLinuxOptions& opts);

	ATMainFrame *m_frame = nullptr;
	IATJoystickManager *m_joystickMgr = nullptr;
};

wxIMPLEMENT_APP(ATApp);

bool ATApp::OnInit() {
	if (!wxApp::OnInit())
		return false;

	// Parse our own arguments (wxWidgets may consume some first)
	ATLinuxOptions opts = ParseArguments(argc, argv);

	if (opts.showHelp) {
		PrintUsage(argv[0]);
		return false;
	}

	if (opts.showVersion) {
		PrintVersion();
		return false;
	}

	fprintf(stderr, "Altirra Linux (wxWidgets) - starting up\n");

	// Detect CPU features (SSE2, AVX, etc.)
	CPUCheckForExtensions();

	// Install signal handlers
	InitATSignalHandlers();

	// Init registry and load settings
	if (!InitRegistry(opts))
		return false;

	// Init simulator core
	if (!InitSimulator())
		return false;

	// Init SDL3 for audio and gamepad only (no SDL_INIT_VIDEO)
	if (!InitSDL3AudioGamepad())
		return false;

	// Init audio subsystem
	if (!InitAudio())
		return false;

	// Init joystick manager
	if (!InitJoystick())
		fprintf(stderr, "Joystick manager init failed (continuing without joystick support)\n");

	// Load settings, profiles, and ROMs
	if (!LoadSettingsAndROMs(opts))
		return false;

	// Create main window
	m_frame = new ATMainFrame();
	m_frame->Show(true);

	// Wire up the display — stubs_linux.cpp and GTIA access it through g_pDisplay
	g_pDisplay = m_frame->GetDisplay();

	// Connect GTIA video output to the display widget
	g_sim.GetGTIA().SetVideoOutput(g_pDisplay);

	// Initialize input system (keyboard/mouse/gamepad)
	m_frame->InitInput();
	fprintf(stderr, "Input system initialized\n");

	// Initialize debugger UI hooks
	ATWxDebuggerInit();
	fprintf(stderr, "wxWidgets UI initialized\n");

	// Cold reset and start emulation
	g_sim.ColdReset();
	g_sim.Resume();
	m_frame->StartEmulation();
	fprintf(stderr, "Emulation started\n");

	return true;
}

int ATApp::OnExit() {
	fprintf(stderr, "Shutting down...\n");

	// Save settings before shutdown
	ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));

	try {
		ATUISaveRegistry(g_settingsPath.c_str());
	} catch (...) {
		fprintf(stderr, "Warning: Failed to save settings\n");
	}

	// Disconnect display from GTIA before destroying it
	g_sim.GetGTIA().SetVideoOutput(nullptr);
	g_pDisplay = nullptr;

	// Shutdown joystick
	if (g_sim.GetJoystickManager()) {
		IATJoystickManager *jm = g_sim.GetJoystickManager();
		g_sim.SetJoystickManager(nullptr);
		jm->Shutdown();
		delete jm;
	}

	// Shutdown debugger UI hooks
	ATWxDebuggerShutdown();

	// Shutdown debugger
	ATShutdownDebugger();
	ATCompatShutdown();

	// Shutdown simulator
	g_sim.Shutdown();

	// Shutdown SDL (audio + gamepad only)
	SDL_Quit();

	// Cleanup registry provider
	VDSetRegistryProvider(nullptr);
	delete g_pRegistryMemory;
	g_pRegistryMemory = nullptr;

	fprintf(stderr, "Shutdown complete\n");
	return wxApp::OnExit();
}

///////////////////////////////////////////////////////////////////////////
// Global hotkey handling
///////////////////////////////////////////////////////////////////////////

int ATApp::FilterEvent(wxEvent& event) {
	if (event.GetEventType() != wxEVT_KEY_DOWN)
		return Event_Skip;

	wxKeyEvent& keyEvent = static_cast<wxKeyEvent&>(event);
	int keyCode = keyEvent.GetKeyCode();
	bool shift = keyEvent.ShiftDown();
	bool ctrl  = keyEvent.ControlDown();
	bool alt   = keyEvent.AltDown();

	switch (keyCode) {
		// Shift+F1: cycle quick input maps
		// Ctrl+F1: cycle display filter mode
		case WXK_F1: {
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
				return Event_Processed;
			} else if (ctrl) {
				ATDisplayFilterMode fm = ATUIGetDisplayFilterMode();
				fm = (ATDisplayFilterMode)(((int)fm + 1) % kATDisplayFilterModeCount);
				ATUISetDisplayFilterMode(fm);
				ATDisplayWx *disp = g_pDisplay;
				if (disp) {
					disp->SetFilterMode(
						(fm == kATDisplayFilterMode_Point)
						? IVDVideoDisplay::kFilterPoint
						: IVDVideoDisplay::kFilterBilinear);
				}
				return Event_Processed;
			}
			break;
		}

		// F5: warm reset / Shift+F5: cold reset
		case WXK_F5: {
			if (shift) {
				g_sim.ColdReset();
				ATImGuiShowToast("Cold reset");
			} else {
				g_sim.WarmReset();
				ATImGuiShowToast("Warm reset");
			}
			return Event_Processed;
		}

		// F8: debugger run/stop
		case WXK_F8: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg) {
				if (dbg->IsRunning())
					dbg->Break();
				else
					dbg->Run(kATDebugSrcMode_Disasm);
			}
			return Event_Processed;
		}

		// F9: toggle pause
		case WXK_F9: {
			if (g_sim.IsPaused()) {
				g_sim.Resume();
				ATImGuiShowToast("Resumed");
			} else {
				g_sim.Pause();
				ATImGuiShowToast("Paused");
			}
			return Event_Processed;
		}

		// Ctrl+Q: quit
		case 'Q': {
			if (ctrl) {
				if (m_frame)
					m_frame->Close();
				return Event_Processed;
			}
			break;
		}

		default:
			break;
	}

	return Event_Skip;
}

void ATApp::InitATSignalHandlers() {
	signal(SIGINT, ATSignalHandler);
	signal(SIGTERM, ATSignalHandler);

	struct sigaction sa {};
	sa.sa_handler = CrashATSignalHandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;
	sigaction(SIGSEGV, &sa, nullptr);
	sigaction(SIGABRT, &sa, nullptr);
	sigaction(SIGFPE, &sa, nullptr);
	sigaction(SIGBUS, &sa, nullptr);
	sigaction(SIGILL, &sa, nullptr);
}

bool ATApp::InitRegistry(const ATLinuxOptions& opts) {
	g_pRegistryMemory = new VDRegistryProviderMemory;
	VDSetRegistryProvider(g_pRegistryMemory);
	VDRegistryAppKey::setDefaultKey("Software\\virtualdub.org\\Altirra\\");

	ATSettingsSetInPortableMode(true);
	ATSetFirmwarePathPortabilityMode(true);

	if (!opts.configPath.empty()) {
		g_settingsPath = opts.configPath;
	} else if (opts.portable) {
		g_settingsPath = VDMakePath(VDGetProgramPath().c_str(), L"Altirra.ini");
	} else {
		g_settingsPath = ATGetLinuxSettingsPath();
	}

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

	VDStringW configDir = ATGetLinuxConfigDir();
	ATSetFirmwareBasePath(configDir.c_str());
	ATScanLinuxFirmwarePaths(configDir);

	if (!opts.romPath.empty())
		s_extraRomPath = opts.romPath;

	return true;
}

bool ATApp::InitSimulator() {
	extern void ATInitSaveStateDeserializer();
	ATInitSaveStateDeserializer();

	g_sim.Init();
	g_sim.SetRandomSeed(rand() ^ (rand() << 15));

	extern void ATRegisterDevices(ATDeviceManager& dm);
	extern void ATRegisterDeviceXCmds(ATDeviceManager& dm);
	ATRegisterDevices(*g_sim.GetDeviceManager());
	ATRegisterDeviceXCmds(*g_sim.GetDeviceManager());

	extern void ATLinuxInitCommands();
	ATLinuxInitCommands();

	ATInitDebugger();
	ATCompatInit();

	fprintf(stderr, "Simulator initialized\n");
	return true;
}

bool ATApp::InitSDL3AudioGamepad() {
	// Initialize SDL3 for audio and gamepad only — wxWidgets handles video/window
	if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "SDL_Init (audio+gamepad) failed: %s\n", SDL_GetError());
		return false;
	}

	fprintf(stderr, "SDL3 initialized (audio + gamepad only)\n");
	return true;
}

bool ATApp::InitAudio() {
	IATAudioOutput *audioOutput = g_sim.GetAudioOutput();
	audioOutput->InitNativeAudio();

	fprintf(stderr, "Audio initialized\n");
	return true;
}

bool ATApp::InitJoystick() {
	m_joystickMgr = ATCreateJoystickManagerSDL3();
	if (m_joystickMgr->Init(nullptr, g_sim.GetInputManager())) {
		g_sim.SetJoystickManager(m_joystickMgr);
		fprintf(stderr, "Joystick manager initialized\n");
		return true;
	}

	delete m_joystickMgr;
	m_joystickMgr = nullptr;
	return false;
}

bool ATApp::LoadSettingsAndROMs(const ATLinuxOptions& opts) {
	extern void ATLoadConfigVars();
	ATLoadConfigVars();

	ATLoadDefaultProfiles();
	ATSettingsLoadLastProfile(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));

	g_sim.SetDiskSectorCounterEnabled(true);

	fprintf(stderr, "Settings loaded\n");

	extern ATUIKeyboardOptions g_kbdOpts;
	ATUIInitVirtualKeyMap(g_kbdOpts);
	fprintf(stderr, "Keyboard mapping initialized\n");

	try {
		g_sim.LoadROMs();
		fprintf(stderr, "ROMs loaded\n");
	} catch (...) {
		fprintf(stderr, "Warning: ROM loading failed, will use HLE kernel\n");
	}

	if (!opts.imagePath.empty()) {
		try {
			g_sim.Load(opts.imagePath.c_str(), kATMediaWriteMode_RO, nullptr);
			fprintf(stderr, "Loaded image: %s\n", VDTextWToU8(opts.imagePath).c_str());
		} catch (...) {
			fprintf(stderr, "Warning: Failed to load image: %s\n", VDTextWToU8(opts.imagePath).c_str());
		}
	}

	return true;
}
