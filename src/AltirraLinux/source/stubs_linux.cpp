//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - stub implementations for Windows-only functionality
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

// Linux stub implementations for ~121 undefined symbols that come from
// Windows-only source files excluded from the Linux build. These are
// no-op/empty stubs to satisfy the linker; real implementations should
// replace these as the Linux port matures.

#include <stdafx.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/math.h>
#include <vd2/system/refcount.h>
#include <vd2/system/function.h>
#include <vd2/system/vectors.h>
#include <vd2/system/atomic.h>
#include <vd2/system/thread.h>
#include <vd2/system/text.h>
#include <vd2/system/time.h>
#include <vd2/system/file.h>
#include <vd2/system/fileasync.h>
#include <vd2/system/filewatcher.h>
#include <vd2/Dita/accel.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/VDDisplay/display.h>
#include <at/atcore/device.h>
#include <at/atcore/enumparse.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/blockdevice.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/timerservice.h>
#include <at/ataudio/audiooutput.h>
#include <at/atnetwork/socket.h>
#include <at/atnetworksockets/vxlantunnel.h>
#include <at/atnetworksockets/worker.h>
#include <at/atui/uimanager.h>
#include <at/atui/uicommandmanager.h>

#include <SDL3/SDL.h>
#include <error_imgui.h>
#include <emulator_imgui.h>
#include "display_sdl3.h"
#include <at/atio/partitiontable.h>
#include <at/atio/partitiondiskview.h>

#include <algorithm>
#include <cerrno>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// Forward declaration: simulator from main_linux.cpp
#include "simulator.h"

// Altirra application headers
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "uiqueue.h"
#include "uimenu.h"
#include "uiclipboard.h"
#include "uicommondialogs.h"
#include "uirender.h"
#include "uiconfirm.h"
#include "debugger.h"
#include "constants.h"
#include "firmwaremanager.h"
#include "settings.h"
#include <at/atdebugger/target.h>
#include "devicemanager.h"
#include "directorywatcher.h"
#include "idephysdisk.h"
#include "modemtcp.h"
#include "customdevice_win32.h"
#include "trace.h"
#include <at/atcore/cio.h>
#include <at/atcore/constants.h>
#include "uienhancedtext.h"

///////////////////////////////////////////////////////////////////////////
// 1. Global variable definitions
///////////////////////////////////////////////////////////////////////////

// ATUIManager instance (defined in uidisplay.cpp on Windows).
// We use raw aligned storage because ATUIManager's ctor/dtor live in the
// ATUI library which is not yet built for Linux. Callers use
// 'extern ATUIManager g_ATUIManager' so the symbol must exist with the
// correct size and alignment. The object is zero-initialized which is
// safe for a class whose members are pointers and POD.
alignas(ATUIManager) static char g_ATUIManager_storage[sizeof(ATUIManager)] = {};
ATUIManager& g_ATUIManager = reinterpret_cast<ATUIManager&>(g_ATUIManager_storage);

// Keyboard options — defined in uikeyboard_linux.cpp
extern ATUIKeyboardOptions g_kbdOpts;

// Device definitions for Windows-only devices
extern const ATDeviceDefinition g_ATDeviceDefBrowser = {
	"browser", nullptr, L"Browser (B:)", nullptr, 0
};

extern const ATDeviceDefinition g_ATDeviceDefIDEPhysDisk = {
	"hdphysdisk", "harddisk", L"Hard disk image (physical disk)", nullptr, 0
};

extern const ATDeviceDefinition g_ATDeviceDefMidiMate = {
	"midimate", nullptr, L"MidiMate", nullptr, 0
};

extern const ATDeviceDefinition g_ATDeviceDefPipeSerial = {
	"pipeserial", "pipeserial", L"Named pipe serial port", nullptr, 0
};

///////////////////////////////////////////////////////////////////////////
// 2. ATUIManager methods (Windows display layer)
//    The ATUI library is not yet built for Linux, so we stub the two
//    methods that are called from Altirra application code.
///////////////////////////////////////////////////////////////////////////

const wchar_t *ATUIManager::GetCustomEffectPath() const {
	return L"";
}

void ATUIManager::SetCustomEffectPath(const wchar_t *, bool) {
}

///////////////////////////////////////////////////////////////////////////
// 3. ATUIQueue (UI step queue)
///////////////////////////////////////////////////////////////////////////

static ATUIQueue s_stubQueue;

void ATUIQueue::PushStep(const vdfunction<void()>& step) {
	mSteps.push_back(step);
}

bool ATUIQueue::Run() {
	if (mSteps.empty())
		return false;

	ATUIStep step(std::move(mSteps.back()));
	mSteps.pop_back();

	try {
		step();
	} catch (const MyError& e) {
		ATUIShowError(e);
	} catch (...) {
	}

	return true;
}

ATUIQueue& ATUIGetQueue() {
	return s_stubQueue;
}

void ATUIPushStep(const ATUIStep& step) {
	s_stubQueue.PushStep(step);
}

// Forward declaration (defined in section 8)
void ATUIUpdateSpeedTiming();

///////////////////////////////////////////////////////////////////////////
// 4. ATUI accessor getters (bool) — simple stubs
///////////////////////////////////////////////////////////////////////////

static bool s_altViewAutoswitch = false;
static bool s_altViewEnabled = false;
static bool s_constrainMouseFS = true;
static bool s_displayIndicators = false;
static bool s_displayPadIndicators = false;
static bool s_drawPadBounds = false;
static bool s_drawPadPointers = false;
static bool s_mouseAutoCapture = false;
static bool s_pauseWhenInactive = false;
static bool s_pointerAutoHide = true;
static bool s_rawInput = false;
static bool s_targetPointerVisible = false;
static bool s_frameRateVSyncAdaptive = false;
static bool s_menuAutoHide = false;

bool ATUIGetAltViewAutoswitchingEnabled() { return s_altViewAutoswitch; }
bool ATUIGetAltViewEnabled() { return s_altViewEnabled; }
bool ATUIGetConstrainMouseFullScreen() { return s_constrainMouseFS; }
bool ATUIGetDisplayIndicators() { return s_displayIndicators; }
bool ATUIGetDisplayPadIndicators() { return s_displayPadIndicators; }
bool ATUIGetDrawPadBoundsEnabled() { return s_drawPadBounds; }
bool ATUIGetDrawPadPointersEnabled() { return s_drawPadPointers; }
bool ATUIGetMouseAutoCapture() { return s_mouseAutoCapture; }
bool ATUIGetPauseWhenInactive() { return s_pauseWhenInactive; }
bool ATUIGetPointerAutoHide() { return s_pointerAutoHide; }
bool ATUIGetRawInputEnabled() { return s_rawInput; }
bool ATUIGetTargetPointerVisible() { return s_targetPointerVisible; }
bool ATUIGetFrameRateVSyncAdaptive() { return s_frameRateVSyncAdaptive; }
bool ATUIIsMenuAutoHideEnabled() { return s_menuAutoHide; }
bool ATUIIsElevationRequiredForMountVHDImage() { return false; }

///////////////////////////////////////////////////////////////////////////
// 4b. ATUI accessor getters (bool) — backed by static variables
//     These are read/written by the ImGui emulator UI.
///////////////////////////////////////////////////////////////////////////

static bool s_showFPS = false;
static bool s_showStatusBar = true;
static bool s_turbo = false;
static bool s_slowMotion = false;
static bool s_fullscreen = false;

bool ATUIGetShowFPS() { return s_showFPS; }
bool ATUIGetShowStatusBar() { return s_showStatusBar; }
bool ATUIGetTurbo() { return s_turbo; }
bool ATUIGetSlowMotion() { return s_slowMotion; }
bool ATUIGetFullscreen() { return s_fullscreen; }

///////////////////////////////////////////////////////////////////////////
// 5. ATUI accessor getters (numeric / enum / string / pointer)
///////////////////////////////////////////////////////////////////////////

static uint32 s_bootUnloadStorageMask = 0;
static uint32 s_resetFlags = 0;
uint32 ATUIGetBootUnloadStorageMask() { return s_bootUnloadStorageMask; }
uint32 ATUIGetResetFlags() { return s_resetFlags; }

static ATDisplayFilterMode s_displayFilterMode = (ATDisplayFilterMode)0;
static ATDisplayStretchMode s_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;
static float s_speedModifier = 1.0f;

ATDisplayFilterMode ATUIGetDisplayFilterMode() { return s_displayFilterMode; }
ATDisplayStretchMode ATUIGetDisplayStretchMode() { return s_displayStretchMode; }
static ATFrameRateMode s_frameRateMode = (ATFrameRateMode)0;
static ATUIEnhancedTextMode s_enhancedTextMode = kATUIEnhancedTextMode_None;
ATFrameRateMode ATUIGetFrameRateMode() { return s_frameRateMode; }
ATUIEnhancedTextMode ATUIGetEnhancedTextMode() { return s_enhancedTextMode; }

