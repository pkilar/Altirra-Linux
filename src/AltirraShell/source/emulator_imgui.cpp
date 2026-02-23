//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - Dear ImGui emulator configuration UI
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/refcount.h>
#include <vd2/system/file.h>
#include <vd2/VDDisplay/display.h>
#include <at/atdebugger/target.h>
#include <at/atcore/media.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/serializable.h>
#include <at/atio/image.h>
#include <at/atio/cartridgeimage.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>

#include <vd2/system/filesys.h>

#include <imgui.h>
#include <emulator_imgui.h>
#include <debugger_imgui.h>
#include <display_sdl2.h>
#include <filedialog_linux.h>
#include <error_imgui.h>
#include <cartridge_names.h>
#include <firmware_names.h>

// Forward declarations needed by simulator.h transitives
class ATIRQController;

#include "simulator.h"
#include "constants.h"
#include "cassette.h"
#include "diskinterface.h"
#include "cartridge.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "gtia.h"
#include "uikeyboard.h"
#include "devicemanager.h"
#include "debugger.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

// Forward declarations for functions defined in cartdetect.cpp (compiled in Linux build)
uint32 ATCartridgeAutodetectMode(const void *data, uint32 size, vdfastvector<int>& cartModes);

// Firmware search path accessor (defined in main_linux.cpp)
void ATGetFirmwareSearchPaths(vdvector<VDStringW>& paths);

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

// Display backend accessor (defined in main_linux.cpp)
extern ATDisplaySDL2 *ATGetLinuxDisplay();

// Quick save state (in-memory)
static vdrefptr<IATSerializable> s_pQuickState;

// Window visibility
static bool s_showSystemConfig = false;
static bool s_showAbout = false;
static bool s_showCartridgeBrowser = false;
static bool s_showFirmwareManager = false;
static bool s_showAudioOptions = false;
static bool s_showVideoConfig = false;
static bool s_showKeyboardConfig = false;
static bool s_showDeviceManager = false;
static bool s_showCassetteControl = false;
static bool s_showBootOptions = false;

// Device manager state
static IATDevice *s_devSelectedDevice = nullptr;
static bool s_devShowConfig = false;
static ATPropertySet s_devEditProps;
static std::string s_devEditTag;

// Cartridge browser state
static bool s_cartMapperActive = false;
static vdfastvector<uint8> s_cartLoadBuffer;
static VDStringW s_cartLoadPath;
static vdfastvector<int> s_cartDetectedModes;
static vdfastvector<int> s_cartDisplayModes;
static int s_cartSelectedMapper = -1;
static bool s_cartShowAll = false;
static uint32 s_cartRecommendedIdx = 0;

// Firmware manager state
static vdvector<ATFirmwareInfo> s_fwList;
static bool s_fwListDirty = true;
static uint64 s_fwSelectedId = 0;
static std::string s_fwScanResult;
static bool s_fwShowScanResult = false;

// FPS counter state
static double s_lastFpsTime = 0;
static int s_fpsFrameCount = 0;
static float s_currentFps = 0;

// Error popup state
static bool s_showErrorPopup = false;
static std::string s_errorTitle;
static std::string s_errorMessage;

// Pending file dialog result tracking
static enum class PendingDialog {
	kNone,
	kOpenImage,
	kMountDisk1,
	kMountDisk2,
	kMountDisk3,
	kMountDisk4,
	kSaveState,
	kLoadState
} s_pendingDialog = PendingDialog::kNone;

static const char *kDiskFilters =
	"Disk Images|*.atr;*.xfd;*.dcm;*.pro;*.atx"
	"|Cartridge Images|*.bin;*.car"
	"|Tape Images|*.cas;*.wav"
	"|All Files|*";

static const char *kDiskOnlyFilters =
	"Disk Images|*.atr;*.xfd;*.dcm;*.pro;*.atx"
	"|All Files|*";

static const char *kSaveStateFilters =
	"Save States|*.atstate2;*.altstate"
	"|All Files|*";

static const char *kCartFilters =
	"Cartridge Images|*.car;*.bin;*.rom"
	"|All Files|*";

static const char *kTapeFilters =
	"Tape Images|*.cas;*.wav"
	"|All Files|*";

static const char *kFirmwareFilters =
	"ROM Images|*.rom;*.bin;*.epr;*.epm"
	"|All Files|*";

// ============= Hardware mode names =============

static const char *kHardwareModeNames[] = {
	"800",
	"800XL",
	"5200",
	"XEGS",
	"1200XL",
	"130XE",
	"1400XL"
};
static_assert(sizeof(kHardwareModeNames) / sizeof(kHardwareModeNames[0]) == kATHardwareModeCount);

static const char *kMemoryModeNames[] = {
	"48K",
	"52K",
	"64K",
	"128K",
	"320K",
	"576K",
	"1088K",
	"16K",
	"8K",
	"24K",
	"32K",
	"40K",
	"320K Compy",
	"576K Compy",
	"256K"
};
static_assert(sizeof(kMemoryModeNames) / sizeof(kMemoryModeNames[0]) == kATMemoryModeCount);

static const char *kVideoStandardNames[] = {
	"NTSC",
	"PAL",
	"SECAM",
	"PAL-60",
	"NTSC-50"
};
static_assert(sizeof(kVideoStandardNames) / sizeof(kVideoStandardNames[0]) == kATVideoStandardCount);

static const char *kDisplayFilterNames[] = {
	"Point",
	"Bilinear",
	"Bicubic",
	"Any Suitable",
	"Sharp Bilinear"
};

static const char *kArtifactModeNames[] = {
	"None", "NTSC", "PAL", "NTSC Hi", "PAL Hi", "Auto", "Auto Hi"
};
static_assert(sizeof(kArtifactModeNames)/sizeof(kArtifactModeNames[0]) == (int)ATArtifactMode::Count);

static const char *kMonitorModeNames[] = {
	"Color", "Peritel", "Green Mono", "Amber Mono", "Bluish White Mono", "White Mono"
};
static_assert(sizeof(kMonitorModeNames)/sizeof(kMonitorModeNames[0]) == (int)ATMonitorMode::Count);

static const char *kOverscanModeNames[] = {
	"Normal (168cc)", "Extended (192cc)", "Full (228cc)", "OS Screen (160cc)", "Widescreen (176cc)"
};

static const char *kVertOverscanModeNames[] = {
	"Default", "OS Screen (192)", "Normal (224)", "Extended (240)", "Full"
};

static const char *kStretchModeNames[] = {
	"Unconstrained", "Preserve Aspect Ratio", "Square Pixels", "Integral", "Integral + Aspect"
};
static_assert(sizeof(kStretchModeNames)/sizeof(kStretchModeNames[0]) == kATDisplayStretchModeCount);

static const char *kColorMatchingNames[] = {
	"None", "sRGB", "Adobe RGB", "Gamma 2.2", "Gamma 2.4"
};

static const char *kLumaRampNames[] = {
	"Linear", "XL"
};

static const char *kCassetteTurboModeNames[] = {
	"None (FSK only)",
	"Command Control",
	"Proceed Sense",
	"Interrupt Sense",
	"KSO Turbo 2000",
	"Turbo D",
	"Data Control",
	"Always"
};
static_assert(sizeof(kCassetteTurboModeNames)/sizeof(kCassetteTurboModeNames[0]) == kATCassetteTurboMode_Always + 1);

// ============= Helper: load file via dialog =============

static void TryLoadImage(const VDStringW& path) {
	if (path.empty())
		return;

	try {
		g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
	} catch (...) {
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "Failed to load image: %s\n", u8.c_str());
	}
}

static void TryMountDisk(int index, const VDStringW& path) {
	if (path.empty())
		return;

	try {
		ATDiskInterface& di = g_sim.GetDiskInterface(index);
		di.LoadDisk(path.c_str());
	} catch (...) {
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "Failed to mount disk D%d: %s\n", index + 1, u8.c_str());
	}
}

// ============= Main Menu Bar =============

