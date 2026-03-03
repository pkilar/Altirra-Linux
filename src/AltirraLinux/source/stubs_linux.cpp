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
#include <at/atnativeui/genericdialog.h>

#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include "dialogs_wx.h"

#include <vd2/Dita/services.h>
#include <vd2/system/filesys.h>
#include "uifilefilters.h"
#include "cartridge.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "disk.h"
#include "diskinterface.h"
#include "cassette.h"
#include "autosavemanager.h"
#include <at/atio/cartridgeimage.h>
#include <at/atio/image.h>

///////////////////////////////////////////////////////////////////////////
// 0. Generic dialog + main window (needed by browser.cpp and others)
///////////////////////////////////////////////////////////////////////////

VDGUIHandle ATUIGetMainWindow() {
	return nullptr;
}

static VDStringW s_defaultGenericDialogCaption(L"Altirra");

void ATUISetDefaultGenericDialogCaption(const wchar_t *s) {
	s_defaultGenericDialogCaption = s ? s : L"Altirra";
}

void ATUIGenericDialogUndoAllIgnores() {
}

ATUIGenericResult ATUIShowGenericDialog(const ATUIGenericDialogOptions& opts) {
	VDStringA msg = VDTextWToU8(VDStringW(opts.mpMessage ? opts.mpMessage : L""));
	VDStringA title = VDTextWToU8(VDStringW(opts.mpTitle ? opts.mpTitle : s_defaultGenericDialogCaption.c_str()));

	long style = 0;

	if (opts.mResultMask & kATUIGenericResultMask_AllowDeny)
		style = wxYES_NO;
	else if (opts.mResultMask & (kATUIGenericResultMask_Yes | kATUIGenericResultMask_No))
		style = wxYES_NO;
	else if (opts.mResultMask & kATUIGenericResultMask_OKCancel)
		style = wxOK | wxCANCEL;
	else
		style = wxOK;

	switch (opts.mIconType) {
		case kATUIGenericIconType_Info:    style |= wxICON_INFORMATION; break;
		case kATUIGenericIconType_Warning: style |= wxICON_WARNING; break;
		case kATUIGenericIconType_Error:   style |= wxICON_ERROR; break;
		default: break;
	}

	int result = wxMessageBox(
		wxString::FromUTF8(msg.c_str()),
		wxString::FromUTF8(title.c_str()),
		style);

	if (opts.mResultMask & kATUIGenericResultMask_AllowDeny)
		return (result == wxYES) ? kATUIGenericResult_Allow : kATUIGenericResult_Deny;
	else if (opts.mResultMask & (kATUIGenericResultMask_Yes | kATUIGenericResultMask_No))
		return (result == wxYES) ? kATUIGenericResult_Yes : kATUIGenericResult_No;
	else if (opts.mResultMask & kATUIGenericResultMask_OKCancel)
		return (result == wxOK) ? kATUIGenericResult_OK : kATUIGenericResult_Cancel;
	else
		return kATUIGenericResult_OK;
}

ATUIGenericResult ATUIShowGenericDialogAutoCenter(const ATUIGenericDialogOptions& opts) {
	return ATUIShowGenericDialog(opts);
}

bool ATUIConfirm(VDGUIHandle, const char *, const wchar_t *message, const wchar_t *title) {
	VDStringA msg = VDTextWToU8(VDStringW(message ? message : L""));
	VDStringA cap = VDTextWToU8(VDStringW(title ? title : L"Altirra"));

	int result = wxMessageBox(
		wxString::FromUTF8(msg.c_str()),
		wxString::FromUTF8(cap.c_str()),
		wxYES_NO | wxICON_QUESTION);

	return result == wxYES;
}

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
// NOTE: g_ATDeviceDefBrowser is defined in browser.cpp (now compiled on Linux)

void ATCreateDeviceIDEPhysDisk(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATIDEPhysicalDisk> p(new ATIDEPhysicalDisk);
	*dev = p;
	(*dev)->AddRef();
}

extern const ATDeviceDefinition g_ATDeviceDefIDEPhysDisk = {
	"hdphysdisk", "harddisk", L"Hard disk image (physical disk)", ATCreateDeviceIDEPhysDisk, 0
};

// NOTE: g_ATDeviceDefMidiMate is defined in midimate_linux.cpp (ALSA implementation)
// NOTE: g_ATDeviceDefPipeSerial is defined in pipeserial_linux.cpp (PTY implementation)

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

// ATUIQueue — now provided by uiqueue.cpp (un-excluded from build)

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
	extern void ATLinuxSetDisplayFilterMode(IVDVideoDisplay::FilterMode);
	IVDVideoDisplay::FilterMode fm =
		(m == kATDisplayFilterMode_Point)
			? IVDVideoDisplay::kFilterPoint
			: IVDVideoDisplay::kFilterBilinear;
	ATLinuxSetDisplayFilterMode(fm);
}
void ATUISetDisplayIndicators(bool v) { s_displayIndicators = v; }
void ATUISetDisplayPadIndicators(bool v) { s_displayPadIndicators = v; }
void ATUISetDisplayPanOffset(const vdfloat2& v) { s_displayPanOffset = v; }
void ATUISetDisplayStretchMode(ATDisplayStretchMode m) {
	s_displayStretchMode = m;
	extern void ATLinuxSetDisplayStretchMode(ATDisplayStretchMode);
	ATLinuxSetDisplayStretchMode(m);
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
	extern void ATLinuxGetDisplayWindowSize(int&, int&);

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
			int w = 0, h = 0;
			ATLinuxGetDisplayWindowSize(w, h);
			if (w > 0 && h > 0)
				g_pEnhancedTextEngine->OnSize(w, h);
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
		ATShowDiskExplorerForImage(nullptr, diskView, devName, !dev->IsReadOnly());
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
// 14. ATIDEPhysicalDisk (Linux physical disk I/O via /dev/sdX)
//     Uses open() + pread()/pwrite() with O_DIRECT for raw disk access.
//     Requires root or membership in the 'disk' group.
///////////////////////////////////////////////////////////////////////////

#include <sys/ioctl.h>
#include <linux/fs.h>

bool ATIDEIsPhysicalDiskPath(const wchar_t *path) {
	if (!path)
		return false;
	VDStringA u8 = VDTextWToU8(VDStringW(path));
	// Linux block device paths: /dev/sd*, /dev/hd*, /dev/nvme*, /dev/loop*
	return strncmp(u8.c_str(), "/dev/sd", 7) == 0
		|| strncmp(u8.c_str(), "/dev/hd", 7) == 0
		|| strncmp(u8.c_str(), "/dev/nvme", 9) == 0
		|| strncmp(u8.c_str(), "/dev/loop", 9) == 0;
}

sint64 ATIDEGetPhysicalDiskSize(const wchar_t *path) {
	VDStringA u8 = VDTextWToU8(VDStringW(path));
	int fd = ::open(u8.c_str(), O_RDONLY);
	if (fd < 0)
		return -1;

	uint64_t size = 0;
	if (ioctl(fd, BLKGETSIZE64, &size) < 0) {
		::close(fd);
		return -1;
	}
	::close(fd);
	return (sint64)size;
}

ATIDEPhysicalDisk::ATIDEPhysicalDisk()
	: mhDisk(reinterpret_cast<void *>(static_cast<intptr_t>(-1)))
	, mpBuffer(nullptr)
	, mSectorCount(0)
{
}

ATIDEPhysicalDisk::~ATIDEPhysicalDisk() {
	Shutdown();
}

int ATIDEPhysicalDisk::AddRef() {
	return ATDevice::AddRef();
}

int ATIDEPhysicalDisk::Release() {
	return ATDevice::Release();
}

void *ATIDEPhysicalDisk::AsInterface(uint32 iid) {
	if (iid == IATBlockDevice::kTypeID)
		return static_cast<IATBlockDevice *>(this);
	return ATDevice::AsInterface(iid);
}

void ATIDEPhysicalDisk::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefIDEPhysDisk;
}

