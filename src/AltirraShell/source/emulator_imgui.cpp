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
#include <vd2/system/registry.h>
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
#include <vd2/system/strutil.h>

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
#include <at/atio/diskfs.h>
#include "cartridge.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "gtia.h"
#include "uikeyboard.h"
#include "devicemanager.h"
#include "cpu.h"
#include "debugger.h"
#include "inputmanager.h"
#include "oshelper.h"
#include "audiowriter.h"
#include "sapwriter.h"
#include "vgmwriter.h"
#include "inputmap.h"
#include "joystick.h"
#include "autosavemanager.h"

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

// Recording state
static vdautoptr<ATAudioWriter> s_pAudioWriter;
static vdautoptr<IATSAPWriter> s_pSapWriter;
static vdrefptr<IATVgmWriter> s_pVgmWriter;

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
static bool s_showCPUOptions = false;
static bool s_showInputSetup = false;
static bool s_showShortcuts = false;
static bool s_quitRequested = false;
static bool s_quitConfirmed = false;
static bool s_showStatusBar = true;

// Device manager state
static IATDevice *s_devSelectedDevice = nullptr;
static bool s_devShowConfig = false;
static bool s_devShowAddPopup = false;
static int s_devAddSelectedIdx = -1;
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

// New disk dialog state
static bool s_showNewDisk = false;
static int s_newDiskSlot = 0;
static int s_newDiskFormat = 1;  // 0=Custom, 1=SD 720, 2=MD 1040, 3=DD 720, 4=DSDD 1440
static int s_newDiskFS = 0;      // 0=None, 1=DOS 2, 2=MyDOS

// Toast notification system
struct Toast {
	std::string message;
	double expireTime;
};
static std::vector<Toast> s_toasts;

static void ShowToast(const char *msg) {
	double now = (double)SDL_GetTicks() / 1000.0;
	s_toasts.push_back({msg, now + 2.5});
}

static void DrawToasts() {
	if (s_toasts.empty())
		return;

	double now = (double)SDL_GetTicks() / 1000.0;

	// Remove expired
	while (!s_toasts.empty() && s_toasts.front().expireTime <= now)
		s_toasts.erase(s_toasts.begin());

	if (s_toasts.empty())
		return;

	ImVec2 vp = ImGui::GetMainViewport()->Size;
	float y = vp.y - 60.0f;

	for (int i = (int)s_toasts.size() - 1; i >= 0; --i) {
		float remaining = (float)(s_toasts[i].expireTime - now);
		float alpha = remaining < 0.5f ? remaining * 2.0f : 1.0f;

		ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.75f * alpha);

		char winId[32];
		snprintf(winId, sizeof(winId), "##toast%d", i);
		ImGui::Begin(winId, nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);

		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", s_toasts[i].message.c_str());
		ImGui::End();

		y -= 30.0f;
	}
}

static void DrawSourceMessage() {
	ATDisplaySDL2 *disp = ATGetLinuxDisplay();
	if (!disp)
		return;

	const char *msg = disp->GetSourceMessage();
	if (!msg)
		return;

	ImVec2 vp = ImGui::GetMainViewport()->Size;
	ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowBgAlpha(0.75f);
	ImGui::Begin("##srcmsg", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
	ImGui::Text("%s", msg);
	ImGui::End();
}

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
	kLoadState,
	kLoadTape,
	kSaveTape,
	kBootImage
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

static const char *kWavFilters =
	"WAV Audio|*.wav"
	"|All Files|*";

static const char *kSapFilters =
	"SAP Type-R|*.sap"
	"|All Files|*";

static const char *kVgmFilters =
	"VGM Files|*.vgm"
	"|All Files|*";

// ============= Recording helpers =============

static bool IsRecordingActive() {
	return s_pAudioWriter || s_pSapWriter || s_pVgmWriter;
}

static void StopAllRecording() {
	if (s_pAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);
		try { s_pAudioWriter->Finalize(); } catch (...) {}
		s_pAudioWriter.reset();
	}
	if (s_pSapWriter) {
		try { s_pSapWriter->Shutdown(); } catch (...) {}
		s_pSapWriter.reset();
	}
	if (s_pVgmWriter) {
		try { s_pVgmWriter->Shutdown(); } catch (...) {}
		s_pVgmWriter.clear();
	}
}

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

static const char *kCPUModeNames[] = {
	"6502",
	"65C02",
	"65C816"
};
static_assert(sizeof(kCPUModeNames)/sizeof(kCPUModeNames[0]) == kATCPUModeCount);

static const char *ATGetControllerTypeName(ATInputControllerType type) {
	switch (type) {
		case kATInputControllerType_Joystick:		return "Joystick";
		case kATInputControllerType_Paddle:			return "Paddle";
		case kATInputControllerType_STMouse:		return "ST Mouse";
		case kATInputControllerType_Console:		return "Console";
		case kATInputControllerType_5200Controller:	return "5200 Controller";
		case kATInputControllerType_InputState:		return "Input State";
		case kATInputControllerType_LightGun:		return "Light Gun (XG-1)";
		case kATInputControllerType_Tablet:			return "Tablet";
		case kATInputControllerType_KoalaPad:		return "KoalaPad";
		case kATInputControllerType_AmigaMouse:		return "Amiga Mouse";
		case kATInputControllerType_Keypad:			return "Keypad";
		case kATInputControllerType_Trackball_CX80:	return "Trackball CX-80";
		case kATInputControllerType_5200Trackball:	return "5200 Trackball";
		case kATInputControllerType_Driving:		return "Driving Controller";
		case kATInputControllerType_Keyboard:		return "Keyboard";
		case kATInputControllerType_LightPen:		return "Light Pen";
		case kATInputControllerType_PowerPad:		return "Power Pad";
		case kATInputControllerType_LightPenStack:	return "Light Pen Stack";
		default:									return "Unknown";
	}
}

// ============= Helper: load file via dialog =============

static void MRUAdd(const wchar_t *path);  // forward declaration

static void TryLoadImage(const VDStringW& path) {
	if (path.empty())
		return;

	try {
		g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
		MRUAdd(path.c_str());
		VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(path.c_str())));
		char msg[256];
		snprintf(msg, sizeof(msg), "Loaded: %s", fname.c_str());
		ShowToast(msg);
	} catch (const std::exception& e) {
		char msg[512];
		snprintf(msg, sizeof(msg), "Load failed: %s", e.what());
		ShowToast(msg);
	} catch (...) {
		ShowToast("Failed to load image");
	}
}

static void TryMountDisk(int index, const VDStringW& path) {
	if (path.empty())
		return;

	try {
		ATDiskInterface& di = g_sim.GetDiskInterface(index);
		di.LoadDisk(path.c_str());
		VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(path.c_str())));
		char msg[128];
		snprintf(msg, sizeof(msg), "D%d: %s", index + 1, fname.c_str());
		ShowToast(msg);
	} catch (const std::exception& e) {
		char msg[256];
		snprintf(msg, sizeof(msg), "D%d mount failed: %s", index + 1, e.what());
		ShowToast(msg);
	} catch (...) {
		char msg[64];
		snprintf(msg, sizeof(msg), "Failed to mount D%d", index + 1);
		ShowToast(msg);
	}
}