static void DrawMenuBar() {
	IATDebugger *dbg = ATGetDebugger();

	if (!ImGui::BeginMainMenuBar())
		return;

	// --- System menu ---
	if (ImGui::BeginMenu("System")) {
		if (ImGui::BeginMenu("Hardware Mode")) {
			ATHardwareMode curHW = g_sim.GetHardwareMode();
			for (uint32 i = 0; i < kATHardwareModeCount; ++i) {
				if (ImGui::MenuItem(kHardwareModeNames[i], nullptr, curHW == (ATHardwareMode)i)) {
					g_sim.SetHardwareMode((ATHardwareMode)i);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Memory")) {
			ATMemoryMode curMem = g_sim.GetMemoryMode();
			for (uint32 i = 0; i < kATMemoryModeCount; ++i) {
				if (ImGui::MenuItem(kMemoryModeNames[i], nullptr, curMem == (ATMemoryMode)i)) {
					g_sim.SetMemoryMode((ATMemoryMode)i);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Video")) {
			ATVideoStandard curVS = g_sim.GetVideoStandard();
			for (uint32 i = 0; i < kATVideoStandardCount; ++i) {
				if (ImGui::MenuItem(kVideoStandardNames[i], nullptr, curVS == (ATVideoStandard)i)) {
					g_sim.SetVideoStandard((ATVideoStandard)i);
				}
			}
			ImGui::EndMenu();
		}

		bool basicEnabled = g_sim.IsBASICEnabled();
		if (ImGui::MenuItem("BASIC", nullptr, &basicEnabled)) {
			g_sim.SetBASICEnabled(basicEnabled);
		}

		ImGui::Separator();

		if (ImGui::MenuItem("System Settings...")) {
			s_showSystemConfig = true;
		}
		if (ImGui::MenuItem("Cartridge...")) {
			s_showCartridgeBrowser = true;
		}
		if (ImGui::MenuItem("Firmware Manager...")) {
			s_showFirmwareManager = true;
			s_fwListDirty = true;
		}
		if (ImGui::MenuItem("Audio Options...")) {
			s_showAudioOptions = true;
		}
		if (ImGui::MenuItem("Video Settings...")) {
			s_showVideoConfig = true;
		}
		if (ImGui::MenuItem("Keyboard...")) {
			s_showKeyboardConfig = true;
		}
		if (ImGui::MenuItem("Devices...")) {
			s_showDeviceManager = true;
		}
		if (ImGui::MenuItem("Boot & Acceleration...")) {
			s_showBootOptions = true;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Cold Reset")) {
			g_sim.ColdReset();
		}
		if (ImGui::MenuItem("Warm Reset")) {
			g_sim.WarmReset();
		}

		ImGui::EndMenu();
	}

	// --- File menu ---
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("Open Image...")) {
			VDStringW path = ATLinuxOpenFileDialog("Open Image", kDiskFilters);
			if (!path.empty()) {
				TryLoadImage(path);
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kOpenImage;
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Quick Save State", "F7")) {
			try {
				s_pQuickState.clear();
				g_sim.CreateSnapshot(~s_pQuickState, nullptr);
			} catch (...) {
				fprintf(stderr, "Quick save state failed\n");
			}
		}
		if (ImGui::MenuItem("Quick Load State", "F8", false, s_pQuickState != nullptr)) {
			try {
				ATStateLoadContext ctx {};
				g_sim.ApplySnapshot(*s_pQuickState, &ctx);
			} catch (...) {
				fprintf(stderr, "Quick load state failed\n");
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Save State As...")) {
			VDStringW path = ATLinuxSaveFileDialog("Save State", kSaveStateFilters);
			if (!path.empty()) {
				try {
					g_sim.SaveState(path.c_str());
				} catch (...) {
					fprintf(stderr, "Save state failed\n");
				}
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kSaveState;
			}
		}
		if (ImGui::MenuItem("Load State...")) {
			VDStringW path = ATLinuxOpenFileDialog("Load State", kSaveStateFilters);
			if (!path.empty()) {
				try {
					g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
				} catch (...) {
					fprintf(stderr, "Load state failed\n");
				}
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kLoadState;
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Cassette..."))
			s_showCassetteControl = true;

		ImGui::Separator();

		if (ImGui::BeginMenu("Disk Drives")) {
			for (int i = 0; i < 4; ++i) {
				char label[32];
				ATDiskInterface& di = g_sim.GetDiskInterface(i);

				if (ImGui::BeginMenu(di.IsDiskLoaded()
					? (snprintf(label, sizeof(label), "D%d: %ls", i + 1,
						VDFileSplitPath(di.GetPath())), label)
					: (snprintf(label, sizeof(label), "D%d: [empty]", i + 1), label)))
				{
					snprintf(label, sizeof(label), "Mount D%d...", i + 1);
					if (ImGui::MenuItem(label)) {
						VDStringW path = ATLinuxOpenFileDialog("Mount Disk", kDiskOnlyFilters);
						if (!path.empty()) {
							TryMountDisk(i, path);
						} else if (ATLinuxFileDialogIsFallbackOpen()) {
							s_pendingDialog = (PendingDialog)((int)PendingDialog::kMountDisk1 + i);
						}
					}

					if (di.IsDiskLoaded()) {
						snprintf(label, sizeof(label), "Unmount D%d", i + 1);
						if (ImGui::MenuItem(label)) {
							di.UnloadDisk();
						}
					}

					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Exit")) {
			// Signal main loop to quit
			SDL_Event quit;
			quit.type = SDL_QUIT;
			SDL_PushEvent(&quit);
		}

		ImGui::EndMenu();
	}

	// --- View menu ---
	if (ImGui::BeginMenu("View")) {
		bool showFPS = ATUIGetShowFPS();
		if (ImGui::MenuItem("Show FPS", nullptr, &showFPS))
			ATUISetShowFPS(showFPS);

		if (ImGui::BeginMenu("Display Filter")) {
			ATDisplayFilterMode curFilter = ATUIGetDisplayFilterMode();
			for (int i = 0; i < 5; ++i) {
				if (ImGui::MenuItem(kDisplayFilterNames[i], nullptr, curFilter == (ATDisplayFilterMode)i)) {
					ATUISetDisplayFilterMode((ATDisplayFilterMode)i);

					// Apply to GL backend
					ATDisplaySDL2 *disp = ATGetLinuxDisplay();
					if (disp) {
						IVDVideoDisplay::FilterMode fm =
							(i == kATDisplayFilterMode_Point)
								? IVDVideoDisplay::kFilterPoint
								: IVDVideoDisplay::kFilterBilinear;
						disp->SetFilterMode(fm);
					}
				}
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();

		bool fullscreen = ATUIGetFullscreen();
		if (ImGui::MenuItem("Fullscreen", "F11", &fullscreen))
			ATSetFullscreen(fullscreen);

		ImGui::EndMenu();
	}

	// --- Speed menu ---
	if (ImGui::BeginMenu("Speed")) {
		bool turbo = ATUIGetTurbo();
		if (ImGui::MenuItem("Turbo", nullptr, &turbo))
			ATUISetTurbo(turbo);

		ImGui::Separator();

		float speed = ATUIGetSpeedModifier();
		ImGui::Text("Speed: %.0f%%", speed * 100.0f);

		if (ImGui::MenuItem("50%"))  ATUISetSpeedModifier(0.5f);
		if (ImGui::MenuItem("100%")) ATUISetSpeedModifier(1.0f);
		if (ImGui::MenuItem("200%")) ATUISetSpeedModifier(2.0f);
		if (ImGui::MenuItem("400%")) ATUISetSpeedModifier(4.0f);

		ImGui::EndMenu();
	}

	// --- Debug menu ---
	if (ImGui::BeginMenu("Debug")) {
		if (dbg) {
			bool running = dbg->IsRunning();

			if (running) {
				if (ImGui::MenuItem("Break", "F5"))
					dbg->Break();
			} else {
				if (ImGui::MenuItem("Run", "F5"))
					dbg->Run(kATDebugSrcMode_Disasm);
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Step Into", "F11", false, !running))
				dbg->StepInto(kATDebugSrcMode_Disasm);
			if (ImGui::MenuItem("Step Over", "F10", false, !running))
				dbg->StepOver(kATDebugSrcMode_Disasm);
			if (ImGui::MenuItem("Step Out", "Shift+F11", false, !running))
				dbg->StepOut(kATDebugSrcMode_Disasm);
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Registers", nullptr, &ATImGuiDebuggerShowRegisters());
			ImGui::MenuItem("Disassembly", nullptr, &ATImGuiDebuggerShowDisassembly());
			ImGui::MenuItem("Memory", nullptr, &ATImGuiDebuggerShowMemory());
			ImGui::MenuItem("Console", nullptr, &ATImGuiDebuggerShowConsole());
			ImGui::MenuItem("Breakpoints", nullptr, &ATImGuiDebuggerShowBreakpoints());
			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}

	// --- Help menu ---
	if (ImGui::BeginMenu("Help")) {
		if (ImGui::MenuItem("About Altirra..."))
			s_showAbout = true;

		ImGui::EndMenu();
	}

	// --- Right-aligned status ---
	{
		ATHardwareMode hw = g_sim.GetHardwareMode();
		ATVideoStandard vs = g_sim.GetVideoStandard();

		const char *hwName = (hw < kATHardwareModeCount) ? kHardwareModeNames[hw] : "?";
		const char *vsName = (vs < kATVideoStandardCount) ? kVideoStandardNames[vs] : "?";

		bool running = dbg && dbg->IsRunning();
		const char *status = running ? "RUNNING" : "STOPPED";
		ImVec4 statusColor = running
			? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
			: ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

		char rightText[64];
		snprintf(rightText, sizeof(rightText), "%s %s   %s", hwName, vsName, status);

		float textW = ImGui::CalcTextSize(rightText).x;
		float barW = ImGui::GetWindowWidth();

		if (barW - textW > 200.0f) {
			ImGui::SameLine(barW - textW - 20.0f);

			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s %s", hwName, vsName);
			ImGui::SameLine(0, 12);
			ImGui::TextColored(statusColor, "%s", status);
		}
	}

	ImGui::EndMainMenuBar();
}

// ============= System Configuration Window =============

static void DrawSystemConfig() {
	if (!s_showSystemConfig)
		return;

	ImGui::SetNextWindowSize(ImVec2(350, 280), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("System Settings", &s_showSystemConfig)) {
		ImGui::End();
		return;
	}

	// Hardware mode
	int hwMode = (int)g_sim.GetHardwareMode();
	ImGui::Text("Hardware:");
	ImGui::SameLine(100);
	ImGui::SetNextItemWidth(180);
	if (ImGui::Combo("##hw", &hwMode, kHardwareModeNames, kATHardwareModeCount))
		g_sim.SetHardwareMode((ATHardwareMode)hwMode);

	// Memory mode
	int memMode = (int)g_sim.GetMemoryMode();
	ImGui::Text("Memory:");
	ImGui::SameLine(100);
	ImGui::SetNextItemWidth(180);
	if (ImGui::Combo("##mem", &memMode, kMemoryModeNames, kATMemoryModeCount))
		g_sim.SetMemoryMode((ATMemoryMode)memMode);

	// Video standard
	int videoStd = (int)g_sim.GetVideoStandard();
	ImGui::Text("Video:");
	ImGui::SameLine(100);
	ImGui::SetNextItemWidth(180);
	if (ImGui::Combo("##vid", &videoStd, kVideoStandardNames, kATVideoStandardCount))
		g_sim.SetVideoStandard((ATVideoStandard)videoStd);

	// BASIC
	bool basicEnabled = g_sim.IsBASICEnabled();
	ImGui::Text("BASIC:");
	ImGui::SameLine(100);
	if (ImGui::Checkbox("##basic", &basicEnabled))
		g_sim.SetBASICEnabled(basicEnabled);

	ImGui::Separator();

	if (ImGui::Button("Apply & Cold Reset", ImVec2(180, 0))) {
		g_sim.ColdReset();
	}

	ImGui::End();
}

// ============= Status Bar =============

static void DrawStatusBar() {
	ImGuiIO& io = ImGui::GetIO();
	float barHeight = ImGui::GetFrameHeight() + 4.0f;

	ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - barHeight));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, barHeight));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoFocusOnAppearing;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 0.95f));

	if (ImGui::Begin("##statusbar", nullptr, flags)) {
		ATHardwareMode hw = g_sim.GetHardwareMode();
		ATVideoStandard vs = g_sim.GetVideoStandard();

		const char *hwName = (hw < kATHardwareModeCount) ? kHardwareModeNames[hw] : "?";
		const char *vsName = (vs < kATVideoStandardCount) ? kVideoStandardNames[vs] : "?";

		ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s %s", hwName, vsName);

		// Disk status
		for (int i = 0; i < 4; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			ImGui::SameLine(0, 16);

			if (di.IsDiskLoaded()) {
				const wchar_t *filename = VDFileSplitPath(di.GetPath());
				VDStringA u8 = VDTextWToU8(VDStringW(filename));
				ImGui::Text("D%d: %s", i + 1, u8.c_str());
			} else {
				ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "D%d: -", i + 1);
			}
		}

		// Cartridge indicator
		if (g_sim.IsCartridgeAttached(0)) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f), "CART");
		}

		// Tape indicator
		{
			ATCassetteEmulator& cas = g_sim.GetCassette();
			if (cas.IsLoaded()) {
				ImGui::SameLine(0, 16);
				if (cas.IsPlayEnabled() && !cas.IsPaused())
					ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "TAPE>");
				else if (cas.IsRecordEnabled() && !cas.IsPaused())
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "TAPE*");
				else
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "TAPE");
			}
		}

		// Turbo indicator
		if (ATUIGetTurbo()) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[TURBO]");
		}

		// FPS counter
		++s_fpsFrameCount;
		double now = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
		if (s_lastFpsTime == 0)
			s_lastFpsTime = now;
		double elapsed = now - s_lastFpsTime;
		if (elapsed >= 0.5) {
			s_currentFps = (float)(s_fpsFrameCount / elapsed);
			s_fpsFrameCount = 0;
			s_lastFpsTime = now;
		}

		if (ATUIGetShowFPS()) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%.1f fps", s_currentFps);
		}
	}
	ImGui::End();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

