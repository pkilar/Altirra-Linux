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
#include <wx/cmdline.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/strutil.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/cpuaccel.h>

#include <at/atcore/device.h>
#include <at/atcore/media.h>
#include <at/atcore/profile.h>
#include <at/atcore/propertyset.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>
#include <at/atio/cartridgetypes.h>
#include <at/atio/image.h>

#include "simulator.h"
#include "cassette.h"
#include "cheatengine.h"
#include "console.h"
#include "constants.h"
#include "devicemanager.h"
#include "disk.h"
#include "gtia.h"
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
#include "dialogs_wx.h"
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

// Display pointer — owned by ATMainFrame, accessed via global functions below.
static ATDisplayWx *g_pDisplay = nullptr;

// stubs_linux.cpp calls these to access display settings without needing
// the concrete type. Replaces ATGetLinuxDisplay() type-unsafe pattern.
void ATLinuxSetDisplayFilterMode(IVDVideoDisplay::FilterMode fm) {
	if (g_pDisplay) g_pDisplay->SetFilterMode(fm);
}
void ATLinuxSetDisplayStretchMode(ATDisplayStretchMode m) {
	if (g_pDisplay) g_pDisplay->SetStretchMode(m);
}
void ATLinuxGetDisplayWindowSize(int& w, int& h) {
	if (g_pDisplay) g_pDisplay->GetWindowSize(w, h);
	else { w = 0; h = 0; }
}

// Clear the global display pointer (called during shutdown).
void ATLinuxClearDisplay() {
	g_pDisplay = nullptr;
}

// Legacy accessor — returns nullptr in wx build since callers should use
// the typed functions above.
ATDisplaySDL3 *ATGetLinuxDisplay() { return nullptr; }

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
	// Existing
	bool portable = false;
	bool fullscreen = false;
	bool showHelp = false;
	bool showVersion = false;
	VDStringW configPath;
	VDStringW romPath;

	// Profile
	VDStringW profileName;
	VDStringW defProfile;       // "800", "xl", "xegs", "1200xl", "5200"
	bool tempProfile = false;
	bool autoProfile = false;
	bool noAutoProfile = false;
	bool baseline = false;
	bool launch = false;

	// System config
	int videoStandard = -1;     // ATVideoStandard or -1 for default
	int hardwareMode = -1;      // ATHardwareMode or -1
	VDStringW kernelMode;       // "default", "osa", "osb", etc.
	VDStringW kernelRef;        // firmware ref string
	VDStringW basicRef;         // firmware ref string
	int memoryMode = -1;        // ATMemoryMode or -1
	int axlonMemSize = -1;      // bit count or -1
	int highBanks = -2;         // -2=unset, -1=na, 0-63=value
	int setBasic = -1;          // -1=unset, 0=disable, 1=enable
	int setStereo = -1;         // -1=unset, 0=disable, 1=enable

	// Acceleration
	int burstIO = -1;           // -1=unset, 0=off, 1=on
	int sioPatch = -1;          // -1=unset, 0=off, 1=on, 2=safe
	int fastBoot = -1;          // 0/1/-1
	int casAutoBoot = -1;
	int casAutoBasicBoot = -1;
	int accurateDisk = -1;

	// Boot write mode
	const ATMediaWriteMode *bootWriteMode = nullptr;

	// Media (vectors for repeatable switches)
	vdvector<VDStringW> carts;
	vdvector<VDStringW> disks;
	vdvector<VDStringW> runs;
	vdvector<VDStringW> runBas;
	vdvector<VDStringW> tapes;
	vdvector<VDStringW> positionalFiles;
	VDStringW tapePos;
	int cartMapper = 0;         // 0=auto, -1=nocartchecksum, >0=mapper
	VDStringW diskEmu;

	// Display
	VDStringW artifactMode;
	int vsync = -1;

	// Devices
	struct DeviceOp {
		enum Type { kClear, kAdd, kSet, kRemove };
		Type type;
		VDStringW arg;
	};
	vdvector<DeviceOp> deviceOps;
	VDStringW soundboardBase;
	bool noSoundboard = false;
	bool slightSID = false;
	bool noSlightSID = false;
	bool covox = false;
	bool noCovox = false;
	VDStringW hdPath;
	VDStringW hdPathRW;
	bool noHDPath = false;
	VDStringW pclink;
	bool noPclink = false;

	// Debug
	bool debug = false;
	int debugBrkRun = -1;
	vdvector<VDStringA> debugCmds;

	// Other
	int rawKeys = -1;
	VDStringW keysToType;
	VDStringW cheatsPath;
	bool noCheats = false;
	bool skipSetup = false;
};

static void PrintUsage(const char *progname) {
	fprintf(stderr,
		"Usage: %s [options] [file...]\n"
		"\n"
		"General:\n"
		"  <file>                   Load disk/cart/tape/program image\n"
		"  --portable               Use settings from program directory\n"
		"  --config <path>          Use alternate settings file\n"
		"  --rom-path <path>        Add firmware ROM search path\n"
		"  --fullscreen             Start in fullscreen mode\n"
		"  --help                   Show this help\n"
		"  --version                Show version\n"
		"\n"
		"Profile:\n"
		"  --profile <name>         Load named profile\n"
		"  --defprofile <type>      Load default profile (800,xl,xegs,1200xl,5200)\n"
		"  --tempprofile            Don't save profile changes on exit\n"
		"  --autoprofile            Enable automatic profile selection\n"
		"  --noautoprofile          Disable automatic profile selection\n"
		"  --baseline               Load baseline (factory default) settings\n"
		"  --launch                 Enable auto-profile if configured\n"
		"\n"
		"System:\n"
		"  --ntsc                   NTSC video standard\n"
		"  --pal                    PAL video standard\n"
		"  --secam                  SECAM video standard\n"
		"  --ntsc50                 NTSC @ 50Hz\n"
		"  --pal60                  PAL @ 60Hz\n"
		"  --hardware <mode>        Hardware mode (800,800xl,1200xl,130xe,xegs,1400xl,5200)\n"
		"  --kernel <mode>          Kernel ROM (default,osa,osb,xl,xegs,1200xl,5200,lle,llexl,5200lle)\n"
		"  --kernelref <ref>        Kernel firmware by reference string\n"
		"  --basicref <ref>         BASIC firmware by reference string\n"
		"  --memsize <size>         Memory size (8K,16K,24K,32K,40K,48K,52K,64K,128K,256K,320K,\n"
		"                           320KCOMPY,576K,576KCOMPY,1088K)\n"
		"  --axlonmemsize <size>    Axlon memory (none,64K,128K,256K,512K,1024K,2048K,4096K)\n"
		"  --highbanks <n>          High memory banks (na,0,1,3,15,63)\n"
		"  --basic / --nobasic      Enable/disable BASIC ROM\n"
		"  --stereo / --nostereo    Enable/disable dual POKEY (stereo)\n"
		"\n"
		"Acceleration:\n"
		"  --burstio / --noburstio  Enable/disable burst I/O transfers\n"
		"  --siopatch               Enable SIO patch\n"
		"  --siopatchsafe           Enable SIO patch (safe mode)\n"
		"  --nosiopatch             Disable SIO patch\n"
		"  --fastboot / --nofastboot Enable/disable fast boot\n"
		"  --casautoboot / --nocasautoboot\n"
		"                           Enable/disable cassette auto-boot\n"
		"  --casautobasicboot / --nocasautobasicboot\n"
		"                           Enable/disable cassette auto-BASIC boot\n"
		"  --accuratedisk / --noaccuratedisk\n"
		"                           Enable/disable accurate disk timing\n"
		"\n"
		"Boot Mode:\n"
		"  --bootro                 Boot media read-only\n"
		"  --bootrw                 Boot media read-write\n"
		"  --bootvrw                Boot media virtual read-write\n"
		"  --bootvrwsafe            Boot media virtual read-write (safe)\n"
		"\n"
		"Media:\n"
		"  --cart <path>            Load cartridge image (repeatable)\n"
		"  --disk <path>            Load disk image (repeatable, sequential drives)\n"
		"  --run <path>             Load and run program (repeatable)\n"
		"  --runbas <path>          Load and run BASIC program (repeatable)\n"
		"  --tape <path>            Load tape image (repeatable)\n"
		"  --tapepos <pos>          Seek tape to position (seconds or mm:ss)\n"
		"  --cartmapper <id>        Force cartridge mapper ID\n"
		"  --nocartchecksum         Disable cartridge checksum validation\n"
		"  --diskemu <mode>         Disk emulation mode (generic,fastest,810,1050,xf551,\n"
		"                           usdoubler,speedy1050,indusgt,happy1050,1050turbo,\n"
		"                           generic57600,happy810)\n"
		"\n"
		"Display:\n"
		"  --artifact <mode>        Artifact mode (none,ntsc,ntschi,pal,palhi,auto,autohi)\n"
		"  --vsync / --novsync      Enable/disable vertical sync\n"
		"\n"
		"Devices:\n"
		"  --soundboard <base>      Add SoundBoard at address (d2c0,d500,d600)\n"
		"  --nosoundboard           Remove SoundBoard\n"
		"  --slightsid / --noslightsid  Add/remove SlightSID\n"
		"  --covox / --nocovox      Add/remove Covox\n"
		"  --hdpath <path>          Set host device read-only path\n"
		"  --hdpathrw <path>        Set host device read-write path\n"
		"  --nohdpath               Remove host device path\n"
		"  --pclink <mode,path>     Set PCLink (mode: ro or rw)\n"
		"  --nopclink               Remove PCLink\n"
		"  --cleardevices           Remove all external devices\n"
		"  --adddevice <tag[,params]>   Add device\n"
		"  --setdevice <tag[,params]>   Set/reconfigure device\n"
		"  --removedevice <tag>     Remove device\n"
		"\n"
		"Debug:\n"
		"  --debug                  Open debugger on startup\n"
		"  --debugbrkrun / --nodebugbrkrun\n"
		"                           Enable/disable break on EXE run address\n"
		"  --debugcmd <cmd>         Queue debugger command (repeatable)\n"
		"\n"
		"Input:\n"
		"  --rawkeys / --norawkeys  Enable/disable raw keyboard mode\n"
		"  --type <text>            Type text into emulator (~ = Enter, ` = quote)\n"
		"\n"
		"Other:\n"
		"  --cheats <path>          Load cheat file\n"
		"  --nocheats               Disable cheat engine\n"
		"  --skipsetup              Skip first-run setup wizard\n",
		progname
	);
}