// ============= Paste text to Atari via POKEY =============
// Character-to-scancode table (ported from uikeyboard.cpp)

static struct CharToScanCodeInit {
	uint8 table[256];
	constexpr CharToScanCodeInit() : table{} {
		for (auto& v : table)
			v = 0xFF;

		// Lower-case letters
		table['a'] = 0x3F; table['b'] = 0x15; table['c'] = 0x12;
		table['d'] = 0x3A; table['e'] = 0x2A; table['f'] = 0x38;
		table['g'] = 0x3D; table['h'] = 0x39; table['i'] = 0x0D;
		table['j'] = 0x01; table['k'] = 0x05; table['l'] = 0x00;
		table['m'] = 0x25; table['n'] = 0x23; table['o'] = 0x08;
		table['p'] = 0x0A; table['q'] = 0x2F; table['r'] = 0x28;
		table['s'] = 0x3E; table['t'] = 0x2D; table['u'] = 0x0B;
		table['v'] = 0x10; table['w'] = 0x2E; table['x'] = 0x16;
		table['y'] = 0x2B; table['z'] = 0x17;

		// Upper-case letters (shift + scancode)
		table['A'] = 0x7F; table['B'] = 0x55; table['C'] = 0x52;
		table['D'] = 0x7A; table['E'] = 0x6A; table['F'] = 0x78;
		table['G'] = 0x7D; table['H'] = 0x79; table['I'] = 0x4D;
		table['J'] = 0x41; table['K'] = 0x45; table['L'] = 0x40;
		table['M'] = 0x65; table['N'] = 0x63; table['O'] = 0x48;
		table['P'] = 0x4A; table['Q'] = 0x6F; table['R'] = 0x68;
		table['S'] = 0x7E; table['T'] = 0x6D; table['U'] = 0x4B;
		table['V'] = 0x50; table['W'] = 0x6E; table['X'] = 0x56;
		table['Y'] = 0x6B; table['Z'] = 0x57;

		// Digits
		table['0'] = 0x32; table['1'] = 0x1F; table['2'] = 0x1E;
		table['3'] = 0x1A; table['4'] = 0x18; table['5'] = 0x1D;
		table['6'] = 0x1B; table['7'] = 0x33; table['8'] = 0x35;
		table['9'] = 0x30;

		// Punctuation and symbols
		table[' '] = 0x21;
		table['!'] = 0x5F; table['"'] = 0x5E; table['#'] = 0x5A;
		table['$'] = 0x58; table['%'] = 0x5D; table['&'] = 0x5B;
		table['\''] = 0x73; table['('] = 0x70; table[')'] = 0x72;
		table['*'] = 0x07; table['+'] = 0x06; table[','] = 0x20;
		table['-'] = 0x0E; table['.'] = 0x22; table['/'] = 0x26;
		table[':'] = 0x42; table[';'] = 0x02; table['<'] = 0x36;
		table['='] = 0x0F; table['>'] = 0x37; table['?'] = 0x66;
		table['@'] = 0x75; table['['] = 0x60; table['\\'] = 0x46;
		table[']'] = 0x62; table['^'] = 0x47; table['_'] = 0x4E;
		table['`'] = 0x27; table['|'] = 0x4F; table['~'] = 0x67;
	}
} s_charToScanCode;

static void PasteTextToEmulator() {
	char *text = SDL_GetClipboardText();
	if (!text || !*text) {
		SDL_free(text);
		return;
	}

	// Convert UTF-8 clipboard to wide chars
	VDStringW ws = VDTextU8ToW(VDStringSpanA(text));
	SDL_free(text);

	auto& pokey = g_sim.GetPokey();

	for (size_t i = 0; i < ws.size(); ++i) {
		wchar_t c = ws[i];

		// Skip null and zero-width characters
		if (!c || (c >= 0x200B && c <= 0x200F) || c == 0xFEFF)
			continue;

		// Normalize smart quotes and dashes
		switch (c) {
			case L'\u2010': case L'\u2011': case L'\u2012':
			case L'\u2013': case L'\u2014': case L'\u2015':
				c = L'-'; break;
			case L'\u2018': case L'\u2019':
				c = L'\''; break;
			case L'\u201C': case L'\u201D':
				c = L'"'; break;
			case L'\u2026':
				pokey.PushKey(s_charToScanCode.table['.'], false, true, false, true);
				pokey.PushKey(s_charToScanCode.table['.'], false, true, false, true);
				c = L'.';
				break;
		}

		// Handle newlines
		if (c == L'\r' || c == L'\n') {
			if (c == L'\r' && i + 1 < ws.size() && ws[i + 1] == L'\n')
				++i;
			pokey.PushKey(0x0C, false, true, false, true);
			continue;
		}

		// Handle tab
		if (c == L'\t') {
			pokey.PushKey(0x2C, false, true, false, true);
			continue;
		}

		// Map character to scancode
		if (c < 0x100) {
			uint8 sc = s_charToScanCode.table[(uint8)c];
			if (sc != 0xFF)
				pokey.PushKey(sc, false, true, false, true);
		}
	}

	ShowToast("Text pasted");
}

// ============= MRU (Recent Files) helpers =============
// Uses same registry format as Windows ATAddMRUListItem/ATGetMRUListItem

static void MRUAdd(const wchar_t *path) {
	VDRegistryAppKey key("MRU List", true);

	VDStringW order;
	key.getString("Order", order);

	// Check if already present — if so, promote it
	VDStringW existing;
	for (size_t i = 0; i < order.size(); ++i) {
		char keyname[2] = { (char)order[i], 0 };
		key.getString(keyname, existing);
		if (existing.comparei(path) == 0) {
			// Promote to front
			wchar_t c = order[i];
			order.erase(i, 1);
			order.insert(order.begin(), c);
			key.setString("Order", order.c_str());
			return;
		}
	}

	// Add new entry
	int slot = 0;
	if (order.size() >= 10) {
		wchar_t c = order.back();
		if (c >= L'A' && c < L'A' + 10)
			slot = c - L'A';
		order.resize(9);
	} else {
		slot = (int)order.size();
	}

	order.insert(order.begin(), L'A' + slot);
	char keyname[2] = { (char)('A' + slot), 0 };
	key.setString(keyname, path);
	key.setString("Order", order.c_str());
}

static VDStringW MRUGet(uint32 index) {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);

	VDStringW s;
	if (index < order.size()) {
		char keyname[2] = { (char)order[index], 0 };
		key.getString(keyname, s);
	}
	return s;
}

static uint32 MRUCount() {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	return (uint32)order.size();
}