// ============= About Window =============

static void DrawAbout() {
	if (!s_showAbout)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("About Altirra", &s_showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Altirra (Linux port) 4.40");
	ImGui::Separator();
	ImGui::TextWrapped(
		"Atari 800/800XL/5200 emulator by Avery Lee.\n"
		"Linux port using SDL2, OpenGL, and Dear ImGui.\n"
		"\n"
		"This program is free software under the GNU General "
		"Public License v2 or later."
	);

	ImGui::Separator();
	if (ImGui::Button("Close", ImVec2(120, 0)))
		s_showAbout = false;

	ImGui::End();
}

// ============= File dialog fallback polling =============

static void PollFileDialogFallback() {
	if (s_pendingDialog == PendingDialog::kNone)
		return;

	VDStringW result;
	if (ATLinuxFileDialogDrawFallback(result)) {
		switch (s_pendingDialog) {
			case PendingDialog::kOpenImage:
				TryLoadImage(result);
				break;
			case PendingDialog::kMountDisk1:
			case PendingDialog::kMountDisk2:
			case PendingDialog::kMountDisk3:
			case PendingDialog::kMountDisk4:
				TryMountDisk((int)s_pendingDialog - (int)PendingDialog::kMountDisk1, result);
				break;
			case PendingDialog::kSaveState:
				if (!result.empty()) {
					try { g_sim.SaveState(result.c_str()); }
					catch (...) { fprintf(stderr, "Save state failed\n"); }
				}
				break;
			case PendingDialog::kLoadState:
				if (!result.empty()) {
					try { g_sim.Load(result.c_str(), kATMediaWriteMode_RO, nullptr); }
					catch (...) { fprintf(stderr, "Load state failed\n"); }
				}
				break;
			default:
				break;
		}
		s_pendingDialog = PendingDialog::kNone;
	}

	if (!ATLinuxFileDialogIsFallbackOpen())
		s_pendingDialog = PendingDialog::kNone;
}

// ============= Error Popup =============

static void CheckPendingErrors() {
	// Pop any errors from the thread-safe queue
	auto errors = ATImGuiPopPendingErrors();
	if (!errors.empty() && !s_showErrorPopup) {
		// Show the first error; rest will be shown on subsequent frames
		s_errorTitle = std::move(errors[0].first);
		s_errorMessage = std::move(errors[0].second);
		s_showErrorPopup = true;
		ImGui::OpenPopup("##error_popup");
	}
}

static void DrawErrorPopup() {
	if (!s_showErrorPopup)
		return;

	// Ensure popup is opened
	if (!ImGui::IsPopupOpen("##error_popup"))
		ImGui::OpenPopup("##error_popup");

	ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("##error_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", s_errorTitle.c_str());
		ImGui::Separator();
		ImGui::TextWrapped("%s", s_errorMessage.c_str());
		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			s_showErrorPopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

// ============= Cartridge Browser Window =============

static void CartBuildDisplayModes() {
	s_cartDisplayModes = s_cartDetectedModes;

	if (s_cartShowAll) {
		vdfastvector<bool> present(kATCartridgeModeCount, false);

		present[kATCartridgeMode_None] = true;
		present[kATCartridgeMode_SuperCharger3D] = true;

		for (int m : s_cartDisplayModes) {
			if (m >= 0 && m < (int)kATCartridgeModeCount)
				present[m] = true;
		}

		for (int i = 0; i <= (int)kATCartridgeMapper_Max; ++i) {
			ATCartridgeMode mode = ATGetCartridgeModeForMapper(i);
			if (mode != kATCartridgeMode_None && !present[mode]) {
				present[mode] = true;
				s_cartDisplayModes.push_back(mode);
			}
		}

		for (int i = 1; i < (int)kATCartridgeModeCount; ++i) {
			if (!present[i])
				s_cartDisplayModes.push_back(i);
		}
	}
}

static void DrawCartridgeBrowser() {
	if (!s_showCartridgeBrowser)
		return;

	ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Cartridge Browser", &s_showCartridgeBrowser)) {
		ImGui::End();
		return;
	}

	// Show current cartridge slots
	for (uint32 slot = 0; slot < 2; ++slot) {
		char slotLabel[64];

		if (g_sim.IsCartridgeAttached(slot)) {
			ATCartridgeEmulator *cart = g_sim.GetCartridge(slot);
			const wchar_t *path = cart ? cart->GetPath() : nullptr;
			const char *modeName = cart ? ATGetCartridgeModeName(cart->GetMode()) : "?";

			if (path && *path) {
				VDStringA u8name = VDTextWToU8(VDStringW(VDFileSplitPath(path)));
				snprintf(slotLabel, sizeof(slotLabel), "Slot %u: %s - %s", slot + 1, modeName, u8name.c_str());
			} else {
				snprintf(slotLabel, sizeof(slotLabel), "Slot %u: %s", slot + 1, modeName);
			}

			ImGui::Text("%s", slotLabel);
			ImGui::SameLine();

			char btnId[32];
			snprintf(btnId, sizeof(btnId), "Unload##slot%u", slot);
			if (ImGui::SmallButton(btnId)) {
				g_sim.UnloadCartridge(slot);
			}
		} else {
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Slot %u: [empty]", slot + 1);
		}
	}

	ImGui::Separator();

	// Load buttons
	if (ImGui::Button("Load Cartridge...")) {
		VDStringW path = ATLinuxOpenFileDialog("Load Cartridge", kCartFilters);
		if (!path.empty()) {
			try {
				ATCartLoadContext loadCtx {};
				loadCtx.mbReturnOnUnknownMapper = true;

				if (!g_sim.LoadCartridge(0, path.c_str(), &loadCtx)) {
					if (loadCtx.mLoadStatus == kATCartLoadStatus_UnknownMapper && loadCtx.mpCaptureBuffer) {
						// Raw file with unknown mapper — activate mapper selection
						s_cartLoadBuffer = *loadCtx.mpCaptureBuffer;
						s_cartLoadPath = path;
						s_cartRecommendedIdx = ATCartridgeAutodetectMode(
							s_cartLoadBuffer.data(), (uint32)s_cartLoadBuffer.size(), s_cartDetectedModes);
						s_cartShowAll = false;
						CartBuildDisplayModes();
						s_cartSelectedMapper = s_cartDisplayModes.empty() ? -1 : 0;
						s_cartMapperActive = true;
					}
				}
			} catch (...) {
				fprintf(stderr, "Failed to load cartridge\n");
			}
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("Load Raw Binary...")) {
		VDStringW path = ATLinuxOpenFileDialog("Load Raw Binary", kCartFilters);
		if (!path.empty()) {
			try {
				VDFile f(path.c_str());
				sint64 fileSize = f.size();
				if (fileSize > 0 && fileSize <= 128 * 1024 * 1024) {
					s_cartLoadBuffer.resize((size_t)fileSize);
					f.read(s_cartLoadBuffer.data(), (long)fileSize);
					f.close();

					s_cartLoadPath = path;
					s_cartRecommendedIdx = ATCartridgeAutodetectMode(
						s_cartLoadBuffer.data(), (uint32)s_cartLoadBuffer.size(), s_cartDetectedModes);
					s_cartShowAll = false;
					CartBuildDisplayModes();
					s_cartSelectedMapper = s_cartDisplayModes.empty() ? -1 : 0;
					s_cartMapperActive = true;
				}
			} catch (...) {
				fprintf(stderr, "Failed to read binary file\n");
			}
		}
	}

	// Mapper selection panel
	if (s_cartMapperActive) {
		ImGui::Separator();
		ImGui::Text("Mapper Selection");

		if (ImGui::Checkbox("Show All Mappers", &s_cartShowAll)) {
			CartBuildDisplayModes();
			s_cartSelectedMapper = s_cartDisplayModes.empty() ? -1 : 0;
		}

		ImGui::BeginChild("##mapperlist", ImVec2(0, 200), ImGuiChildFlags_Borders);

		for (int i = 0; i < (int)s_cartDisplayModes.size(); ++i) {
			int mode = s_cartDisplayModes[i];
			int mapper = ATGetCartridgeMapperForMode((ATCartridgeMode)mode, 0);
			const char *name = ATGetCartridgeModeName(mode);
			const char *desc = ATGetCartridgeModeDesc(mode);

			char label[256];
			if (mapper > 0) {
				if ((uint32)i < s_cartRecommendedIdx && s_cartRecommendedIdx == 1)
					snprintf(label, sizeof(label), "#%d  %s (recommended) - %s", mapper, name, desc);
				else if ((uint32)i < s_cartRecommendedIdx)
					snprintf(label, sizeof(label), "#%d  *%s - %s", mapper, name, desc);
				else
					snprintf(label, sizeof(label), "#%d  %s - %s", mapper, name, desc);
			} else {
				if ((uint32)i < s_cartRecommendedIdx && s_cartRecommendedIdx == 1)
					snprintf(label, sizeof(label), "%s (recommended) - %s", name, desc);
				else if ((uint32)i < s_cartRecommendedIdx)
					snprintf(label, sizeof(label), "*%s - %s", name, desc);
				else
					snprintf(label, sizeof(label), "%s - %s", name, desc);
			}

			if (ImGui::Selectable(label, s_cartSelectedMapper == i))
				s_cartSelectedMapper = i;
		}

		ImGui::EndChild();

		if (ImGui::Button("OK", ImVec2(80, 0)) && s_cartSelectedMapper >= 0
			&& s_cartSelectedMapper < (int)s_cartDisplayModes.size())
		{
			try {
				ATCartLoadContext loadCtx {};
				loadCtx.mCartMapper = s_cartDisplayModes[s_cartSelectedMapper];
				g_sim.LoadCartridge(0, s_cartLoadPath.c_str(), &loadCtx);
			} catch (...) {
				fprintf(stderr, "Failed to load cartridge with selected mapper\n");
			}

			s_cartMapperActive = false;
			s_cartLoadBuffer.clear();
			s_cartDetectedModes.clear();
			s_cartDisplayModes.clear();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			s_cartMapperActive = false;
			s_cartLoadBuffer.clear();
			s_cartDetectedModes.clear();
			s_cartDisplayModes.clear();
		}
	}

	ImGui::End();
}

// ============= Firmware Manager Window =============

static void FirmwareRefreshList() {
	ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();
	if (fwMgr) {
		s_fwList.clear();
		fwMgr->GetFirmwareList(s_fwList);
	}
	s_fwListDirty = false;
}

static void DrawFirmwareManager() {
	if (!s_showFirmwareManager)
		return;

	if (s_fwListDirty)
		FirmwareRefreshList();

	ImGui::SetNextWindowSize(ImVec2(650, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Firmware Manager", &s_showFirmwareManager)) {
		ImGui::End();
		return;
	}

	ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();

	// Toolbar
	if (ImGui::Button("Scan Directories")) {
		int addedCount = 0;
		vdvector<VDStringW> searchPaths;
		ATGetFirmwareSearchPaths(searchPaths);

		for (const VDStringW& dirPath : searchPaths) {
			VDStringA u8dir = VDTextWToU8(dirPath);

			VDDirectoryIterator it(VDMakePath(dirPath.c_str(), L"*").c_str());
			while (it.Next()) {
				if (it.IsDirectory())
					continue;

				sint64 fileSize = it.GetSize();
				if (!ATFirmwareAutodetectCheckSize(fileSize))
					continue;

				try {
					VDStringW filePath = VDMakePath(dirPath.c_str(), it.GetName());
					VDFile f(filePath.c_str());
					vdfastvector<uint8> buf((size_t)fileSize);
					f.read(buf.data(), (long)fileSize);
					f.close();

					ATFirmwareInfo info {};
					ATSpecificFirmwareType specificType {};
					sint32 knownIdx = -1;

					ATFirmwareDetection detection = ATFirmwareAutodetect(
						buf.data(), (uint32)buf.size(), info, specificType, knownIdx);

					if (detection != ATFirmwareDetection::None) {
						// Check if already registered
						bool alreadyExists = false;
						for (const ATFirmwareInfo& existing : s_fwList) {
							if (existing.mPath == filePath) {
								alreadyExists = true;
								break;
							}
						}

						if (!alreadyExists) {
							info.mId = kATFirmwareId_Custom + (uint64)rand();
							info.mPath = filePath;
							info.mbVisible = true;
							info.mbAutoselect = true;
							if (info.mName.empty())
								info.mName = VDStringW(it.GetName());

							if (fwMgr)
								fwMgr->AddFirmware(info);
							++addedCount;
						}
					}
				} catch (...) {
					// Skip files that can't be read
				}
			}
		}

		char msg[128];
		snprintf(msg, sizeof(msg), "Scan complete: %d new firmware image(s) found.", addedCount);
		s_fwScanResult = msg;
		s_fwShowScanResult = true;
		s_fwListDirty = true;
	}

	ImGui::SameLine();

	if (ImGui::Button("Add File...")) {
		VDStringW path = ATLinuxOpenFileDialog("Add Firmware", kFirmwareFilters);
		if (!path.empty()) {
			try {
				VDFile f(path.c_str());
				sint64 fileSize = f.size();
				vdfastvector<uint8> buf((size_t)fileSize);
				f.read(buf.data(), (long)fileSize);
				f.close();

				ATFirmwareInfo info {};
				ATSpecificFirmwareType specificType {};
				sint32 knownIdx = -1;

				ATFirmwareDetection detection = ATFirmwareAutodetect(
					buf.data(), (uint32)buf.size(), info, specificType, knownIdx);

				if (detection == ATFirmwareDetection::None) {
					info.mType = kATFirmwareType_Unknown;
				}

				info.mId = kATFirmwareId_Custom + (uint64)rand();
				info.mPath = path;
				info.mbVisible = true;
				info.mbAutoselect = true;
				if (info.mName.empty())
					info.mName = VDStringW(VDFileSplitPath(path.c_str()));

				if (fwMgr)
					fwMgr->AddFirmware(info);
				s_fwListDirty = true;
			} catch (...) {
				fprintf(stderr, "Failed to add firmware file\n");
			}
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("Remove") && s_fwSelectedId != 0) {
		if (fwMgr)
			fwMgr->RemoveFirmware(s_fwSelectedId);
		s_fwSelectedId = 0;
		s_fwListDirty = true;
	}

	if (s_fwShowScanResult) {
		ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", s_fwScanResult.c_str());
	}

	// Re-fetch if dirty after toolbar actions
	if (s_fwListDirty)
		FirmwareRefreshList();

	// Firmware list table
	ImGui::Separator();

	if (ImGui::BeginTable("##fwtable", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, 200)))
	{
		ImGui::TableSetupColumn("Name", 0, 2.0f);
		ImGui::TableSetupColumn("Type", 0, 1.0f);
		ImGui::TableSetupColumn("Path", 0, 2.0f);
		ImGui::TableHeadersRow();

		for (const ATFirmwareInfo& fw : s_fwList) {
			if (!fw.mbVisible)
				continue;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			VDStringA u8name = VDTextWToU8(fw.mName);
			bool selected = (s_fwSelectedId == fw.mId);

			if (ImGui::Selectable(u8name.c_str(), selected,
				ImGuiSelectableFlags_SpanAllColumns))
			{
				s_fwSelectedId = fw.mId;
			}

			ImGui::TableNextColumn();
			ImGui::Text("%s", ATGetFirmwareTypeDisplayName(fw.mType));

			ImGui::TableNextColumn();
			VDStringA u8path = VDTextWToU8(fw.mPath);
			ImGui::Text("%s", u8path.c_str());
		}

		ImGui::EndTable();
	}

	// Defaults section
	ImGui::Separator();
	ImGui::Text("Default Firmware:");

	struct DefaultEntry {
		const char *label;
		ATFirmwareType type;
	};

	static const DefaultEntry kDefaults[] = {
		{ "400/800 Kernel", kATFirmwareType_Kernel800_OSB },
		{ "XL/XE Kernel",   kATFirmwareType_KernelXL },
		{ "BASIC",          kATFirmwareType_Basic },
		{ "5200 OS",        kATFirmwareType_Kernel5200 },
		{ "XEGS OS",        kATFirmwareType_KernelXEGS },
	};

	for (const DefaultEntry& def : kDefaults) {
		uint64 currentDefault = fwMgr ? fwMgr->GetDefaultFirmware(def.type) : 0;

		ImGui::Text("%s:", def.label);
		ImGui::SameLine(140);
		ImGui::SetNextItemWidth(300);

		// Build combo items: "Built-in" + matching firmware
		char comboId[64];
		snprintf(comboId, sizeof(comboId), "##fwdef_%u", (unsigned)def.type);

		// Find current display name
		const char *currentName = "Built-in HLE";
		for (const ATFirmwareInfo& fw : s_fwList) {
			if (fw.mId == currentDefault) {
				// Use a static buffer for the preview
				static VDStringA s_previewBuf;
				s_previewBuf = VDTextWToU8(fw.mName);
				currentName = s_previewBuf.c_str();
				break;
			}
		}

		if (ImGui::BeginCombo(comboId, currentName)) {
			// Built-in option
			if (ImGui::Selectable("Built-in HLE", currentDefault == 0)) {
				if (fwMgr)
					fwMgr->SetDefaultFirmware(def.type, 0);
			}

			// Matching firmware entries
			for (const ATFirmwareInfo& fw : s_fwList) {
				if (fw.mType != def.type || !fw.mbVisible)
					continue;

				VDStringA u8name = VDTextWToU8(fw.mName);
				bool isSelected = (fw.mId == currentDefault);

				if (ImGui::Selectable(u8name.c_str(), isSelected)) {
					if (fwMgr)
						fwMgr->SetDefaultFirmware(def.type, fw.mId);
				}
			}

			ImGui::EndCombo();
		}
	}

	ImGui::End();
}

// ============= Audio Options Window =============

static void DrawAudioOptions() {
	if (!s_showAudioOptions)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Audio Options", &s_showAudioOptions)) {
		ImGui::End();
		return;
	}

	IATAudioOutput *audioOut = g_sim.GetAudioOutput();

	// Volume controls
	if (ImGui::CollapsingHeader("Volume", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (audioOut) {
			float masterVol = audioOut->GetVolume();
			ImGui::Text("Master Volume:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderFloat("##mastervol", &masterVol, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
				audioOut->SetVolume(masterVol);
			}

			bool mute = audioOut->GetMute();
			ImGui::SameLine();
			if (ImGui::Checkbox("Mute", &mute))
				audioOut->SetMute(mute);

			float driveVol = audioOut->GetMixLevel(kATAudioMix_Drive);
			ImGui::Text("Drive Volume:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderFloat("##drivevol", &driveVol, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
				audioOut->SetMixLevel(kATAudioMix_Drive, driveVol);
			}

			float covoxVol = audioOut->GetMixLevel(kATAudioMix_Covox);
			ImGui::Text("Covox Volume:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderFloat("##covoxvol", &covoxVol, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
				audioOut->SetMixLevel(kATAudioMix_Covox, covoxVol);
			}
		} else {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Audio output not available");
		}
	}

	// Latency
	if (ImGui::CollapsingHeader("Buffering")) {
		if (audioOut) {
			int latency = audioOut->GetLatency();
			ImGui::Text("Latency:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderInt("##latency", &latency, 10, 500, "%d ms")) {
				audioOut->SetLatency(latency);
			}

			int extraBuf = audioOut->GetExtraBuffer();
			ImGui::Text("Extra Buffer:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderInt("##extrabuf", &extraBuf, 20, 500, "%d ms")) {
				audioOut->SetExtraBuffer(extraBuf);
			}
		}
	}

	// POKEY options
	if (ImGui::CollapsingHeader("POKEY Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		ATPokeyEmulator& pokey = g_sim.GetPokey();

		bool stereoMono = pokey.IsStereoAsMonoEnabled();
		if (ImGui::Checkbox("Downmix stereo to mono", &stereoMono))
			pokey.SetStereoAsMonoEnabled(stereoMono);

		bool nonlinear = pokey.IsNonlinearMixingEnabled();
		if (ImGui::Checkbox("Non-linear mixing", &nonlinear))
			pokey.SetNonlinearMixingEnabled(nonlinear);

		bool speakerFilter = pokey.IsSpeakerFilterEnabled();
		if (ImGui::Checkbox("Speaker filter", &speakerFilter))
			pokey.SetSpeakerFilterEnabled(speakerFilter);

		bool serialNoise = pokey.IsSerialNoiseEnabled();
		if (ImGui::Checkbox("Serial noise", &serialNoise))
			pokey.SetSerialNoiseEnabled(serialNoise);
	}

	// Audio status
	if (ImGui::CollapsingHeader("Audio Status")) {
		if (audioOut) {
			ATUIAudioStatus status = audioOut->GetAudioStatus();

			ImGui::Text("Sampling Rate:  %.1f Hz", status.mSamplingRate);
			ImGui::Text("Incoming Rate:  %.1f Hz", status.mIncomingRate);
			ImGui::Text("Expected Rate:  %.1f Hz", status.mExpectedRate);
			ImGui::Text("Buffer Range:   %d - %d (target: %d - %d)",
				status.mMeasuredMin, status.mMeasuredMax,
				status.mTargetMin, status.mTargetMax);
			ImGui::Text("Underflows: %d  Overflows: %d  Drops: %d",
				status.mUnderflowCount, status.mOverflowCount, status.mDropCount);
			ImGui::Text("Stereo Mixing:  %s", status.mbStereoMixing ? "Yes" : "No");
		}
	}

	ImGui::End();
}

// ============= Device Configuration Window =============

// Generic property set editor - renders ImGui controls for each property
static bool DrawPropertySetEditor(ATPropertySet& props) {
	bool changed = false;

	struct PropEntry {
		std::string name;
		ATPropertyType type;
		ATPropertyValue value;
	};

	// Collect properties into a sortable list
	std::vector<PropEntry> entries;
	props.EnumProperties([&](const char *name, const ATPropertyValue& val) {
		PropEntry e;
		e.name = name;
		e.type = val.mType;
		e.value = val;
		// For string type, deep copy
		if (val.mType == kATPropertyType_String16 && val.mValStr16) {
			// Store pointer directly; it's valid for the editor's lifetime
			e.value.mValStr16 = val.mValStr16;
		}
		entries.push_back(std::move(e));
	});

	// Sort alphabetically
	std::sort(entries.begin(), entries.end(), [](const PropEntry& a, const PropEntry& b) {
		return a.name < b.name;
	});

	for (const PropEntry& e : entries) {
		ImGui::PushID(e.name.c_str());

		switch (e.type) {
			case kATPropertyType_Bool: {
				bool val = e.value.mValBool;
				if (ImGui::Checkbox(e.name.c_str(), &val)) {
					props.SetBool(e.name.c_str(), val);
					changed = true;
				}
				break;
			}

			case kATPropertyType_Int32: {
				int val = (int)e.value.mValI32;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt(e.name.c_str(), &val)) {
					props.SetInt32(e.name.c_str(), (sint32)val);
					changed = true;
				}
				break;
			}

			case kATPropertyType_Uint32: {
				int val = (int)e.value.mValU32;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt(e.name.c_str(), &val)) {
					if (val >= 0) {
						props.SetUint32(e.name.c_str(), (uint32)val);
						changed = true;
					}
				}
				break;
			}

			case kATPropertyType_Float: {
				float val = e.value.mValF;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputFloat(e.name.c_str(), &val, 0, 0, "%.3f")) {
					props.SetFloat(e.name.c_str(), val);
					changed = true;
				}
				break;
			}

			case kATPropertyType_Double: {
				float val = (float)e.value.mValD;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputFloat(e.name.c_str(), &val, 0, 0, "%.6f")) {
					props.SetDouble(e.name.c_str(), (double)val);
					changed = true;
				}
				break;
			}

			case kATPropertyType_String16: {
				VDStringA u8val;
				if (e.value.mValStr16)
					u8val = VDTextWToU8(VDStringW(e.value.mValStr16));

				char buf[512];
				strncpy(buf, u8val.c_str(), sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = 0;

				ImGui::SetNextItemWidth(300);
				if (ImGui::InputText(e.name.c_str(), buf, sizeof(buf))) {
					VDStringW wstr = VDTextU8ToW(VDStringA(buf));
					props.SetString(e.name.c_str(), wstr.c_str());
					changed = true;
				}
				break;
			}

			default:
				ImGui::Text("%s: <unsupported type>", e.name.c_str());
				break;
		}

		ImGui::PopID();
	}

	if (entries.empty()) {
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No configurable properties.");
	}

	return changed;
}

static void DrawDeviceManager() {
	if (!s_showDeviceManager)
		return;

	ATDeviceManager *devMgr = g_sim.GetDeviceManager();
	if (!devMgr) {
		s_showDeviceManager = false;
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Device Manager", &s_showDeviceManager)) {
		ImGui::End();
		return;
	}

	// Toolbar
	if (ImGui::Button("Configure...") && s_devSelectedDevice) {
		ATDeviceInfo info;
		s_devSelectedDevice->GetDeviceInfo(info);
		s_devEditTag = info.mpDef ? info.mpDef->mpTag : "";
		s_devEditProps.Clear();
		s_devSelectedDevice->GetSettings(s_devEditProps);
		s_devShowConfig = true;
	}

	ImGui::SameLine();

	if (ImGui::Button("Remove") && s_devSelectedDevice) {
		devMgr->RemoveDevice(s_devSelectedDevice);
		s_devSelectedDevice = nullptr;
		s_devShowConfig = false;
	}

	// Device list
	ImGui::Separator();

	uint32 devCount = devMgr->GetDeviceCount();

	if (ImGui::BeginTable("##devtable", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, 200)))
	{
		ImGui::TableSetupColumn("Device", 0, 2.0f);
		ImGui::TableSetupColumn("Type", 0, 1.5f);
		ImGui::TableSetupColumn("Settings", 0, 2.0f);
		ImGui::TableHeadersRow();

		for (uint32 i = 0; i < devCount; ++i) {
			IATDevice *dev = devMgr->GetDeviceByIndex(i);
			if (!dev)
				continue;

			ATDeviceInfo info;
			dev->GetDeviceInfo(info);

			VDStringW blurb;
			dev->GetSettingsBlurb(blurb);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			const wchar_t *devName = info.mpDef ? info.mpDef->mpName : L"Unknown";
			VDStringA u8name = VDTextWToU8(VDStringW(devName));
			bool selected = (s_devSelectedDevice == dev);

			if (ImGui::Selectable(u8name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
				s_devSelectedDevice = dev;

			ImGui::TableNextColumn();
			ImGui::Text("%s", info.mpDef ? info.mpDef->mpTag : "?");

			ImGui::TableNextColumn();
			VDStringA u8blurb = VDTextWToU8(blurb);
			ImGui::Text("%s", u8blurb.c_str());
		}

		ImGui::EndTable();
	}

	// Device configuration editor (inline)
	if (s_devShowConfig && s_devSelectedDevice) {
		ImGui::Separator();

		ATDeviceInfo info;
		s_devSelectedDevice->GetDeviceInfo(info);
		const wchar_t *devName = info.mpDef ? info.mpDef->mpName : L"Device";
		VDStringA u8name = VDTextWToU8(VDStringW(devName));

		ImGui::Text("Configure: %s", u8name.c_str());

		ImGui::BeginChild("##devcfg", ImVec2(0, 150), ImGuiChildFlags_Borders);
		DrawPropertySetEditor(s_devEditProps);
		ImGui::EndChild();

		if (ImGui::Button("Apply")) {
			devMgr->ReconfigureDevice(*s_devSelectedDevice, s_devEditProps);
			s_devShowConfig = false;
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
			s_devShowConfig = false;
	}

	ImGui::End();
}

// ============= Keyboard Configuration Window =============

static void DrawKeyboardConfig() {
	if (!s_showKeyboardConfig)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Keyboard Settings", &s_showKeyboardConfig)) {
		ImGui::End();
		return;
	}

	// Layout mode
	if (ImGui::CollapsingHeader("Layout Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
		int layoutMode = (int)g_kbdOpts.mLayoutMode;
		bool changed = false;

		changed |= ImGui::RadioButton("Natural", &layoutMode, ATUIKeyboardOptions::kLM_Natural);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Direct (Raw)", &layoutMode, ATUIKeyboardOptions::kLM_Raw);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Custom", &layoutMode, ATUIKeyboardOptions::kLM_Custom);

		if (changed) {
			g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)layoutMode;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		}

		ImGui::TextWrapped(
			layoutMode == ATUIKeyboardOptions::kLM_Natural
				? "Host keyboard layout mapped to logical Atari keys."
			: layoutMode == ATUIKeyboardOptions::kLM_Raw
				? "Physical scancodes map directly to Atari keyboard matrix."
				: "User-customizable per-key mappings."
		);
	}

	// Keyboard mode
	if (ImGui::CollapsingHeader("Keyboard Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
		int keyMode = g_kbdOpts.mbFullRawKeys ? 2 : (g_kbdOpts.mbRawKeys ? 1 : 0);
		bool changed = false;

		changed |= ImGui::RadioButton("Cooked", &keyMode, 0);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Raw", &keyMode, 1);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Full Raw", &keyMode, 2);

		if (changed) {
			g_kbdOpts.mbRawKeys = (keyMode >= 1);
			g_kbdOpts.mbFullRawKeys = (keyMode >= 2);
		}
	}

	// Arrow key mode
	if (ImGui::CollapsingHeader("Arrow Key Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
		int arrowMode = (int)g_kbdOpts.mArrowKeyMode;
		bool changed = false;

		changed |= ImGui::RadioButton("Invert Ctrl", &arrowMode, ATUIKeyboardOptions::kAKM_InvertCtrl);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Auto Ctrl", &arrowMode, ATUIKeyboardOptions::kAKM_AutoCtrl);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Default Ctrl", &arrowMode, ATUIKeyboardOptions::kAKM_DefaultCtrl);

		if (changed) {
			g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)arrowMode;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		}
	}

	// Toggles
	if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox("1200XL function keys (F1-F4)", &g_kbdOpts.mbEnableFunctionKeys))
			ATUIInitVirtualKeyMap(g_kbdOpts);

		ImGui::Checkbox("Allow Shift on cold reset", &g_kbdOpts.mbAllowShiftOnColdReset);
		ImGui::Checkbox("Allow input map keyboard overlap", &g_kbdOpts.mbAllowInputMapOverlap);
		ImGui::Checkbox("Allow input map modifier overlap", &g_kbdOpts.mbAllowInputMapModifierOverlap);
	}

	ImGui::End();
}