static float s_displayZoom = 1.0f;
float ATUIGetDisplayZoom() { return s_displayZoom; }
float ATUIGetSpeedModifier() { return s_speedModifier; }
static int s_viewFilterSharpness = 0;
int ATUIGetViewFilterSharpness() { return s_viewFilterSharpness; }

static vdfloat2 s_displayPanOffset{0, 0};
vdfloat2 ATUIGetDisplayPanOffset() { return s_displayPanOffset; }

const char *ATUIGetCurrentAltOutputName() { return ""; }

static VDStringA s_windowCaptionTemplate;
const char *ATUIGetWindowCaptionTemplate() { return s_windowCaptionTemplate.c_str(); }

VDGUIHandle ATUIGetNewPopupOwner() { return nullptr; }

///////////////////////////////////////////////////////////////////////////
// 6. ATUI accessor setters
///////////////////////////////////////////////////////////////////////////

void ATUISetAltViewAutoswitchingEnabled(bool v) { s_altViewAutoswitch = v; }
void ATUISetAltViewEnabled(bool v) { s_altViewEnabled = v; }
void ATUISetBootUnloadStorageMask(uint32 v) { s_bootUnloadStorageMask = v; }
void ATUISetConstrainMouseFullScreen(bool v) {
	s_constrainMouseFS = v;
	extern SDL_Window *ATGetLinuxWindow();
	SDL_Window *w = ATGetLinuxWindow();
	if (w && ATUIGetFullscreen())
		SDL_SetWindowMouseGrab(w, v);
}
void ATUISetCurrentAltOutputName(const char *) {}
void ATUISetDisplayFilterMode(ATDisplayFilterMode m) {
	s_displayFilterMode = m;
	extern ATDisplaySDL3 *ATGetLinuxDisplay();
	ATDisplaySDL3 *disp = ATGetLinuxDisplay();
	if (disp) {
		IVDVideoDisplay::FilterMode fm =
			(m == kATDisplayFilterMode_Point)
				? IVDVideoDisplay::kFilterPoint
				: IVDVideoDisplay::kFilterBilinear;
		disp->SetFilterMode(fm);
	}
}
void ATUISetDisplayIndicators(bool v) { s_displayIndicators = v; }
void ATUISetDisplayPadIndicators(bool v) { s_displayPadIndicators = v; }
void ATUISetDisplayPanOffset(const vdfloat2& v) { s_displayPanOffset = v; }
void ATUISetDisplayStretchMode(ATDisplayStretchMode m) {
	s_displayStretchMode = m;
	extern ATDisplaySDL3 *ATGetLinuxDisplay();
	ATDisplaySDL3 *disp = ATGetLinuxDisplay();
	if (disp)
		disp->SetStretchMode(m);
}
void ATUISetDisplayZoom(float v) { s_displayZoom = v; }
void ATUISetDrawPadBoundsEnabled(bool v) { s_drawPadBounds = v; }
void ATUISetDrawPadPointersEnabled(bool v) { s_drawPadPointers = v; }
// Enhanced text engine instance — accessible from emulator_imgui.cpp and main_linux.cpp
static IATUIEnhancedTextEngine *g_pEnhancedTextEngine = nullptr;

IATUIEnhancedTextEngine *ATUIGetEnhancedTextEngine() {
	return g_pEnhancedTextEngine;
}

// Output callback that triggers display refresh
class ATLinuxEnhancedTextOutput : public IATUIEnhancedTextOutput {
public:
	void InvalidateTextOutput() override {
		// The display update is driven by the per-frame Update() call in
		// RenderAndSwap(), so we don't need to do anything special here.
	}
};

static ATLinuxEnhancedTextOutput g_enhancedTextOutput;

void ATUISetEnhancedTextMode(ATUIEnhancedTextMode v) {
	extern ATSimulator g_sim;
	extern ATDisplaySDL3 *ATGetLinuxDisplay();

	ATUIEnhancedTextMode oldMode = s_enhancedTextMode;
	s_enhancedTextMode = v;

	// Destroy old engine if switching away from enhanced text
	if (oldMode != kATUIEnhancedTextMode_None && v == kATUIEnhancedTextMode_None) {
		if (g_pEnhancedTextEngine) {
			g_pEnhancedTextEngine->Shutdown();
			delete g_pEnhancedTextEngine;
			g_pEnhancedTextEngine = nullptr;
		}

		g_sim.SetVirtualScreenEnabled(false);
		return;
	}

	switch (v) {
		case kATUIEnhancedTextMode_None:
			g_sim.SetVirtualScreenEnabled(false);
			break;

		case kATUIEnhancedTextMode_Hardware:
			g_sim.SetVirtualScreenEnabled(false);
			break;

		case kATUIEnhancedTextMode_Software:
			g_sim.SetVirtualScreenEnabled(true);
			g_sim.GetPokey().PushBreak();
			break;
	}

	if (v != kATUIEnhancedTextMode_None) {
		// Keep GTIA connected to the display so frame timing continues
		// to work normally. The enhanced text engine's framebuffer is
		// set as persistent source in RenderAndSwap(), overwriting
		// GTIA's output before the display renders.

		if (!g_pEnhancedTextEngine) {
			g_pEnhancedTextEngine = ATUICreateEnhancedTextEngine();
			g_pEnhancedTextEngine->Init(&g_enhancedTextOutput, &g_sim);

			// Initialize with current window size
			ATDisplaySDL3 *disp = ATGetLinuxDisplay();
			if (disp) {
				int w = 0, h = 0;
				disp->GetWindowSize(w, h);
				if (w > 0 && h > 0)
					g_pEnhancedTextEngine->OnSize(w, h);
			}
		}
	}
}
void ATUISetFrameRateMode(ATFrameRateMode v) { s_frameRateMode = v; ATUIUpdateSpeedTiming(); }
static void ApplyVSyncSetting() {
	if (s_turbo) {
		SDL_GL_SetSwapInterval(0);
	} else if (s_frameRateVSyncAdaptive) {
		// -1 = adaptive vsync: tear if late, sync otherwise
		if (!SDL_GL_SetSwapInterval(-1))
			SDL_GL_SetSwapInterval(1);  // fallback to regular vsync
	} else {
		SDL_GL_SetSwapInterval(1);
	}
}
void ATUISetFrameRateVSyncAdaptive(bool v) { s_frameRateVSyncAdaptive = v; ApplyVSyncSetting(); }
void ATUISetMenuAutoHideEnabled(bool v) { s_menuAutoHide = v; }
void ATUISetMouseAutoCapture(bool v) { s_mouseAutoCapture = v; }
void ATUISetPauseWhenInactive(bool v) { s_pauseWhenInactive = v; }
void ATUISetPointerAutoHide(bool v) { s_pointerAutoHide = v; }
void ATUISetRawInputEnabled(bool v) { s_rawInput = v; }
void ATUISetResetFlags(uint32 v) { s_resetFlags = v; }
void ATUISetShowFPS(bool v) { s_showFPS = v; }
void ATUISetShowStatusBar(bool v) { s_showStatusBar = v; }
void ATUISetSpeedModifier(float v) {
	s_speedModifier = v;
	ATUIUpdateSpeedTiming();
}
void ATUISetTargetPointerVisible(bool v) { s_targetPointerVisible = v; }
void ATUISetSlowMotion(bool v) {
	s_slowMotion = v;
	ATUIUpdateSpeedTiming();
}
void ATUISetTurbo(bool v) {
	s_turbo = v;
	extern ATSimulator g_sim;
	g_sim.SetTurboModeEnabled(v);
	ApplyVSyncSetting();
	ATUIUpdateSpeedTiming();
}
void ATUISetViewFilterSharpness(int v) { s_viewFilterSharpness = v; }
void ATUISetWindowCaptionTemplate(const char *s) { s_windowCaptionTemplate = s ? s : ""; }

///////////////////////////////////////////////////////////////////////////
// 7. ATUI keyboard map functions — implemented in uikeyboard_linux.cpp
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// 8. ATUI miscellaneous functions
///////////////////////////////////////////////////////////////////////////

// ATSetFullscreen is implemented via a callback from main_linux.cpp
// so that SDL_Window* doesn't need to be exposed globally.
static void (*s_pfnSetFullscreen)(bool) = nullptr;

void ATSetFullscreenCallback(void (*pfn)(bool)) {
	s_pfnSetFullscreen = pfn;
}