static void PrintVersion() {
	fprintf(stderr, "Altirra (Linux port) " AT_VERSION "\n");
}

enum {
	OPT_NTSC = 256, OPT_PAL, OPT_SECAM, OPT_NTSC50, OPT_PAL60,
	OPT_HARDWARE, OPT_KERNEL, OPT_KERNELREF, OPT_BASICREF,
	OPT_MEMSIZE, OPT_AXLONMEMSIZE, OPT_HIGHBANKS,
	OPT_BASIC, OPT_NOBASIC, OPT_STEREO, OPT_NOSTEREO,
	OPT_BURSTIO, OPT_NOBURSTIO, OPT_SIOPATCH, OPT_SIOPATCHSAFE, OPT_NOSIOPATCH,
	OPT_FASTBOOT, OPT_NOFASTBOOT,
	OPT_CASAUTOBOOT, OPT_NOCASAUTOBOOT, OPT_CASAUTOBASICBOOT, OPT_NOCASAUTOBASICBOOT,
	OPT_ACCURATEDISK, OPT_NOACCURATEDISK,
	OPT_BOOTRO, OPT_BOOTRW, OPT_BOOTVRW, OPT_BOOTVRWSAFE,
	OPT_CART, OPT_DISK, OPT_RUN, OPT_RUNBAS, OPT_TAPE, OPT_TAPEPOS,
	OPT_CARTMAPPER, OPT_NOCARTCHECKSUM, OPT_DISKEMU,
	OPT_ARTIFACT, OPT_VSYNC, OPT_NOVSYNC,
	OPT_SOUNDBOARD, OPT_NOSOUNDBOARD,
	OPT_SLIGHTSID, OPT_NOSLIGHTSID, OPT_COVOX, OPT_NOCOVOX,
	OPT_HDPATH, OPT_HDPATHRW, OPT_NOHDPATH,
	OPT_PCLINK, OPT_NOPCLINK,
	OPT_CLEARDEVICES, OPT_ADDDEVICE, OPT_SETDEVICE, OPT_REMOVEDEVICE,
	OPT_DEBUG, OPT_DEBUGBRKRUN, OPT_NODEBUGBRKRUN, OPT_DEBUGCMD,
	OPT_PROFILE, OPT_DEFPROFILE, OPT_TEMPPROFILE,
	OPT_AUTOPROFILE, OPT_NOAUTOPROFILE, OPT_BASELINE, OPT_LAUNCH,
	OPT_TYPE, OPT_RAWKEYS, OPT_NORAWKEYS,
	OPT_CHEATS, OPT_NOCHEATS, OPT_SKIPSETUP,
};