void ATIDEPhysicalDisk::GetSettings(ATPropertySet& settings) {
	if (!mPath.empty())
		settings.SetString("path", mPath.c_str());
}

bool ATIDEPhysicalDisk::SetSettings(const ATPropertySet& settings) {
	const wchar_t *path = settings.GetString("path");
	if (path)
		mPath = path;
	return true;
}

void ATIDEPhysicalDisk::Shutdown() {
	int fd = static_cast<int>(reinterpret_cast<intptr_t>(mhDisk));
	if (fd >= 0) {
		::close(fd);
		mhDisk = reinterpret_cast<void *>(static_cast<intptr_t>(-1));
	}
	if (mpBuffer) {
		free(mpBuffer);
		mpBuffer = nullptr;
	}
	mSectorCount = 0;
}

ATBlockDeviceGeometry ATIDEPhysicalDisk::GetGeometry() const {
	return ATBlockDeviceGeometry {};
}

uint32 ATIDEPhysicalDisk::GetSerialNumber() const {
	if (mPath.empty())
		return 0;
	// Simple hash of path string for device identification
	uint32 hash = 2166136261u;
	for (auto ch : mPath) {
		hash ^= (uint32)ch;
		hash *= 16777619u;
	}
	return hash;
}

void ATIDEPhysicalDisk::Init(const wchar_t *path) {
	Shutdown();

	if (!path || !*path)
		return;

	mPath = path;
	VDStringA u8 = VDTextWToU8(VDStringW(path));

	int fd = ::open(u8.c_str(), O_RDONLY);
	if (fd < 0)
		return;

	// Get disk size via ioctl
	uint64_t diskSize = 0;
	if (ioctl(fd, BLKGETSIZE64, &diskSize) < 0) {
		::close(fd);
		return;
	}

	mSectorCount = (uint32)(diskSize / 512);
	mhDisk = reinterpret_cast<void *>(static_cast<intptr_t>(fd));

	// Allocate a 16KB read buffer (32 sectors)
	mpBuffer = malloc(512 * 32);
}

void ATIDEPhysicalDisk::Flush() {
	int fd = static_cast<int>(reinterpret_cast<intptr_t>(mhDisk));
	if (fd >= 0)
		fsync(fd);
}

void ATIDEPhysicalDisk::ReadSectors(void *data, uint32 lba, uint32 n) {
	int fd = static_cast<int>(reinterpret_cast<intptr_t>(mhDisk));
	if (fd < 0 || !mpBuffer || !data)
		return;

	uint8 *dst = (uint8 *)data;
	uint32 remaining = n;

	while (remaining > 0) {
		uint32 chunk = remaining > 32 ? 32 : remaining;
		uint64_t offset = (uint64_t)lba * 512;
		ssize_t bytesRead = ::pread(fd, mpBuffer, chunk * 512, (off_t)offset);
		if (bytesRead <= 0)
			break;
		memcpy(dst, mpBuffer, (size_t)bytesRead);
		uint32 sectorsRead = (uint32)bytesRead / 512;
		dst += sectorsRead * 512;
		lba += sectorsRead;
		remaining -= sectorsRead;
	}
}