// ============= Boot & Acceleration Options =============

static void DrawBootOptions() {
	if (!s_showBootOptions)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 420), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Boot & Acceleration", &s_showBootOptions)) {
		ImGui::End();
		return;
	}

	// Boot options
	if (ImGui::CollapsingHeader("Boot Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool fastBoot = g_sim.IsFastBootEnabled();
		if (ImGui::Checkbox("Fast boot", &fastBoot))
			g_sim.SetFastBootEnabled(fastBoot);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Skip memory test and OS initialization delay");

		bool selfTest = g_sim.IsForcedSelfTest();
		if (ImGui::Checkbox("Force self-test on cold reset", &selfTest))
			g_sim.SetForcedSelfTest(selfTest);

		bool kbdPresent = g_sim.IsKeyboardPresent();
		if (ImGui::Checkbox("Keyboard present", &kbdPresent))
			g_sim.SetKeyboardPresent(kbdPresent);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Force keyboard present line (needed for some 5200 games)");
	}

	// SIO acceleration
	if (ImGui::CollapsingHeader("SIO Acceleration", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool sioPatch = g_sim.IsSIOPatchEnabled();
		if (ImGui::Checkbox("SIO patch (OS acceleration)", &sioPatch))
			g_sim.SetSIOPatchEnabled(sioPatch);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Accelerate SIO transfers through OS hooks");

		bool diskSIO = g_sim.IsDiskSIOPatchEnabled();
		if (ImGui::Checkbox("Disk SIO patch", &diskSIO))
			g_sim.SetDiskSIOPatchEnabled(diskSIO);

		bool diskBurst = g_sim.GetDiskBurstTransfersEnabled();
		if (ImGui::Checkbox("Disk burst transfers", &diskBurst))
			g_sim.SetDiskBurstTransfersEnabled(diskBurst);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Use burst mode for faster disk access");

		bool diskAccurate = g_sim.IsDiskAccurateTimingEnabled();
		if (ImGui::Checkbox("Accurate disk timing", &diskAccurate))
			g_sim.SetDiskAccurateTimingEnabled(diskAccurate);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Enable cycle-accurate disk drive timing");

		bool sioOverride = g_sim.IsDiskSIOOverrideDetectEnabled();
		if (ImGui::Checkbox("SIO override detection", &sioOverride))
			g_sim.SetDiskSIOOverrideDetectEnabled(sioOverride);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Detect when programs hook SIO and disable acceleration");
	}

	// Other patches
	if (ImGui::CollapsingHeader("Acceleration Patches")) {
		bool fpPatch = g_sim.IsFPPatchEnabled();
		if (ImGui::Checkbox("Floating-point acceleration", &fpPatch))
			g_sim.SetFPPatchEnabled(fpPatch);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Accelerate math pack routines");

		bool cioPBI = g_sim.IsCIOPBIPatchEnabled();
		if (ImGui::Checkbox("CIO PBI patch", &cioPBI))
			g_sim.SetCIOPBIPatchEnabled(cioPBI);

		bool sioPBI = g_sim.IsSIOPBIPatchEnabled();
		if (ImGui::Checkbox("SIO PBI patch", &sioPBI))
			g_sim.SetSIOPBIPatchEnabled(sioPBI);

		bool devSIO = g_sim.GetDeviceSIOPatchEnabled();
		if (ImGui::Checkbox("Device SIO patch", &devSIO))
			g_sim.SetDeviceSIOPatchEnabled(devSIO);

		bool devCIOBurst = g_sim.GetDeviceCIOBurstTransfersEnabled();
		if (ImGui::Checkbox("Device CIO burst transfers", &devCIOBurst))
			g_sim.SetDeviceCIOBurstTransfersEnabled(devCIOBurst);

		bool devSIOBurst = g_sim.GetDeviceSIOBurstTransfersEnabled();
		if (ImGui::Checkbox("Device SIO burst transfers", &devSIOBurst))
			g_sim.SetDeviceSIOBurstTransfersEnabled(devSIOBurst);

		bool sectorCounter = g_sim.IsDiskSectorCounterEnabled();
		if (ImGui::Checkbox("Disk sector counter", &sectorCounter))
			g_sim.SetDiskSectorCounterEnabled(sectorCounter);
	}

	ImGui::End();
}

// ============= Cassette Control Window =============

static void FormatTapeTime(char *buf, size_t bufLen, float seconds) {
	int mins = (int)(seconds / 60.0f);
	float secs = seconds - mins * 60.0f;
	snprintf(buf, bufLen, "%d:%05.2f", mins, secs);
}

static void DrawCassetteControl() {
	if (!s_showCassetteControl)
		return;

	ATCassetteEmulator& cas = g_sim.GetCassette();

	ImGui::SetNextWindowSize(ImVec2(460, 340), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Cassette Control", &s_showCassetteControl)) {
		ImGui::End();
		return;
	}

	// Load/New/Unload/Save toolbar
	if (ImGui::Button("Load Tape...")) {
		VDStringW path = ATLinuxOpenFileDialog("Load Tape", kTapeFilters);
		if (!path.empty()) {
			try {
				cas.Load(path.c_str());
			} catch (...) {
				fprintf(stderr, "Failed to load tape image\n");
			}
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("New Tape")) {
		cas.LoadNew();
	}

	ImGui::SameLine();

	if (ImGui::Button("Unload") && cas.IsLoaded()) {
		cas.Unload();
	}

	ImGui::SameLine();

	if (ImGui::Button("Save As...") && cas.IsLoaded()) {
		VDStringW path = ATLinuxSaveFileDialog("Save Tape", kTapeFilters);
		if (!path.empty()) {
			try {
				cas.SetImagePersistent(path.c_str());
				cas.SetImageClean();
			} catch (...) {
				fprintf(stderr, "Failed to save tape image\n");
			}
		}
	}

	// Current tape info
	ImGui::Separator();

	if (cas.IsLoaded()) {
		const wchar_t *tapePath = cas.GetPath();
		if (tapePath && *tapePath) {
			VDStringA u8path = VDTextWToU8(VDStringW(VDFileSplitPath(tapePath)));
			ImGui::Text("File: %s", u8path.c_str());
		} else {
			ImGui::Text("File: [new tape]");
		}

		if (cas.IsImageDirty())
			ImGui::SameLine(), ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(modified)");
	} else {
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No tape loaded");
	}

	// Position display and scrubber
	float pos = cas.GetPosition();
	float len = cas.GetLength();

	char posStr[32], lenStr[32];
	FormatTapeTime(posStr, sizeof(posStr), pos);
	FormatTapeTime(lenStr, sizeof(lenStr), len);

	ImGui::Text("Position: %s / %s", posStr, lenStr);

	if (cas.IsLoaded() && len > 0) {
		float scrubPos = pos;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##tapepos", &scrubPos, 0.0f, len, "")) {
			cas.SeekToTime(scrubPos);
		}
	}

	// Transport controls
	ImGui::Separator();

	bool loaded = cas.IsLoaded();
	bool playing = cas.IsPlayEnabled();
	bool recording = cas.IsRecordEnabled();
	bool paused = cas.IsPaused();
	bool stopped = cas.IsStopped();

	// Rewind
	if (ImGui::Button("Rewind") && loaded)
		cas.RewindToStart();

	ImGui::SameLine();

	// Play
	if (playing && !paused) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
		if (ImGui::Button("Play"))
			cas.SetPaused(true);
		ImGui::PopStyleColor();
	} else {
		if (ImGui::Button("Play") && loaded) {
			if (paused)
				cas.SetPaused(false);
			else
				cas.Play();
		}
	}

	ImGui::SameLine();

	// Record
	if (recording && !paused) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
		if (ImGui::Button("Record"))
			cas.SetPaused(true);
		ImGui::PopStyleColor();
	} else {
		if (ImGui::Button("Record") && loaded) {
			if (paused && recording)
				cas.SetPaused(false);
			else
				cas.Record();
		}
	}

	ImGui::SameLine();

	// Stop
	if (ImGui::Button("Stop") && loaded && !stopped)
		cas.Stop();

	ImGui::SameLine();

	// Skip forward
	if (ImGui::Button("+10s") && loaded)
		cas.SkipForward(10.0f);

	// Status indicators
	{
		ImGui::SameLine(0, 16);

		if (playing && !paused)
			ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "PLAY");
		else if (recording && !paused)
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC");
		else if (paused)
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "PAUSED");
		else
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "STOP");

		// Motor indicator
		if (cas.IsMotorRunning()) {
			ImGui::SameLine(0, 12);
			ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "MOTOR");
		}
	}

	// Tape options
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Options")) {
		// Turbo mode
		int turboMode = (int)cas.GetTurboMode();
		ImGui::Text("Turbo Mode:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(200);
		if (ImGui::Combo("##turbomode", &turboMode, kCassetteTurboModeNames, kATCassetteTurboMode_Always + 1))
			cas.SetTurboMode((ATCassetteTurboMode)turboMode);

		bool autoRewind = cas.IsAutoRewindEnabled();
		if (ImGui::Checkbox("Auto-rewind on load", &autoRewind))
			cas.SetAutoRewindEnabled(autoRewind);

		bool loadAsAudio = cas.IsLoadDataAsAudioEnabled();
		if (ImGui::Checkbox("Load data as audio", &loadAsAudio))
			cas.SetLoadDataAsAudioEnable(loadAsAudio);
	}

	ImGui::End();
}