static ATLinuxOptions ParseArguments(int argc, char *argv[]) {
	ATLinuxOptions opts;

	static const struct option long_options[] = {
		// General
		{"portable",          no_argument,       nullptr, 'p'},
		{"config",            required_argument, nullptr, 'c'},
		{"rom-path",          required_argument, nullptr, 'r'},
		{"fullscreen",        no_argument,       nullptr, 'f'},
		{"help",              no_argument,       nullptr, 'h'},
		{"version",           no_argument,       nullptr, 'v'},

		// Profile
		{"profile",           required_argument, nullptr, OPT_PROFILE},
		{"defprofile",        required_argument, nullptr, OPT_DEFPROFILE},
		{"tempprofile",       no_argument,       nullptr, OPT_TEMPPROFILE},
		{"autoprofile",       no_argument,       nullptr, OPT_AUTOPROFILE},
		{"noautoprofile",     no_argument,       nullptr, OPT_NOAUTOPROFILE},
		{"baseline",          no_argument,       nullptr, OPT_BASELINE},
		{"launch",            no_argument,       nullptr, OPT_LAUNCH},

		// System
		{"ntsc",              no_argument,       nullptr, OPT_NTSC},
		{"pal",               no_argument,       nullptr, OPT_PAL},
		{"secam",             no_argument,       nullptr, OPT_SECAM},
		{"ntsc50",            no_argument,       nullptr, OPT_NTSC50},
		{"pal60",             no_argument,       nullptr, OPT_PAL60},
		{"hardware",          required_argument, nullptr, OPT_HARDWARE},
		{"kernel",            required_argument, nullptr, OPT_KERNEL},
		{"kernelref",         required_argument, nullptr, OPT_KERNELREF},
		{"basicref",          required_argument, nullptr, OPT_BASICREF},
		{"memsize",           required_argument, nullptr, OPT_MEMSIZE},
		{"axlonmemsize",      required_argument, nullptr, OPT_AXLONMEMSIZE},
		{"highbanks",         required_argument, nullptr, OPT_HIGHBANKS},
		{"basic",             no_argument,       nullptr, OPT_BASIC},
		{"nobasic",           no_argument,       nullptr, OPT_NOBASIC},
		{"stereo",            no_argument,       nullptr, OPT_STEREO},
		{"nostereo",          no_argument,       nullptr, OPT_NOSTEREO},

		// Acceleration
		{"burstio",           no_argument,       nullptr, OPT_BURSTIO},
		{"noburstio",         no_argument,       nullptr, OPT_NOBURSTIO},
		{"siopatch",          no_argument,       nullptr, OPT_SIOPATCH},
		{"siopatchsafe",      no_argument,       nullptr, OPT_SIOPATCHSAFE},
		{"nosiopatch",        no_argument,       nullptr, OPT_NOSIOPATCH},
		{"fastboot",          no_argument,       nullptr, OPT_FASTBOOT},
		{"nofastboot",        no_argument,       nullptr, OPT_NOFASTBOOT},
		{"casautoboot",       no_argument,       nullptr, OPT_CASAUTOBOOT},
		{"nocasautoboot",     no_argument,       nullptr, OPT_NOCASAUTOBOOT},
		{"casautobasicboot",  no_argument,       nullptr, OPT_CASAUTOBASICBOOT},
		{"nocasautobasicboot",no_argument,       nullptr, OPT_NOCASAUTOBASICBOOT},
		{"accuratedisk",      no_argument,       nullptr, OPT_ACCURATEDISK},
		{"noaccuratedisk",    no_argument,       nullptr, OPT_NOACCURATEDISK},

		// Boot mode
		{"bootro",            no_argument,       nullptr, OPT_BOOTRO},
		{"bootrw",            no_argument,       nullptr, OPT_BOOTRW},
		{"bootvrw",           no_argument,       nullptr, OPT_BOOTVRW},
		{"bootvrwsafe",       no_argument,       nullptr, OPT_BOOTVRWSAFE},

		// Media
		{"cart",              required_argument, nullptr, OPT_CART},
		{"disk",              required_argument, nullptr, OPT_DISK},
		{"run",               required_argument, nullptr, OPT_RUN},
		{"runbas",            required_argument, nullptr, OPT_RUNBAS},
		{"tape",              required_argument, nullptr, OPT_TAPE},
		{"tapepos",           required_argument, nullptr, OPT_TAPEPOS},
		{"cartmapper",        required_argument, nullptr, OPT_CARTMAPPER},
		{"nocartchecksum",    no_argument,       nullptr, OPT_NOCARTCHECKSUM},
		{"diskemu",           required_argument, nullptr, OPT_DISKEMU},

		// Display
		{"artifact",          required_argument, nullptr, OPT_ARTIFACT},
		{"vsync",             no_argument,       nullptr, OPT_VSYNC},
		{"novsync",           no_argument,       nullptr, OPT_NOVSYNC},

		// Devices
		{"soundboard",        required_argument, nullptr, OPT_SOUNDBOARD},
		{"nosoundboard",      no_argument,       nullptr, OPT_NOSOUNDBOARD},
		{"slightsid",         no_argument,       nullptr, OPT_SLIGHTSID},
		{"noslightsid",       no_argument,       nullptr, OPT_NOSLIGHTSID},
		{"covox",             no_argument,       nullptr, OPT_COVOX},
		{"nocovox",           no_argument,       nullptr, OPT_NOCOVOX},
		{"hdpath",            required_argument, nullptr, OPT_HDPATH},
		{"hdpathrw",          required_argument, nullptr, OPT_HDPATHRW},
		{"nohdpath",          no_argument,       nullptr, OPT_NOHDPATH},
		{"pclink",            required_argument, nullptr, OPT_PCLINK},
		{"nopclink",          no_argument,       nullptr, OPT_NOPCLINK},
		{"cleardevices",      no_argument,       nullptr, OPT_CLEARDEVICES},
		{"adddevice",         required_argument, nullptr, OPT_ADDDEVICE},
		{"setdevice",         required_argument, nullptr, OPT_SETDEVICE},
		{"removedevice",      required_argument, nullptr, OPT_REMOVEDEVICE},

		// Debug
		{"debug",             no_argument,       nullptr, OPT_DEBUG},
		{"debugbrkrun",       no_argument,       nullptr, OPT_DEBUGBRKRUN},
		{"nodebugbrkrun",     no_argument,       nullptr, OPT_NODEBUGBRKRUN},
		{"debugcmd",          required_argument, nullptr, OPT_DEBUGCMD},

		// Input
		{"rawkeys",           no_argument,       nullptr, OPT_RAWKEYS},
		{"norawkeys",         no_argument,       nullptr, OPT_NORAWKEYS},
		{"type",              required_argument, nullptr, OPT_TYPE},

		// Other
		{"cheats",            required_argument, nullptr, OPT_CHEATS},
		{"nocheats",          no_argument,       nullptr, OPT_NOCHEATS},
		{"skipsetup",         no_argument,       nullptr, OPT_SKIPSETUP},

		{nullptr, 0, nullptr, 0}
	};

	// Reset getopt state in case wxWidgets has already parsed argv
	optind = 1;

	int opt;
	while ((opt = getopt_long(argc, argv, "pc:r:fhv", long_options, nullptr)) != -1) {
		switch (opt) {
			// General
			case 'p': opts.portable = true; break;
			case 'c': opts.configPath = VDTextU8ToW(VDStringA(optarg)); break;
			case 'r': opts.romPath = VDTextU8ToW(VDStringA(optarg)); break;
			case 'f': opts.fullscreen = true; break;
			case 'h': opts.showHelp = true; break;
			case 'v': opts.showVersion = true; break;

			// Profile
			case OPT_PROFILE:       opts.profileName = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_DEFPROFILE:    opts.defProfile = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_TEMPPROFILE:   opts.tempProfile = true; break;
			case OPT_AUTOPROFILE:   opts.autoProfile = true; break;
			case OPT_NOAUTOPROFILE: opts.noAutoProfile = true; break;
			case OPT_BASELINE:      opts.baseline = true; break;
			case OPT_LAUNCH:        opts.launch = true; break;

			// Video standard
			case OPT_NTSC:   opts.videoStandard = kATVideoStandard_NTSC; break;
			case OPT_PAL:    opts.videoStandard = kATVideoStandard_PAL; break;
			case OPT_SECAM:  opts.videoStandard = kATVideoStandard_SECAM; break;
			case OPT_NTSC50: opts.videoStandard = kATVideoStandard_NTSC50; break;
			case OPT_PAL60:  opts.videoStandard = kATVideoStandard_PAL60; break;

			// System config with arguments
			case OPT_HARDWARE:     opts.hardwareMode = -2; {
				VDStringW arg = VDTextU8ToW(VDStringA(optarg));
				if (!vdwcsicmp(arg.c_str(), L"800"))        opts.hardwareMode = kATHardwareMode_800;
				else if (!vdwcsicmp(arg.c_str(), L"800xl"))  opts.hardwareMode = kATHardwareMode_800XL;
				else if (!vdwcsicmp(arg.c_str(), L"1200xl")) opts.hardwareMode = kATHardwareMode_1200XL;
				else if (!vdwcsicmp(arg.c_str(), L"130xe"))  opts.hardwareMode = kATHardwareMode_130XE;
				else if (!vdwcsicmp(arg.c_str(), L"xegs"))   opts.hardwareMode = kATHardwareMode_XEGS;
				else if (!vdwcsicmp(arg.c_str(), L"1400xl")) opts.hardwareMode = kATHardwareMode_1400XL;
				else if (!vdwcsicmp(arg.c_str(), L"5200"))   opts.hardwareMode = kATHardwareMode_5200;
				else {
					fprintf(stderr, "Error: Invalid hardware mode '%s'\n", optarg);
					opts.showHelp = true;
				}
			} break;

			case OPT_KERNEL: opts.kernelMode = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_KERNELREF: opts.kernelRef = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_BASICREF: opts.basicRef = VDTextU8ToW(VDStringA(optarg)); break;

			case OPT_MEMSIZE: opts.memoryMode = -2; {
				VDStringW arg = VDTextU8ToW(VDStringA(optarg));
				if (!vdwcsicmp(arg.c_str(), L"8K"))            opts.memoryMode = kATMemoryMode_8K;
				else if (!vdwcsicmp(arg.c_str(), L"16K"))      opts.memoryMode = kATMemoryMode_16K;
				else if (!vdwcsicmp(arg.c_str(), L"24K"))      opts.memoryMode = kATMemoryMode_24K;
				else if (!vdwcsicmp(arg.c_str(), L"32K"))      opts.memoryMode = kATMemoryMode_32K;
				else if (!vdwcsicmp(arg.c_str(), L"40K"))      opts.memoryMode = kATMemoryMode_40K;
				else if (!vdwcsicmp(arg.c_str(), L"48K"))      opts.memoryMode = kATMemoryMode_48K;
				else if (!vdwcsicmp(arg.c_str(), L"52K"))      opts.memoryMode = kATMemoryMode_52K;
				else if (!vdwcsicmp(arg.c_str(), L"64K"))      opts.memoryMode = kATMemoryMode_64K;
				else if (!vdwcsicmp(arg.c_str(), L"128K"))     opts.memoryMode = kATMemoryMode_128K;
				else if (!vdwcsicmp(arg.c_str(), L"256K"))     opts.memoryMode = kATMemoryMode_256K;
				else if (!vdwcsicmp(arg.c_str(), L"320K"))     opts.memoryMode = kATMemoryMode_320K;
				else if (!vdwcsicmp(arg.c_str(), L"320KCOMPY"))opts.memoryMode = kATMemoryMode_320K_Compy;
				else if (!vdwcsicmp(arg.c_str(), L"576K"))     opts.memoryMode = kATMemoryMode_576K;
				else if (!vdwcsicmp(arg.c_str(), L"576KCOMPY"))opts.memoryMode = kATMemoryMode_576K_Compy;
				else if (!vdwcsicmp(arg.c_str(), L"1088K"))    opts.memoryMode = kATMemoryMode_1088K;
				else {
					fprintf(stderr, "Error: Invalid memory size '%s'\n", optarg);
					opts.showHelp = true;
				}
			} break;

			case OPT_AXLONMEMSIZE: {
				VDStringW arg = VDTextU8ToW(VDStringA(optarg));
				if (!vdwcsicmp(arg.c_str(), L"none"))          opts.axlonMemSize = 0;
				else if (!vdwcsicmp(arg.c_str(), L"64K"))      opts.axlonMemSize = 2;
				else if (!vdwcsicmp(arg.c_str(), L"128K"))     opts.axlonMemSize = 3;
				else if (!vdwcsicmp(arg.c_str(), L"256K"))     opts.axlonMemSize = 4;
				else if (!vdwcsicmp(arg.c_str(), L"512K"))     opts.axlonMemSize = 5;
				else if (!vdwcsicmp(arg.c_str(), L"1024K"))    opts.axlonMemSize = 6;
				else if (!vdwcsicmp(arg.c_str(), L"2048K"))    opts.axlonMemSize = 7;
				else if (!vdwcsicmp(arg.c_str(), L"4096K"))    opts.axlonMemSize = 7;
				else {
					fprintf(stderr, "Error: Invalid Axlon memory size '%s'\n", optarg);
					opts.showHelp = true;
				}
			} break;

			case OPT_HIGHBANKS: {
				VDStringW arg = VDTextU8ToW(VDStringA(optarg));
				if (!vdwcsicmp(arg.c_str(), L"na"))       opts.highBanks = -1;
				else if (!vdwcsicmp(arg.c_str(), L"0"))    opts.highBanks = 0;
				else if (!vdwcsicmp(arg.c_str(), L"1"))    opts.highBanks = 1;
				else if (!vdwcsicmp(arg.c_str(), L"3"))    opts.highBanks = 3;
				else if (!vdwcsicmp(arg.c_str(), L"15"))   opts.highBanks = 15;
				else if (!vdwcsicmp(arg.c_str(), L"63"))   opts.highBanks = 63;
				else {
					fprintf(stderr, "Error: Invalid high banks value '%s'\n", optarg);
					opts.showHelp = true;
				}
			} break;

			case OPT_BASIC:    opts.setBasic = 1; break;
			case OPT_NOBASIC:  opts.setBasic = 0; break;
			case OPT_STEREO:   opts.setStereo = 1; break;
			case OPT_NOSTEREO: opts.setStereo = 0; break;

			// Acceleration
			case OPT_BURSTIO:            opts.burstIO = 1; break;
			case OPT_NOBURSTIO:          opts.burstIO = 0; break;
			case OPT_SIOPATCH:           opts.sioPatch = 1; break;
			case OPT_SIOPATCHSAFE:       opts.sioPatch = 2; break;
			case OPT_NOSIOPATCH:         opts.sioPatch = 0; break;
			case OPT_FASTBOOT:           opts.fastBoot = 1; break;
			case OPT_NOFASTBOOT:         opts.fastBoot = 0; break;
			case OPT_CASAUTOBOOT:        opts.casAutoBoot = 1; break;
			case OPT_NOCASAUTOBOOT:      opts.casAutoBoot = 0; break;
			case OPT_CASAUTOBASICBOOT:   opts.casAutoBasicBoot = 1; break;
			case OPT_NOCASAUTOBASICBOOT: opts.casAutoBasicBoot = 0; break;
			case OPT_ACCURATEDISK:       opts.accurateDisk = 1; break;
			case OPT_NOACCURATEDISK:     opts.accurateDisk = 0; break;

			// Boot mode
			case OPT_BOOTRO: {
				static const auto mode = kATMediaWriteMode_RO;
				opts.bootWriteMode = &mode;
			} break;
			case OPT_BOOTRW: {
				static const auto mode = kATMediaWriteMode_RW;
				opts.bootWriteMode = &mode;
			} break;
			case OPT_BOOTVRW: {
				static const auto mode = kATMediaWriteMode_VRW;
				opts.bootWriteMode = &mode;
			} break;
			case OPT_BOOTVRWSAFE: {
				static const auto mode = kATMediaWriteMode_VRWSafe;
				opts.bootWriteMode = &mode;
			} break;

			// Media
			case OPT_CART:   opts.carts.push_back(VDTextU8ToW(VDStringA(optarg))); break;
			case OPT_DISK:   opts.disks.push_back(VDTextU8ToW(VDStringA(optarg))); break;
			case OPT_RUN:    opts.runs.push_back(VDTextU8ToW(VDStringA(optarg))); break;
			case OPT_RUNBAS: opts.runBas.push_back(VDTextU8ToW(VDStringA(optarg))); break;
			case OPT_TAPE:   opts.tapes.push_back(VDTextU8ToW(VDStringA(optarg))); break;
			case OPT_TAPEPOS: opts.tapePos = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_CARTMAPPER: {
				int mapper = atoi(optarg);
				opts.cartMapper = ATGetCartridgeModeForMapper(mapper);
				if (opts.cartMapper <= 0 || opts.cartMapper >= kATCartridgeModeCount) {
					fprintf(stderr, "Error: Invalid cartridge mapper '%s'\n", optarg);
					opts.showHelp = true;
				}
			} break;
			case OPT_NOCARTCHECKSUM: opts.cartMapper = -1; break;
			case OPT_DISKEMU: opts.diskEmu = VDTextU8ToW(VDStringA(optarg)); break;

			// Display
			case OPT_ARTIFACT: opts.artifactMode = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_VSYNC:   opts.vsync = 1; break;
			case OPT_NOVSYNC: opts.vsync = 0; break;

			// Devices
			case OPT_SOUNDBOARD:   opts.soundboardBase = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_NOSOUNDBOARD: opts.noSoundboard = true; break;
			case OPT_SLIGHTSID:    opts.slightSID = true; break;
			case OPT_NOSLIGHTSID:  opts.noSlightSID = true; break;
			case OPT_COVOX:        opts.covox = true; break;
			case OPT_NOCOVOX:      opts.noCovox = true; break;
			case OPT_HDPATH:       opts.hdPath = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_HDPATHRW:     opts.hdPathRW = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_NOHDPATH:     opts.noHDPath = true; break;
			case OPT_PCLINK:       opts.pclink = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_NOPCLINK:     opts.noPclink = true; break;
			case OPT_CLEARDEVICES:
				opts.deviceOps.push_back({ATLinuxOptions::DeviceOp::kClear, VDStringW()});
				break;
			case OPT_ADDDEVICE:
				opts.deviceOps.push_back({ATLinuxOptions::DeviceOp::kAdd, VDTextU8ToW(VDStringA(optarg))});
				break;
			case OPT_SETDEVICE:
				opts.deviceOps.push_back({ATLinuxOptions::DeviceOp::kSet, VDTextU8ToW(VDStringA(optarg))});
				break;
			case OPT_REMOVEDEVICE:
				opts.deviceOps.push_back({ATLinuxOptions::DeviceOp::kRemove, VDTextU8ToW(VDStringA(optarg))});
				break;

			// Debug
			case OPT_DEBUG:         opts.debug = true; break;
			case OPT_DEBUGBRKRUN:   opts.debugBrkRun = 1; break;
			case OPT_NODEBUGBRKRUN: opts.debugBrkRun = 0; break;
			case OPT_DEBUGCMD:      opts.debugCmds.push_back(VDStringA(optarg)); break;

			// Input
			case OPT_RAWKEYS:   opts.rawKeys = 1; break;
			case OPT_NORAWKEYS: opts.rawKeys = 0; break;
			case OPT_TYPE:      opts.keysToType += VDTextU8ToW(VDStringA(optarg)); break;

			// Other
			case OPT_CHEATS:    opts.cheatsPath = VDTextU8ToW(VDStringA(optarg)); break;
			case OPT_NOCHEATS:  opts.noCheats = true; break;
			case OPT_SKIPSETUP: opts.skipSetup = true; break;

			default:
				break;
		}
	}

	// Collect positional arguments (image files)
	while (optind < argc) {
		opts.positionalFiles.push_back(VDTextU8ToW(VDStringA(argv[optind])));
		++optind;
	}

	return opts;
}