void ATIDEPhysicalDisk::WriteSectors(const void *data, uint32 lba, uint32 n) {
	// Read-only device — writes not supported
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

///////////////////////////////////////////////////////////////////////////
// 16. VDGetLoadFileName / VDGetSaveFileName — Dita file dialog stubs
//     Parse null-separated Win32 filter strings into wxFileDialog format.
///////////////////////////////////////////////////////////////////////////

static std::map<long, VDStringW> s_lastLoadSavePaths;
static std::map<long, VDStringW> s_lastLoadSaveFileNames;

// Parse null-separated filter string into wxWidgets pipe-separated format.
// Input:  L"Description\0*.ext\0Description2\0*.ext2\0\0"
// Output: "Description|*.ext|Description2|*.ext2"
static wxString ParseWin32FilterString(const wchar_t *filters) {
	if (!filters)
		return wxString();

	wxString result;
	const wchar_t *p = filters;

	while (*p) {
		// Description
		const wchar_t *desc = p;
		while (*p) ++p;

		if (!*desc) break;

		++p; // skip null
		if (!*p) break;

		// Pattern
		const wchar_t *pattern = p;
		while (*p) ++p;
		++p; // skip null

		if (!result.empty())
			result += '|';

		VDStringA descU8 = VDTextWToU8(VDStringW(desc));
		VDStringA patU8 = VDTextWToU8(VDStringW(pattern));
		result += wxString::FromUTF8(descU8.c_str());
		result += '|';
		result += wxString::FromUTF8(patU8.c_str());
	}

	return result;
}

const VDStringW VDGetLoadFileName(long nKey, VDGUIHandle, const wchar_t *pszTitle, const wchar_t *pszFilters, const wchar_t *pszExt, const VDFileDialogOption *, int *) {
	wxString title = pszTitle ? wxString::FromUTF8(VDTextWToU8(VDStringW(pszTitle)).c_str()) : wxString("Open");
	wxString filters = ParseWin32FilterString(pszFilters);
	if (filters.empty())
		filters = "All files (*.*)|*.*";

	wxString defaultDir;
	auto it = s_lastLoadSavePaths.find(nKey);
	if (it != s_lastLoadSavePaths.end())
		defaultDir = wxString::FromUTF8(VDTextWToU8(it->second).c_str());

	wxFileDialog dlg(nullptr, title, defaultDir, wxEmptyString, filters, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK)
		return VDStringW();

	VDStringW path = VDTextU8ToW(VDStringSpanA(dlg.GetPath().utf8_str().data()));
	s_lastLoadSavePaths[nKey] = VDFileSplitPathLeft(path);
	return path;
}

const VDStringW VDGetSaveFileName(long nKey, VDGUIHandle, const wchar_t *pszTitle, const wchar_t *pszFilters, const wchar_t *pszExt, const VDFileDialogOption *, int *) {
	wxString title = pszTitle ? wxString::FromUTF8(VDTextWToU8(VDStringW(pszTitle)).c_str()) : wxString("Save");
	wxString filters = ParseWin32FilterString(pszFilters);
	if (filters.empty())
		filters = "All files (*.*)|*.*";

	wxString defaultDir;
	auto it = s_lastLoadSavePaths.find(nKey);
	if (it != s_lastLoadSavePaths.end())
		defaultDir = wxString::FromUTF8(VDTextWToU8(it->second).c_str());

	wxFileDialog dlg(nullptr, title, defaultDir, wxEmptyString, filters, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_OK)
		return VDStringW();

	VDStringW path = VDTextU8ToW(VDStringSpanA(dlg.GetPath().utf8_str().data()));
	s_lastLoadSavePaths[nKey] = VDFileSplitPathLeft(path);
	return path;
}

void VDSetLastLoadSavePath(long nKey, const wchar_t *path) {
	if (path)
		s_lastLoadSavePaths[nKey] = path;
}

const VDStringW VDGetLastLoadSavePath(long nKey) {
	auto it = s_lastLoadSavePaths.find(nKey);
	return it != s_lastLoadSavePaths.end() ? it->second : VDStringW();
}

void VDSetLastLoadSaveFileName(long nKey, const wchar_t *fileName) {
	if (fileName)
		s_lastLoadSaveFileNames[nKey] = fileName;
}

const VDStringW VDGetDirectory(long nKey, VDGUIHandle, const wchar_t *pszTitle) {
	wxString title = pszTitle ? wxString::FromUTF8(VDTextWToU8(VDStringW(pszTitle)).c_str()) : wxString("Select Directory");

	wxString defaultDir;
	auto it = s_lastLoadSavePaths.find(nKey);
	if (it != s_lastLoadSavePaths.end())
		defaultDir = wxString::FromUTF8(VDTextWToU8(it->second).c_str());

	wxDirDialog dlg(nullptr, title, defaultDir, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK)
		return VDStringW();

	VDStringW path = VDTextU8ToW(VDStringSpanA(dlg.GetPath().utf8_str().data()));
	s_lastLoadSavePaths[nKey] = path;
	return path;
}

void VDLoadFilespecSystemData() {}
void VDSaveFilespecSystemData() {}
void VDClearFilespecSystemData() {}
void VDInitFilespecSystem() {}

sint32 VDUIShowColorPicker(VDGUIHandle, uint32, vdspan<const uint32>, const char *) {
	return -1;
}

///////////////////////////////////////////////////////////////////////////
// 17. uiconfirm.cpp stubs — confirmation dialogs
///////////////////////////////////////////////////////////////////////////

static uint32 s_uiResetFlags = kATUIResetFlag_Default;

bool ATUIIsResetNeeded(uint32 flag) {
	return (s_uiResetFlags & flag) != 0;
}

void ATUIModifyResetFlag(uint32 flag, bool newState) {
	if (newState)
		s_uiResetFlags |= flag;
	else
		s_uiResetFlags &= ~flag;
}

bool ATUIConfirmDiscardMemory(VDGUIHandle h, const wchar_t *title) {
	return ATUIConfirm(h, "DiscardMemory", L"Memory contents will be lost. Continue?", title);
}

bool ATUIConfirmReset(VDGUIHandle h, const char *key, const wchar_t *message, const wchar_t *title) {
	return ATUIConfirm(h, key, message, title);
}

void ATUIConfirmResetComplete() {
	extern ATSimulator g_sim;
	g_sim.ColdReset();
}

bool ATUIConfirmBasicChangeReset() {
	return true;
}

void ATUIConfirmBasicChangeResetComplete() {
	extern ATSimulator g_sim;
	g_sim.ColdReset();
}

bool ATUIConfirmVideoStandardChangeReset() {
	return true;
}

void ATUIConfirmVideoStandardChangeResetComplete() {
	extern ATSimulator g_sim;
	g_sim.ColdReset();
}

bool ATUIConfirmCartridgeChangeReset() {
	return true;
}

void ATUIConfirmCartridgeChangeResetComplete() {
	extern ATSimulator g_sim;
	g_sim.ColdReset();
}

bool ATUIConfirmSystemChangeReset() {
	return true;
}

void ATUIConfirmSystemChangeResetComplete() {
	extern ATSimulator g_sim;
	g_sim.ColdReset();
}

bool ATUIConfirmAddFullDrive() {
	return true;
}

// Extended confirmations used by main.cpp command handlers
bool ATUIConfirmPartiallyAccurateSnapshot() {
	return true;
}

bool ATUIConfirmDiscardAllStorage(VDGUIHandle h, const wchar_t *prompt, bool, uint32) {
	return ATUIConfirm(h, "DiscardAll", prompt ? prompt : L"OK to discard?", L"Confirm");
}

///////////////////////////////////////////////////////////////////////////
// 18. Dialog redirect stubs — bridge ATUIShow*Dialog(VDGUIHandle) to
//     ATShow*Dialog(wxWindow*) for the wxWidgets implementations.
///////////////////////////////////////////////////////////////////////////

void ATUIShowAudioOptionsDialog(VDGUIHandle) {
	ATShowAudioOptionsDialog(nullptr);
}

void ATUIShowCPUOptionsDialog(VDGUIHandle) {
	ATShowCPUOptionsDialog(nullptr);
}

void ATUIShowDialogConfigureSystem(VDGUIHandle) {
	ATShowSystemConfigDialog(nullptr);
}

void ATUIShowDialogDevices(VDGUIHandle) {
	ATShowDeviceManagerDialog(nullptr);
}

void ATUIShowDialogFirmware(VDGUIHandle, ATFirmwareManager&, bool *) {
	ATShowFirmwareManagerDialog(nullptr);
}

void ATUIShowDialogProfiles(VDGUIHandle) {
	ATShowProfileManagerDialog(nullptr);
}

void ATUIOpenAdjustColorsDialog(VDGUIHandle) {
	ATShowColorSettingsDialog(nullptr);
}

void ATUIShowDialogAdvancedConfigurationModeless(VDGUIHandle) {
	ATShowAdvancedConfigDialog(nullptr);
}

void ATUIShowDialogCompatDB(VDGUIHandle) {
	ATShowCompatBrowserDialog(nullptr);
}

void ATUIShowTapeControlDialog(VDGUIHandle, ATCassetteEmulator&) {
	ATShowCassetteControlDialog(nullptr);
}

void ATUIShowDialogTapeEditor() {
	ATShowTapeEditorDialog(nullptr);
}

void ATUIShowDialogKeyboardCustomize(VDGUIHandle) {
	ATShowKeyboardSettingsDialog(nullptr);
}

void ATUIShowDialogSpeedOptions(VDGUIHandle) {
	// No separate speed dialog on Linux; speed is set via menu
}

void ATUIOpenAdjustScreenEffectsDialog(VDGUIHandle) {
	// Not available on Linux
}

void ATUIShowDialogRewind(IATAutoSaveManager&) {
	// No-op — rewind is handled directly
}

void ATUIShowDialogDebugFont(VDGUIHandle) {
	// No-op on Linux
}

void ATUIShowDialogVerifier(VDGUIHandle, ATSimulator&) {
	// No-op on Linux
}

void ATUIShowDialogNewBreakpoint() {
	// No-op on Linux
}

void ATUIOpenTraceViewer(VDGUIHandle, ATTraceCollection *) {
	// No-op on Linux
}

int ATUIShowDialogCartridgeMapper(VDGUIHandle, uint32, const void *) {
	return -1;  // auto-detect
}

void ATUIShowDialogSetFileAssociations(VDGUIHandle, bool, bool) {
	// Windows-only
}

void ATUIShowDialogRemoveFileAssociations(VDGUIHandle, bool, bool) {
	// Windows-only
}

bool ATUIShowWarningConfirm(VDGUIHandle, const wchar_t *text, const wchar_t *title) {
	VDStringA msg = VDTextWToU8(VDStringW(text ? text : L""));
	VDStringA cap = VDTextWToU8(VDStringW(title ? title : L"Warning"));

	int result = wxMessageBox(
		wxString::FromUTF8(msg.c_str()),
		wxString::FromUTF8(cap.c_str()),
		wxYES_NO | wxICON_WARNING);

	return result == wxYES;
}

void ATUIShowInfo(VDGUIHandle, const wchar_t *text) {
	if (text) {
		VDStringA msg = VDTextWToU8(VDStringW(text));
		wxMessageBox(wxString::FromUTF8(msg.c_str()), "Info", wxOK | wxICON_INFORMATION);
	}
}

bool ATUIGetNativeDialogMode() { return true; }
void ATUISetNativeDialogMode(bool) {}

vdrefptr<ATUIFutureWithResult<bool>> ATUIShowAlertWarningConfirm(const wchar_t *text, const wchar_t *title) {
	bool result = ATUIShowWarningConfirm(nullptr, text, title);
	return vdrefptr<ATUIFutureWithResult<bool>>(new ATUIFutureWithResult<bool>(result));
}

vdrefptr<ATUIFutureWithResult<bool>> ATUIShowAlertError(const wchar_t *text, const wchar_t *title) {
	if (text) {
		VDStringA msg = VDTextWToU8(VDStringW(text));
		VDStringA cap = VDTextWToU8(VDStringW(title ? title : L"Error"));
		wxMessageBox(wxString::FromUTF8(msg.c_str()), wxString::FromUTF8(cap.c_str()), wxOK | wxICON_ERROR);
	}
	return vdrefptr<ATUIFutureWithResult<bool>>(new ATUIFutureWithResult<bool>(true));
}

vdrefptr<ATUIFileDialogResult> ATUIShowOpenFileDialog(uint32 id, const wchar_t *title, const wchar_t *filters) {
	// Async file dialog API — not used on Linux (cmd*.cpp uses VDGetLoadFileName instead)
	return {};
}

vdrefptr<ATUIFileDialogResult> ATUIShowSaveFileDialog(uint32 id, const wchar_t *title, const wchar_t *filters) {
	// Async file dialog API — not used on Linux (cmd*.cpp uses VDGetSaveFileName instead)
	return {};
}

///////////////////////////////////////////////////////////////////////////
// 19. Additional dialogs referenced by cmd*.cpp / main.cpp forward decls
///////////////////////////////////////////////////////////////////////////

// Disk drive dialog (uses device manager dialog on Linux)
void ATUIShowDiskDriveDialog(VDGUIHandle) {
	ATShowDeviceManagerDialog(nullptr);
}

void ATUIShowDialogCheater(VDGUIHandle, void *) {
	ATShowCheaterDialog(nullptr);
}

void ATUIShowDialogAbout(VDGUIHandle) {
	// The wx About dialog is shown from the menu bar directly;
	// this stub exists for command handler compatibility.
	wxMessageBox(
		"Altirra - Atari 800/800XL/5200 Emulator\n"
		"Copyright (C) 2008-2024 Avery Lee\n"
		"Linux port contributions\n\n"
		"Licensed under GNU GPL v2+",
		"About Altirra", wxOK | wxICON_INFORMATION);
}

void ATUIShowDialogKeyboardOptions(VDGUIHandle) {
	ATShowKeyboardSettingsDialog(nullptr);
}

void ATUIShowDialogInputMappings(void *, ATInputManager&, IATJoystickManager *) {
	ATShowInputSetupDialog(nullptr);
}

void ATUIShowDialogInputSetup(void *, ATInputManager&, IATJoystickManager *) {
	ATShowInputSetupDialog(nullptr);
}

void ATUIShowDialogLightPen(VDGUIHandle, void *) {
	// No light pen dialog on Linux
}

void ATShowChangeLog(VDGUIHandle) {
	// Open URL in browser
	system("xdg-open 'https://github.com/joelsgp/altirra-linux/releases' &");
}

void ATUIShowDialogCmdLineHelp(VDGUIHandle) {
	wxMessageBox(
		"Usage: altirra [options] [image-file]\n\n"
		"Options:\n"
		"  /ntsc, /pal         Set video standard\n"
		"  /800, /xl, /5200    Set hardware mode\n"
		"  /debug              Enable debugger\n"
		"  /run                Auto-run after load\n",
		"Command Line Help", wxOK | wxICON_INFORMATION);
}

void ATUIShowDialogCheckForUpdates(VDGUIHandle) {
	ATCheckForUpdates(nullptr);
}

void ATShowHelp(void *, const char *) {
	system("xdg-open 'https://www.virtualdub.org/docs/altirra/' &");
}

void ATUIShowDialogSetupWizard(VDGUIHandle) {
	ATShowSetupWizard(nullptr);
}

// ATUIShowSourceListDialog is defined in console_wx.cpp

void ATUIShowDialogEditAccelerators(const char *) {
	// Shows keyboard shortcuts — redirect to our keyboard settings
	ATShowKeyboardSettingsDialog(nullptr);
}

///////////////////////////////////////////////////////////////////////////
// 20. Accessor stubs missing from cmd*.cpp requirements
///////////////////////////////////////////////////////////////////////////

// Global variables referenced by cmds.cpp
bool g_xepViewEnabled = false;
bool g_xepViewAutoswitchingEnabled = false;
bool g_showFps = false;

bool ATUIIsXEPViewEnabled() { return g_xepViewEnabled; }
void ATUISetXEPViewEnabled(bool v) { g_xepViewEnabled = v; }

bool ATUIIsAltOutputAvailable() { return false; }
void ATUISelectPrevAltOutput() {}
void ATUISelectNextAltOutput() {}
void ATUIToggleAltOutput(const char *) {}

sint32 ATUIGetCurrentAltViewIndex() { return -1; }
void ATUISetAltViewByIndex(sint32) {}

// Turbo pulse
static bool s_turboPulse = false;
bool ATUIGetTurboPulse() { return s_turboPulse; }
void ATUISetTurboPulse(bool v) {
	s_turboPulse = v;
	if (v)
		ATUISetTurbo(true);
	else
		ATUISetTurbo(false);
}

// Drive sounds
bool ATUIGetDriveSoundsEnabled() {
	extern ATSimulator g_sim;
	const ATDiskInterface& diskIf = g_sim.GetDiskInterface(0);
	return diskIf.AreDriveSoundsEnabled();
}

void ATUISetDriveSoundsEnabled(bool enabled) {
	extern ATSimulator g_sim;
	for (int i = 0; i < 15; ++i) {
		ATDiskInterface& diskIf = g_sim.GetDiskInterface(i);
		diskIf.SetDriveSoundsEnabled(enabled);
	}
}

// Recording status
ATUIRecordingStatus ATUIGetRecordingStatus() {
	return kATUIRecordingStatus_None;
}

// Device buttons
static uint32 s_deviceButtonMask = 0;
static uint32 s_deviceButtonChangeCounter = 0;

bool ATUIGetDeviceButtonSupported(uint32 idx) {
	extern ATSimulator g_sim;
	ATDeviceManager& dm = *g_sim.GetDeviceManager();
	const uint32 cc = dm.GetChangeCounter();

	if (s_deviceButtonChangeCounter != cc) {
		s_deviceButtonChangeCounter = cc;
		s_deviceButtonMask = 0;

		for (IATDeviceButtons *p : dm.GetInterfaces<IATDeviceButtons>(false, false, false))
			s_deviceButtonMask |= p->GetSupportedButtons();
	}

	return (s_deviceButtonMask & (1u << idx)) != 0;
}

bool ATUIGetDeviceButtonDepressed(uint32 idx) {
	if (!(s_deviceButtonMask & (1u << idx)))
		return false;

	extern ATSimulator g_sim;
	for (IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false)) {
		if (p->IsButtonDepressed((ATDeviceButton)idx))
			return true;
	}
	return false;
}

