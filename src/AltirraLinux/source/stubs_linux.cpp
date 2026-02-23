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
#include <vd2/system/refcount.h>
#include <vd2/system/function.h>
#include <vd2/system/vectors.h>
#include <vd2/system/atomic.h>
#include <vd2/system/thread.h>
#include <vd2/system/text.h>
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

#include <SDL.h>
#include <error_imgui.h>
#include "display_sdl2.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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
#include "devicemanager.h"
#include "directorywatcher.h"
#include "idephysdisk.h"
#include "modemtcp.h"
#include "customdevice_win32.h"
#include "trace.h"

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

// Keyboard options (defined in main.cpp on Windows)
ATUIKeyboardOptions g_kbdOpts {};

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

void ATUIQueue::PushStep(const vdfunction<void()>&) {
}

ATUIQueue& ATUIGetQueue() {
	return s_stubQueue;
}

///////////////////////////////////////////////////////////////////////////
// 4. ATUI accessor getters (bool) — simple stubs
///////////////////////////////////////////////////////////////////////////

bool ATUIGetAltViewAutoswitchingEnabled() { return false; }
bool ATUIGetAltViewEnabled() { return false; }
bool ATUIGetConstrainMouseFullScreen() { return false; }
bool ATUIGetDisplayIndicators() { return false; }
bool ATUIGetDisplayPadIndicators() { return false; }
bool ATUIGetDrawPadBoundsEnabled() { return false; }
bool ATUIGetDrawPadPointersEnabled() { return false; }
bool ATUIGetMouseAutoCapture() { return false; }
bool ATUIGetPauseWhenInactive() { return false; }
bool ATUIGetPointerAutoHide() { return false; }
bool ATUIGetRawInputEnabled() { return false; }
bool ATUIGetTargetPointerVisible() { return false; }
bool ATUIGetFrameRateVSyncAdaptive() { return false; }
bool ATUIIsMenuAutoHideEnabled() { return false; }
bool ATUIIsElevationRequiredForMountVHDImage() { return false; }

///////////////////////////////////////////////////////////////////////////
// 4b. ATUI accessor getters (bool) — backed by static variables
//     These are read/written by the ImGui emulator UI.
///////////////////////////////////////////////////////////////////////////

static bool s_showFPS = false;
static bool s_turbo = false;
static bool s_fullscreen = false;

bool ATUIGetShowFPS() { return s_showFPS; }
bool ATUIGetTurbo() { return s_turbo; }
bool ATUIGetFullscreen() { return s_fullscreen; }

///////////////////////////////////////////////////////////////////////////
// 5. ATUI accessor getters (numeric / enum / string / pointer)
///////////////////////////////////////////////////////////////////////////

uint32 ATUIGetBootUnloadStorageMask() { return 0; }
uint32 ATUIGetResetFlags() { return 0; }

static ATDisplayFilterMode s_displayFilterMode = (ATDisplayFilterMode)0;
static ATDisplayStretchMode s_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;
static float s_speedModifier = 1.0f;

ATDisplayFilterMode ATUIGetDisplayFilterMode() { return s_displayFilterMode; }
ATDisplayStretchMode ATUIGetDisplayStretchMode() { return s_displayStretchMode; }
ATFrameRateMode ATUIGetFrameRateMode() { return (ATFrameRateMode)0; }
ATUIEnhancedTextMode ATUIGetEnhancedTextMode() { return kATUIEnhancedTextMode_None; }

float ATUIGetDisplayZoom() { return 1.0f; }
float ATUIGetSpeedModifier() { return s_speedModifier; }
int ATUIGetViewFilterSharpness() { return 0; }

vdfloat2 ATUIGetDisplayPanOffset() { return vdfloat2{0, 0}; }

const char *ATUIGetCurrentAltOutputName() { return ""; }
const char *ATUIGetWindowCaptionTemplate() { return ""; }

VDGUIHandle ATUIGetNewPopupOwner() { return nullptr; }

///////////////////////////////////////////////////////////////////////////
// 6. ATUI accessor setters
///////////////////////////////////////////////////////////////////////////