void ATSetFullscreen(bool fs) {
	s_fullscreen = fs;
	if (s_pfnSetFullscreen)
		s_pfnSetFullscreen(fs);
}
void ATUIResizeDisplay() {}

// ATSetWindowSize callback — sets the SDL window size without exposing SDL_Window*
static void (*s_pfnSetWindowSize)(int, int) = nullptr;

void ATSetWindowSizeCallback(void (*pfn)(int, int)) {
	s_pfnSetWindowSize = pfn;
}

void ATSetWindowSize(int w, int h) {
	if (s_pfnSetWindowSize)
		s_pfnSetWindowSize(w, h);
}
// Frame timing variables — computed by ATUIUpdateSpeedTiming(), consumed by main loop
sint64	g_frameTicks;
uint32	g_frameSubTicks;
sint64	g_frameErrorBound;
sint64	g_frameTimeout;

void ATUIUpdateSpeedTiming() {
	extern ATSimulator g_sim;

	// NTSC: 1.7897725MHz master clock, 262 scanlines of 114 clocks each
	// PAL:  1.773447MHz master clock, 312 scanlines of 114 clocks each
	// SECAM: 1.7815MHz master clock, 312 scanlines of 114 clocks each
	static constexpr double kMasterClocks[3] = {
		kATMasterClock_NTSC,
		kATMasterClock_PAL,
		kATMasterClock_SECAM,
	};

	static constexpr double kPeriods[3][3] = {
		{ 1.0 / kATFrameRate_NTSC, 1.0 / kATFrameRate_PAL, 1.0 / kATFrameRate_SECAM },
		{ 1.0 / 59.9400, 1.0 / 50.0000, 1.0 / 50.0 },
		{ 1.0 / 60.0000, 1.0 / 50.0000, 1.0 / 50.0 },
	};

	const auto vstd = g_sim.GetVideoStandard();
	const bool hz50 = vstd != kATVideoStandard_NTSC && vstd != kATVideoStandard_PAL60;
	const bool isSECAM = vstd == kATVideoStandard_SECAM;
	const int tableIndex = isSECAM ? 2 : hz50 ? 1 : 0;
	double rawSecondsPerFrame = kPeriods[s_frameRateMode][tableIndex];

	const double cyclesPerSecond = kMasterClocks[tableIndex] * kPeriods[0][tableIndex] / rawSecondsPerFrame;

	// Linux UI stores s_speedModifier as a direct multiplier (1.0 = 100%,
	// 0.5 = 50%, 2.0 = 200%). Windows uses an offset convention where
	// rate = g_speedModifier + 1.0, but our UI already provides the rate.
	double rate = 1.0;

	if (!g_sim.IsTurboModeEnabled()) {
		rate = (double)s_speedModifier;
		if (s_slowMotion)
			rate *= 0.5;
	}

	rate = std::clamp<double>(rate, 0.01, 100.0);

	IATAudioOutput *audioOutput = g_sim.GetAudioOutput();
	if (audioOutput)
		audioOutput->SetCyclesPerSecond(cyclesPerSecond, 1.0 / rate);

	// Compute frame timing for main loop pacing (matches Windows main.cpp logic)
	double secondsPerFrame = rawSecondsPerFrame / rate;
	double secondTime = VDGetPreciseTicksPerSecond();
	double frameTimeF = secondTime * secondsPerFrame;

	g_frameTicks = VDFloorToInt64(frameTimeF);
	g_frameSubTicks = VDRoundToInt32((frameTimeF - g_frameTicks) * 65536.0);
	g_frameErrorBound = std::max<sint64>(2 * g_frameTicks, VDRoundToInt64(secondTime * 0.1f));
	g_frameTimeout = std::max<sint64>(5 * g_frameTicks, VDGetPreciseTicksPerSecondI());
}
void ATSyncCPUHistoryState() {
	extern ATSimulator g_sim;
	const bool historyEnabled = g_sim.GetCPU().IsHistoryEnabled();

	for (IATDeviceDebugTarget *devtarget : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceDebugTarget>(false, false, false)) {
		uint32 index = 0;

		while (IATDebugTarget *target = devtarget->GetDebugTarget(index++)) {
			auto *thist = vdpoly_cast<IATDebugTargetHistory *>(target);

			if (thist)
				thist->SetHistoryEnabled(historyEnabled);
		}
	}
}

static bool s_appActive = true;
bool ATUIGetAppActive() { return s_appActive; }
void ATUISetAppActive(bool active) { s_appActive = active; }

bool ATUIClipIsTextAvailable() {
	return SDL_HasClipboardText();
}

bool ATUIClipGetText(VDStringA& s8, VDStringW& s16, bool& use16) {
	char *text = SDL_GetClipboardText();
	if (!text || !*text) {
		SDL_free(text);
		return false;
	}
	s16 = VDTextU8ToW(VDStringSpanA(text));
	SDL_free(text);
	use16 = true;
	return true;
}

bool ATUIClipGetText(VDStringW& s) {
	char *text = SDL_GetClipboardText();
	if (!text || !*text) {
		SDL_free(text);
		return false;
	}
	s = VDTextU8ToW(VDStringSpanA(text));
	SDL_free(text);
	return true;
}

void ATUIExecuteCommandStringAndShowErrors(const char *cmd, const ATUICommandOptions *opts) noexcept {
	if (!cmd || !*cmd)
		return;

	extern ATUICommandManager g_ATUICommandMgr;

	ATUICommandOptions defaultOpts;
	g_ATUICommandMgr.ExecuteCommandNT(cmd, opts ? *opts : defaultOpts);
}

///////////////////////////////////////////////////////////////////////////
// 8b. Error dialog queue (thread-safe, consumed by ImGui overlay)
///////////////////////////////////////////////////////////////////////////

static std::mutex s_errorMutex;
static std::vector<std::pair<std::string,std::string>> s_pendingErrors;

std::vector<std::pair<std::string,std::string>> ATImGuiPopPendingErrors() {
	std::lock_guard<std::mutex> lock(s_errorMutex);
	std::vector<std::pair<std::string,std::string>> result;
	result.swap(s_pendingErrors);
	return result;
}

void ATUIShowWarning(VDGUIHandle, const wchar_t *text, const wchar_t *caption) {
	if (text) {
		std::string capStr = caption ? VDTextWToU8(VDStringW(caption)).c_str() : "Warning";
		std::string textStr = VDTextWToU8(VDStringW(text)).c_str();
		std::lock_guard<std::mutex> lock(s_errorMutex);
		s_pendingErrors.emplace_back(std::move(capStr), std::move(textStr));
	}
}

void ATUIShowError2(VDGUIHandle, const wchar_t *text, const wchar_t *title) {
	if (text) {
		std::string capStr = title ? VDTextWToU8(VDStringW(title)).c_str() : "Error";
		std::string textStr = VDTextWToU8(VDStringW(text)).c_str();
		std::lock_guard<std::mutex> lock(s_errorMutex);
		s_pendingErrors.emplace_back(std::move(capStr), std::move(textStr));
	}
}

void ATUIShowError(VDGUIHandle, const wchar_t *text) {
	if (text) {
		std::string textStr = VDTextWToU8(VDStringW(text)).c_str();
		std::lock_guard<std::mutex> lock(s_errorMutex);
		s_pendingErrors.emplace_back("Error", std::move(textStr));
	}
}

void ATUIShowError(VDGUIHandle h, const VDException& e) {
	const wchar_t *msg = e.wc_str();
	if (msg)
		ATUIShowError(h, msg);
}

void ATUIShowError(const VDException& e) {
	ATUIShowError(nullptr, e);
}

void ATUIShowDialogDiskExplorer(VDGUIHandle, IATBlockDevice *dev, const wchar_t *devName) {
	if (!dev)
		return;

	try {
		vdvector<ATPartitionInfo> partitions;
		ATDecodePartitionTable(*dev, partitions);

		if (partitions.empty()) {
			ATImGuiShowToast("No partitions found on block device");
			return;
		}

		vdrefptr<IATDiskImage> diskView(new ATPartitionDiskView(*dev, partitions[0]));
		ATImGuiOpenDiskExplorer(diskView, devName, dev->IsReadOnly());
	} catch (const std::exception& e) {
		char msg[256];
		snprintf(msg, sizeof(msg), "Disk explorer failed: %s", e.what());
		ATImGuiShowToast(msg);
	}
}