void ATUIActivateDeviceButton(uint32 idx, bool state) {
	if (!(s_deviceButtonMask & (1u << idx)))
		return;

	extern ATSimulator g_sim;
	for (IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton((ATDeviceButton)idx, state);
}

// Overscan mode
void ATUISetOverscanMode(ATGTIAEmulator::OverscanMode mode) {
	extern ATSimulator g_sim;
	g_sim.GetGTIA().SetOverscanMode(mode);
	ATUIResizeDisplay();
}

// Video standard
void ATSetVideoStandard(ATVideoStandard mode) {
	extern ATSimulator g_sim;
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;
	g_sim.SetVideoStandard(mode);
	ATUIUpdateSpeedTiming();
}

// BASIC switch
void ATUISwitchBasic(uint64 basicId) {
	extern ATSimulator g_sim;
	if (g_sim.GetBasicId() == basicId)
		return;
	g_sim.SetBasic(basicId);
	if (ATUIConfirmBasicChangeReset())
		ATUIConfirmBasicChangeResetComplete();
}

// Menu rebuild — no-op, wxWidgets menus rebuild directly
void ATUIRebuildDynamicMenu(int) {}

// Pan/zoom and light pen — no-op on Linux
void ATUIRecalibrateLightPen() {}
void ATUIActivatePanZoomTool() {}

// On-screen keyboard
void ATUIOpenOnScreenKeyboard() {
	ATShowOnScreenKeyboard(nullptr);
}

// Hold keys toggle
static bool g_holdKeysActive = false;

void ATUIToggleHoldKeys() {
	extern ATSimulator g_sim;
	g_holdKeysActive = !g_holdKeysActive;

	if (!g_holdKeysActive) {
		g_sim.ClearPendingHeldKey();
		g_sim.SetPendingHeldSwitches(0);
	}

	auto *pUIR = g_sim.GetUIRenderer();
	if (pUIR)
		pUIR->SetPendingHoldMode(g_holdKeysActive);
}

// Exit
void ATUIExit(bool) {
	// Exit is handled by the wx event loop
	wxExit();
}

// Mouse capture
bool ATUIIsMouseCaptured() { return false; }

// Window state
bool ATUICanManipulateWindows() { return true; }
bool ATUIIsModalActive() { return false; }

// Display fullscreen (same as ATUIGetFullscreen)
bool ATUIGetDisplayFullscreen() { return ATUIGetFullscreen(); }

// Dispatcher
static IATAsyncDispatcher *s_pDispatcher = nullptr;
IATAsyncDispatcher *ATUIGetDispatcher() { return s_pDispatcher; }
void ATUISetDispatcher(IATAsyncDispatcher *p) { s_pDispatcher = p; }

// ATUIManager accessor
ATUIManager& ATUIGetManager() { return g_ATUIManager; }

// Boot image
void ATUIBootImage(const wchar_t *path) {
	extern ATSimulator g_sim;
	if (!path)
		return;

	try {
		g_sim.UnloadAll();
		g_sim.Load(path, kATMediaWriteMode_RO, nullptr);
		g_sim.ColdReset();
		g_sim.Resume();
	} catch (const MyError& e) {
		ATUIShowError(e);
	}
}

// Export debug help
void ATUIExportDebugHelp() {
	// No-op on Linux
}

// ATLaunchURL
void ATLaunchURL(const char *url) {
	if (url) {
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "xdg-open '%s' &", url);
		system(cmd);
	}
}

