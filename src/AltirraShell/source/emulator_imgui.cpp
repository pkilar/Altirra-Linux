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
#include "diskinterface.h"
#include "cartridge.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "debugger.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

// Forward declarations for functions defined in cartdetect.cpp (compiled in Linux build)
uint32 ATCartridgeAutodetectMode(const void *data, uint32 size, vdfastvector<int>& cartModes);

// Firmware search path accessor (defined in main_linux.cpp)
void ATGetFirmwareSearchPaths(vdvector<VDStringW>& paths);

extern ATSimulator g_sim;

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

// ============= Public API =============

void ATImGuiEmulatorInit() {
	// No init needed yet
}

void ATImGuiEmulatorDraw() {
	DrawMenuBar();
	DrawSystemConfig();
	DrawCartridgeBrowser();
	DrawFirmwareManager();
	DrawAudioOptions();
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
}