bool ATUISwitchHardwareMode(VDGUIHandle, ATHardwareMode mode, bool switchProfiles) {
	extern ATSimulator g_sim;

	ATHardwareMode prevMode = g_sim.GetHardwareMode();
	if (prevMode == mode)
		return true;

	// Map hardware mode to default profile
	ATDefaultProfile defaultProfile;
	switch (mode) {
		case kATHardwareMode_800:
			defaultProfile = kATDefaultProfile_800;
			break;
		case kATHardwareMode_5200:
			defaultProfile = kATDefaultProfile_5200;
			break;
		case kATHardwareMode_XEGS:
			defaultProfile = kATDefaultProfile_XEGS;
			break;
		case kATHardwareMode_1200XL:
			defaultProfile = kATDefaultProfile_1200XL;
			break;
		default:
			defaultProfile = kATDefaultProfile_XL;
			break;
	}

	const uint32 oldProfileId = ATSettingsGetCurrentProfileId();
	const uint32 newProfileId = ATGetDefaultProfileId(defaultProfile);
	const bool switchingProfile = switchProfiles
		&& (newProfileId != kATProfileId_Invalid && newProfileId != oldProfileId);

	const bool switching5200 = (mode == kATHardwareMode_5200 || prevMode == kATHardwareMode_5200);

	// Switch profile if needed (loads all settings for that hardware)
	if (switchingProfile)
		ATSettingsSwitchProfile(newProfileId);

	if (switching5200) {
		g_sim.UnloadAll();

		if (mode == kATHardwareMode_5200) {
			g_sim.LoadCartridge5200Default();
			g_sim.SetMemoryMode(kATMemoryMode_16K);
		}
	}

	g_sim.SetHardwareMode(mode);

	// Check for incompatible kernel
	switch (g_sim.GetKernelMode()) {
		case kATKernelMode_Default:
			break;
		case kATKernelMode_XL:
			if (!kATHardwareModeTraits[mode].mbRunsXLOS)
				g_sim.SetKernel(0);
			break;
		case kATKernelMode_5200:
			if (mode != kATHardwareMode_5200)
				g_sim.SetKernel(0);
			break;
		default:
			if (mode == kATHardwareMode_5200)
				g_sim.SetKernel(0);
			break;
	}

	if (mode == kATHardwareMode_5200 && g_sim.GetVideoStandard() != kATVideoStandard_NTSC) {
		g_sim.SetVideoStandard(kATVideoStandard_NTSC);
		ATUIUpdateSpeedTiming();
	}

	g_sim.ColdReset();
	return true;
}

void ATUISwitchMemoryMode(VDGUIHandle, ATMemoryMode mode) {
	extern ATSimulator g_sim;

	if (g_sim.GetMemoryMode() == mode)
		return;

	switch (g_sim.GetHardwareMode()) {
		case kATHardwareMode_5200:
			if (mode != kATMemoryMode_16K)
				return;
			break;

		case kATHardwareMode_800XL:
			if (mode == kATMemoryMode_48K ||
				mode == kATMemoryMode_52K ||
				mode == kATMemoryMode_8K ||
				mode == kATMemoryMode_24K ||
				mode == kATMemoryMode_32K ||
				mode == kATMemoryMode_40K)
				return;
			break;

		case kATHardwareMode_1200XL:
		case kATHardwareMode_XEGS:
		case kATHardwareMode_130XE:
		case kATHardwareMode_1400XL:
			if (mode == kATMemoryMode_48K ||
				mode == kATMemoryMode_52K ||
				mode == kATMemoryMode_8K ||
				mode == kATMemoryMode_16K ||
				mode == kATMemoryMode_24K ||
				mode == kATMemoryMode_32K ||
				mode == kATMemoryMode_40K)
				return;
			break;
	}

	g_sim.SetMemoryMode(mode);
	g_sim.ColdReset();
}
bool ATUISwitchKernel(VDGUIHandle, uint64 kernelId) {
	extern ATSimulator g_sim;

	if (g_sim.GetKernelId() == kernelId)
		return true;

	ATFirmwareManager& fwm = *g_sim.GetFirmwareManager();

	if (kernelId) {
		ATFirmwareInfo fwinfo;
		if (!fwm.GetFirmwareInfo(kernelId, fwinfo))
			return false;

		const auto hwmode = g_sim.GetHardwareMode();
		const bool canUseXLOS = kATHardwareModeTraits[hwmode].mbRunsXLOS;

		switch (fwinfo.mType) {
			case kATFirmwareType_Kernel1200XL:
				if (!canUseXLOS)
					g_sim.SetHardwareMode(kATHardwareMode_1200XL);
				break;

			case kATFirmwareType_KernelXL:
				if (!canUseXLOS)
					g_sim.SetHardwareMode(kATHardwareMode_800XL);
				break;

			case kATFirmwareType_KernelXEGS:
				if (!canUseXLOS)
					g_sim.SetHardwareMode(kATHardwareMode_XEGS);
				break;

			case kATFirmwareType_Kernel800_OSA:
			case kATFirmwareType_Kernel800_OSB:
				if (hwmode == kATHardwareMode_5200)
					g_sim.SetHardwareMode(kATHardwareMode_800);
				break;

			case kATFirmwareType_Kernel5200:
				if (hwmode != kATHardwareMode_5200)
					g_sim.SetHardwareMode(kATHardwareMode_5200);
				break;

			default:
				break;
		}

		// XL kernels can't run with 48K or less (except 16K = 600XL config)
		switch (fwinfo.mType) {
			case kATFirmwareType_KernelXL:
			case kATFirmwareType_Kernel1200XL:
				switch (g_sim.GetMemoryMode()) {
					case kATMemoryMode_8K:
					case kATMemoryMode_24K:
					case kATMemoryMode_32K:
					case kATMemoryMode_40K:
					case kATMemoryMode_48K:
					case kATMemoryMode_52K:
						g_sim.SetMemoryMode(kATMemoryMode_64K);
						break;
					default:
						break;
				}
				break;
			default:
				break;
		}
	}

	g_sim.SetKernel(kernelId);
	g_sim.ColdReset();
	return true;
}

void ATUITemporarilyMountVHDImageW32(VDGUIHandle, const wchar_t *, bool) {}

void ATRegisterDeviceConfigurers(ATDeviceManager&) {}

///////////////////////////////////////////////////////////////////////////
// 9. (Removed — debugger accessors now provided by debugger.cpp)
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// 10. Win32-to-SIO error translation (PCLink)
///////////////////////////////////////////////////////////////////////////

uint8 ATTranslateWin32ErrorToSIOError(uint32 err) {
	// On Linux, VDWin32Exception stores errno values (not Win32 error codes).
	// Translate errno to Atari CIO status codes for PCLink/host device.
	switch (err) {
		case ENOENT:
			return kATCIOStat_FileNotFound;

		case ENOTDIR:
			return kATCIOStat_PathNotFound;

		case EEXIST:
			return kATCIOStat_FileExists;

		case ENOSPC:
			return kATCIOStat_DiskFull;

		case ENOTEMPTY:
			return kATCIOStat_DirNotEmpty;

		case EACCES:
		case EPERM:
			return kATCIOStat_AccessDenied;

		case EAGAIN:
		case EBUSY:
			return kATCIOStat_FileLocked;

		default:
			return kATCIOStat_SystemError;
	}
}

///////////////////////////////////////////////////////////////////////////
// 11. Factory / creation functions
///////////////////////////////////////////////////////////////////////////

// Custom network engine — provided by customdevice_win32.cpp (platform-agnostic)
// Network socket VXLAN tunnel — provided by vxlantunnel.cpp (platform-agnostic)
// Network socket worker — provided by worker_linux.cpp

// Modem driver — provided by modemtcp_linux.cpp

// Native ETW/WPR tracer (Windows trace infrastructure)
vdrefptr<IVDRefCount> ATCreateNativeTracer(ATTraceContext&, const ATNativeTraceSettings&) {
	return nullptr;
}

// Timer service — provided by ATCore/source/timerserviceimpl_linux.cpp

// UI renderer — captures indicator state for ImGui status bar.
// The Windows build uses a full GDI/Direct3D overlay renderer. On Linux we
// capture indicator data into ATImGuiIndicatorState (emulator_imgui.h) so
// the ImGui status bar can display drive activity, H:, PCLink, IDE, and
// flash write indicators. Non-indicator methods remain no-ops.

#include <emulator_imgui.h>

static ATImGuiIndicatorState s_indicatorState;

ATImGuiIndicatorState& ATImGuiGetIndicatorState() {
	return s_indicatorState;
}

class ATImGuiUIRenderer final : public vdrefcount, public IATUIRenderer {
public:
	int AddRef() override { return vdrefcount::AddRef(); }
	int Release() override { return vdrefcount::Release(); }

	// IATDeviceIndicatorManager — capture state for ImGui rendering
	void SetStatusFlags(uint32 flags) override {
		s_indicatorState.mStatusFlags |= flags;
	}

