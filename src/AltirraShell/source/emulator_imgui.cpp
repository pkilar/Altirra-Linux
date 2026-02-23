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
#include <vd2/VDDisplay/display.h>
#include <at/atdebugger/target.h>
#include <at/atcore/media.h>
#include <at/atcore/serializable.h>
#include <at/atio/image.h>

#include <vd2/system/filesys.h>

#include <imgui.h>
#include <emulator_imgui.h>
#include <debugger_imgui.h>
#include <display_sdl2.h>
#include <filedialog_linux.h>
#include <error_imgui.h>

// Forward declarations needed by simulator.h transitives
class ATIRQController;

#include "simulator.h"
#include "constants.h"
#include "diskinterface.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "debugger.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>

extern ATSimulator g_sim;

// Display backend accessor (defined in main_linux.cpp)
extern ATDisplaySDL2 *ATGetLinuxDisplay();

// Quick save state (in-memory)
static vdrefptr<IATSerializable> s_pQuickState;

// Window visibility
static bool s_showSystemConfig = false;
static bool s_showAbout = false;

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

// ============= Public API =============

void ATImGuiEmulatorInit() {
	// No init needed yet
}

void ATImGuiEmulatorDraw() {
	DrawMenuBar();
	DrawSystemConfig();
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
}