///////////////////////////////////////////////////////////////////////////
// 21. DoLoad / DoBootWithConfirm — main.cpp load functions
///////////////////////////////////////////////////////////////////////////

void DoLoad(VDGUIHandle, const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType, bool *suppressColdReset, int loadIndex, bool) {
	extern ATSimulator g_sim;

	if (!path || !*path)
		return;

	try {
		ATMediaWriteMode wm = writeMode ? *writeMode : kATMediaWriteMode_RO;

		if (loadType == kATImageType_Cartridge || cartmapper >= 0) {
			ATCartLoadContext ctx;
			ctx.mCartMapper = cartmapper;
			g_sim.LoadCartridge(0, path, &ctx);
		} else {
			ATImageLoadContext ctx;
			ctx.mLoadType = loadType;
			ctx.mLoadIndex = loadIndex;
			g_sim.Load(path, wm, &ctx);
		}

		if (suppressColdReset && *suppressColdReset)
			return;

		g_sim.ColdReset();
		g_sim.Resume();
	} catch (const MyError& e) {
		ATUIShowError(e);
	}
}

void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper) {
	extern ATSimulator g_sim;

	if (!ATUIConfirmDiscardAllStorage(nullptr, L"OK to discard?", false, 0))
		return;

	g_sim.UnloadAll();
	DoLoad(nullptr, path, writeMode, cartmapper, kATImageType_None, nullptr, -1, false);
}