	void ResetStatusFlags(uint32 flags, uint32 holdTime) override {
		if (!flags)
			return;

		s_indicatorState.mStatusFlags &= ~flags;

		if (holdTime) {
			s_indicatorState.mStatusHoldFlags |= flags;
			for (uint32 f = flags; f; f &= f - 1) {
				int idx = __builtin_ctz(f);
				if (idx < 17)
					s_indicatorState.mStatusHoldCounters[idx] = holdTime;
			}
		}
	}

	void PulseStatusFlags(uint32 flags) override {
		SetStatusFlags(flags);
		ResetStatusFlags(flags, 1);
	}

	void SetStatusCounter(uint32 index, uint32 value) override {
		if (index < 15)
			s_indicatorState.mStatusCounter[index] = value;
	}

	void SetDiskLEDState(uint32 index, sint32 state) override {
		if (index < 15) {
			if (state)
				s_indicatorState.mDiskLEDFlags |= (1u << index);
			else
				s_indicatorState.mDiskLEDFlags &= ~(1u << index);
		}
	}

	void SetDiskMotorActivity(uint32 index, bool on) override {
		if (on)
			s_indicatorState.mDiskMotorFlags |= (1u << index);
		else
			s_indicatorState.mDiskMotorFlags &= ~(1u << index);
	}

	void SetDiskErrorState(uint32 index, bool error) override {
		if (error)
			s_indicatorState.mDiskErrorFlags |= (1u << index);
		else
			s_indicatorState.mDiskErrorFlags &= ~(1u << index);
	}

	void SetHActivity(bool write) override {
		if (write)
			s_indicatorState.mHWriteCounter = 30;
		else
			s_indicatorState.mHReadCounter = 30;
	}

	void SetIDEActivity(bool write, uint32 lba) override {
		if (s_indicatorState.mHardDiskLBA != lba) {
			s_indicatorState.mbHardDiskWrite = false;
			s_indicatorState.mbHardDiskRead = false;
		}
		s_indicatorState.mHardDiskCounter = 3;
		if (write)
			s_indicatorState.mbHardDiskWrite = true;
		else
			s_indicatorState.mbHardDiskRead = true;
		s_indicatorState.mHardDiskLBA = lba;
	}

	void SetPCLinkActivity(bool write) override {
		if (write)
			s_indicatorState.mPCLinkWriteCounter = 30;
		else
			s_indicatorState.mPCLinkReadCounter = 30;
	}

	void SetFlashWriteActivity() override {
		s_indicatorState.mFlashWriteCounter = 20;
	}

	void SetCartridgeActivity(sint32, sint32) override {
		s_indicatorState.mCartridgeActivityCounter = 20;
	}
	// Cassette indicator — status bar reads g_sim.GetCassette() directly.
	void SetCassetteIndicatorVisible(bool) override {}
	void SetCassettePosition(float, float, bool, bool) override {}
	void SetRecordingPosition() override {
		s_indicatorState.mRecordingTime = -1.0f;
		s_indicatorState.mRecordingSize = 0;
		s_indicatorState.mbRecordingPaused = false;
	}
	void SetRecordingPositionPaused() override {
		s_indicatorState.mbRecordingPaused = true;
	}
	void SetRecordingPosition(float t, sint64 sz, bool paused) override {
		s_indicatorState.mRecordingTime = t;
		s_indicatorState.mRecordingSize = sz;
		s_indicatorState.mbRecordingPaused = paused;
	}
	void SetModemConnection(const char *desc) override {
		if (desc) {
			strncpy(s_indicatorState.mModemConnection, desc, sizeof(s_indicatorState.mModemConnection) - 1);
			s_indicatorState.mModemConnection[sizeof(s_indicatorState.mModemConnection) - 1] = 0;
		} else {
			s_indicatorState.mModemConnection[0] = 0;
		}
	}
	void SetStatusMessage(const wchar_t *msg) override {
		if (msg && msg[0]) {
			VDStringA u8 = VDTextWToU8(VDStringW(msg));
			ATImGuiShowToast(u8.c_str());
		}
	}
	uint32 AllocateErrorSourceId() override {
		if (!++mErrorSourceCounter)
			++mErrorSourceCounter;
		return mErrorSourceCounter;
	}
	void ClearErrors(uint32) override {}
	void ReportError(uint32, const wchar_t *msg) override {
		if (msg && msg[0]) {
			VDStringA u8 = VDTextWToU8(VDStringW(msg));
			ATImGuiShowToast(u8.c_str());
		}
	}

	// IATUIRenderer — non-indicator methods

	// Not applicable on Linux — Windows overlay visibility management.
	bool IsVisible() const override { return false; }
	void SetVisible(bool) override {}

	void SetCyclesPerSecond(double rate) override {
		ATImGuiGetIndicatorState().mCyclesPerSecond = rate;
	}
	void SetLedStatus(uint8 mask) override {
		ATImGuiGetIndicatorState().mLedStatus = mask;
	}

	// Group A — Store to indicator state for status bar rendering
	void SetHeldButtonStatus(uint8 consolMask) override {
		ATImGuiGetIndicatorState().mHeldButtonMask = consolMask;
	}
	void SetPendingHoldMode(bool enabled) override {
		ATImGuiGetIndicatorState().mbPendingHoldMode = enabled;
	}
	void SetPendingHeldKey(int key) override {
		ATImGuiGetIndicatorState().mPendingHeldKey = key;
	}
	void SetPendingHeldButtons(uint8 consolMask) override {
		ATImGuiGetIndicatorState().mPendingHeldButtons = consolMask;
	}
	void SetTracingSize(sint64 size) override {
		ATImGuiGetIndicatorState().mTracingSize = size;
	}
	void SetMessage(StatusPriority priority, const wchar_t *msg) override {
		auto& ind = ATImGuiGetIndicatorState();
		int idx = (int)priority;
		if ((unsigned)idx > (unsigned)StatusPriority::Max)
			return;
		if (msg && *msg) {
			VDStringA u8 = VDTextWToU8(VDStringW(msg));
			strncpy(ind.mStatusMessages[idx], u8.c_str(), sizeof(ind.mStatusMessages[idx]) - 1);
			ind.mStatusMessages[idx][sizeof(ind.mStatusMessages[idx]) - 1] = 0;
		} else {
			ind.mStatusMessages[idx][0] = 0;
		}
		if (idx == 0)
			ind.mStatusMessageTimestamp = SDL_GetTicks();
	}
	void ClearMessage(StatusPriority priority) override {
		int idx = (int)priority;
		if ((unsigned)idx <= (unsigned)StatusPriority::Max)
			ATImGuiGetIndicatorState().mStatusMessages[idx][0] = 0;
	}

	// Group B — Watched values
	void SetWatchedValue(int index, uint32 value, WatchFormat format) override {
		if ((unsigned)index >= 8)
			return;
		auto& slot = ATImGuiGetIndicatorState().mWatchSlots[index];
		slot.active = true;
		slot.value = value;
		slot.format = (int)format;
	}
	void ClearWatchedValue(int index) override {
		if ((unsigned)index >= 8)
			return;
		ATImGuiGetIndicatorState().mWatchSlots[index].active = false;
	}

	// Group C — Intentional no-ops with documentation

	// Audio status / SlightSID — audio config dialog queries audioOut->GetAudioStatus() directly.
	void SetAudioStatus(const ATUIAudioStatus *) override {}
	void SetSlightSID(ATSlightSIDEmulator *) override {}

	// Audio monitor/scope — store state for ImGui rendering in emulator_imgui.cpp.
	void SetAudioMonitor(bool secondary, ATAudioMonitor *mon) override {
		ATImGuiGetIndicatorState().mpAudioMonitors[secondary ? 1 : 0] = mon;
	}
	void SetAudioDisplayEnabled(bool secondary, bool enable) override {
		ATImGuiGetIndicatorState().mbAudioDisplayEnabled[secondary ? 1 : 0] = enable;
	}
	void SetAudioScopeEnabled(bool enable) override {
		ATImGuiGetIndicatorState().mbAudioScopeEnabled = enable;
	}

	// Pad input — callers in uivideodisplaywindow.cpp are excluded from the Linux build.
	vdrect32 GetPadArea() const override { return vdrect32(0, 0, 0, 0); }
	void SetPadInputEnabled(bool) override {}

	// FPS — status bar calculates FPS independently; caller (main.cpp) excluded.
	void SetFpsIndicator(float) override {}

	// Hover tip — callers excluded from Linux build.
	void SetHoverTip(int, int, const wchar_t *) override {}

	// Paused state — status bar checks g_sim.IsPaused() directly.
	void SetPaused(bool) override {}