static void MRUClear() {
	VDRegistryAppKey key("MRU List", true);
	key.removeValue("Order");
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
		if (ImGui::MenuItem("Input Setup...")) {
			s_showInputSetup = true;
		}
		if (ImGui::MenuItem("Cycle Quick Maps", "Shift+F1")) {
			ATInputManager *inputMgr = g_sim.GetInputManager();
			if (inputMgr) {
				ATInputMap *pMap = inputMgr->CycleQuickMaps();
				if (pMap) {
					VDStringA name = VDTextWToU8(VDStringW(pMap->GetName()));
					char msg[128];
					snprintf(msg, sizeof(msg), "Quick map: %s", name.c_str());
					ShowToast(msg);
				} else {
					ShowToast("Quick maps disabled");
				}
			}
		}
		if (ImGui::MenuItem("Devices...")) {
			s_showDeviceManager = true;
		}
		if (ImGui::MenuItem("Boot & Acceleration...")) {
			s_showBootOptions = true;
		}
		if (ImGui::MenuItem("CPU & Memory...")) {
			s_showCPUOptions = true;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Cold Reset", "Shift+F6")) {
			g_sim.ColdReset();
		}
		if (ImGui::MenuItem("Warm Reset", "F6")) {
			g_sim.WarmReset();
		}

		if (ImGui::BeginMenu("Console Switches")) {
			ATGTIAEmulator& gtia = g_sim.GetGTIA();
			uint8 consw = gtia.ReadConsoleSwitchInputs();

			// Console switch inputs are active-low: bit=0 means pressed
			bool optionDown = !(consw & 0x04);
			bool selectDown = !(consw & 0x02);
			bool startDown  = !(consw & 0x01);

			if (ImGui::MenuItem("Option", nullptr, optionDown))
				gtia.SetConsoleSwitch(0x04, !optionDown);
			if (ImGui::MenuItem("Select", nullptr, selectDown))
				gtia.SetConsoleSwitch(0x02, !selectDown);
			if (ImGui::MenuItem("Start", nullptr, startDown))
				gtia.SetConsoleSwitch(0x01, !startDown);

			ImGui::Separator();
			if (ImGui::MenuItem("Release All")) {
				gtia.SetConsoleSwitch(0x07, false);
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		{
			IATAutoSaveManager& asMgr = g_sim.GetAutoSaveManager();
			bool rewindEnabled = asMgr.GetRewindEnabled();
			if (ImGui::MenuItem("Enable Rewind", nullptr, &rewindEnabled))
				asMgr.SetRewindEnabled(rewindEnabled);
			if (ImGui::MenuItem("Rewind", nullptr, false, rewindEnabled))
				asMgr.Rewind();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Save Settings", "Ctrl+S")) {
			extern void ATLinuxSaveSettings();
			ATLinuxSaveSettings();
			ShowToast("Settings saved");
		}

		if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
			ATImGuiRequestQuit();
		}

		ImGui::EndMenu();
	}

	// --- File menu ---
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("Open Image...", "Ctrl+O")) {
			VDStringW path = ATLinuxOpenFileDialog("Open Image", kDiskFilters);
			if (!path.empty()) {
				TryLoadImage(path);
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kOpenImage;
			}
		}

		if (ImGui::MenuItem("Boot Image...", "Ctrl+Shift+O")) {
			VDStringW path = ATLinuxOpenFileDialog("Boot Image", kDiskFilters);
			if (!path.empty()) {
				try {
					g_sim.UnloadAll();
					g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
					g_sim.ColdReset();
					MRUAdd(path.c_str());
					VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(path.c_str())));
					char msg[256];
					snprintf(msg, sizeof(msg), "Booted: %s", fname.c_str());
					ShowToast(msg);
				} catch (...) {
					ShowToast("Failed to boot image");
				}
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kBootImage;
			}
		}

		// Recent files submenu
		uint32 mruCount = MRUCount();
		if (ImGui::BeginMenu("Recent Files", mruCount > 0)) {
			for (uint32 i = 0; i < mruCount && i < 10; ++i) {
				VDStringW wpath = MRUGet(i);
				if (wpath.empty())
					continue;

				VDStringA u8path = VDTextWToU8(VDStringW(VDFileSplitPath(wpath.c_str())));
				char label[256];
				snprintf(label, sizeof(label), "%u. %s", i + 1, u8path.c_str());

				if (ImGui::MenuItem(label)) {
					TryLoadImage(wpath);
				}

				// Tooltip with full path
				if (ImGui::IsItemHovered()) {
					VDStringA fullU8 = VDTextWToU8(wpath);
					ImGui::SetTooltip("%s", fullU8.c_str());
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Clear Recent Files")) {
				MRUClear();
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Quick Save State", "F7")) {
			try {
				s_pQuickState.clear();
				g_sim.CreateSnapshot(~s_pQuickState, nullptr);
				ShowToast("State saved");
			} catch (...) {
				ShowToast("Save state failed");
			}
		}
		if (ImGui::MenuItem("Quick Load State", "F8", false, s_pQuickState != nullptr)) {
			try {
				ATStateLoadContext ctx {};
				g_sim.ApplySnapshot(*s_pQuickState, &ctx);
				ShowToast("State loaded");
			} catch (...) {
				ShowToast("Load state failed");
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Save State As...")) {
			VDStringW path = ATLinuxSaveFileDialog("Save State", kSaveStateFilters);
			if (!path.empty()) {
				try {
					g_sim.SaveState(path.c_str());
					ShowToast("State saved to file");
				} catch (...) {
					ShowToast("Save state failed");
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
				} catch (const std::exception& e) {
					char msg[512];
					snprintf(msg, sizeof(msg), "Load state failed: %s", e.what());
					ShowToast(msg);
				} catch (...) {
					ShowToast("Load state failed");
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

					snprintf(label, sizeof(label), "New Disk in D%d...", i + 1);
					if (ImGui::MenuItem(label)) {
						s_showNewDisk = true;
						s_newDiskSlot = i;
					}

					if (di.IsDiskLoaded()) {
						if (di.IsDirty()) {
							snprintf(label, sizeof(label), "Save D%d", i + 1);
							if (ImGui::MenuItem(label)) {
								try {
									di.SaveDisk();
									char msg[64];
									snprintf(msg, sizeof(msg), "D%d saved", i + 1);
									ShowToast(msg);
								} catch (...) {
									ShowToast("Disk save failed");
								}
							}
						}

						snprintf(label, sizeof(label), "Save D%d As...", i + 1);
						if (ImGui::MenuItem(label)) {
							VDStringW path = ATLinuxSaveFileDialog("Save Disk As",
								"ATR Disk Images|*.atr|XFD Disk Images|*.xfd|All Files|*");
							if (!path.empty()) {
								try {
									ATDiskImageFormat fmt = kATDiskImageFormat_ATR;
									const wchar_t *ext = VDFileSplitExt(path.c_str());
									if (ext && !vdwcsicmp(ext, L".xfd"))
										fmt = kATDiskImageFormat_XFD;
									di.SaveDiskAs(path.c_str(), fmt);
									ShowToast("Disk saved");
								} catch (...) {
									ShowToast("Disk save failed");
								}
							}
						}

						ImGui::Separator();

						ATMediaWriteMode wm = di.GetWriteMode();
						bool readOnly = (wm == kATMediaWriteMode_RO);
						if (ImGui::MenuItem("Read Only", nullptr, readOnly)) {
							di.SetWriteMode(readOnly ? kATMediaWriteMode_VRW : kATMediaWriteMode_RO);
						}

						ImGui::Separator();

						snprintf(label, sizeof(label), "Unmount D%d", i + 1);
						if (ImGui::MenuItem(label)) {
							di.UnloadDisk();
						}
					}

					ImGui::EndMenu();
				}
			}

			ImGui::Separator();

			// Find highest active drive for rotation
			int activeDrives = 0;
			for (int i = 14; i >= 0; --i) {
				if (g_sim.GetDiskInterface(i).IsDiskLoaded()) {
					activeDrives = i + 1;
					break;
				}
			}

			if (ImGui::MenuItem("Rotate Down", nullptr, false, activeDrives >= 2)) {
				g_sim.RotateDrives(activeDrives, +1);
				const wchar_t *label = g_sim.GetDiskInterface(0).GetMountedImageLabel().c_str();
				VDStringA u8 = VDTextWToU8(VDStringW(label));
				char msg[128];
				snprintf(msg, sizeof(msg), "D1: %s", u8.c_str());
				ShowToast(msg);
			}
			if (ImGui::MenuItem("Rotate Up", nullptr, false, activeDrives >= 2)) {
				g_sim.RotateDrives(activeDrives, -1);
				const wchar_t *label = g_sim.GetDiskInterface(0).GetMountedImageLabel().c_str();
				VDStringA u8 = VDTextWToU8(VDStringW(label));
				char msg[128];
				snprintf(msg, sizeof(msg), "D1: %s", u8.c_str());
				ShowToast(msg);
			}

			ImGui::Separator();

			bool hasDirty = false;
			for (int i = 0; i < 15 && !hasDirty; ++i) {
				ATDiskInterface& di = g_sim.GetDiskInterface(i);
				if (di.IsDiskLoaded() && di.IsDirty())
					hasDirty = true;
			}

			if (ImGui::MenuItem("Save All Modified", nullptr, false, hasDirty)) {
				int saved = 0;
				for (int i = 0; i < 15; ++i) {
					ATDiskInterface& di = g_sim.GetDiskInterface(i);
					if (di.IsDiskLoaded() && di.IsDirty()) {
						try { di.SaveDisk(); ++saved; } catch (...) {}
					}
				}
				char msg[64];
				snprintf(msg, sizeof(msg), "Saved %d disk(s)", saved);
				ShowToast(msg);
			}

			if (ImGui::MenuItem("Unmount All", nullptr, false, activeDrives >= 1)) {
				for (int i = 0; i < 15; ++i)
					g_sim.GetDiskInterface(i).UnloadDisk();
				ShowToast("All disks unmounted");
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Save Screenshot...", "F9")) {
			VDPixmapBuffer pxbuf;
			VDPixmap px;
			if (g_sim.GetGTIA().GetLastFrameBuffer(pxbuf, px)) {
				VDStringW path = ATLinuxSaveFileDialog("Save Screenshot",
					"PNG Images|*.png|All Files|*");
				if (!path.empty()) {
					try {
						// Ensure .png extension
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".png";
						ATSaveFrame(px, path.c_str());
					} catch (const std::exception& e) {
						char msg[512];
						snprintf(msg, sizeof(msg), "Screenshot failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						ShowToast("Failed to save screenshot");
					}
				}
			}
		}

		if (ImGui::BeginMenu("Record")) {
			bool recording = IsRecordingActive();
			bool isPAL = g_sim.GetVideoStandard() == kATVideoStandard_PAL;
			bool isStereo = g_sim.IsDualPokeysEnabled();

			if (ImGui::MenuItem("Record Audio (WAV)...", nullptr, false, !recording)) {
				VDStringW path = ATLinuxSaveFileDialog("Record Audio", kWavFilters);
				if (!path.empty()) {
					try {
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".wav";
						s_pAudioWriter = new ATAudioWriter(path.c_str(), false, isStereo, isPAL, nullptr);
						g_sim.GetAudioOutput()->SetAudioTap(s_pAudioWriter);
					} catch (const std::exception& e) {
						s_pAudioWriter.reset();
						char msg[512];
						snprintf(msg, sizeof(msg), "Audio recording failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						s_pAudioWriter.reset();
						ShowToast("Failed to start audio recording");
					}
				}
			}

			if (ImGui::MenuItem("Record Raw Audio (PCM)...", nullptr, false, !recording)) {
				VDStringW path = ATLinuxSaveFileDialog("Record Raw Audio",
					"Raw PCM|*.pcm|All Files|*");
				if (!path.empty()) {
					try {
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".pcm";
						s_pAudioWriter = new ATAudioWriter(path.c_str(), true, isStereo, isPAL, nullptr);
						g_sim.GetAudioOutput()->SetAudioTap(s_pAudioWriter);
					} catch (const std::exception& e) {
						s_pAudioWriter.reset();
						char msg[512];
						snprintf(msg, sizeof(msg), "Raw audio recording failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						s_pAudioWriter.reset();
						ShowToast("Failed to start raw audio recording");
					}
				}
			}

			if (ImGui::MenuItem("Record SAP Type-R...", nullptr, false, !recording)) {
				VDStringW path = ATLinuxSaveFileDialog("Record SAP", kSapFilters);
				if (!path.empty()) {
					try {
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".sap";
						s_pSapWriter = ATCreateSAPWriter();
						s_pSapWriter->Init(
							g_sim.GetEventManager(),
							&g_sim.GetPokey(),
							nullptr,
							path.c_str(),
							isPAL);
					} catch (const std::exception& e) {
						s_pSapWriter.reset();
						char msg[512];
						snprintf(msg, sizeof(msg), "SAP recording failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						s_pSapWriter.reset();
						ShowToast("Failed to start SAP recording");
					}
				}
			}

			if (ImGui::MenuItem("Record VGM...", nullptr, false, !recording)) {
				VDStringW path = ATLinuxSaveFileDialog("Record VGM", kVgmFilters);
				if (!path.empty()) {
					try {
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".vgm";
						s_pVgmWriter = ATCreateVgmWriter();
						s_pVgmWriter->Init(path.c_str(), g_sim);
					} catch (const std::exception& e) {
						s_pVgmWriter.clear();
						char msg[512];
						snprintf(msg, sizeof(msg), "VGM recording failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						s_pVgmWriter.clear();
						ShowToast("Failed to start VGM recording");
					}
				}
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Stop Recording", nullptr, false, recording)) {
				StopAllRecording();
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

	// --- Edit menu ---
	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Paste Text", "Ctrl+V"))
			PasteTextToEmulator();

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

		if (ImGui::BeginMenu("Stretch Mode")) {
			ATDisplayStretchMode curStretch = ATUIGetDisplayStretchMode();
			for (int i = 0; i < (int)kATDisplayStretchModeCount; ++i) {
				if (ImGui::MenuItem(kStretchModeNames[i], nullptr, curStretch == (ATDisplayStretchMode)i))
					ATUISetDisplayStretchMode((ATDisplayStretchMode)i);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window Size")) {
			// Base resolution depends on video standard
			bool isPAL = g_sim.GetVideoStandard() == kATVideoStandard_PAL
				|| g_sim.GetVideoStandard() == kATVideoStandard_SECAM;
			int baseW = 456;
			int baseH = isPAL ? 312 : 262;

			for (int scale = 1; scale <= 4; ++scale) {
				int w = baseW * scale;
				int h = baseH * scale;
				char label[48];
				snprintf(label, sizeof(label), "%dx (%dx%d)", scale, w, h);
				if (ImGui::MenuItem(label))
					ATSetWindowSize(w, h);
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Video Settings..."))
			s_showVideoConfig = true;

		bool fullscreen = ATUIGetFullscreen();
		if (ImGui::MenuItem("Fullscreen", "F11", &fullscreen))
			ATSetFullscreen(fullscreen);

		ImGui::MenuItem("Status Bar", nullptr, &s_showStatusBar);

		{
			bool autoHide = ATUIGetPointerAutoHide();
			if (ImGui::MenuItem("Auto-Hide Cursor", nullptr, &autoHide))
				ATUISetPointerAutoHide(autoHide);
		}

		{
			bool confine = ATUIGetConstrainMouseFullScreen();
			if (ImGui::MenuItem("Confine Mouse in Fullscreen", nullptr, &confine))
				ATUISetConstrainMouseFullScreen(confine);
		}

		ImGui::EndMenu();
	}

	// --- Speed menu ---
	if (ImGui::BeginMenu("Speed")) {
		bool paused = g_sim.IsPaused();
		if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Pause")) {
			if (paused)
				g_sim.Resume();
			else
				g_sim.Pause();
		}

		bool turbo = ATUIGetTurbo();
		if (ImGui::MenuItem("Turbo", nullptr, &turbo))
			ATUISetTurbo(turbo);

		ImGui::Separator();

		float speed = ATUIGetSpeedModifier();

		if (ImGui::MenuItem("50%",  nullptr, speed == 0.5f))  ATUISetSpeedModifier(0.5f);
		if (ImGui::MenuItem("100%", nullptr, speed == 1.0f))  ATUISetSpeedModifier(1.0f);
		if (ImGui::MenuItem("200%", nullptr, speed == 2.0f))  ATUISetSpeedModifier(2.0f);
		if (ImGui::MenuItem("400%", nullptr, speed == 4.0f))  ATUISetSpeedModifier(4.0f);

		ImGui::Separator();

		int speedPct = (int)(speed * 100.0f + 0.5f);
		ImGui::SetNextItemWidth(120);
		if (ImGui::SliderInt("##speed", &speedPct, 10, 800, "%d%%")) {
			ATUISetSpeedModifier((float)speedPct / 100.0f);
		}

		ImGui::Separator();

		{
			IATAudioOutput *audioOut = g_sim.GetAudioOutput();
			if (audioOut) {
				bool mute = audioOut->GetMute();
				if (ImGui::MenuItem("Mute Audio", "F4", &mute)) {
					audioOut->SetMute(mute);
				}
			}
		}

		ImGui::Separator();

		{
			bool pauseInactive = ATUIGetPauseWhenInactive();
			if (ImGui::MenuItem("Pause When Inactive", nullptr, &pauseInactive))
				ATUISetPauseWhenInactive(pauseInactive);
		}

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
			ImGui::MenuItem("Watch", nullptr, &ATImGuiDebuggerShowWatch());
			ImGui::MenuItem("Call Stack", nullptr, &ATImGuiDebuggerShowCallStack());
			ImGui::MenuItem("History", nullptr, &ATImGuiDebuggerShowHistory());
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Load Symbols...", nullptr, false, dbg != nullptr)) {
			VDStringW path = ATLinuxOpenFileDialog("Load Symbols",
				"Symbol Files|*.lbl;*.lst;*.lab;*.sym;*.mads"
				"|All Files|*");
			if (!path.empty() && dbg) {
				dbg->LoadSymbols(path.c_str(), true);
				ShowToast("Symbols loaded");
			}
		}

		if (ImGui::MenuItem("Unload All Symbols", nullptr, false, dbg != nullptr)) {
			if (dbg)
				dbg->QueueCommand(".unloadsym", false);
		}

		ImGui::EndMenu();
	}

	// --- Help menu ---
	if (ImGui::BeginMenu("Help")) {
		if (ImGui::MenuItem("Keyboard Shortcuts..."))
			s_showShortcuts = true;

		ImGui::Separator();

		if (ImGui::MenuItem("Open Config Directory")) {
			vdvector<VDStringW> fwPaths;
			ATGetFirmwareSearchPaths(fwPaths);
			if (!fwPaths.empty()) {
				// First firmware path parent is the config dir
				VDStringW configDir = VDFileSplitPathLeft(fwPaths[0]);
				VDStringW dummy = VDMakePath(configDir.c_str(), L".");
				ATShowFileInSystemExplorer(dummy.c_str());
			}
		}
		if (ImGui::MenuItem("Open Firmware Directory")) {
			vdvector<VDStringW> fwPaths;
			ATGetFirmwareSearchPaths(fwPaths);
			if (!fwPaths.empty()) {
				VDStringW dummy = VDMakePath(fwPaths[0].c_str(), L".");
				ATShowFileInSystemExplorer(dummy.c_str());
			}
		}

		ImGui::Separator();

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
	if (!s_showStatusBar)
		return;

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

		// Disk status (show dirty indicator when disk has unsaved changes)
		for (int i = 0; i < 4; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			ImGui::SameLine(0, 16);

			if (di.IsDiskLoaded()) {
				const wchar_t *filename = VDFileSplitPath(di.GetPath());
				VDStringA u8 = VDTextWToU8(VDStringW(filename));
				if (di.IsDirty())
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "D%d: %s*", i + 1, u8.c_str());
				else
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

		// Tape indicator with position
		{
			ATCassetteEmulator& cas = g_sim.GetCassette();
			if (cas.IsLoaded()) {
				ImGui::SameLine(0, 16);
				float pos = cas.GetPosition();
				int posMin = (int)(pos / 60.0f);
				int posSec = (int)pos % 60;
				if (cas.IsPlayEnabled() && !cas.IsPaused())
					ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "TAPE> %d:%02d", posMin, posSec);
				else if (cas.IsRecordEnabled() && !cas.IsPaused())
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "TAPE* %d:%02d", posMin, posSec);
				else
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "TAPE %d:%02d", posMin, posSec);
			}
		}

		// Pause indicator
		if (g_sim.IsPaused()) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "PAUSED");
		}

		// Turbo/speed indicator
		if (ATUIGetTurbo()) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[TURBO]");
		} else {
			float speed = ATUIGetSpeedModifier();
			int pct = (int)(speed * 100.0f + 0.5f);
			if (pct != 100) {
				ImGui::SameLine(0, 16);
				ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "%d%%", pct);
			}
		}

		// Recording indicator
		if (IsRecordingActive()) {
			ImGui::SameLine(0, 16);
			const char *recType = s_pAudioWriter
				? (s_pAudioWriter->IsRecordingRaw() ? "REC:PCM" : "REC:WAV")
				: s_pSapWriter ? "REC:SAP"
				: "REC:VGM";
			ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", recType);
		}

		// Mute indicator
		{
			IATAudioOutput *audioOut = g_sim.GetAudioOutput();
			if (audioOut && audioOut->GetMute()) {
				ImGui::SameLine(0, 16);
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "MUTE");
			}
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

// ============= Keyboard Shortcuts Window =============

static void DrawShortcuts() {
	if (!s_showShortcuts)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Keyboard Shortcuts", &s_showShortcuts)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
		ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 140);
		ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		auto row = [](const char *key, const char *action) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(key);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(action);
		};

		row("F1 (hold)", "Turbo / Warp");
		row("Shift+F1", "Cycle Quick Maps");
		row("F4", "Toggle Mute");
		row("F5", "Break / Run (debugger)");
		row("F6", "Warm Reset");
		row("Shift+F6", "Cold Reset");
		row("F7", "Quick Save State");
		row("F8", "Quick Load State");
		row("F9", "Save Screenshot");
		row("F10", "Step Over (debugger)");
		row("F11", "Fullscreen / Step Into");
		row("Shift+F11", "Step Out (debugger)");
		row("F12", "Toggle Overlay");
		row("Alt+Return", "Toggle Fullscreen");
		row("Pause", "Pause / Resume");
		row("Ctrl+O", "Open Image");
		row("Ctrl+Shift+O", "Boot Image");
		row("Ctrl+V", "Paste Text");
		row("Ctrl+S", "Save Settings");
		row("Ctrl+Q", "Quit");

		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::TextDisabled("F5/F10/F11(step) only active with overlay open");

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
					try { g_sim.SaveState(result.c_str()); ShowToast("State saved"); }
					catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Save state failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Save state failed"); }
				}
				break;
			case PendingDialog::kLoadState:
				if (!result.empty()) {
					try { g_sim.Load(result.c_str(), kATMediaWriteMode_RO, nullptr); ShowToast("State loaded"); }
					catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Load state failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Load state failed"); }
				}
				break;
			case PendingDialog::kLoadTape:
				if (!result.empty()) {
					try { g_sim.GetCassette().Load(result.c_str()); ShowToast("Tape loaded"); }
					catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Load tape failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Load tape failed"); }
				}
				break;
			case PendingDialog::kSaveTape:
				if (!result.empty()) {
					try {
						g_sim.GetCassette().SetImagePersistent(result.c_str());
						g_sim.GetCassette().SetImageClean();
						ShowToast("Tape saved");
					} catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Save tape failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Save tape failed"); }
				}
				break;
			case PendingDialog::kBootImage:
				if (!result.empty()) {
					try {
						g_sim.UnloadAll();
						g_sim.Load(result.c_str(), kATMediaWriteMode_RO, nullptr);
						g_sim.ColdReset();
						MRUAdd(result.c_str());
						ShowToast("Booted image");
					} catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Boot failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Failed to boot image"); }
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
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Load cartridge failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to load cartridge");
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
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Read binary failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to read binary file");
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
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Load cartridge failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to load cartridge with selected mapper");
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
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Add firmware failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to add firmware file");
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

	// Drive sounds
	if (ImGui::CollapsingHeader("Drive Sounds", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool driveSounds = g_sim.GetDiskInterface(0).AreDriveSoundsEnabled();
		if (ImGui::Checkbox("Enable drive sounds", &driveSounds)) {
			for (int i = 0; i < 15; ++i)
				g_sim.GetDiskInterface(i).SetDriveSoundsEnabled(driveSounds);
		}
	}

	// POKEY options
	if (ImGui::CollapsingHeader("POKEY Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		ATPokeyEmulator& pokey = g_sim.GetPokey();

		bool dualPokey = g_sim.IsDualPokeysEnabled();
		if (ImGui::Checkbox("Dual POKEY (stereo)", &dualPokey))
			g_sim.SetDualPokeysEnabled(dualPokey);

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
	if (ImGui::Button("Add...")) {
		s_devShowAddPopup = true;
		s_devAddSelectedIdx = -1;
		ImGui::OpenPopup("Add Device");
	}

	ImGui::SameLine();

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

	// Add Device popup
	if (ImGui::BeginPopupModal("Add Device", &s_devShowAddPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
		const auto& defs = devMgr->GetDeviceDefinitions();

		ImGui::Text("Select device type:");
		if (ImGui::BeginListBox("##devlist", ImVec2(400, 250))) {
			for (int i = 0; i < (int)defs.size(); ++i) {
				const ATDeviceDefinition *def = defs[i];
				if (def->mFlags & (kATDeviceDefFlag_Internal | kATDeviceDefFlag_Hidden))
					continue;

				VDStringA u8name = VDTextWToU8(VDStringW(def->mpName));
				if (ImGui::Selectable(u8name.c_str(), s_devAddSelectedIdx == i))
					s_devAddSelectedIdx = i;
			}
			ImGui::EndListBox();
		}

		if (ImGui::Button("Add", ImVec2(80, 0)) && s_devAddSelectedIdx >= 0
			&& s_devAddSelectedIdx < (int)defs.size()) {
			const ATDeviceDefinition *def = defs[s_devAddSelectedIdx];
			ATPropertySet props;
			try {
				devMgr->AddDevice(def->mpTag, props);
			} catch (const MyError& e) {
				extern void ATUIShowError(const VDException& e);
				ATUIShowError(e);
			}
			s_devShowAddPopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			s_devShowAddPopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
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

// ============= CPU & Memory Options =============

static void DrawCPUOptions() {
	if (!s_showCPUOptions)
		return;

	ImGui::SetNextWindowSize(ImVec2(380, 340), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("CPU & Memory", &s_showCPUOptions)) {
		ImGui::End();
		return;
	}

	// CPU type
	if (ImGui::CollapsingHeader("CPU", ImGuiTreeNodeFlags_DefaultOpen)) {
		int cpuMode = (int)g_sim.GetCPUMode();
		ImGui::Text("CPU Type:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(180);
		if (ImGui::Combo("##cpumode", &cpuMode, kCPUModeNames, kATCPUModeCount))
			g_sim.SetCPUMode((ATCPUMode)cpuMode, 1);

		// Profiling/verification tools
		bool profiling = g_sim.IsProfilingEnabled();
		if (ImGui::Checkbox("CPU profiling", &profiling))
			g_sim.SetProfilingEnabled(profiling);

		bool verifier = g_sim.IsVerifierEnabled();
		if (ImGui::Checkbox("CPU verifier", &verifier))
			g_sim.SetVerifierEnabled(verifier);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Enable illegal instruction and address checks");

		bool heatmap = g_sim.IsHeatMapEnabled();
		if (ImGui::Checkbox("Heat map", &heatmap))
			g_sim.SetHeatMapEnabled(heatmap);
	}

	// Memory options
	if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
		// Memory mode (also in System Settings, duplicated for convenience)
		int memMode = (int)g_sim.GetMemoryMode();
		ImGui::Text("Memory:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(180);
		if (ImGui::Combo("##memmode", &memMode, kMemoryModeNames, kATMemoryModeCount))
			g_sim.SetMemoryMode((ATMemoryMode)memMode);

		// Axlon memory
		uint8 axlonBits = g_sim.GetAxlonMemoryMode();
		int axlonKB = axlonBits ? (1 << axlonBits) / 1024 * 16 : 0;
		const char *axlonNames[] = { "Disabled", "64K", "128K", "256K", "512K", "1024K", "2048K", "4096K" };
		int axlonIdx = 0;
		if (axlonBits >= 2 && axlonBits <= 8)
			axlonIdx = axlonBits - 1;

		ImGui::Text("Axlon RAM:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(180);
		if (ImGui::Combo("##axlon", &axlonIdx, axlonNames, 8))
			g_sim.SetAxlonMemoryMode(axlonIdx == 0 ? 0 : (uint8)(axlonIdx + 1));

		bool axlonAlias = g_sim.GetAxlonAliasingEnabled();
		if (ImGui::Checkbox("Axlon aliasing", &axlonAlias))
			g_sim.SetAxlonAliasingEnabled(axlonAlias);

		// High memory banks
		sint32 highBanks = g_sim.GetHighMemoryBanks();
		int highIdx = highBanks < 0 ? 0 : (highBanks == 0 ? 1 : 2 + highBanks);
		ImGui::Text("High RAM:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(180);
		const char *highNames[] = { "Auto", "None", "64K", "256K", "1024K", "4096K", "16384K", "65536K" };
		if (ImGui::Combo("##highmem", &highIdx, highNames, 8)) {
			sint32 newBanks = highIdx == 0 ? -1 : (highIdx == 1 ? 0 : highIdx - 2);
			g_sim.SetHighMemoryBanks(newBanks);
		}

		bool mapRAM = g_sim.IsMapRAMEnabled();
		if (ImGui::Checkbox("MapRAM", &mapRAM))
			g_sim.SetMapRAMEnabled(mapRAM);

		bool shadowROM = g_sim.GetShadowROMEnabled();
		if (ImGui::Checkbox("Shadow ROM", &shadowROM))
			g_sim.SetShadowROMEnabled(shadowROM);
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

// ============= Input Setup Window =============

static void DrawInputSetup() {
	if (!s_showInputSetup)
		return;

	ATInputManager *inputMgr = g_sim.GetInputManager();
	if (!inputMgr)
		return;

	ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Input Setup", &s_showInputSetup)) {
		ImGui::End();
		return;
	}

	// --- Detected controllers ---
	if (ImGui::CollapsingHeader("Detected Controllers", ImGuiTreeNodeFlags_DefaultOpen)) {
		int numJoysticks = SDL_NumJoysticks();
		if (numJoysticks > 0) {
			for (int i = 0; i < numJoysticks; ++i) {
				const char *name = SDL_JoystickNameForIndex(i);
				if (SDL_IsGameController(i)) {
					ImGui::BulletText("Gamepad %d: %s", i + 1, name ? name : "(unknown)");
				} else {
					ImGui::BulletText("Joystick %d: %s", i + 1, name ? name : "(unknown)");
				}
			}
		} else {
			ImGui::TextDisabled("No joysticks/gamepads detected");
		}

		if (ImGui::Button("Rescan")) {
			IATJoystickManager *jm = g_sim.GetJoystickManager();
			if (jm)
				jm->RescanForDevices();
		}
	}

	// --- Active input maps ---
	if (ImGui::CollapsingHeader("Input Maps", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("Input maps bind host inputs (keyboard, mouse, gamepad) to Atari controllers.");
		ImGui::Spacing();

		uint32 mapCount = inputMgr->GetInputMapCount();

		if (mapCount == 0) {
			ImGui::TextDisabled("No input maps configured. Click 'Reset to Defaults' to add standard maps.");
		}

		// Scrollable list of maps
		if (ImGui::BeginChild("##maplist", ImVec2(0, 280), ImGuiChildFlags_Borders)) {
			static int s_selectedMapIdx = -1;

			for (uint32 i = 0; i < mapCount; ++i) {
				ATInputMap *imap = nullptr;
				if (!inputMgr->GetInputMapByIndex(i, &imap))
					continue;

				ImGui::PushID((int)i);

				bool enabled = inputMgr->IsInputMapEnabled(imap);
				VDStringA u8name = VDTextWToU8(VDStringW(imap->GetName()));

				// Checkbox for enable/disable
				if (ImGui::Checkbox("##en", &enabled)) {
					inputMgr->ActivateInputMap(imap, enabled);
				}
				ImGui::SameLine();

				// Build inline summary of controller types and ports
				char portInfo[128] = "";
				{
					uint32 ctrlCount = imap->GetControllerCount();
					int pos = 0;
					for (uint32 c = 0; c < ctrlCount && pos < 100; ++c) {
						const ATInputMap::Controller& ctrl = imap->GetController(c);
						if (c > 0)
							pos += snprintf(portInfo + pos, sizeof(portInfo) - pos, ", ");
						pos += snprintf(portInfo + pos, sizeof(portInfo) - pos, "%s P%u",
							ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1);
					}
				}

				// Selectable row with port info
				bool selected = (s_selectedMapIdx == (int)i);
				char label[256];
				snprintf(label, sizeof(label), "%-30s  %s", u8name.c_str(), portInfo);
				if (ImGui::Selectable(label, selected)) {
					s_selectedMapIdx = (int)i;
				}

				// Tooltip with full map details on hover
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					uint32 mappingCount = imap->GetMappingCount();
					ImGui::Text("%u mapping(s)", mappingCount);
					if (imap->IsQuickMap())
						ImGui::Text("[Quick Map]");
					ImGui::EndTooltip();
				}

				ImGui::PopID();
			}

			// Buttons for selected map
			ImGui::Spacing();
			ImGui::Separator();

			ATInputMap *selMap = nullptr;
			if (s_selectedMapIdx >= 0 && (uint32)s_selectedMapIdx < mapCount) {
				inputMgr->GetInputMapByIndex(s_selectedMapIdx, &selMap);
			}

			if (ImGui::Button("Remove") && selMap) {
				inputMgr->RemoveInputMap(selMap);
				s_selectedMapIdx = -1;
			}
		}
		ImGui::EndChild();
	}

	// --- Add from presets ---
	if (ImGui::CollapsingHeader("Add Preset Map")) {
		uint32 presetCount = inputMgr->GetPresetInputMapCount();

		static int s_selectedPresetIdx = -1;

		if (ImGui::BeginChild("##presetlist", ImVec2(0, 200), ImGuiChildFlags_Borders)) {
			for (uint32 i = 0; i < presetCount; ++i) {
				vdrefptr<ATInputMap> preset;
				if (!inputMgr->GetPresetInputMapByIndex(i, ~preset))
					continue;

				VDStringA u8name = VDTextWToU8(VDStringW(preset->GetName()));
				bool selected = (s_selectedPresetIdx == (int)i);

				if (ImGui::Selectable(u8name.c_str(), selected)) {
					s_selectedPresetIdx = (int)i;
				}
			}
		}
		ImGui::EndChild();

		if (ImGui::Button("Add Selected Preset") && s_selectedPresetIdx >= 0) {
			vdrefptr<ATInputMap> preset;
			if (inputMgr->GetPresetInputMapByIndex(s_selectedPresetIdx, ~preset)) {
				inputMgr->AddInputMap(preset);
			}
		}
	}

	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button("Reset to Defaults")) {
		inputMgr->ResetToDefaults();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Remove all maps and add back the default set.\nIncludes arrow keys, numpad, mouse, and gamepad presets.");
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
				ShowToast("Tape loaded");
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Load tape failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to load tape image");
			}
		} else if (ATLinuxFileDialogIsFallbackOpen()) {
			s_pendingDialog = PendingDialog::kLoadTape;
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
				ShowToast("Tape saved");
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Save tape failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to save tape image");
			}
		} else if (ATLinuxFileDialogIsFallbackOpen()) {
			s_pendingDialog = PendingDialog::kSaveTape;
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

// ============= New Disk Dialog =============

static const struct {
	const char *name;
	uint32 sectorCount;
	uint32 sectorSize;
} kNewDiskFormats[] = {
	{ "Single Density (90K - 720 sectors, 128 bytes)",   720, 128 },
	{ "Medium Density (130K - 1040 sectors, 128 bytes)", 1040, 128 },
	{ "Double Density (180K - 720 sectors, 256 bytes)",  720, 256 },
	{ "Double-Sided DD (360K - 1440 sectors, 256 bytes)", 1440, 256 },
};

static const char *kNewDiskFSNames[] = {
	"None (unformatted)",
	"DOS 2.0",
	"MyDOS",
};

static void DrawNewDisk() {
	if (!s_showNewDisk)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Create New Disk", &s_showNewDisk, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Create blank disk image in D%d:", s_newDiskSlot + 1);
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Disk Format:");
	for (int i = 0; i < (int)(sizeof(kNewDiskFormats)/sizeof(kNewDiskFormats[0])); ++i) {
		ImGui::RadioButton(kNewDiskFormats[i].name, &s_newDiskFormat, i);
	}

	ImGui::Spacing();
	ImGui::Text("Filesystem:");
	ImGui::Combo("##fs", &s_newDiskFS, kNewDiskFSNames, 3);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button("Create", ImVec2(100, 0))) {
		try {
			ATDiskInterface& di = g_sim.GetDiskInterface(s_newDiskSlot);
			di.UnloadDisk();

			uint32 sectorCount = kNewDiskFormats[s_newDiskFormat].sectorCount;
			uint32 sectorSize = kNewDiskFormats[s_newDiskFormat].sectorSize;
			di.CreateDisk(sectorCount, 3, sectorSize);
			di.SetWriteMode(kATMediaWriteMode_VRW);

			IATDiskImage *image = di.GetDiskImage();
			if (image && s_newDiskFS > 0) {
				vdautoptr<IATDiskFS> fs;
				if (s_newDiskFS == 1)
					fs = ATDiskFormatImageDOS2(image);
				else if (s_newDiskFS == 2)
					fs = ATDiskFormatImageMyDOS(image);

				if (fs)
					fs->Flush();
			}
			char msg[64];
			snprintf(msg, sizeof(msg), "New disk created in D%d", s_newDiskSlot + 1);
			ShowToast(msg);
		} catch (...) {
			ShowToast("Failed to create new disk");
		}
		s_showNewDisk = false;
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 0))) {
		s_showNewDisk = false;
	}

	ImGui::End();
}

// ============= Quit Confirmation =============

static bool HasDirtyDisks() {
	for (int i = 0; i < 15; ++i) {
		ATDiskInterface& di = g_sim.GetDiskInterface(i);
		if (di.IsDiskLoaded() && di.IsDirty())
			return true;
	}
	return false;
}

static void DrawQuitConfirmation() {
	if (!s_quitRequested)
		return;

	ImGui::OpenPopup("Quit Altirra?");
	s_quitRequested = false;

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Quit Altirra?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("The following disks have unsaved changes:");
		ImGui::Separator();

		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			if (di.IsDiskLoaded() && di.IsDirty()) {
				const wchar_t *filename = VDFileSplitPath(di.GetPath());
				VDStringA u8 = VDTextWToU8(VDStringW(filename));
				ImGui::BulletText("D%d: %s", i + 1, u8.c_str());
			}
		}

		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Save All & Quit", ImVec2(140, 0))) {
			for (int i = 0; i < 15; ++i) {
				ATDiskInterface& di = g_sim.GetDiskInterface(i);
				if (di.IsDiskLoaded() && di.IsDirty()) {
					try { di.SaveDisk(); } catch (...) {}
				}
			}
			s_quitConfirmed = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Quit Without Saving", ImVec2(160, 0))) {
			s_quitConfirmed = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
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
	DrawCPUOptions();
	DrawBootOptions();
	DrawCassetteControl();
	DrawCartridgeBrowser();
	DrawFirmwareManager();
	DrawAudioOptions();
	DrawVideoConfig();
	DrawKeyboardConfig();
	DrawInputSetup();
	DrawDeviceManager();
	DrawStatusBar();
	DrawAbout();
	DrawShortcuts();
	DrawNewDisk();
	DrawQuitConfirmation();
	PollFileDialogFallback();
	CheckPendingErrors();
	DrawErrorPopup();

	DrawToasts();
	DrawSourceMessage();

	// Draw debugger windows (without toolbar — menu bar handles that now)
	ATImGuiDebuggerDrawWindows();
}

void ATImGuiEmulatorShutdown() {
	StopAllRecording();
	s_pQuickState.clear();
	s_cartLoadBuffer.clear();
	s_cartDetectedModes.clear();
	s_cartDisplayModes.clear();
	s_fwList.clear();
	s_devSelectedDevice = nullptr;
	s_devEditProps.Clear();
}

void ATImGuiRequestQuit() {
	if (HasDirtyDisks()) {
		s_quitRequested = true;
		s_quitConfirmed = false;
	} else {
		s_quitConfirmed = true;
	}
}

bool ATImGuiIsQuitConfirmed() {
	if (s_quitConfirmed) {
		s_quitConfirmed = false;
		return true;
	}
	return false;
}

void ATImGuiShowToast(const char *message) {
	ShowToast(message);
}

void ATImGuiPasteText() {
	PasteTextToEmulator();
}

void ATImGuiOpenImage() {
	VDStringW path = ATLinuxOpenFileDialog("Open Image", kDiskFilters);
	if (!path.empty())
		TryLoadImage(path);
}

void ATImGuiBootImage() {
	VDStringW path = ATLinuxOpenFileDialog("Boot Image", kDiskFilters);
	if (!path.empty()) {
		try {
			g_sim.UnloadAll();
			g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
			g_sim.ColdReset();
			MRUAdd(path.c_str());
			VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(path.c_str())));
			char msg[256];
			snprintf(msg, sizeof(msg), "Booted: %s", fname.c_str());
			ShowToast(msg);
		} catch (const std::exception& e) {
			char msg[512];
			snprintf(msg, sizeof(msg), "Boot failed: %s", e.what());
			ShowToast(msg);
		} catch (...) {
			ShowToast("Failed to boot image");
		}
	}
}

static double s_startTime = 0;

void ATImGuiDrawToastsOnly() {
	DrawToasts();

	// Show overlay hint for first 5 seconds
	if (s_startTime == 0)
		s_startTime = (double)SDL_GetTicks() / 1000.0;

	double elapsed = (double)SDL_GetTicks() / 1000.0 - s_startTime;
	if (elapsed < 5.0) {
		float alpha = elapsed > 4.0f ? (float)(5.0 - elapsed) : 1.0f;
		ImVec2 vp = ImGui::GetMainViewport()->Size;
		ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, 30.0f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowBgAlpha(0.6f * alpha);
		ImGui::Begin("##hint", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "Press F12 for menu");
		ImGui::End();
	}
}