// ============= Video Configuration Window =============

static void DrawVideoConfig() {
	if (!s_showVideoConfig)
		return;

	ImGui::SetNextWindowSize(ImVec2(500, 550), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Video Settings", &s_showVideoConfig)) {
		ImGui::End();
		return;
	}

	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	// Display options
	if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
		// Display filter (already in View menu, but also here for convenience)
		int filterMode = (int)ATUIGetDisplayFilterMode();
		ImGui::Text("Filter:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##filter", &filterMode, kDisplayFilterNames, 5)) {
			ATUISetDisplayFilterMode((ATDisplayFilterMode)filterMode);

			ATDisplaySDL2 *disp = ATGetLinuxDisplay();
			if (disp) {
				IVDVideoDisplay::FilterMode fm =
					(filterMode == kATDisplayFilterMode_Point)
						? IVDVideoDisplay::kFilterPoint
						: IVDVideoDisplay::kFilterBilinear;
				disp->SetFilterMode(fm);
			}
		}

		// Stretch mode
		int stretchMode = (int)ATUIGetDisplayStretchMode();
		ImGui::Text("Stretch:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##stretch", &stretchMode, kStretchModeNames, kATDisplayStretchModeCount))
			ATUISetDisplayStretchMode((ATDisplayStretchMode)stretchMode);

		// Overscan
		int osMode = (int)gtia.GetOverscanMode();
		ImGui::Text("H Overscan:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##hoverscan", &osMode, kOverscanModeNames, ATGTIAEmulator::kOverscanCount))
			gtia.SetOverscanMode((ATGTIAEmulator::OverscanMode)osMode);

		int vosMode = (int)gtia.GetVerticalOverscanMode();
		ImGui::Text("V Overscan:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##voverscan", &vosMode, kVertOverscanModeNames, ATGTIAEmulator::kVerticalOverscanCount))
			gtia.SetVerticalOverscanMode((ATGTIAEmulator::VerticalOverscanMode)vosMode);
	}

	// Artifacting
	if (ImGui::CollapsingHeader("Artifacting", ImGuiTreeNodeFlags_DefaultOpen)) {
		int artMode = (int)gtia.GetArtifactingMode();
		ImGui::Text("Mode:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##artifact", &artMode, kArtifactModeNames, (int)ATArtifactMode::Count))
			gtia.SetArtifactingMode((ATArtifactMode)artMode);

		// Monitor mode
		int monMode = (int)gtia.GetMonitorMode();
		ImGui::Text("Monitor:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##monitor", &monMode, kMonitorModeNames, (int)ATMonitorMode::Count))
			gtia.SetMonitorMode((ATMonitorMode)monMode);

		// Scanlines / interlace
		bool scanlines = gtia.AreScanlinesEnabled();
		if (ImGui::Checkbox("Scanlines", &scanlines))
			gtia.SetScanlinesEnabled(scanlines);

		bool interlace = gtia.IsInterlaceEnabled();
		if (ImGui::Checkbox("Interlace", &interlace))
			gtia.SetInterlaceEnabled(interlace);

		if (interlace) {
			int deinterlace = (int)gtia.GetDeinterlaceMode();
			const char *deinterlaceNames[] = { "None", "Adaptive Bob" };
			ImGui::Text("Deinterlace:");
			ImGui::SameLine(120);
			ImGui::SetNextItemWidth(220);
			if (ImGui::Combo("##deinterlace", &deinterlace, deinterlaceNames, 2))
				gtia.SetDeinterlaceMode((ATVideoDeinterlaceMode)deinterlace);
		}
	}

	// Color adjustment
	if (ImGui::CollapsingHeader("Color Adjustment")) {
		ATColorSettings cs = gtia.GetColorSettings();
		bool isPAL = gtia.IsPALMode();
		ATColorParams& params = isPAL ? cs.mPALParams : cs.mNTSCParams;
		bool changed = false;

		ImGui::Text("Editing: %s parameters", isPAL ? "PAL" : "NTSC");

		bool usePALParams = cs.mbUsePALParams;
		if (ImGui::Checkbox("Use separate PAL parameters", &usePALParams)) {
			cs.mbUsePALParams = usePALParams;
			changed = true;
		}

		ImGui::Separator();

		// Presets
		uint32 presetCount = ATGetColorPresetCount();
		if (presetCount > 0 && ImGui::BeginCombo("##preset", "Load Preset...")) {
			for (uint32 i = 0; i < presetCount; ++i) {
				VDStringA name = VDTextWToU8(VDStringW(ATGetColorPresetNameByIndex(i)));
				if (ImGui::Selectable(name.c_str())) {
					params = ATGetColorPresetByIndex(i);
					changed = true;
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Hue Start", &params.mHueStart, -60.0f, 60.0f, "%.1f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Hue Range", &params.mHueRange, 160.0f, 360.0f, "%.1f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Brightness", &params.mBrightness, -0.5f, 0.5f, "%.3f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Contrast", &params.mContrast, 0.0f, 2.0f, "%.3f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Saturation", &params.mSaturation, 0.0f, 1.0f, "%.3f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Gamma", &params.mGammaCorrect, 0.5f, 3.0f, "%.2f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Intensity", &params.mIntensityScale, 0.5f, 2.0f, "%.2f");

		// Color matching
		int colorMatch = (int)params.mColorMatchingMode;
		ImGui::SetNextItemWidth(200);
		if (ImGui::Combo("Color Matching", &colorMatch, kColorMatchingNames, 5)) {
			params.mColorMatchingMode = (ATColorMatchingMode)colorMatch;
			changed = true;
		}

		// Luma ramp
		int lumaRamp = (int)params.mLumaRampMode;
		ImGui::SetNextItemWidth(200);
		if (ImGui::Combo("Luma Ramp", &lumaRamp, kLumaRampNames, kATLumaRampModeCount)) {
			params.mLumaRampMode = (ATLumaRampMode)lumaRamp;
			changed = true;
		}

		if (ImGui::TreeNode("Artifact Tuning")) {
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Artifact Hue", &params.mArtifactHue, -180.0f, 180.0f, "%.1f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Artifact Sat", &params.mArtifactSat, 0.0f, 2.0f, "%.3f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Artifact Sharpness", &params.mArtifactSharpness, 0.0f, 1.0f, "%.3f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("RGB Correction")) {
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Red Shift", &params.mRedShift, -30.0f, 30.0f, "%.1f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Red Scale", &params.mRedScale, 0.5f, 2.0f, "%.3f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Green Shift", &params.mGrnShift, -30.0f, 30.0f, "%.1f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Green Scale", &params.mGrnScale, 0.5f, 2.0f, "%.3f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Blue Shift", &params.mBluShift, -30.0f, 30.0f, "%.1f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Blue Scale", &params.mBluScale, 0.5f, 2.0f, "%.3f");
			ImGui::TreePop();
		}

		bool palQuirks = params.mbUsePALQuirks;
		if (ImGui::Checkbox("PAL quirks", &palQuirks)) {
			params.mbUsePALQuirks = palQuirks;
			changed = true;
		}

		if (changed)
			gtia.SetColorSettings(cs);

		if (ImGui::Button("Reset to Defaults")) {
			ATColorSettings defaults = gtia.GetDefaultColorSettings();
			gtia.SetColorSettings(defaults);
		}
	}

	ImGui::End();
}

// ============= Public API =============

void ATImGuiEmulatorInit() {
	// No init needed yet
}

void ATImGuiEmulatorDraw() {
	DrawMenuBar();
	DrawSystemConfig();
	DrawBootOptions();
	DrawCassetteControl();
	DrawCartridgeBrowser();
	DrawFirmwareManager();
	DrawAudioOptions();
	DrawVideoConfig();
	DrawKeyboardConfig();
	DrawDeviceManager();
	DrawStatusBar();
	DrawAbout();
	PollFileDialogFallback();
	CheckPendingErrors();
	DrawErrorPopup();

	// Draw debugger windows (without toolbar — menu bar handles that now)
	ATImGuiDebuggerDrawWindows();
}

void ATImGuiEmulatorShutdown() {
	s_pQuickState.clear();
	s_cartLoadBuffer.clear();
	s_cartDetectedModes.clear();
	s_cartDisplayModes.clear();
	s_fwList.clear();
	s_devSelectedDevice = nullptr;
	s_devEditProps.Clear();
}