	// Windows UI management — not applicable on Linux.
	void SetUIManager(ATUIManager *) override {}
	void Relayout(int, int) override {}
	void Update() override {}
	sint32 GetIndicatorSafeHeight() const override { return 0; }
	void AddIndicatorSafeHeightChangedHandler(const vdfunction<void()> *) override {}
	void RemoveIndicatorSafeHeightChangedHandler(const vdfunction<void()> *) override {}
	void BeginCustomization() override {}

private:
	uint32 mErrorSourceCounter = 0;
};

void ATCreateUIRenderer(IATUIRenderer **r) {
	auto *p = new ATImGuiUIRenderer;
	p->AddRef();
	*r = p;
}

///////////////////////////////////////////////////////////////////////////
// 12. (Removed — ATEnumLookupTable specializations now provided by debugger.cpp)
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// 13. ATDirectoryWatcher — Linux inotify implementation
//     Uses inotify for filesystem change notification with recursive
//     subdirectory watching.  Falls back to polling if inotify_init fails.
//     Member reuse: mhDir = inotify fd (intptr_t cast, -1 = invalid),
//     mhExitEvent = eventfd for thread exit signaling.
///////////////////////////////////////////////////////////////////////////

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <vd2/system/binary.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <at/atcore/checksum.h>
#include <map>

// Watch descriptor → relative path mapping (thread-local to watcher thread)
static thread_local std::map<int, VDStringW> s_wdToRelPath;

bool ATDirectoryWatcher::sbShouldUsePolling = false;

ATDirectoryWatcher::ATDirectoryWatcher()
	: VDThread("Altirra directory watcher")
	, mhDir(reinterpret_cast<void *>(static_cast<intptr_t>(-1)))
	, mhExitEvent(reinterpret_cast<void *>(static_cast<intptr_t>(-1)))
	, mhDirChangeEvent(nullptr)
	, mpChangeBuffer(nullptr)
	, mChangeBufferSize(0)
	, mbRecursive(false)
	, mbAllChanged(false)
{
}

ATDirectoryWatcher::~ATDirectoryWatcher() {
	Shutdown();
}

void ATDirectoryWatcher::Init(const wchar_t *basePath, bool recursive) {
	Shutdown();

	mBasePath = VDGetLongPath(basePath);
	mbRecursive = recursive;

	int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (efd < 0)
		return;

	mhExitEvent = reinterpret_cast<void *>(static_cast<intptr_t>(efd));

	ThreadStart();
}

void ATDirectoryWatcher::Shutdown() {
	if (isThreadAttached()) {
		int efd = static_cast<int>(reinterpret_cast<intptr_t>(mhExitEvent));
		if (efd >= 0) {
			uint64_t val = 1;
			[[maybe_unused]] auto r = ::write(efd, &val, sizeof(val));
		}
		ThreadWait();
	}

	// Close inotify fd
	int ifd = static_cast<int>(reinterpret_cast<intptr_t>(mhDir));
	if (ifd >= 0) {
		::close(ifd);
		mhDir = reinterpret_cast<void *>(static_cast<intptr_t>(-1));
	}

	int efd = static_cast<int>(reinterpret_cast<intptr_t>(mhExitEvent));
	if (efd >= 0) {
		::close(efd);
		mhExitEvent = reinterpret_cast<void *>(static_cast<intptr_t>(-1));
	}
}

bool ATDirectoryWatcher::CheckForChanges() {
	bool changed = false;

	vdsynchronized(mMutex) {
		changed = mbAllChanged;

		if (changed) {
			mbAllChanged = false;
		} else if (!mChangedDirs.empty()) {
			mChangedDirs.clear();
			changed = true;
		}
	}

	return changed;
}

bool ATDirectoryWatcher::CheckForChanges(vdfastvector<wchar_t>& strheap) {
	bool allChanged = false;
	strheap.clear();

	vdsynchronized(mMutex) {
		allChanged = mbAllChanged;

		if (allChanged) {
			mbAllChanged = false;
		} else {
			for (const auto& s : mChangedDirs) {
				const wchar_t *t = s.c_str();
				strheap.insert(strheap.end(), t, t + s.size() + 1);
			}
		}

		mChangedDirs.clear();
	}

	return allChanged;
}

// Add an inotify watch for a directory and record its relative path.
// Returns the watch descriptor, or -1 on failure.
static int AddInotifyWatch(int ifd, const char *absPath, const VDStringW& relPath) {
	int wd = inotify_add_watch(ifd, absPath,
		IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO
		| IN_ATTRIB | IN_DONT_FOLLOW);

	if (wd >= 0)
		s_wdToRelPath[wd] = relPath;

	return wd;
}

// Recursively add inotify watches for a directory tree.
static void AddWatchesRecursive(int ifd, const VDStringA& absDir, const VDStringW& relDir, int depth) {
	if (depth > 8)
		return;

	AddInotifyWatch(ifd, absDir.c_str(), relDir);

	DIR *d = opendir(absDir.c_str());
	if (!d)
		return;

	struct dirent *ent;
	while ((ent = readdir(d)) != nullptr) {
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
			(ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;

		if (ent->d_type != DT_DIR)
			continue;

		VDStringA childAbs(absDir);
		if (!childAbs.empty() && childAbs.back() != '/')
			childAbs += '/';
		childAbs += ent->d_name;

		VDStringW childRel(relDir);
		if (!childRel.empty())
			childRel += L'/';
		childRel += VDTextU8ToW(VDStringA(ent->d_name));

		AddWatchesRecursive(ifd, childAbs, childRel, depth + 1);
	}

	closedir(d);
}

void ATDirectoryWatcher::ThreadRun() {
	if (sbShouldUsePolling) {
		RunPollThread();
	} else {
		RunNotifyThread();
	}
}

void ATDirectoryWatcher::RunPollThread() {
	int efd = static_cast<int>(reinterpret_cast<intptr_t>(mhExitEvent));
	uint32 delay = 1000;
	uint32 lastChecksum[8] {};
	bool firstPoll = true;

	for (;;) {
		uint32 newChecksum[8] {};
		PollDirectory(newChecksum, mBasePath, 0);

		if (memcmp(newChecksum, lastChecksum, sizeof newChecksum) || firstPoll) {
			memcpy(lastChecksum, newChecksum, sizeof lastChecksum);

			if (firstPoll)
				firstPoll = false;
			else
				NotifyAllChanged();
		}

		struct pollfd pfd {};
		pfd.fd = efd;
		pfd.events = POLLIN;

		int ret = ::poll(&pfd, 1, delay);
		if (ret > 0)
			break;
	}
}

void ATDirectoryWatcher::PollDirectory(uint32 *orderIndependentChecksum, const VDStringSpanW& path, uint32 nestingLevel) {
	ATChecksumEngineSHA256 checksumEngine;

	VDDirectoryIterator it(VDMakePath(path, VDStringSpanW(L"*")).c_str());
	while (it.Next()) {
		const VDStringW& fullItemPath = it.GetFullPath();

		checksumEngine.Reset();
		checksumEngine.Process(fullItemPath.data(), fullItemPath.size() * sizeof(fullItemPath[0]));

		const struct MiscData {
			sint64 mSize;
			uint64 mCreationDate;
			uint64 mLastWriteDate;
			uint32 mAttributes;
			uint32 mPad;
		} miscData = {
			it.GetSize(),
			it.GetCreationDate().mTicks,
			it.GetLastWriteDate().mTicks,
			it.GetAttributes()
		};

		checksumEngine.Process(&miscData, sizeof miscData);
		const auto& checksum = checksumEngine.Finalize();

		uint32 c = 0;
		for (uint32 i = 0; i < 8; ++i) {
			uint32 x = orderIndependentChecksum[i];
			uint32 y = VDReadUnalignedU32(&checksum.mDigest[i * 4]);
			uint64 sum = (uint64)x + y + c;

			orderIndependentChecksum[i] = (uint32)sum;
			c = (uint32)(sum >> 32);
		}

		if (it.IsDirectory() && !it.IsLink() && nestingLevel < 8 && mbRecursive)
			PollDirectory(orderIndependentChecksum, fullItemPath, nestingLevel + 1);
	}
}

void ATDirectoryWatcher::RunNotifyThread() {
	int efd = static_cast<int>(reinterpret_cast<intptr_t>(mhExitEvent));

	int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ifd < 0) {
		// inotify unavailable — fall back to polling
		RunPollThread();
		return;
	}

	mhDir = reinterpret_cast<void *>(static_cast<intptr_t>(ifd));
	s_wdToRelPath.clear();

	// Set up initial watches
	VDStringA u8base = VDTextWToU8(mBasePath);
	if (mbRecursive) {
		AddWatchesRecursive(ifd, u8base, VDStringW(), 0);
	} else {
		AddInotifyWatch(ifd, u8base.c_str(), VDStringW());
	}

	// Event read buffer
	char buf[8192] __attribute__((aligned(__alignof__(struct inotify_event))));

	for (;;) {
		struct pollfd pfds[2] {};
		pfds[0].fd = efd;
		pfds[0].events = POLLIN;
		pfds[1].fd = ifd;
		pfds[1].events = POLLIN;

		int ret = ::poll(pfds, 2, -1);
		if (ret < 0)
			continue;

		// Exit signal
		if (pfds[0].revents & POLLIN)
			break;

		// inotify events
		if (pfds[1].revents & POLLIN) {
			for (;;) {
				ssize_t len = ::read(ifd, buf, sizeof(buf));
				if (len <= 0)
					break;

				const char *ptr = buf;
				while (ptr < buf + len) {
					const struct inotify_event *ev =
						reinterpret_cast<const struct inotify_event *>(ptr);

					if (ev->mask & IN_Q_OVERFLOW) {
						// Kernel event queue overflow — report everything changed
						NotifyAllChanged();
					} else if (ev->len > 0) {
						// Find the relative directory path for this watch
						auto it = s_wdToRelPath.find(ev->wd);
						VDStringW relDir = (it != s_wdToRelPath.end()) ? it->second : VDStringW();

						// Build relative path of changed item
						VDStringW relPath(relDir);
						if (!relPath.empty())
							relPath += L'/';
						relPath += VDTextU8ToW(VDStringA(ev->name));

						// If a new subdirectory was created, add a watch for it
						if (mbRecursive && (ev->mask & (IN_CREATE | IN_MOVED_TO)) && (ev->mask & IN_ISDIR)) {
							VDStringA absChild = VDTextWToU8(
								VDMakePath(VDStringSpanW(mBasePath), VDStringSpanW(relPath)));
							AddWatchesRecursive(ifd, absChild, relPath, 0);
						}

						// Record the containing directory as changed
						vdsynchronized(mMutex) {
							mChangedDirs.insert(relDir.empty() ? VDStringW(L".") : relDir);
						}
					}

					ptr += sizeof(struct inotify_event) + ev->len;
				}
			}
		}
	}

	// Cleanup: close inotify fd (auto-removes all watches)
	s_wdToRelPath.clear();
	::close(ifd);
	mhDir = reinterpret_cast<void *>(static_cast<intptr_t>(-1));
}