// Unload storage for boot (simplified)
void ATUIUnloadStorageForBoot() {
	extern ATSimulator g_sim;
	g_sim.UnloadAll();
}

///////////////////////////////////////////////////////////////////////////
// Paste() — keyboard text injection (ported from Windows main.cpp)
///////////////////////////////////////////////////////////////////////////

#include <at/ataudio/pokey.h>
#include "uikeyboard.h"

void Paste(const wchar_t *s, size_t len, bool useCooldown) {
	extern ATSimulator g_sim;
	vdfastvector<wchar_t> pasteChars;

	while (len--) {
		wchar_t c = *s++;

		if (!c)
			continue;

		int repeat = 1;

		switch (c) {
			case L'\u200B':	// zero width space
			case L'\u200C':	// zero width non-joiner
			case L'\u200D':	// zero width joiner
			case L'\u200E':	// left to right mark
			case L'\u200F':	// right to left mark
				continue;

			case L'\u2010':	// hyphen
			case L'\u2011':	// non-breaking hyphen
			case L'\u2012':	// figure dash
			case L'\u2013':	// en dash
			case L'\u2014':	// em dash
			case L'\u2015':	// horizontal bar
				c = L'-';
				break;

			case L'\u2018':	// left single quotation mark
			case L'\u2019':	// right single quotation mark
				c = L'\'';
				break;

			case L'\u201C':	// left double quotation mark
			case L'\u201D':	// right double quotation mark
				c = L'"';
				break;

			case L'\u2026':	// ellipsis
				c = L'.';
				repeat = 3;
				break;

			case L'\uFEFF':	// byte order mark
				continue;
		}

		while (repeat--)
			pasteChars.push_back(c);
	}

	pasteChars.push_back(0);

	auto& pokey = g_sim.GetPokey();
	wchar_t skipLT = 0;

	const wchar_t *t = pasteChars.data();

	while (wchar_t c = *t++) {
		if (c == skipLT) {
			skipLT = 0;
			continue;
		}

		skipLT = 0;

		const uint8 kInvalidScancode = 0xFF;
		uint8 scancode = kInvalidScancode;

		switch (c) {
			case L'\r':
			case L'\n':
				skipLT = c ^ (L'\r' ^ L'\n');
				scancode = 0x0C;
				break;

			case L'\t':
				scancode = 0x2C;
				break;

			case L'\x001B':
				scancode = 0x1C;
				break;

			default:
				if (ATUIGetDefaultScanCodeForCharacter(c, scancode)) {
					// For control characters that map to visible ATASCII chars,
					// inject ESC first so they display rather than act as controls
					switch (scancode) {
						case 0x1C:	// escape
						case 0x8E:	// up arrow
						case 0x8F:	// down arrow
						case 0x86:	// left arrow
						case 0x87:	// right arrow
						case 0x82:	// spade
						case 0x76:	// curved arrow up-left
						case 0x34:	// tall left arrow
						case 0x2C:	// tall right arrow
							pokey.PushKey(0x1C /*esc*/, false, true, false, useCooldown);
							break;
					}
				} else {
					scancode = kInvalidScancode;
				}
				break;
		}

		if (scancode != kInvalidScancode)
			pokey.PushKey(scancode, false, true, false, useCooldown);
	}
}

///////////////////////////////////////////////////////////////////////////
// 22. OnCommand* stubs — handlers defined in main.cpp (Windows) that
//     are forward-declared in cmds.cpp and used in command tables.
///////////////////////////////////////////////////////////////////////////

#include "cmdhelpers.h"
#include "gtia.h"

void OnCommandOpenImage(ATUICommandContext&) {
	VDStringW path = VDGetLoadFileName('load', nullptr, L"Open Image",
		L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.exe;*.bin;*.rom;*.car;*.cas;*.wav;*.flac;*.ogg;*.atz;*.gz;*.zip\0"
		L"Disk images (*.atr,*.xfd,*.dcm,*.pro,*.atx)\0*.atr;*.xfd;*.dcm;*.pro;*.atx\0"
		L"Programs (*.xex,*.obx,*.com,*.exe)\0*.xex;*.obx;*.com;*.exe\0"
		L"Cartridges (*.bin,*.rom,*.car)\0*.bin;*.rom;*.car\0"
		L"Cassette tapes (*.cas,*.wav)\0*.cas;*.wav\0"
		L"All files (*.*)\0*.*\0",
		nullptr);

	if (!path.empty())
		DoLoad(nullptr, path.c_str(), nullptr, -1, kATImageType_None, nullptr, -1, false);
}

void OnCommandBootImage(ATUICommandContext&) {
	VDStringW path = VDGetLoadFileName('load', nullptr, L"Boot Image",
		L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.exe;*.bin;*.rom;*.car;*.cas;*.wav;*.flac;*.ogg;*.atz;*.gz;*.zip\0"
		L"All files (*.*)\0*.*\0",
		nullptr);

	if (!path.empty())
		DoBootWithConfirm(path.c_str(), nullptr, -1);
}

bool OnTestCommandQuickLoadState() { return false; }
void OnCommandQuickLoadState() {}
void OnCommandQuickSaveState() {}

void OnCommandLoadState() {
	VDStringW fn = VDGetLoadFileName('save', nullptr, L"Load save state",
		g_ATUIFileFilter_LoadState, L"atstate2");
	if (!fn.empty()) {
		DoLoad(nullptr, fn.c_str(), nullptr, 0, kATImageType_SaveState, nullptr, -1, false);
	}
}