void ATUISetAltViewAutoswitchingEnabled(bool) {}
void ATUISetAltViewEnabled(bool) {}
void ATUISetBootUnloadStorageMask(uint32) {}
void ATUISetConstrainMouseFullScreen(bool) {}
void ATUISetCurrentAltOutputName(const char *) {}
void ATUISetDisplayFilterMode(ATDisplayFilterMode m) { s_displayFilterMode = m; }
void ATUISetDisplayIndicators(bool) {}
void ATUISetDisplayPadIndicators(bool) {}
void ATUISetDisplayPanOffset(const vdfloat2&) {}
void ATUISetDisplayStretchMode(ATDisplayStretchMode m) {
	s_displayStretchMode = m;
	extern ATDisplaySDL2 *ATGetLinuxDisplay();
	ATDisplaySDL2 *disp = ATGetLinuxDisplay();
	if (disp)
		disp->SetStretchMode(m);
}
void ATUISetDisplayZoom(float) {}
void ATUISetDrawPadBoundsEnabled(bool) {}
void ATUISetDrawPadPointersEnabled(bool) {}
void ATUISetEnhancedTextMode(ATUIEnhancedTextMode) {}
void ATUISetFrameRateMode(ATFrameRateMode) {}
void ATUISetFrameRateVSyncAdaptive(bool) {}
void ATUISetMenuAutoHideEnabled(bool) {}
void ATUISetMouseAutoCapture(bool) {}
void ATUISetPauseWhenInactive(bool) {}
void ATUISetPointerAutoHide(bool) {}
void ATUISetRawInputEnabled(bool) {}
void ATUISetResetFlags(uint32) {}
void ATUISetShowFPS(bool v) { s_showFPS = v; }
void ATUISetSpeedModifier(float v) {
	s_speedModifier = v;
	extern ATSimulator g_sim;
	double rate = std::clamp((double)v, 0.01, 100.0);
	IATAudioOutput *audio = g_sim.GetAudioOutput();
	if (audio)
		audio->SetCyclesPerSecond(1789772.5, 1.0 / rate);
}
void ATUISetTargetPointerVisible(bool) {}
void ATUISetTurbo(bool v) {
	s_turbo = v;
	extern ATSimulator g_sim;
	g_sim.SetTurboModeEnabled(v);
	SDL_GL_SetSwapInterval(v ? 0 : 1);
}
void ATUISetViewFilterSharpness(int) {}
void ATUISetWindowCaptionTemplate(const char *) {}

///////////////////////////////////////////////////////////////////////////
// 7. ATUI keyboard map functions
///////////////////////////////////////////////////////////////////////////

void ATUIInitVirtualKeyMap(const ATUIKeyboardOptions&) {}
void ATUIGetCustomKeyMap(vdfastvector<uint32>&) {}
void ATUISetCustomKeyMap(const uint32 *, size_t) {}

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
void ATUIUpdateSpeedTiming() {}
void ATSyncCPUHistoryState() {}