void ATDirectoryWatcher::NotifyAllChanged() {
	vdsynchronized(mMutex) {
		mbAllChanged = true;
	}
}

///////////////////////////////////////////////////////////////////////////
// 14. ATIDEPhysicalDisk (Windows physical disk I/O)
//     This class inherits from IATBlockDevice and ATDevice, so we must
//     provide all pure virtual method implementations for vtable.
///////////////////////////////////////////////////////////////////////////

bool ATIDEIsPhysicalDiskPath(const wchar_t *) {
	return false;
}

ATIDEPhysicalDisk::ATIDEPhysicalDisk()
	: mhDisk(nullptr)
	, mpBuffer(nullptr)
	, mSectorCount(0)
{
}

ATIDEPhysicalDisk::~ATIDEPhysicalDisk() {
}

int ATIDEPhysicalDisk::AddRef() {
	return ATDevice::AddRef();
}

int ATIDEPhysicalDisk::Release() {
	return ATDevice::Release();
}

void *ATIDEPhysicalDisk::AsInterface(uint32 iid) {
	return ATDevice::AsInterface(iid);
}

void ATIDEPhysicalDisk::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefIDEPhysDisk;
}

void ATIDEPhysicalDisk::GetSettings(ATPropertySet&) {
}

bool ATIDEPhysicalDisk::SetSettings(const ATPropertySet&) {
	return false;
}

void ATIDEPhysicalDisk::Shutdown() {
}

ATBlockDeviceGeometry ATIDEPhysicalDisk::GetGeometry() const {
	return ATBlockDeviceGeometry {};
}

uint32 ATIDEPhysicalDisk::GetSerialNumber() const {
	return 0;
}

void ATIDEPhysicalDisk::Init(const wchar_t *) {
	// Physical disk access not supported on Linux yet
}

void ATIDEPhysicalDisk::Flush() {
}

void ATIDEPhysicalDisk::ReadSectors(void *, uint32, uint32) {
}

void ATIDEPhysicalDisk::WriteSectors(const void *, uint32, uint32) {
}

///////////////////////////////////////////////////////////////////////////
// 15. VDFileWatcher — Linux inotify implementation
//     mChangeHandle stores inotify fd (via intptr_t cast, -1 = inactive).
//     mTimerId stores the inotify watch descriptor.
///////////////////////////////////////////////////////////////////////////

#include <sys/inotify.h>

VDFileWatcher::VDFileWatcher()
	: mChangeHandle(reinterpret_cast<void *>(static_cast<intptr_t>(-1)))
	, mLastWriteTime(0)
	, mbWatchDir(false)
	, mpCB(nullptr)
	, mbRepeatRequested(false)
	, mbThunksInited(false)
	, mpThunk(nullptr)
	, mTimerId(0)
{
}

VDFileWatcher::~VDFileWatcher() {
	Shutdown();
}

bool VDFileWatcher::IsActive() const {
	return reinterpret_cast<intptr_t>(mChangeHandle) >= 0;
}

void VDFileWatcher::Init(const wchar_t *file, IVDFileWatcherCallback *callback) {
	Shutdown();

	const wchar_t *pathEnd = VDFileSplitPath(file);
	VDStringW basePath(file, pathEnd);
	if (basePath.empty())
		basePath = L".";

	int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ifd < 0)
		return;

	VDStringA u8path = VDTextWToU8(basePath);
	int wd = inotify_add_watch(ifd, u8path.c_str(),
		IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
	if (wd < 0) {
		::close(ifd);
		return;
	}

	mChangeHandle = reinterpret_cast<void *>(static_cast<intptr_t>(ifd));
	mTimerId = static_cast<uint32>(wd);
	mPath = file;
	mLastWriteTime = VDFileGetLastWriteTime(mPath.c_str());
	mpCB = callback;
	mbRepeatRequested = false;
	mbWatchDir = false;
}

void VDFileWatcher::InitDir(const wchar_t *path, bool subdirs, IVDFileWatcherCallback *callback) {
	Shutdown();

	int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ifd < 0)
		return;

	VDStringA u8path = VDTextWToU8(VDStringW(path));
	int wd = inotify_add_watch(ifd, u8path.c_str(),
		IN_MODIFY | IN_CREATE | IN_DELETE | IN_ATTRIB |
		IN_MOVED_FROM | IN_MOVED_TO);
	if (wd < 0) {
		::close(ifd);
		return;
	}

	mChangeHandle = reinterpret_cast<void *>(static_cast<intptr_t>(ifd));
	mTimerId = static_cast<uint32>(wd);
	mPath = path;
	mpCB = callback;
	mbRepeatRequested = false;
	mbWatchDir = true;
}

void VDFileWatcher::Shutdown() {
	int ifd = static_cast<int>(reinterpret_cast<intptr_t>(mChangeHandle));
	if (ifd >= 0) {
		::close(ifd);  // closing the fd also removes all watches
		mChangeHandle = reinterpret_cast<void *>(static_cast<intptr_t>(-1));
		mTimerId = 0;
	}
}