void OnCommandSaveState() {
	VDStringW fn = VDGetSaveFileName('save', nullptr, L"Save state",
		g_ATUIFileFilter_SaveState, L"atstate2");
	if (!fn.empty()) {
		extern ATSimulator g_sim;
		try {
			g_sim.SaveState(fn.c_str());
		} catch (const MyError& e) {
			ATUIShowError(e);
		}
	}
}

void OnCommandSaveFirmwareIDEMain() {}
void OnCommandSaveFirmwareIDESDX() {}
void OnCommandSaveFirmwareU1MB() {}
void OnCommandSaveFirmwareRapidusFlash() {}

void OnCommandExit(ATUICommandContext& ctx) {
	ATUIExit(ctx.mbQuiet);
}

void OnCommandAnticVisualizationNext() {
	extern ATSimulator g_sim;
	ATAnticEmulator& antic = g_sim.GetAntic();
	antic.SetAnalysisMode((ATAnticEmulator::AnalysisMode)(((int)antic.GetAnalysisMode() + 1) % 5));
}

void OnCommandGTIAVisualizationNext() {
	extern ATSimulator g_sim;
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	auto mode = gtia.GetAnalysisMode();
	int next = ((int)mode + 1) % ((int)ATGTIAEmulator::kAnalyzeCount);
	gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)next);
}

void OnCommandVideoToggleXEP80Output() {
	ATUIToggleAltOutput("xep80");
}

void OnCommandVideoToggleOutputAutoswitching() {
	g_xepViewAutoswitchingEnabled = !g_xepViewAutoswitchingEnabled;
}

void OnCommandVideoEnhancedTextFontDialog() {
	// No font picker on Linux
}

void OnCommandViewVerticalOverscan(ATGTIAEmulator::VerticalOverscanMode mode) {
	extern ATSimulator g_sim;
	g_sim.GetGTIA().SetVerticalOverscanMode(mode);
	ATUIResizeDisplay();
}

void OnCommandViewTogglePALExtended() {
	extern ATSimulator g_sim;
	g_sim.GetGTIA().SetOverscanPALExtended(!g_sim.GetGTIA().IsOverscanPALExtended());
	ATUIResizeDisplay();
}

void OnCommandViewToggleVSync() {
	extern ATSimulator g_sim;
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetVsyncEnabled(!gtia.IsVsyncEnabled());
}

void OnCommandViewAdjustWindowSize() {
	// No-op on Linux (wxWidgets handles sizing)
}

void OnCommandViewResetWindowLayout() {
	// No-op on Linux
}

void OnCommandPane(uint32) {
	// Pane activation is handled by wxAuiManager
}

void OnCommandEditCopyFrame() {}
void OnCommandEditCopyFrameTrueAspect() {}
void OnCommandEditSaveFrame() {}
void OnCommandEditSaveFrameTrueAspect() {}
void OnCommandEditDeselect() {}
void OnCommandEditSelectAll() {}
void OnCommandEditCopyText() {}
void OnCommandEditCopyEscapedText() {}
void OnCommandEditCopyHex() {}
void OnCommandEditCopyUnicode() {}

void OnCommandEditPasteText() {
	// Text paste requires complex character-to-scancode conversion.
	// The wxWidgets menu handler in menubar.cpp handles this directly.
}

void OnCommandConsoleHoldKeys() {
	ATUIToggleHoldKeys();
}

// Device button command handlers
void OnCommandConsoleBlackBoxDumpScreen() {
	ATUIActivateDeviceButton(kATDeviceButton_BlackBoxDumpScreen, true);
}

void OnCommandConsoleBlackBoxMenu() {
	ATUIActivateDeviceButton(kATDeviceButton_BlackBoxMenu, true);
}

void OnCommandConsoleIDEPlus2SwitchDisks() {
	ATUIActivateDeviceButton(kATDeviceButton_IDEPlus2SwitchDisks, true);
}

void OnCommandConsoleIDEPlus2WriteProtect() {
	ATUIActivateDeviceButton(kATDeviceButton_IDEPlus2WriteProtect, true);
}

void OnCommandConsoleIDEPlus2SDX() {
	ATUIActivateDeviceButton(kATDeviceButton_IDEPlus2SDX, true);
}

void OnCommandConsoleIndusGTId() {
	ATUIActivateDeviceButton(kATDeviceButton_IndusGTId, true);
}

void OnCommandConsoleIndusGTError() {
	ATUIActivateDeviceButton(kATDeviceButton_IndusGTError, true);
}

void OnCommandConsoleIndusGTTrack() {
	ATUIActivateDeviceButton(kATDeviceButton_IndusGTTrack, true);
}

void OnCommandConsoleIndusGTBootCPM() {
	ATUIActivateDeviceButton(kATDeviceButton_IndusGTBootCPM, true);
}

void OnCommandConsoleIndusGTChangeDensity() {
	ATUIActivateDeviceButton(kATDeviceButton_IndusGTChangeDensity, true);
}

void OnCommandConsoleHappyToggleFastSlow() {
	extern ATSimulator g_sim;
	for (IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_HappySlow, !p->IsButtonDepressed(kATDeviceButton_HappySlow));
}

void OnCommandConsoleHappyToggleWriteProtect() {
	extern ATSimulator g_sim;
	for (IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_HappyWPEnable, !p->IsButtonDepressed(kATDeviceButton_HappyWPEnable));
}

void OnCommandConsoleHappyToggleWriteEnable() {
	extern ATSimulator g_sim;
	for (IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_HappyWPDisable, !p->IsButtonDepressed(kATDeviceButton_HappyWPDisable));
}

void OnCommandConsoleATR8000Reset() {
	extern ATSimulator g_sim;
	for (IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_ATR8000Reset, true);
}

void OnCommandConsoleXELCFSwap() {
	extern ATSimulator g_sim;
	for (IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_XELCFSwap, true);
}

// Disk commands from main.cpp
void OnCommandDiskDrivesDialog() {
	ATShowDeviceManagerDialog(nullptr);
}

void OnCommandDiskToggleSIOPatch() {
	extern ATSimulator g_sim;
	g_sim.SetDiskSIOPatchEnabled(!g_sim.IsDiskSIOPatchEnabled());
}

void OnCommandDiskToggleSIOOverrideDetection() {
	extern ATSimulator g_sim;
	g_sim.SetDiskSIOOverrideDetectEnabled(!g_sim.IsDiskSIOOverrideDetectEnabled());
}

void OnCommandDiskToggleAccurateSectorTiming() {
	extern ATSimulator g_sim;
	g_sim.SetDiskAccurateTimingEnabled(!g_sim.IsDiskAccurateTimingEnabled());
}

void OnCommandDiskToggleDriveSounds() {
	ATUISetDriveSoundsEnabled(!ATUIGetDriveSoundsEnabled());
}