bool ATUIClipIsTextAvailable() {
	return SDL_HasClipboardText() == SDL_TRUE;
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

void ATUIExecuteCommandStringAndShowErrors(const char *, const ATUICommandOptions *) noexcept {}

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

void ATUIShowDialogDiskExplorer(VDGUIHandle, IATBlockDevice *, const wchar_t *) {}

bool ATUISwitchHardwareMode(VDGUIHandle, ATHardwareMode mode, bool) {
	extern ATSimulator g_sim;
	g_sim.SetHardwareMode(mode);
	return true;
}
bool ATUISwitchKernel(VDGUIHandle, uint64) { return false; }

void ATUITemporarilyMountVHDImageW32(VDGUIHandle, const wchar_t *, bool) {}

void ATRegisterDeviceConfigurers(ATDeviceManager&) {}

///////////////////////////////////////////////////////////////////////////
// 9. (Removed — debugger accessors now provided by debugger.cpp)
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// 10. Win32-to-SIO error translation (PCLink)
///////////////////////////////////////////////////////////////////////////

uint8 ATTranslateWin32ErrorToSIOError(uint32) {
	// Return a generic SIO error. On Linux this should not be called with
	// Win32 error codes; a proper Linux implementation should translate
	// errno values instead.
	return 0xFF;
}

///////////////////////////////////////////////////////////////////////////
// 11. Factory / creation functions
///////////////////////////////////////////////////////////////////////////

// Custom network engine (Windows named pipe / TCP based)
vdrefptr<IATDeviceCustomNetworkEngine> ATCreateDeviceCustomNetworkEngine(
	uint16, IATTimerService&, vdfunction<void()>)
{
	return nullptr;
}

// Modem driver (Winsock based)
IATModemDriver *ATCreateModemDriverTCP() {
	return nullptr;
}

// Native ETW/WPR tracer (Windows trace infrastructure)
vdrefptr<IVDRefCount> ATCreateNativeTracer(ATTraceContext&, const ATNativeTraceSettings&) {
	return nullptr;
}

// Network socket VXLAN tunnel (Winsock)
void ATCreateNetSockVxlanTunnel(
	uint32, uint16, uint16, IATEthernetSegment *, uint32,
	IATAsyncDispatcher *, IATNetSockVxlanTunnel **)
{
	// No-op: leaves *pp unchanged (caller should check for null)
}

// Network socket worker (Winsock)
void ATCreateNetSockWorker(
	IATEmuNetUdpStack *, IATEmuNetTcpStack *, bool, uint32, uint16,
	IATNetSockWorker **)
{
	// No-op: leaves *pp unchanged (caller should check for null)
}

// Timer service (Windows multimedia timers)
IATTimerService *ATCreateTimerService(IATAsyncDispatcher&) {
	return nullptr;
}

// UI renderer (Windows GDI / Direct3D overlay)
void ATCreateUIRenderer(IATUIRenderer **r) {
	if (r)
		*r = nullptr;
}

///////////////////////////////////////////////////////////////////////////
// 12. (Removed — ATEnumLookupTable specializations now provided by debugger.cpp)
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// 13. ATDirectoryWatcher (Windows ReadDirectoryChangesW)
//     ATDirectoryWatcher inherits VDThread so we must provide ThreadRun().
///////////////////////////////////////////////////////////////////////////

bool ATDirectoryWatcher::sbShouldUsePolling = true;

ATDirectoryWatcher::ATDirectoryWatcher()
	: mhDir(nullptr)
	, mhExitEvent(nullptr)
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

void ATDirectoryWatcher::Init(const wchar_t *, bool) {
	// Stub: directory watching not yet implemented on Linux.
	// A real implementation would use inotify.
}

void ATDirectoryWatcher::Shutdown() {
	// Stub: nothing to clean up
}

bool ATDirectoryWatcher::CheckForChanges() {
	return false;
}

bool ATDirectoryWatcher::CheckForChanges(vdfastvector<wchar_t>&) {
	return false;
}

void ATDirectoryWatcher::ThreadRun() {
	// Stub: no background thread on Linux
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
// 15. VDFileWatcher (Windows FindFirstChangeNotification)
///////////////////////////////////////////////////////////////////////////

VDFileWatcher::VDFileWatcher()
	: mChangeHandle(nullptr)
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

void VDFileWatcher::InitDir(const wchar_t *, bool, IVDFileWatcherCallback *) {
	// Stub: file watching not yet implemented on Linux.
	// A real implementation would use inotify.
}

void VDFileWatcher::Shutdown() {
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

// Minimal synchronous implementation for Linux
namespace {
	class VDFileAsyncStub final : public IVDFileAsync {
	public:
		void SetPreemptiveExtend(bool) override {}
		bool IsPreemptiveExtendActive() override { return false; }
		bool IsOpen() override { return false; }
		void Open(const wchar_t *, uint32, uint32) override {}
		void Open(VDFileHandle, uint32, uint32) override {}
		void Close() override {}
		void FastWrite(const void *, uint32) override {}
		void FastWriteEnd() override {}
		void Write(sint64, const void *, uint32) override {}
		bool Extend(sint64) override { return false; }
		void Truncate(sint64) override {}
		void SafeTruncateAndClose(sint64) override {}
		sint64 GetFastWritePos() override { return 0; }
		sint64 GetSize() override { return 0; }
	};
}

IVDFileAsync *VDCreateFileAsync(IVDFileAsync::Mode) {
	return new VDFileAsyncStub;
}

///////////////////////////////////////////////////////////////////////////
// 19. VDUIGetAcceleratorString (Windows virtual key name lookup)
///////////////////////////////////////////////////////////////////////////

void VDUIGetAcceleratorString(const VDUIAccelerator&, VDStringW& s) {
	s.clear();
}