bool VDFileWatcher::Wait(uint32 delay) {
	int ifd = static_cast<int>(reinterpret_cast<intptr_t>(mChangeHandle));
	if (ifd < 0)
		return false;

	// Poll the inotify fd with the given timeout
	struct pollfd pfd {};
	pfd.fd = ifd;
	pfd.events = POLLIN;

	int ret = ::poll(&pfd, 1, delay == 0xFFFFFFFFU ? -1 : (int)delay);
	if (ret <= 0)
		return false;

	// Drain inotify events
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	while (::read(ifd, buf, sizeof(buf)) > 0) {}

	if (!mbWatchDir) {
		uint64 t = VDFileGetLastWriteTime(mPath.c_str());
		if (mLastWriteTime == t)
			return false;
		mLastWriteTime = t;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////
// 16. VDVideoDisplayFrame (Windows display pipeline)
///////////////////////////////////////////////////////////////////////////

VDVideoDisplayFrame::VDVideoDisplayFrame()
	: mRefCount(0)
{
}

VDVideoDisplayFrame::~VDVideoDisplayFrame() {
}

int VDVideoDisplayFrame::AddRef() {
	return mRefCount.operator++();
}

int VDVideoDisplayFrame::Release() {
	int rc = mRefCount.operator--();
	if (rc == 0)
		delete this;
	return rc;
}

///////////////////////////////////////////////////////////////////////////
// 17. VDDisplay bloom settings
///////////////////////////////////////////////////////////////////////////

void VDDSetBloomV2Settings(const VDDBloomV2Settings&) {
}

///////////////////////////////////////////////////////////////////////////
// 18. VDCreateFileAsync (Windows async file I/O)
///////////////////////////////////////////////////////////////////////////

// Synchronous implementation using POSIX file I/O
namespace {
	class VDFileAsyncLinux final : public IVDFileAsync {
	public:
		~VDFileAsyncLinux() override { Close(); }

		void SetPreemptiveExtend(bool b) override { mbPreemptiveExtend = b; }
		bool IsPreemptiveExtendActive() override { return mbPreemptiveExtend; }
		bool IsOpen() override { return mFD >= 0; }

		void Open(const wchar_t *path, uint32, uint32) override {
			Close();
			VDStringA u8path = VDTextWToU8(VDStringW(path));
			mFD = ::open(u8path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (mFD < 0)
				throw MyWin32Error("Cannot open file: %%s", errno);
			mbOwned = true;
			mFastWritePos = 0;
		}

		void Open(VDFileHandle h, uint32, uint32) override {
			Close();
			mFD = h;
			mbOwned = false;
			mFastWritePos = ::lseek(mFD, 0, SEEK_CUR);
			if (mFastWritePos < 0)
				mFastWritePos = 0;
		}

		void Close() override {
			if (mFD >= 0 && mbOwned)
				::close(mFD);
			mFD = -1;
			mbOwned = false;
			mFastWritePos = 0;
		}

		void FastWrite(const void *data, uint32 bytes) override {
			if (mFD < 0 || bytes == 0) return;
			// Handle NULL data as zero-padding (used for AVI chunk alignment)
			const uint8 zeroBuf[8] = {0};
			const uint8 *p = data ? (const uint8 *)data : zeroBuf;
			uint32 remaining = bytes;
			while (remaining > 0) {
				uint32 toWrite = remaining;
				if (!data && toWrite > sizeof(zeroBuf))
					toWrite = sizeof(zeroBuf);
				ssize_t written = ::write(mFD, p, toWrite);
				if (written < 0) {
					if (errno == EINTR) continue;
					throw MyWin32Error("Write error: %%s", errno);
				}
				if (data) p += written;
				remaining -= (uint32)written;
			}
			mFastWritePos += bytes;
		}

		void FastWriteEnd() override {}

		void Write(sint64 pos, const void *data, uint32 bytes) override {
			if (mFD < 0) return;
			const uint8 *p = (const uint8 *)data;
			uint32 remaining = bytes;
			sint64 offset = pos;
			while (remaining > 0) {
				ssize_t written = ::pwrite(mFD, p, remaining, offset);
				if (written < 0) {
					if (errno == EINTR) continue;
					throw MyWin32Error("Write error: %%s", errno);
				}
				p += written;
				remaining -= (uint32)written;
				offset += written;
			}
		}

		bool Extend(sint64 pos) override {
			if (mFD < 0) return false;
			return ::ftruncate(mFD, pos) == 0;
		}

		void Truncate(sint64 pos) override {
			if (mFD < 0) return;
			::ftruncate(mFD, pos);
		}

		void SafeTruncateAndClose(sint64 pos) override {
			if (mFD >= 0) {
				::ftruncate(mFD, pos);
				if (mbOwned)
					::close(mFD);
				mFD = -1;
				mbOwned = false;
			}
		}

		sint64 GetFastWritePos() override { return mFastWritePos; }

		sint64 GetSize() override {
			if (mFD < 0) return 0;
			struct stat st;
			if (::fstat(mFD, &st) < 0) return 0;
			return st.st_size;
		}

	private:
		int mFD = -1;
		bool mbOwned = false;
		bool mbPreemptiveExtend = false;
		sint64 mFastWritePos = 0;
	};
}

IVDFileAsync *VDCreateFileAsync(IVDFileAsync::Mode) {
	return new VDFileAsyncLinux;
}

///////////////////////////////////////////////////////////////////////////
// 19. VDUIGetAcceleratorString (Windows virtual key name lookup)
///////////////////////////////////////////////////////////////////////////

static const wchar_t *VDUIGetVKKeyName(uint32 vk) {
	// Map Windows virtual key codes to human-readable names.
	// Letters and digits handled separately; this covers special keys.
	switch (vk) {
		case 0x08: return L"Backspace";
		case 0x09: return L"Tab";
		case 0x0D: return L"Enter";
		case 0x10: return L"Shift";
		case 0x11: return L"Ctrl";
		case 0x12: return L"Alt";
		case 0x13: return L"Pause";
		case 0x14: return L"Caps Lock";
		case 0x1B: return L"Esc";
		case 0x20: return L"Space";
		case 0x21: return L"Page Up";
		case 0x22: return L"Page Down";
		case 0x23: return L"End";
		case 0x24: return L"Home";
		case 0x25: return L"Left";
		case 0x26: return L"Up";
		case 0x27: return L"Right";
		case 0x28: return L"Down";
		case 0x2C: return L"Print Screen";
		case 0x2D: return L"Insert";
		case 0x2E: return L"Delete";
		case 0x5B: return L"Left Win";
		case 0x5C: return L"Right Win";
		case 0x5D: return L"Apps";
		case 0x60: return L"Num 0";
		case 0x61: return L"Num 1";
		case 0x62: return L"Num 2";
		case 0x63: return L"Num 3";
		case 0x64: return L"Num 4";
		case 0x65: return L"Num 5";
		case 0x66: return L"Num 6";
		case 0x67: return L"Num 7";
		case 0x68: return L"Num 8";
		case 0x69: return L"Num 9";
		case 0x6A: return L"Num *";
		case 0x6B: return L"Num +";
		case 0x6D: return L"Num -";
		case 0x6E: return L"Num .";
		case 0x6F: return L"Num /";
		case 0x70: return L"F1";
		case 0x71: return L"F2";
		case 0x72: return L"F3";
		case 0x73: return L"F4";
		case 0x74: return L"F5";
		case 0x75: return L"F6";
		case 0x76: return L"F7";
		case 0x77: return L"F8";
		case 0x78: return L"F9";
		case 0x79: return L"F10";
		case 0x7A: return L"F11";
		case 0x7B: return L"F12";
		case 0x90: return L"Num Lock";
		case 0x91: return L"Scroll Lock";
		case 0xA0: return L"Left Shift";
		case 0xA1: return L"Right Shift";
		case 0xA2: return L"Left Ctrl";
		case 0xA3: return L"Right Ctrl";
		case 0xBA: return L";";
		case 0xBB: return L"=";
		case 0xBC: return L",";
		case 0xBD: return L"-";
		case 0xBE: return L".";
		case 0xBF: return L"/";
		case 0xC0: return L"`";
		case 0xDB: return L"[";
		case 0xDC: return L"\\";
		case 0xDD: return L"]";
		case 0xDE: return L"'";
		default:   return nullptr;
	}
}

void VDUIGetAcceleratorString(const VDUIAccelerator& accel, VDStringW& s) {
	s.clear();

	if (accel.mModifiers & VDUIAccelerator::kModUp)
		s = L"^";

	if (accel.mModifiers & VDUIAccelerator::kModCooked) {
		s += L"\"";
		const wchar_t c = (wchar_t)accel.mVirtKey;
		const wchar_t *name = VDUIGetVKKeyName(accel.mVirtKey);
		if (name)
			s += name;
		else
			s += c;
		s += L"\"";
	} else {
		if (accel.mModifiers & VDUIAccelerator::kModCtrl)
			s += L"Ctrl+";

		if (accel.mModifiers & VDUIAccelerator::kModAlt)
			s += L"Alt+";

		if (accel.mModifiers & VDUIAccelerator::kModShift)
			s += L"Shift+";

		// Letters A-Z
		if (accel.mVirtKey >= 0x41 && accel.mVirtKey <= 0x5A) {
			s += (wchar_t)accel.mVirtKey;
		}
		// Digits 0-9
		else if (accel.mVirtKey >= 0x30 && accel.mVirtKey <= 0x39) {
			s += (wchar_t)accel.mVirtKey;
		}
		// Named keys
		else {
			const wchar_t *name = VDUIGetVKKeyName(accel.mVirtKey);
			if (name)
				s += name;
			else {
				wchar_t buf[16];
				swprintf(buf, sizeof(buf)/sizeof(buf[0]), L"Key 0x%02X", accel.mVirtKey);
				s += buf;
			}
		}
	}
}