void OnCommandDiskToggleSectorCounter() {
	extern ATSimulator g_sim;
	g_sim.SetDiskSectorCounterEnabled(!g_sim.IsDiskSectorCounterEnabled());
}

void OnCommandDiskAttach(int index) {
	VDStringW fn = VDGetLoadFileName('disk', nullptr, L"Attach Disk Image",
		g_ATUIFileFilter_DiskWithArchives, L"atr");
	if (!fn.empty()) {
		extern ATSimulator g_sim;
		try {
			ATDiskInterface& di = g_sim.GetDiskInterface(index);
			di.LoadDisk(fn.c_str());
		} catch (const MyError& e) {
			ATUIShowError(e);
		}
	}
}

void OnCommandDiskDetach(int index) {
	extern ATSimulator g_sim;
	if (index < 0) {
		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			if (di.IsDiskLoaded())
				di.UnloadDisk();
		}
	} else {
		ATDiskInterface& di = g_sim.GetDiskInterface(index);
		if (di.IsDiskLoaded())
			di.UnloadDisk();
	}
}

void OnCommandDiskDetachAll() {
	OnCommandDiskDetach(-1);
}

void OnCommandDiskRotate(int delta) {
	extern ATSimulator g_sim;
	int activeDrives = 0;
	for (int i = 14; i >= 0; --i) {
		if (g_sim.GetDiskDrive(i).IsEnabled() || g_sim.GetDiskInterface(i).GetClientCount() > 1) {
			activeDrives = i + 1;
			break;
		}
	}
	if (activeDrives > 0)
		g_sim.RotateDrives(activeDrives, delta);
}

// Input commands from main.cpp
void OnCommandInputCaptureMouse() {}
void OnCommandInputToggleAutoCaptureMouse() {
	ATUISetMouseAutoCapture(!ATUIGetMouseAutoCapture());
}

void OnCommandInputInputMappingsDialog() {
	ATShowInputSetupDialog(nullptr);
}

void OnCommandInputInputSetupDialog() {
	ATShowInputSetupDialog(nullptr);
}

void OnCommandInputKeyboardDialog() {
	ATShowKeyboardSettingsDialog(nullptr);
}

void OnCommandInputLightPenDialog() {
	// No-op on Linux
}

void OnCommandInputCycleQuickMaps() {
	extern ATSimulator g_sim;
	auto *pIM = g_sim.GetInputManager();
	if (pIM) {
		ATInputMap *pMap = pIM->CycleQuickMaps();
		auto *pUIR = g_sim.GetUIRenderer();
		if (pUIR) {
			if (pMap)
				pUIR->SetStatusMessage((VDStringW(L"Quick map: ") + pMap->GetName()).c_str());
			else
				pUIR->SetStatusMessage(L"Quick maps disabled");
		}
	}
}

// Recording commands — simplified stubs
void OnCommandRecordStop() {}
void OnCommandRecordRawAudio() {}
void OnCommandRecordAudio() {}
void OnCommandRecordVideo() {}
void OnCommandRecordPause() {}
void OnCommandRecordResume() {}
void OnCommandRecordPauseResume() {}
void OnCommandRecordSapTypeR() {}
void OnCommandRecordVgm() {}

// Cheat commands
void OnCommandCheatTogglePMCollisions() {
	extern ATSimulator g_sim;
	g_sim.GetGTIA().SetPMCollisionsEnabled(!g_sim.GetGTIA().ArePMCollisionsEnabled());
}

void OnCommandCheatTogglePFCollisions() {
	extern ATSimulator g_sim;
	g_sim.GetGTIA().SetPFCollisionsEnabled(!g_sim.GetGTIA().ArePFCollisionsEnabled());
}

void OnCommandCheatCheatDialog() {
	ATShowCheaterDialog(nullptr);
}

// Tools commands
void OnCommandToolsDiskExplorer() {
	ATShowDiskExplorerDialog(nullptr);
}

void OnCommandToolsConvertSapToExe() {
	// Simplified: no-op stub
}

void OnCommandToolsExportROMSet() {
	// No-op on Linux
}

void OnCommandToolsKeyboardShortcutsDialog() {
	ATShowKeyboardSettingsDialog(nullptr);
}

void OnCommandToolsOptionsDialog() {
	// No separate options dialog on Linux
}

void OnCommandToolsSetupWizard() {
	ATShowSetupWizard(nullptr);
}

// Help commands
void OnCommandHelpContents() {
	ATShowHelp(nullptr, nullptr);
}

void OnCommandHelpAbout() {
	ATUIShowDialogAbout(nullptr);
}

void OnCommandHelpChangeLog() {
	ATShowChangeLog(nullptr);
}

void OnCommandHelpCmdLine() {
	ATUIShowDialogCmdLineHelp(nullptr);
}

void OnCommandHelpOnline() {
	ATLaunchURL("https://www.virtualdub.org/altirra.html");
}

void OnCommandHelpCheckForUpdates() {
	ATCheckForUpdates(nullptr);
}

// Window commands (no-ops, cmdwindow.cpp is excluded)
void OnCommandWindowClose() {}
void OnCommandWindowUndock() {}
void OnCommandWindowPrevPane() {}
void OnCommandWindowNextPane() {}

///////////////////////////////////////////////////////////////////////////
// 23. cmddebug.cpp dependencies
///////////////////////////////////////////////////////////////////////////

// ATUIGetDebugSrcMode is defined in cmddebug.cpp
// ATUIGetActivePaneId is defined in console_wx.cpp

///////////////////////////////////////////////////////////////////////////
// 24. Miscellaneous missing symbols
///////////////////////////////////////////////////////////////////////////

// Calibration screen — static ShowDialog only (no-op on Linux).
// Only include the header for the class declaration; don't provide
// constructor/destructor to avoid triggering vtable emission which
// would require the full ATUI widget hierarchy.
#include "uicalibrationscreen.h"
void ATUICalibrationScreen::ShowDialog() {}

// Cartridge discard confirm — not in our uiconfirm stubs above
bool ATUIConfirmDiscardCartridge(VDGUIHandle) {
	extern ATSimulator g_sim;
	if (!g_sim.IsStorageDirty(kATStorageId_Cartridge))
		return true;
	return ATUIConfirm(nullptr, nullptr,
		L"Modified cartridge image has not been saved. Discard it anyway?",
		L"Altirra Warning");
}

// ATUISwitchKernel single-argument overload (called by cmdsystem.cpp)
void ATUISwitchKernel(uint64 id) {
	ATUISwitchKernel(nullptr, id);
}

// Additional toggles used by cmd*.cpp
void OnCommandSystemProgramLoadModeDefault() {}
void OnCommandToggleAutoReset() {}
void OnCommandToggleBootUnload() {}