///////////////////////////////////////////////////////////////////////////
// Command-line processing (mirrors Windows ReadCommandLine order)
///////////////////////////////////////////////////////////////////////////

// Forward declarations from stubs_linux.cpp / main.cpp
void DoLoad(VDGUIHandle, const wchar_t *path, const ATMediaWriteMode *writeMode,
	int cartmapper, ATImageType loadType = kATImageType_None,
	bool *suppressColdReset = nullptr, int loadIndex = -1, bool autoProfile = false);
void Paste(const wchar_t *s, size_t len, bool useCooldown);

// Forward declaration from settings.cpp
extern void LoadBaselineSettings();

static void ATProcessCommandLine(const ATLinuxOptions& opts) {
	extern ATSimulator g_sim;
	extern ATUIKeyboardOptions g_kbdOpts;
	bool coldReset = false;
	bool debugModeSuspend = false;

	// --- Profile / auto-profile ---
	bool autoProfile = opts.autoProfile;
	if (opts.launch) {
		// Match Windows: /launch enables auto-profile if configured
		autoProfile = true;
	}
	if (opts.noAutoProfile)
		autoProfile = false;
	ATSettingsSetBootstrapProfileMode(autoProfile);

	try {
		// --- Baseline settings ---
		if (opts.baseline) {
			LoadBaselineSettings();
			coldReset = true;
		}

		// --- Video standard ---
		if (opts.videoStandard >= 0)
			g_sim.SetVideoStandard((ATVideoStandard)opts.videoStandard);

		// --- Disk burst I/O ---
		if (opts.burstIO == 1)
			g_sim.SetDiskBurstTransfersEnabled(true);
		else if (opts.burstIO == 0)
			g_sim.SetDiskBurstTransfersEnabled(false);

		// --- SIO patch ---
		if (opts.sioPatch == 1) {
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskSIOOverrideDetectEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(true);
		} else if (opts.sioPatch == 2) {
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskSIOOverrideDetectEnabled(true);
			g_sim.SetCassetteSIOPatchEnabled(true);
		} else if (opts.sioPatch == 0) {
			g_sim.SetDiskSIOPatchEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(false);
		}

		// --- Fast boot ---
		if (opts.fastBoot == 1)
			g_sim.SetFastBootEnabled(true);
		else if (opts.fastBoot == 0)
			g_sim.SetFastBootEnabled(false);

		// --- Cassette boot options ---
		if (opts.casAutoBoot == 1)
			g_sim.SetCassetteAutoBootEnabled(true);
		else if (opts.casAutoBoot == 0)
			g_sim.SetCassetteAutoBootEnabled(false);

		if (opts.casAutoBasicBoot == 1)
			g_sim.SetCassetteAutoBasicBootEnabled(true);
		else if (opts.casAutoBasicBoot == 0)
			g_sim.SetCassetteAutoBasicBootEnabled(false);

		// --- Disk accurate timing ---
		if (opts.accurateDisk == 1)
			g_sim.SetDiskAccurateTimingEnabled(true);
		else if (opts.accurateDisk == 0)
			g_sim.SetDiskAccurateTimingEnabled(false);

		// --- Stereo / BASIC ---
		if (opts.setStereo == 1) {
			g_sim.SetDualPokeysEnabled(true);
			coldReset = true;
		} else if (opts.setStereo == 0) {
			g_sim.SetDualPokeysEnabled(false);
			coldReset = true;
		}

		if (opts.setBasic == 1) {
			g_sim.SetBASICEnabled(true);
			coldReset = true;
		} else if (opts.setBasic == 0) {
			g_sim.SetBASICEnabled(false);
			coldReset = true;
		}

		// --- Hardware mode ---
		if (opts.hardwareMode >= 0)
			g_sim.SetHardwareMode((ATHardwareMode)opts.hardwareMode);

		// --- Kernel ---
		if (!opts.kernelMode.empty()) {
			const wchar_t *k = opts.kernelMode.c_str();
			if (!vdwcsicmp(k, L"default"))
				g_sim.SetKernel(0);
			else if (!vdwcsicmp(k, L"osa"))
				g_sim.SetKernel(g_sim.GetFirmwareManager()->GetFirmwareOfType(kATFirmwareType_Kernel800_OSA, true));
			else if (!vdwcsicmp(k, L"osb"))
				g_sim.SetKernel(g_sim.GetFirmwareManager()->GetFirmwareOfType(kATFirmwareType_Kernel800_OSB, true));
			else if (!vdwcsicmp(k, L"xl"))
				g_sim.SetKernel(g_sim.GetFirmwareManager()->GetFirmwareOfType(kATFirmwareType_KernelXL, true));
			else if (!vdwcsicmp(k, L"xegs"))
				g_sim.SetKernel(g_sim.GetFirmwareManager()->GetFirmwareOfType(kATFirmwareType_KernelXEGS, true));
			else if (!vdwcsicmp(k, L"1200xl"))
				g_sim.SetKernel(g_sim.GetFirmwareManager()->GetFirmwareOfType(kATFirmwareType_Kernel1200XL, true));
			else if (!vdwcsicmp(k, L"5200"))
				g_sim.SetKernel(g_sim.GetFirmwareManager()->GetFirmwareOfType(kATFirmwareType_Kernel5200, true));
			else if (!vdwcsicmp(k, L"lle"))
				g_sim.SetKernel(kATFirmwareId_Kernel_LLE);
			else if (!vdwcsicmp(k, L"llexl"))
				g_sim.SetKernel(kATFirmwareId_Kernel_LLEXL);
			else if (!vdwcsicmp(k, L"5200lle"))
				g_sim.SetKernel(kATFirmwareId_5200_LLE);
			else
				fprintf(stderr, "Warning: Unknown kernel mode '%s'\n", VDTextWToU8(opts.kernelMode).c_str());
		}

		// --- Kernel/BASIC by reference string ---
		if (!opts.kernelRef.empty()) {
			const auto id = g_sim.GetFirmwareManager()->GetFirmwareByRefString(opts.kernelRef.c_str(), ATIsKernelFirmwareType);
			if (id)
				g_sim.SetKernel(id);
			else
				fprintf(stderr, "Warning: Unable to find kernel matching reference '%s'\n", VDTextWToU8(opts.kernelRef).c_str());
		}

		if (!opts.basicRef.empty()) {
			const auto id = g_sim.GetFirmwareManager()->GetFirmwareByRefString(opts.basicRef.c_str(),
				[](ATFirmwareType type) { return type == kATFirmwareType_Basic; });
			if (id)
				g_sim.SetBasic(id);
			else
				fprintf(stderr, "Warning: Unable to find BASIC matching reference '%s'\n", VDTextWToU8(opts.basicRef).c_str());
		}

		// --- Memory configuration ---
		if (opts.memoryMode >= 0)
			g_sim.SetMemoryMode((ATMemoryMode)opts.memoryMode);

		if (opts.axlonMemSize >= 0)
			g_sim.SetAxlonMemoryMode(opts.axlonMemSize);

		if (opts.highBanks > -2)
			g_sim.SetHighMemoryBanks(opts.highBanks);

		// --- Artifacting ---
		if (!opts.artifactMode.empty()) {
			const wchar_t *a = opts.artifactMode.c_str();
			if (!vdwcsicmp(a, L"none"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
			else if (!vdwcsicmp(a, L"ntsc"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSC);
			else if (!vdwcsicmp(a, L"ntschi"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSCHi);
			else if (!vdwcsicmp(a, L"pal"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PAL);
			else if (!vdwcsicmp(a, L"palhi"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PALHi);
			else if (!vdwcsicmp(a, L"auto"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::Auto);
			else if (!vdwcsicmp(a, L"autohi"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::AutoHi);
			else
				fprintf(stderr, "Warning: Unknown artifact mode '%s'\n", VDTextWToU8(opts.artifactMode).c_str());
		}

		// --- V-Sync ---
		if (opts.vsync == 1)
			g_sim.GetGTIA().SetVsyncEnabled(true);
		else if (opts.vsync == 0)
			g_sim.GetGTIA().SetVsyncEnabled(false);

		// --- Sound devices ---
		auto *dm = g_sim.GetDeviceManager();

		if (!opts.soundboardBase.empty()) {
			uint32 base = 0;
			const wchar_t *sb = opts.soundboardBase.c_str();
			if (!vdwcsicmp(sb, L"d2c0"))      base = 0xD2C0;
			else if (!vdwcsicmp(sb, L"d500"))  base = 0xD500;
			else if (!vdwcsicmp(sb, L"d600"))  base = 0xD600;
			else {
				fprintf(stderr, "Warning: Invalid SoundBoard base '%s' (use d2c0, d500, or d600)\n",
					VDTextWToU8(opts.soundboardBase).c_str());
				goto skip_soundboard;
			}
			{
				ATPropertySet pset;
				pset.SetUint32("base", base);
				IATDevice *dev = dm->GetDeviceByTag("soundboard");
				if (dev)
					dm->ReconfigureDevice(*dev, pset);
				else
					dm->AddDevice("soundboard", pset);
				coldReset = true;
			}
			skip_soundboard:;
		} else if (opts.noSoundboard) {
			dm->RemoveDevice("soundboard");
			coldReset = true;
		}

		if (opts.slightSID) {
			if (!dm->GetDeviceByTag("slightsid"))
				dm->AddDevice("slightsid", ATPropertySet());
			coldReset = true;
		} else if (opts.noSlightSID) {
			dm->RemoveDevice("slightsid");
			coldReset = true;
		}

		if (opts.covox) {
			if (!dm->GetDeviceByTag("covox"))
				dm->AddDevice("covox", ATPropertySet());
			coldReset = true;
		} else if (opts.noCovox) {
			dm->RemoveDevice("covox");
			coldReset = true;
		}

		// --- PCLink ---
		if (opts.noPclink) {
			dm->RemoveDevice("pclink");
		} else if (!opts.pclink.empty()) {
			VDStringRefW tokenizer(opts.pclink.c_str());
			VDStringRefW mode;

			if (tokenizer.split(',', mode)) {
				bool write = false;
				if (mode == L"rw")
					write = true;
				else if (mode != L"ro")
					fprintf(stderr, "Warning: Invalid PCLink mode (use ro or rw)\n");

				ATPropertySet pset;
				pset.SetString("path", VDStringW(tokenizer).c_str());
				if (write)
					pset.SetBool("write", true);

				IATDevice *dev = dm->GetDeviceByTag("pclink");
				if (dev)
					dm->ReconfigureDevice(*dev, pset);
				else
					dm->AddDevice("pclink", pset);
			} else {
				fprintf(stderr, "Warning: Invalid PCLink string (use: mode,path)\n");
			}
		}

		// --- Host device path ---
		if (opts.noHDPath) {
			dm->RemoveDevice("hostfs");
		} else if (!opts.hdPathRW.empty()) {
			ATPropertySet pset;
			pset.SetString("path", opts.hdPathRW.c_str());
			pset.SetBool("write", true);
			IATDevice *dev = dm->GetDeviceByTag("hostfs");
			if (dev)
				dm->ReconfigureDevice(*dev, pset);
			else
				dm->AddDevice("hostfs", pset);
		} else if (!opts.hdPath.empty()) {
			ATPropertySet pset;
			pset.SetString("path", opts.hdPath.c_str());
			IATDevice *dev = dm->GetDeviceByTag("hostfs");
			if (dev)
				dm->ReconfigureDevice(*dev, pset);
			else
				dm->AddDevice("hostfs", pset);
		}

		// --- Generic device operations (ordered) ---
		for (const auto& op : opts.deviceOps) {
			switch (op.type) {
				case ATLinuxOptions::DeviceOp::kClear:
					dm->RemoveAllDevices(false);
					break;

				case ATLinuxOptions::DeviceOp::kAdd: {
					VDStringRefW params(op.arg.c_str());
					VDStringRefW tag;
					if (!params.split(L',', tag)) {
						tag = params;
						params = VDStringRefW();
					}

					ATPropertySet pset;
					if (!params.empty())
						pset.ParseFromCommandLineString(params.data());

					const VDStringA tagA = VDTextWToA(tag);
					const ATDeviceDefinition *def = dm->GetDeviceDefinition(tagA.c_str());
					if (!def || (def->mFlags & kATDeviceDefFlag_Hidden)) {
						fprintf(stderr, "Warning: Unknown device type '%s'\n", tagA.c_str());
						break;
					}

					if (def->mFlags & kATDeviceDefFlag_Internal) {
						IATDevice *dev = dm->GetDeviceByTag(tagA.c_str());
						if (dev)
							dm->ReconfigureDevice(*dev, pset);
						else
							fprintf(stderr, "Warning: Missing internal device '%s'\n", tagA.c_str());
					} else {
						dm->AddDevice(def, pset);
					}
					coldReset = true;
				} break;

				case ATLinuxOptions::DeviceOp::kSet: {
					VDStringRefW params(op.arg.c_str());
					VDStringRefW tag;
					if (!params.split(L',', tag)) {
						tag = params;
						params = VDStringRefW();
					}

					ATPropertySet pset;
					if (!params.empty())
						pset.ParseFromCommandLineString(params.data());

					const VDStringA tagA = VDTextWToA(tag);
					const ATDeviceDefinition *def = dm->GetDeviceDefinition(tagA.c_str());
					if (!def || (def->mFlags & kATDeviceDefFlag_Hidden)) {
						fprintf(stderr, "Warning: Unknown device type '%s'\n", tagA.c_str());
						break;
					}

					IATDevice *dev = dm->GetDeviceByTag(tagA.c_str());
					if (def->mFlags & kATDeviceDefFlag_Internal) {
						if (dev)
							dm->ReconfigureDevice(*dev, pset);
						else
							fprintf(stderr, "Warning: Missing internal device '%s'\n", tagA.c_str());
					} else {
						if (dev)
							dm->ReconfigureDevice(*dev, pset);
						else
							dm->AddDevice(def, pset);
					}
					coldReset = true;
				} break;

				case ATLinuxOptions::DeviceOp::kRemove: {
					const VDStringA tagA = VDTextWToA(VDStringRefW(op.arg.c_str()));
					IATDevice *dev = dm->GetDeviceByTag(tagA.c_str(), 0, true, true);
					if (dev) {
						ATDeviceInfo devInfo;
						dev->GetDeviceInfo(devInfo);
						if (!(devInfo.mpDef->mFlags & kATDeviceDefFlag_Internal))
							dm->RemoveDevice(dev);
					}
					coldReset = true;
				} break;
			}
		}

		// --- Cheats ---
		if (opts.noCheats) {
			g_sim.SetCheatEngineEnabled(false);
		} else if (!opts.cheatsPath.empty()) {
			g_sim.SetCheatEngineEnabled(true);
			g_sim.GetCheatEngine()->Load(opts.cheatsPath.c_str());
		}

		// --- Disk emulation mode ---
		if (!opts.diskEmu.empty()) {
			auto result = ATParseEnum<ATDiskEmulationMode>(VDTextWToU8(VDStringSpanW(opts.diskEmu.c_str(), opts.diskEmu.c_str() + opts.diskEmu.size())));
			if (result.mValid) {
				for (int i = 0; i < 15; ++i)
					g_sim.GetDiskDrive(i).SetEmulationMode(result.mValue);
			} else {
				fprintf(stderr, "Warning: Unknown disk emulation mode '%s'\n", VDTextWToU8(opts.diskEmu).c_str());
			}
		}

		// --- Raw keys ---
		if (opts.rawKeys == 1)
			g_kbdOpts.mbRawKeys = true;
		else if (opts.rawKeys == 0)
			g_kbdOpts.mbRawKeys = false;

		// --- Media loading ---
		bool unloaded = false;
		bool hasMedia = !opts.carts.empty() || !opts.disks.empty() || !opts.runs.empty()
			|| !opts.runBas.empty() || !opts.tapes.empty() || !opts.positionalFiles.empty();

		auto ensureUnloaded = [&]() {
			if (!unloaded && hasMedia) {
				unloaded = true;
				g_sim.UnloadAll();
			}
		};

		for (const auto& path : opts.carts) {
			ensureUnloaded();
			DoLoad(nullptr, path.c_str(), opts.bootWriteMode, opts.cartMapper,
				kATImageType_Cartridge, nullptr, -1, autoProfile);
			coldReset = true;
		}

		int diskIndex = 0;
		for (const auto& path : opts.disks) {
			ensureUnloaded();
			DoLoad(nullptr, path.c_str(), opts.bootWriteMode, opts.cartMapper,
				kATImageType_Disk, nullptr, diskIndex++, autoProfile);
			coldReset = true;
		}

		for (const auto& path : opts.runs) {
			ensureUnloaded();
			DoLoad(nullptr, path.c_str(), opts.bootWriteMode, opts.cartMapper,
				kATImageType_Program, nullptr, -1, autoProfile);
			coldReset = true;
		}

		for (const auto& path : opts.runBas) {
			ensureUnloaded();
			DoLoad(nullptr, path.c_str(), opts.bootWriteMode, opts.cartMapper,
				kATImageType_BasicProgram, nullptr, -1, autoProfile);
			coldReset = true;
		}

		for (const auto& path : opts.tapes) {
			ensureUnloaded();
			DoLoad(nullptr, path.c_str(), opts.bootWriteMode, opts.cartMapper,
				kATImageType_Tape, nullptr, -1, autoProfile);
			coldReset = true;
		}

		// Positional arguments treated as generic image loads
		for (const auto& path : opts.positionalFiles) {
			ensureUnloaded();
			DoLoad(nullptr, path.c_str(), opts.bootWriteMode, opts.cartMapper,
				kATImageType_None, nullptr, -1, autoProfile);
			coldReset = true;
		}

		// --- Tape position ---
		if (!opts.tapePos.empty()) {
			// Simple seconds-based position parsing
			float pos = (float)wcstod(opts.tapePos.c_str(), nullptr);
			if (pos > 0)
				g_sim.GetCassette().SeekToTime(pos);
		}

		// --- Debug ---
		if (opts.debug)
			ATShowConsole();

		IATDebugger *dbg = ATGetDebugger();
		if (opts.debugBrkRun == 1)
			dbg->SetBreakOnEXERunAddrEnabled(true);
		else if (opts.debugBrkRun == 0)
			dbg->SetBreakOnEXERunAddrEnabled(false);

		for (const auto& cmd : opts.debugCmds) {
			debugModeSuspend = true;
			dbg->QueueCommand(cmd.c_str(), false);
		}

		if (debugModeSuspend) {
			g_sim.Suspend();
			dbg->QueueCommand("`g -n", false);
		}

		// --- Type text ---
		if (!opts.keysToType.empty()) {
			VDStringW text(opts.keysToType);

			// Replace ~ with newline and ` with double-quote (matching Windows convention)
			for (size_t i = 0; i < text.size(); ++i) {
				if (text[i] == L'~')
					text[i] = L'\n';
				else if (text[i] == L'`')
					text[i] = L'"';
			}

			Paste(text.data(), text.size(), false);
		}

		// --- Cold reset if any settings changed ---
		if (coldReset) {
			g_sim.ColdReset();
		}

	} catch (const MyError& e) {
		fprintf(stderr, "Command-line error: %s\n", e.c_str());
	}
}

///////////////////////////////////////////////////////////////////////////
// wxApp
///////////////////////////////////////////////////////////////////////////

class ATApp : public wxApp {
public:
	void OnInitCmdLine(wxCmdLineParser& parser) override;
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
	bool m_firstRun = false;
};

wxIMPLEMENT_APP_NO_MAIN(ATApp);

int main(int argc, char *argv[]) {
	// Handle --help and --version before wxWidgets initialization
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			PrintUsage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
			PrintVersion();
			return 0;
		}
	}

	wxEntryStart(argc, argv);
	wxTheApp->CallOnInit();
	if (wxTheApp->OnInit()) {
		wxTheApp->OnRun();
	}
	wxTheApp->OnExit();
	wxEntryCleanup();
	return 0;
}

void ATApp::OnInitCmdLine(wxCmdLineParser& parser) {
	// Suppress wxWidgets' built-in command-line parsing (--help, --verbose, etc.)
	// so we can handle all arguments ourselves via getopt_long.
	parser.SetDesc(nullptr);
}

bool ATApp::OnInit() {
	if (!wxApp::OnInit())
		return false;

	// Parse our own arguments (wxWidgets may consume some first)
	ATLinuxOptions opts = ParseArguments(argc, argv);

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

	// Process command-line switches (system config, media, devices, debug, etc.)
	// This must happen after settings/ROMs are loaded but before the window is created.
	bool hasCommandLineSwitches = opts.videoStandard >= 0 || opts.hardwareMode >= 0
		|| !opts.kernelMode.empty() || !opts.kernelRef.empty() || !opts.basicRef.empty()
		|| opts.memoryMode >= 0 || opts.axlonMemSize >= 0 || opts.highBanks > -2
		|| opts.setBasic >= 0 || opts.setStereo >= 0
		|| opts.burstIO >= 0 || opts.sioPatch >= 0 || opts.fastBoot >= 0
		|| opts.casAutoBoot >= 0 || opts.casAutoBasicBoot >= 0 || opts.accurateDisk >= 0
		|| opts.bootWriteMode || !opts.carts.empty() || !opts.disks.empty()
		|| !opts.runs.empty() || !opts.runBas.empty() || !opts.tapes.empty()
		|| !opts.positionalFiles.empty() || !opts.artifactMode.empty() || opts.vsync >= 0
		|| !opts.soundboardBase.empty() || opts.noSoundboard || opts.slightSID || opts.noSlightSID
		|| opts.covox || opts.noCovox || !opts.pclink.empty() || opts.noPclink
		|| !opts.hdPath.empty() || !opts.hdPathRW.empty() || opts.noHDPath
		|| !opts.deviceOps.empty() || !opts.cheatsPath.empty() || opts.noCheats
		|| !opts.diskEmu.empty() || opts.rawKeys >= 0 || !opts.keysToType.empty()
		|| opts.debug || opts.debugBrkRun >= 0 || !opts.debugCmds.empty()
		|| opts.baseline || opts.autoProfile || opts.noAutoProfile || opts.launch;

	if (hasCommandLineSwitches)
		ATProcessCommandLine(opts);

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

	// Apply fullscreen after window creation
	if (opts.fullscreen)
		ATSetFullscreen(true);

	// Show setup wizard on first run (skip if --skipsetup or command-line media provided)
	if (m_firstRun && !opts.skipSetup && !hasCommandLineSwitches) {
		ATShowSetupWizard(m_frame);
	}

	// Cold reset and start emulation
	// (ATProcessCommandLine may have already done a cold reset if it loaded media,
	//  but ColdReset is idempotent and needed for the no-args case)
	g_sim.ColdReset();
	g_sim.Resume();
	m_frame->StartEmulation();
	fprintf(stderr, "Emulation started\n");

	return true;
}

// Close functions defined in individual dialog files
void ATCloseAudioWindows();
void ATCloseVideoSettingsWindow();

void ATCloseAllNonModalWindows() {
	ATCloseAudioWindows();
	ATCloseVideoSettingsWindow();
	ATCloseOnScreenKeyboard();
}

int ATApp::OnExit() {
	fprintf(stderr, "Shutting down...\n");

	// NOTE: Do NOT access m_frame here — it may already be deleted.
	// OnClose() already called StopEmulation(), disconnected GTIA,
	// cleared g_pDisplay, and closed non-modal windows before calling
	// Destroy().  wxWidgets may have deleted the frame during idle
	// processing before OnExit() was called.
	m_frame = nullptr;

	// Save settings before shutdown
	ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));

	try {
		ATUISaveRegistry(g_settingsPath.c_str());
	} catch (...) {
		fprintf(stderr, "Warning: Failed to save settings\n");
	}

	// Disconnect display from GTIA (idempotent — OnClose already did this,
	// but guard against abnormal shutdown paths)
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

		// Ctrl+V: paste text to emulator
		case 'V': {
			if (ctrl) {
				// Paste via SDL clipboard (SDL3 is still linked for audio/gamepad)
				char *text = SDL_GetClipboardText();
				if (text && *text) {
					VDStringW ws = VDTextU8ToW(VDStringSpanA(text));
					auto& pokey = g_sim.GetPokey();

					for (size_t i = 0; i < ws.size(); ++i) {
						wchar_t c = ws[i];
						if (!c) continue;

						// Normalize smart quotes/dashes
						switch (c) {
							case L'\u2010': case L'\u2011': case L'\u2012':
							case L'\u2013': case L'\u2014': case L'\u2015':
								c = L'-'; break;
							case L'\u2018': case L'\u2019':
								c = L'\''; break;
							case L'\u201C': case L'\u201D':
								c = L'"'; break;
						}

						if (c == L'\r' || c == L'\n') {
							if (c == L'\r' && i + 1 < ws.size() && ws[i + 1] == L'\n')
								++i;
							pokey.PushKey(0x0C, false, true, false, true);
							continue;
						}
						if (c == L'\t') {
							pokey.PushKey(0x2C, false, true, false, true);
							continue;
						}
					}
					ATImGuiShowToast("Text pasted");
				}
				SDL_free(text);
				return Event_Processed;
			}
			break;
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
		m_firstRun = true;
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

	// Handle --profile and --defprofile before loading settings
	if (!opts.profileName.empty()) {
		uint32 profileId = ATSettingsFindProfileByName(opts.profileName.c_str());
		if (profileId)
			ATSettingsLoadProfile(profileId, ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
		else {
			fprintf(stderr, "Warning: Profile '%s' not found, using last profile\n", VDTextWToU8(opts.profileName).c_str());
			ATSettingsLoadLastProfile(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
		}
	} else if (!opts.defProfile.empty()) {
		ATDefaultProfile dp = kATDefaultProfile_XL;
		const wchar_t *d = opts.defProfile.c_str();
		if (!vdwcsicmp(d, L"800"))        dp = kATDefaultProfile_800;
		else if (!vdwcsicmp(d, L"xl"))     dp = kATDefaultProfile_XL;
		else if (!vdwcsicmp(d, L"xegs"))   dp = kATDefaultProfile_XEGS;
		else if (!vdwcsicmp(d, L"1200xl")) dp = kATDefaultProfile_1200XL;
		else if (!vdwcsicmp(d, L"5200"))   dp = kATDefaultProfile_5200;
		else
			fprintf(stderr, "Warning: Unknown default profile '%s'\n", VDTextWToU8(opts.defProfile).c_str());

		uint32 profileId = ATGetDefaultProfileId(dp);
		if (profileId)
			ATSettingsLoadProfile(profileId, ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
		else
			ATSettingsLoadLastProfile(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
	} else {
		ATSettingsLoadLastProfile(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));
	}

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

	return true;
}
