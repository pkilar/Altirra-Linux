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
#include "mainframe.h"
#include "menu_ids.h"
#include "dialogs_wx.h"
#include <debugger_wx.h>
#include <display_wx.h>
#include <statusbar_wx.h>

#include <wx/aboutdlg.h>
#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/text.h>
#include <at/atcore/media.h>
#include <at/atcore/serializable.h>
#include <at/atio/image.h>

#include "simulator.h"
#include "oshelper.h"
#include "audiowriter.h"
#include "cartridge.h"
#include "cassette.h"
#include "constants.h"
#include "debugger.h"
#include "devicemanager.h"
#include "disk.h"
#include <at/atcore/device.h>
#include <at/atio/diskfs.h>
#include <at/atio/diskfsutil.h>
#include "firmwaremanager.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "joystick.h"
#include "autosavemanager.h"
#include "sapconverter.h"
#include "sapwriter.h"
#include "settings.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include "versioninfo.h"
#include "vgmwriter.h"
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>
#include <at/atio/cassetteimage.h>
#include <vd2/system/file.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/resample.h>

#include <algorithm>
#include <vector>

#include <SDL3/SDL.h>

// External symbols
extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

// Toast notification (defined in main_wx.cpp)
void ATImGuiShowToast(const char *message);

// Settings save (defined in main_wx.cpp)
void ATLinuxSaveSettings();

// Audio recording state
static vdautoptr<ATAudioWriter> s_pAudioWriter;
static vdautoptr<IATSAPWriter> s_pSapWriter;
static vdrefptr<IATVgmWriter> s_pVgmWriter;

static bool ATIsAnyAudioRecording() {
	return s_pAudioWriter || s_pSapWriter || s_pVgmWriter;
}

static void ATStopAudioRecording() {
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

///////////////////////////////////////////////////////////////////////////
// Device button definitions
///////////////////////////////////////////////////////////////////////////

struct DeviceButtonEntry { ATDeviceButton id; const char *name; };
static const DeviceButtonEntry kDeviceButtons[] = {
	{ kATDeviceButton_BlackBoxDumpScreen,   "BlackBox: Dump Screen" },
	{ kATDeviceButton_BlackBoxMenu,         "BlackBox: Menu" },
	{ kATDeviceButton_CartridgeResetBank,   "Cart: Reset Bank" },
	{ kATDeviceButton_CartridgeSwitch,      "Cart: Switch" },
	{ kATDeviceButton_IDEPlus2SwitchDisks,  "IDE Plus 2.0: Switch Disks" },
	{ kATDeviceButton_IDEPlus2WriteProtect, "IDE Plus 2.0: Write Protect" },
	{ kATDeviceButton_IDEPlus2SDX,          "IDE Plus 2.0: SDX" },
	{ kATDeviceButton_IndusGTError,         "Indus GT: Error" },
	{ kATDeviceButton_IndusGTTrack,         "Indus GT: Track" },
	{ kATDeviceButton_IndusGTId,            "Indus GT: ID" },
	{ kATDeviceButton_IndusGTBootCPM,       "Indus GT: Boot CP/M" },
	{ kATDeviceButton_IndusGTChangeDensity, "Indus GT: Change Density" },
	{ kATDeviceButton_HappySlow,            "Happy: Slow" },
	{ kATDeviceButton_HappyWPEnable,        "Happy: Write Protect" },
	{ kATDeviceButton_HappyWPDisable,       "Happy: Write Enable" },
	{ kATDeviceButton_ATR8000Reset,         "ATR8000: Reset" },
	{ kATDeviceButton_XELCFSwap,            "XEL-CF3: Swap" },
};

///////////////////////////////////////////////////////////////////////////
// Memory mode table (sorted by size for menu display)
///////////////////////////////////////////////////////////////////////////

struct MemoryModeEntry { ATMemoryMode mode; const char *label; };
static const MemoryModeEntry kMemoryModes[] = {
	{ kATMemoryMode_8K,    "8K" },
	{ kATMemoryMode_16K,   "16K" },
	{ kATMemoryMode_24K,   "24K" },
	{ kATMemoryMode_32K,   "32K" },
	{ kATMemoryMode_40K,   "40K" },
	{ kATMemoryMode_48K,   "48K" },
	{ kATMemoryMode_52K,   "52K" },
	{ kATMemoryMode_64K,   "64K" },
	{ kATMemoryMode_128K,  "128K" },
	{ kATMemoryMode_256K,  "256K" },
	{ kATMemoryMode_320K,  "320K (Rambo)" },
	{ kATMemoryMode_576K,  "576K (Rambo)" },
	{ kATMemoryMode_1088K, "1088K" },
};

///////////////////////////////////////////////////////////////////////////
// ROM set export entries
///////////////////////////////////////////////////////////////////////////

struct RomExport { uint64 fwId; const char *filename; };
static const RomExport kRomExports[] = {
	{ kATFirmwareId_Kernel_LLE,    "altirraos-800.rom" },
	{ kATFirmwareId_Kernel_LLEXL,  "altirraos-xl.rom" },
	{ kATFirmwareId_Kernel_816,    "altirraos-816.rom" },
	{ kATFirmwareId_5200_LLE,      "altirraos-5200.rom" },
	{ kATFirmwareId_Basic_ATBasic, "atbasic.rom" },
};

///////////////////////////////////////////////////////////////////////////
// Disk drive action helpers
///////////////////////////////////////////////////////////////////////////

enum DiskDriveAction {
	kDiskAction_Mount = 0,
	kDiskAction_NewDisk,
	kDiskAction_Save,
	kDiskAction_SaveAs,
	kDiskAction_ReadOnly,
	kDiskAction_Explore,
	kDiskAction_Unmount,
	kDiskActionCount
};

// Quick save state (in-memory)
static vdrefptr<IATSerializable> s_pQuickState;

///////////////////////////////////////////////////////////////////////////
// Paste text to emulator via POKEY
///////////////////////////////////////////////////////////////////////////

static struct CharToScanCodeInit {
	uint8 table[256];
	constexpr CharToScanCodeInit() : table{} {
		for (auto& v : table)
			v = 0xFF;

		table['a'] = 0x3F; table['b'] = 0x15; table['c'] = 0x12;
		table['d'] = 0x3A; table['e'] = 0x2A; table['f'] = 0x38;
		table['g'] = 0x3D; table['h'] = 0x39; table['i'] = 0x0D;
		table['j'] = 0x01; table['k'] = 0x05; table['l'] = 0x00;
		table['m'] = 0x25; table['n'] = 0x23; table['o'] = 0x08;
		table['p'] = 0x0A; table['q'] = 0x2F; table['r'] = 0x28;
		table['s'] = 0x3E; table['t'] = 0x2D; table['u'] = 0x0B;
		table['v'] = 0x10; table['w'] = 0x2E; table['x'] = 0x16;
		table['y'] = 0x2B; table['z'] = 0x17;

		table['A'] = 0x7F; table['B'] = 0x55; table['C'] = 0x52;
		table['D'] = 0x7A; table['E'] = 0x6A; table['F'] = 0x78;
		table['G'] = 0x7D; table['H'] = 0x79; table['I'] = 0x4D;
		table['J'] = 0x41; table['K'] = 0x45; table['L'] = 0x40;
		table['M'] = 0x65; table['N'] = 0x63; table['O'] = 0x48;
		table['P'] = 0x4A; table['Q'] = 0x6F; table['R'] = 0x68;
		table['S'] = 0x7E; table['T'] = 0x6D; table['U'] = 0x4B;
		table['V'] = 0x50; table['W'] = 0x6E; table['X'] = 0x56;
		table['Y'] = 0x6B; table['Z'] = 0x57;

		table['0'] = 0x32; table['1'] = 0x1F; table['2'] = 0x1E;
		table['3'] = 0x1A; table['4'] = 0x18; table['5'] = 0x1D;
		table['6'] = 0x1B; table['7'] = 0x33; table['8'] = 0x35;
		table['9'] = 0x30;

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

	VDStringW ws = VDTextU8ToW(VDStringSpanA(text));
	SDL_free(text);

	auto& pokey = g_sim.GetPokey();

	for (size_t i = 0; i < ws.size(); ++i) {
		wchar_t c = ws[i];

		if (!c || (c >= 0x200B && c <= 0x200F) || c == 0xFEFF)
			continue;

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

		if (c < 0x100) {
			uint8 sc = s_charToScanCode.table[(uint8)c];
			if (sc != 0xFF)
				pokey.PushKey(sc, false, true, false, true);
		}
	}

	ATImGuiShowToast("Text pasted");
}

///////////////////////////////////////////////////////////////////////////
// MRU (Recent Files) — uses same registry format as ImGui/Windows build
///////////////////////////////////////////////////////////////////////////

void MRUAdd(const wchar_t *path) {
	VDRegistryAppKey key("MRU List", true);

	VDStringW order;
	key.getString("Order", order);

	// Check if already present — promote to front
	VDStringW existing;
	for (size_t i = 0; i < order.size(); ++i) {
		char keyname[2] = { (char)order[i], 0 };
		key.getString(keyname, existing);
		if (existing.comparei(path) == 0) {
			wchar_t c = order[i];
			order.erase(i, 1);
			order.insert(order.begin(), c);
			key.setString("Order", order.c_str());
			return;
		}
	}

	// Add new entry (max 10)
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

///////////////////////////////////////////////////////////////////////////
// Special cartridge definitions
///////////////////////////////////////////////////////////////////////////

struct SpecialCartEntry {
	ATCartridgeMode mode;
	const char *name;
};

static const SpecialCartEntry kSpecialCarts[] = {
	{ kATCartridgeMode_SuperCharger3D,       "SuperCharger 3D" },
	{ kATCartridgeMode_MaxFlash_128K,        "MaxFlash 128K" },
	{ kATCartridgeMode_MaxFlash_128K_MyIDE,  "MaxFlash 128K (MyIDE)" },
	{ kATCartridgeMode_MaxFlash_1024K,       "MaxFlash 1MB" },
	{ kATCartridgeMode_MaxFlash_1024K_Bank0, "MaxFlash 1MB (Bank 0)" },
	{ kATCartridgeMode_JAtariCart_128K,      "JAtariCart 128K" },
	{ kATCartridgeMode_JAtariCart_256K,      "JAtariCart 256K" },
	{ kATCartridgeMode_JAtariCart_512K,      "JAtariCart 512K" },
	{ kATCartridgeMode_JAtariCart_1024K,     "JAtariCart 1024K" },
	{ kATCartridgeMode_DCart,                "DCart" },
	{ kATCartridgeMode_SIC_128K,             "SIC! 128K" },
	{ kATCartridgeMode_SIC_256K,             "SIC! 256K" },
	{ kATCartridgeMode_SIC_512K,             "SIC! 512K" },
	{ kATCartridgeMode_SICPlus,              "SIC+" },
	{ kATCartridgeMode_MegaCart_512K,        "MegaCart 512K" },
	{ kATCartridgeMode_MegaCart_4M_3,        "MegaCart 4MB" },
	{ kATCartridgeMode_TheCart_32M,          "TheCart 32MB" },
	{ kATCartridgeMode_TheCart_64M,          "TheCart 64MB" },
	{ kATCartridgeMode_TheCart_128M,         "TheCart 128MB" },
};

static_assert(std::size(kSpecialCarts) <= (ID_SPECIAL_CART_LAST - ID_SPECIAL_CART_FIRST + 1),
	"Too many special carts for ID range");

///////////////////////////////////////////////////////////////////////////
// Menu bar construction
///////////////////////////////////////////////////////////////////////////

wxMenuBar *ATMainFrame::CreateMenuBar() {
	wxMenuBar *menuBar = new wxMenuBar;

	// ===== System menu =====
	wxMenu *systemMenu = new wxMenu;

	wxMenu *hwMenu = new wxMenu;
	hwMenu->AppendRadioItem(ID_HW_800, "Atari &800");
	hwMenu->AppendRadioItem(ID_HW_800XL, "Atari 800&XL");
	hwMenu->AppendRadioItem(ID_HW_1200XL, "Atari &1200XL");
	hwMenu->AppendRadioItem(ID_HW_130XE, "Atari 130X&E");
	hwMenu->AppendRadioItem(ID_HW_XEGS, "Atari XE&GS");
	hwMenu->AppendRadioItem(ID_HW_5200, "Atari &5200");
	systemMenu->AppendSubMenu(hwMenu, "&Hardware Mode");

	// Kernel submenu — built dynamically in OnMenuOpen, but static items here
	mpKernelMenu = new wxMenu;
	mpKernelMenu->AppendRadioItem(ID_KERNEL_AUTOSELECT, "[&Autoselect]");
	mpKernelMenu->AppendRadioItem(ID_KERNEL_INTERNAL_OSB, "Internal &OS-B");
	mpKernelMenu->AppendRadioItem(ID_KERNEL_INTERNAL_XL, "Internal &XL OS");
	mpKernelMenu->AppendRadioItem(ID_KERNEL_INTERNAL_5200, "Internal &5200 OS");
	// User firmware items added dynamically in OnMenuOpen
	systemMenu->AppendSubMenu(mpKernelMenu, "&Kernel");

	// Memory submenu
	wxMenu *memMenu = new wxMenu;
	for (size_t i = 0; i < std::size(kMemoryModes) && i <= (size_t)(ID_MEMORY_LAST - ID_MEMORY_FIRST); ++i)
		memMenu->AppendRadioItem(ID_MEMORY_FIRST + (int)i, kMemoryModes[i].label);
	systemMenu->AppendSubMenu(memMenu, "&Memory");

	wxMenu *vidStdMenu = new wxMenu;
	vidStdMenu->AppendRadioItem(ID_VIDEOSTD_NTSC, "&NTSC");
	vidStdMenu->AppendRadioItem(ID_VIDEOSTD_PAL, "&PAL");
	vidStdMenu->AppendRadioItem(ID_VIDEOSTD_SECAM, "&SECAM");
	vidStdMenu->AppendRadioItem(ID_VIDEOSTD_NTSC50, "NTSC-&50");
	vidStdMenu->AppendRadioItem(ID_VIDEOSTD_PAL60, "PAL-6&0");
	systemMenu->AppendSubMenu(vidStdMenu, "&Video Standard");

	systemMenu->AppendCheckItem(ID_SYSTEM_TOGGLE_BASIC, "&BASIC");
	systemMenu->AppendSeparator();

	wxMenu *consoleMenu = new wxMenu;
	consoleMenu->Append(ID_CONSOLE_START, "S&tart");
	consoleMenu->Append(ID_CONSOLE_SELECT, "&Select");
	consoleMenu->Append(ID_CONSOLE_OPTION, "&Option");
	consoleMenu->AppendSeparator();
	consoleMenu->Append(ID_CONSOLE_RELEASE_ALL, "&Release All");
	systemMenu->AppendSubMenu(consoleMenu, "&Console Switches");

	// Device Buttons submenu (dynamic — only shows if devices with buttons exist)
	mpDeviceButtonsMenu = new wxMenu;
	for (size_t i = 0; i < std::size(kDeviceButtons) && i <= (size_t)(ID_DEVBTN_LAST - ID_DEVBTN_FIRST); ++i)
		mpDeviceButtonsMenu->Append(ID_DEVBTN_FIRST + (int)i, kDeviceButtons[i].name);
	systemMenu->AppendSubMenu(mpDeviceButtonsMenu, "Device &Buttons");

	// Power-On Delay submenu
	wxMenu *powerOnMenu = new wxMenu;
	powerOnMenu->AppendRadioItem(ID_POWERON_AUTO, "&Auto");
	powerOnMenu->AppendRadioItem(ID_POWERON_NONE, "&None");
	powerOnMenu->AppendRadioItem(ID_POWERON_1SEC, "&1 second");
	powerOnMenu->AppendRadioItem(ID_POWERON_2SEC, "&2 seconds");
	powerOnMenu->AppendRadioItem(ID_POWERON_3SEC, "&3 seconds");
	systemMenu->AppendSubMenu(powerOnMenu, "Power-On &Delay");

	systemMenu->AppendCheckItem(ID_SYSTEM_HOLD_KEYS_FOR_RESET, "&Hold Keys for Reset");
	systemMenu->AppendCheckItem(ID_SYSTEM_AUTO_BOOT_TAPE, "Auto-Boot &Tape (Hold Start)");

	systemMenu->AppendSeparator();
	systemMenu->Append(ID_SYSTEM_WARM_RESET, "&Warm Reset\tF5");
	systemMenu->Append(ID_SYSTEM_COLD_RESET, "&Cold Reset\tShift+F5");
	systemMenu->AppendSeparator();
	systemMenu->AppendCheckItem(ID_SYSTEM_TOGGLE_REWIND, "Enable &Rewind");
	systemMenu->Append(ID_SYSTEM_REWIND, "Re&wind");
	systemMenu->AppendSeparator();

	wxMenu *configMenu = new wxMenu;
	configMenu->Append(ID_CONFIGURE_SYSTEM, "&System Settings...");
	configMenu->Append(ID_CONFIGURE_CPU, "&CPU && Memory...");
	configMenu->Append(ID_CONFIGURE_BOOT, "&Boot && Acceleration...");
	configMenu->Append(ID_CONFIGURE_KEYBOARD, "&Keyboard...");
	configMenu->Append(ID_CONFIGURE_AUDIO, "&Audio...");
	configMenu->Append(ID_CONFIGURE_VIDEO, "&Video...");
	configMenu->AppendSeparator();
	configMenu->Append(ID_CONFIGURE_INPUT, "&Input Setup...");
	configMenu->Append(ID_INPUT_ON_SCREEN_KEYBOARD, "On-Screen &Keyboard...");
	systemMenu->AppendSubMenu(configMenu, "&Configure");

	systemMenu->Append(ID_SYSTEM_CYCLE_QUICK_MAPS, "Cycle Quick &Maps\tShift+F1");
	systemMenu->AppendSeparator();
	systemMenu->Append(ID_SYSTEM_SAVE_SETTINGS, "&Save Settings\tCtrl+S");
	systemMenu->AppendSeparator();
	systemMenu->Append(ID_SYSTEM_QUIT, "&Quit\tCtrl+Q");
	menuBar->Append(systemMenu, "&System");

	// ===== Profiles menu =====
	mpProfilesMenu = new wxMenu;
	// Items added dynamically in OnMenuOpen
	menuBar->Append(mpProfilesMenu, "P&rofiles");

	// ===== File menu =====
	wxMenu *fileMenu = new wxMenu;
	fileMenu->Append(ID_FILE_OPEN_IMAGE, "&Open Image...\tCtrl+O");
	fileMenu->Append(ID_FILE_BOOT_IMAGE, "&Boot Image...\tCtrl+Shift+O");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_QUICK_SAVE_STATE, "Quick &Save State\tF7");
	fileMenu->Append(ID_FILE_QUICK_LOAD_STATE, "Quick &Load State\tF8");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_SAVE_STATE, "Save State &As...");
	fileMenu->Append(ID_FILE_LOAD_STATE, "&Load State...");
	fileMenu->AppendSeparator();

	// Per-drive disk submenus (D1-D15, built dynamically in OnMenuOpen)
	mpDiskDrivesMenu = new wxMenu;
	// Static items at bottom (dynamic per-drive items added in OnMenuOpen)
	for (int i = 0; i < 15; ++i) {
		wxMenu *dm = new wxMenu;
		int base = ID_DISK_DRIVE_FIRST + i * kDiskActionCount;
		dm->Append(base + kDiskAction_Mount,   wxString::Format("Mount D%d...", i + 1));
		dm->Append(base + kDiskAction_NewDisk,  wxString::Format("New Disk in D%d...", i + 1));
		dm->AppendSeparator();
		dm->Append(base + kDiskAction_Save,     wxString::Format("Save D%d", i + 1));
		dm->Append(base + kDiskAction_SaveAs,   wxString::Format("Save D%d As...", i + 1));
		dm->AppendSeparator();
		dm->AppendCheckItem(base + kDiskAction_ReadOnly, "Read Only");
		dm->Append(base + kDiskAction_Explore, "Explore...");
		dm->AppendSeparator();
		dm->Append(base + kDiskAction_Unmount,  wxString::Format("Unmount D%d", i + 1));
		mpDiskDrivesMenu->AppendSubMenu(dm, wxString::Format("D%d: [empty]", i + 1));
	}
	mpDiskDrivesMenu->AppendSeparator();
	mpDiskDrivesMenu->Append(ID_DISK_ROTATE_NEXT, "Rotate &Down");
	mpDiskDrivesMenu->Append(ID_DISK_ROTATE_PREV, "Rotate &Up");
	mpDiskDrivesMenu->AppendSeparator();
	mpDiskDrivesMenu->Append(ID_DISK_SAVE_ALL_MODIFIED, "&Save All Modified");
	mpDiskDrivesMenu->Append(ID_DISK_UNMOUNT_ALL, "Unmount &All");
	fileMenu->AppendSubMenu(mpDiskDrivesMenu, "&Disk Drives");

	fileMenu->Append(ID_CART_ATTACH, "Attach &Cartridge...");
	fileMenu->Append(ID_CART_DETACH, "De&tach Cartridge");

	wxMenu *specialCartMenu = new wxMenu;
	for (size_t i = 0; i < std::size(kSpecialCarts); ++i)
		specialCartMenu->Append(ID_SPECIAL_CART_FIRST + (int)i, kSpecialCarts[i].name);
	fileMenu->AppendSubMenu(specialCartMenu, "Attach &Special Cartridge");

	fileMenu->Append(ID_CART_SAVE, "Sa&ve Cartridge...");

	wxMenu *saveFwMenu = new wxMenu;
	saveFwMenu->Append(ID_SAVE_FW_IDE_MAIN, "IDE Main Firmware...");
	saveFwMenu->Append(ID_SAVE_FW_IDE_SDX, "IDE SDX Firmware...");
	saveFwMenu->Append(ID_SAVE_FW_U1MB, "Ultimate1MB Firmware...");
	saveFwMenu->Append(ID_SAVE_FW_RAPIDUS, "Rapidus Flash...");
	fileMenu->AppendSubMenu(saveFwMenu, "Save &Firmware");

	fileMenu->AppendSeparator();
	fileMenu->Append(ID_CART_ATTACH_SECONDARY, "Attach Secon&dary Cartridge...");
	fileMenu->Append(ID_CART_DETACH_SECONDARY, "Detach S&econdary Cartridge");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_CASSETTE_LOAD, "Load C&assette...");
	fileMenu->Append(ID_CASSETTE_UNLOAD, "Unload Casse&tte");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_SAVE_SCREENSHOT, "Save &Screenshot...\tF9");
	fileMenu->Append(ID_FILE_SAVE_SCREENSHOT_TRUE_ASPECT, "Save Screenshot (&True Aspect)...");

	// Recent Files submenu (populated dynamically in OnMenuUpdateUI)
	fileMenu->AppendSeparator();
	mpMRUMenu = new wxMenu;
	fileMenu->AppendSubMenu(mpMRUMenu, "&Recent Files");

	menuBar->Append(fileMenu, "&File");

	// ===== Edit menu =====
	wxMenu *editMenu = new wxMenu;
	editMenu->Append(ID_EDIT_PASTE_TEXT, "Paste &Text\tCtrl+V");
	editMenu->Append(ID_EDIT_COPY_FRAME, "Copy &Frame to Clipboard");
	menuBar->Append(editMenu, "&Edit");

	// ===== View menu =====
	wxMenu *viewMenu = new wxMenu;
	viewMenu->AppendCheckItem(ID_VIEW_TOGGLE_FPS, "Show &FPS");
	viewMenu->AppendCheckItem(ID_VIEW_TOGGLE_STATUSBAR, "Show &Status Bar");

	wxMenu *filterMenu = new wxMenu;
	filterMenu->AppendRadioItem(ID_FILTER_POINT, "&Point");
	filterMenu->AppendRadioItem(ID_FILTER_BILINEAR, "&Bilinear");
	filterMenu->AppendRadioItem(ID_FILTER_SHARP_BILINEAR, "&Sharp Bilinear");
	filterMenu->AppendRadioItem(ID_FILTER_BICUBIC, "Bi&cubic");
	filterMenu->AppendRadioItem(ID_FILTER_DEFAULT, "&Default");
	viewMenu->AppendSubMenu(filterMenu, "Display &Filter");

	wxMenu *stretchMenu = new wxMenu;
	stretchMenu->AppendRadioItem(ID_STRETCH_FIT, "&Fit to Window");
	stretchMenu->AppendRadioItem(ID_STRETCH_ASPECT, "Preserve &Aspect Ratio");
	stretchMenu->AppendRadioItem(ID_STRETCH_ASPECT_INT, "Preserve Aspect Ratio (&Integer)");
	stretchMenu->AppendRadioItem(ID_STRETCH_SQUARE, "&Square Pixels");
	stretchMenu->AppendRadioItem(ID_STRETCH_SQUARE_INT, "Square Pixels (I&nteger)");
	viewMenu->AppendSubMenu(stretchMenu, "&Stretch Mode");

	wxMenu *winSizeMenu = new wxMenu;
	winSizeMenu->Append(ID_WINSIZE_1X, "&1x");
	winSizeMenu->Append(ID_WINSIZE_2X, "&2x");
	winSizeMenu->Append(ID_WINSIZE_3X, "&3x");
	winSizeMenu->Append(ID_WINSIZE_4X, "&4x");
	viewMenu->AppendSubMenu(winSizeMenu, "&Window Size");

	wxMenu *enhTextMenu = new wxMenu;
	enhTextMenu->AppendRadioItem(ID_ENHTEXT_NONE, "&None");
	enhTextMenu->AppendRadioItem(ID_ENHTEXT_HARDWARE, "&Hardware");
	enhTextMenu->AppendRadioItem(ID_ENHTEXT_SOFTWARE, "&Software (CIO)");
	viewMenu->AppendSubMenu(enhTextMenu, "&Enhanced Text");

	wxMenu *overscanMenu = new wxMenu;
	overscanMenu->AppendRadioItem(ID_OVERSCAN_NORMAL, "&Normal (168cc)");
	overscanMenu->AppendRadioItem(ID_OVERSCAN_EXTENDED, "&Extended (192cc)");
	overscanMenu->AppendRadioItem(ID_OVERSCAN_FULL, "&Full (228cc)");
	overscanMenu->AppendRadioItem(ID_OVERSCAN_OS_SCREEN, "&OS Screen (160cc)");
	overscanMenu->AppendRadioItem(ID_OVERSCAN_WIDESCREEN, "&Widescreen (176cc)");
	viewMenu->AppendSubMenu(overscanMenu, "O&verscan Mode");

	wxMenu *vertMenu = new wxMenu;
	vertMenu->AppendRadioItem(ID_VERT_DEFAULT, "&Default");
	vertMenu->AppendRadioItem(ID_VERT_OS_SCREEN, "&OS Screen (192)");
	vertMenu->AppendRadioItem(ID_VERT_NORMAL, "&Normal (224)");
	vertMenu->AppendRadioItem(ID_VERT_EXTENDED, "&Extended (240)");
	vertMenu->AppendRadioItem(ID_VERT_FULL, "&Full");
	viewMenu->AppendSubMenu(vertMenu, "Vertical O&verride");

	wxMenu *artifactMenu = new wxMenu;
	artifactMenu->AppendRadioItem(ID_ARTIFACT_NONE, "&None");
	artifactMenu->AppendRadioItem(ID_ARTIFACT_NTSC, "N&TSC");
	artifactMenu->AppendRadioItem(ID_ARTIFACT_PAL, "&PAL");
	artifactMenu->AppendRadioItem(ID_ARTIFACT_NTSC_HI, "NTSC &Hi");
	artifactMenu->AppendRadioItem(ID_ARTIFACT_PAL_HI, "PAL H&i");
	artifactMenu->AppendRadioItem(ID_ARTIFACT_AUTO, "&Auto");
	artifactMenu->AppendRadioItem(ID_ARTIFACT_AUTO_HI, "Auto Hi&-Res");
	viewMenu->AppendSubMenu(artifactMenu, "Ar&tifacting");

	viewMenu->AppendSeparator();
	viewMenu->AppendCheckItem(ID_VIEW_TOGGLE_VSYNC, "&Vertical Sync");
	viewMenu->AppendCheckItem(ID_VIEW_TOGGLE_FRAME_BLENDING, "Frame &Blending");
	viewMenu->AppendCheckItem(ID_VIEW_TOGGLE_CONFINE_MOUSE, "&Confine Mouse in Fullscreen");
	viewMenu->AppendCheckItem(ID_VIEW_TOGGLE_AUTO_HIDE_CURSOR, "Auto-&Hide Cursor");
	viewMenu->AppendSeparator();
	viewMenu->Append(ID_VIEW_TOGGLE_AUDIO_MONITOR, "Audio &Monitor...");
	viewMenu->Append(ID_VIEW_TOGGLE_AUDIO_SCOPE, "Audio Sc&ope...");
	viewMenu->Append(ID_VIEW_VIDEO_SETTINGS, "&Video Settings...");
	viewMenu->Append(ID_VIEW_COLOR_SETTINGS, "&Color Settings...");
	viewMenu->AppendSeparator();
	viewMenu->AppendCheckItem(ID_VIEW_TOGGLE_FULLSCREEN, "F&ullscreen\tF11");
	menuBar->Append(viewMenu, "&View");

	// ===== Speed menu =====
	wxMenu *speedMenu = new wxMenu;
	speedMenu->Append(ID_SPEED_TOGGLE_PAUSE, "&Pause / Resume\tPause");
	speedMenu->AppendCheckItem(ID_SPEED_TOGGLE_TURBO, "&Turbo");
	speedMenu->AppendCheckItem(ID_SPEED_TOGGLE_SLOW, "&Slow Motion");
	speedMenu->AppendSeparator();
	speedMenu->AppendRadioItem(ID_SPEED_50, "&50%");
	speedMenu->AppendRadioItem(ID_SPEED_100, "&100%");
	speedMenu->AppendRadioItem(ID_SPEED_200, "&200%");
	speedMenu->AppendRadioItem(ID_SPEED_400, "&400%");
	speedMenu->Append(ID_SPEED_CUSTOM_FIRST, "&Custom Speed...");
	speedMenu->AppendSeparator();
	speedMenu->AppendCheckItem(ID_SPEED_TOGGLE_MUTE, "&Mute Audio\tF4");
	speedMenu->AppendCheckItem(ID_SPEED_PAUSE_INACTIVE, "Pause When &Inactive");
	menuBar->Append(speedMenu, "S&peed");

	// ===== Debug menu =====
	wxMenu *debugMenu = new wxMenu;
	debugMenu->Append(ID_DEBUG_RUN_STOP, "&Run / Break\tF8");
	debugMenu->AppendSeparator();
	debugMenu->Append(ID_DEBUG_STEP_INTO, "Step &Into\tF11");
	debugMenu->Append(ID_DEBUG_STEP_OVER, "Step &Over\tF10");
	debugMenu->Append(ID_DEBUG_STEP_OUT, "Step Ou&t\tShift+F11");
	debugMenu->AppendSeparator();
	debugMenu->AppendCheckItem(ID_DEBUG_TOGGLE_DEBUGGER, "Enable &Debugger");
	debugMenu->AppendCheckItem(ID_DEBUG_TOGGLE_BREAK_AT_EXE, "&Break at EXE Run Address");
	debugMenu->AppendCheckItem(ID_DEBUG_TOGGLE_AUTO_RELOAD_ROMS, "&Auto-Reload ROMs");
	debugMenu->AppendSeparator();
	debugMenu->Append(ID_DEBUG_LOAD_SYMBOLS, "&Load Symbols...");
	debugMenu->Append(ID_DEBUG_UNLOAD_ALL_SYMBOLS, "&Unload All Symbols");
	debugMenu->AppendSeparator();
	debugMenu->AppendCheckItem(ID_DEBUG_AUTO_LOAD_KERNEL_SYMBOLS, "Auto-Load &Kernel Symbols");
	debugMenu->AppendCheckItem(ID_DEBUG_AUTO_LOAD_SYSTEM_SYMBOLS, "Auto-Load &System Symbols");
	debugMenu->AppendCheckItem(ID_DEBUG_DEBUG_LINK, "Debug &Link");
	menuBar->Append(debugMenu, "&Debug");

	// ===== Tools menu =====
	wxMenu *toolsMenu = new wxMenu;
	toolsMenu->Append(ID_TOOLS_FIRMWARE_MANAGER, "&Firmware Manager...");
	toolsMenu->Append(ID_TOOLS_DEVICE_MANAGER, "&Device Manager...");
	toolsMenu->Append(ID_TOOLS_CARTRIDGE_BROWSER, "&Cartridge Browser...");
	toolsMenu->Append(ID_TOOLS_CASSETTE_CONTROL, "C&assette Control...");
	toolsMenu->Append(ID_TOOLS_TAPE_EDITOR, "&Tape Editor...");
	toolsMenu->Append(ID_TOOLS_PROFILE_MANAGER, "&Profile Manager...");
	toolsMenu->Append(ID_TOOLS_CHEAT_ENGINE, "Ch&eat Engine...");
	toolsMenu->Append(ID_TOOLS_COMPAT_BROWSER, "Compati&bility Database...");
	toolsMenu->Append(ID_TOOLS_AUDIO_MONITOR, "Audio &Monitor/Scope...");
	toolsMenu->Append(ID_TOOLS_DISK_EXPLORER, "&Disk Explorer...");
	toolsMenu->AppendSeparator();
	toolsMenu->Append(ID_TOOLS_RECORD_VIDEO, "&Record Video...");
	toolsMenu->Append(ID_TOOLS_RECORD_AUDIO_WAV, "Record &Audio (WAV)...");
	toolsMenu->Append(ID_TOOLS_RECORD_AUDIO_PCM, "Record Raw Audio (&PCM)...");
	toolsMenu->Append(ID_TOOLS_RECORD_SAP, "Record &SAP Type-R...");
	toolsMenu->Append(ID_TOOLS_RECORD_VGM, "Record V&GM...");
	toolsMenu->Append(ID_TOOLS_VIDEO_PAUSE_RESUME, "Pause Recording");
	toolsMenu->Append(ID_TOOLS_STOP_RECORDING, "&Stop Recording");
	toolsMenu->AppendSeparator();
	toolsMenu->Append(ID_TOOLS_EXPORT_ROM_SET, "&Export ROM Set...");
	toolsMenu->Append(ID_TOOLS_CONVERT_SAP_TO_EXE, "Convert &SAP to EXE...");
	toolsMenu->Append(ID_TOOLS_ANALYZE_TAPE_DECODING, "Analyze &Tape Decoding...");
	toolsMenu->AppendSeparator();
	toolsMenu->Append(ID_TOOLS_ADVANCED_CONFIG, "&Advanced Configuration...");
	toolsMenu->AppendSeparator();
	toolsMenu->Append(ID_TOOLS_OPEN_CONFIG_DIR, "Open &Config Directory");
	toolsMenu->Append(ID_TOOLS_OPEN_FIRMWARE_DIR, "Open F&irmware Directory");
	menuBar->Append(toolsMenu, "&Tools");

	// ===== Help menu =====
	wxMenu *helpMenu = new wxMenu;
	helpMenu->Append(ID_HELP_KEYBOARD_SHORTCUTS, "&Keyboard Shortcuts...");
	helpMenu->AppendSeparator();
	helpMenu->Append(ID_HELP_HOME_PAGE, "&Home Page...");
	helpMenu->Append(ID_HELP_CHANGELOG, "Change &Log...");
	helpMenu->AppendSeparator();
	helpMenu->Append(ID_HELP_CHECK_FOR_UPDATES, "Check for &Updates...");
	helpMenu->AppendSeparator();
	helpMenu->Append(ID_HELP_ABOUT, "&About Altirra...");
	menuBar->Append(helpMenu, "&Help");

	return menuBar;
}

///////////////////////////////////////////////////////////////////////////
// Menu event handler
///////////////////////////////////////////////////////////////////////////

void ATMainFrame::OnMenuCommand(wxCommandEvent& event) {
	int id = event.GetId();

	switch (id) {
		// ---- System ----
		case ID_SYSTEM_WARM_RESET:
			g_sim.WarmReset();
			ATImGuiShowToast("Warm reset");
			break;

		case ID_SYSTEM_COLD_RESET:
			g_sim.ColdReset();
			ATImGuiShowToast("Cold reset");
			break;

		case ID_SYSTEM_TOGGLE_BASIC:
			g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled());
			break;

		case ID_SYSTEM_TOGGLE_PAUSE_INACTIVE: {
			bool cur = ATUIGetPauseWhenInactive();
			ATUISetPauseWhenInactive(!cur);
			break;
		}

		case ID_SYSTEM_TOGGLE_REWIND: {
			IATAutoSaveManager& asMgr = g_sim.GetAutoSaveManager();
			asMgr.SetRewindEnabled(!asMgr.GetRewindEnabled());
			break;
		}

		case ID_SYSTEM_REWIND:
			if (g_sim.GetAutoSaveManager().GetRewindEnabled())
				g_sim.GetAutoSaveManager().Rewind();
			break;

		case ID_SYSTEM_SAVE_SETTINGS:
			ATLinuxSaveSettings();
			ATImGuiShowToast("Settings saved");
			break;

		case ID_SYSTEM_QUIT:
			Close();
			break;

		// ---- Hardware mode ----
		case ID_HW_800:    ATUISwitchHardwareMode(nullptr, kATHardwareMode_800, true); break;
		case ID_HW_800XL:  ATUISwitchHardwareMode(nullptr, kATHardwareMode_800XL, true); break;
		case ID_HW_1200XL: ATUISwitchHardwareMode(nullptr, kATHardwareMode_1200XL, true); break;
		case ID_HW_130XE:  ATUISwitchHardwareMode(nullptr, kATHardwareMode_130XE, true); break;
		case ID_HW_XEGS:   ATUISwitchHardwareMode(nullptr, kATHardwareMode_XEGS, true); break;
		case ID_HW_5200:   ATUISwitchHardwareMode(nullptr, kATHardwareMode_5200, true); break;

		// ---- Video standard ----
		case ID_VIDEOSTD_NTSC:   g_sim.SetVideoStandard(kATVideoStandard_NTSC); break;
		case ID_VIDEOSTD_PAL:    g_sim.SetVideoStandard(kATVideoStandard_PAL); break;
		case ID_VIDEOSTD_SECAM:  g_sim.SetVideoStandard(kATVideoStandard_SECAM); break;
		case ID_VIDEOSTD_NTSC50: g_sim.SetVideoStandard(kATVideoStandard_NTSC50); break;
		case ID_VIDEOSTD_PAL60:  g_sim.SetVideoStandard(kATVideoStandard_PAL60); break;

		// ---- Console switches ----
		case ID_CONSOLE_START:
			g_sim.GetGTIA().SetConsoleSwitch(0x01, true);
			break;
		case ID_CONSOLE_SELECT:
			g_sim.GetGTIA().SetConsoleSwitch(0x02, true);
			break;
		case ID_CONSOLE_OPTION:
			g_sim.GetGTIA().SetConsoleSwitch(0x04, true);
			break;
		case ID_CONSOLE_RELEASE_ALL:
			g_sim.GetGTIA().SetConsoleSwitch(0x07, false);
			break;

		// ---- Kernel ROM ----
		case ID_KERNEL_AUTOSELECT:
			g_sim.SetKernel(0);
			break;
		case ID_KERNEL_INTERNAL_OSB:
			g_sim.SetKernel(kATFirmwareId_Kernel_LLE);
			break;
		case ID_KERNEL_INTERNAL_XL:
			g_sim.SetKernel(kATFirmwareId_Kernel_LLEXL);
			break;
		case ID_KERNEL_INTERNAL_5200:
			g_sim.SetKernel(kATFirmwareId_5200_LLE);
			break;

		// ---- Memory mode ----
		// (handled via range check in default case below)

		// ---- Power-On Delay ----
		case ID_POWERON_AUTO: g_sim.SetPowerOnDelay(-1); break;
		case ID_POWERON_NONE: g_sim.SetPowerOnDelay(0); break;
		case ID_POWERON_1SEC: g_sim.SetPowerOnDelay(10); break;
		case ID_POWERON_2SEC: g_sim.SetPowerOnDelay(20); break;
		case ID_POWERON_3SEC: g_sim.SetPowerOnDelay(30); break;

		// ---- System toggles ----
		case ID_SYSTEM_HOLD_KEYS_FOR_RESET: {
			uint8 held = g_sim.GetPendingHeldSwitches();
			if (held)
				g_sim.SetPendingHeldSwitches(0);
			else
				g_sim.SetPendingHeldSwitches(0x03);
			break;
		}

		case ID_SYSTEM_AUTO_BOOT_TAPE:
			g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled());
			break;

		case ID_SYSTEM_CYCLE_QUICK_MAPS: {
			auto *pIM = g_sim.GetInputManager();
			if (pIM) {
				ATInputMap *pMap = pIM->CycleQuickMaps();
				if (pMap) {
					const wchar_t *mapName = pMap->GetName();
					VDStringA name = VDTextWToU8(mapName, -1);
					char msg[128];
					snprintf(msg, sizeof(msg), "Quick map: %s", name.c_str());
					ATImGuiShowToast(msg);
				}
			}
			break;
		}

		// ---- File operations ----
		case ID_FILE_OPEN_IMAGE: {
			wxFileDialog dlg(this, "Open Image", "", "",
				"All supported|*.atr;*.atx;*.xfd;*.dcm;*.pro;*.xex;*.obx;*.com;*.car;*.rom;*.bin;*.cas;*.wav;*.atz;*.gz"
				"|Disk images (*.atr;*.atx;*.xfd;*.dcm;*.pro)|*.atr;*.atx;*.xfd;*.dcm;*.pro"
				"|Executables (*.xex;*.obx;*.com)|*.xex;*.obx;*.com"
				"|Cartridges (*.car;*.rom;*.bin)|*.car;*.rom;*.bin"
				"|Cassettes (*.cas;*.wav)|*.cas;*.wav"
				"|All files (*)|*",
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
					MRUAdd(path.c_str());
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Load Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_FILE_BOOT_IMAGE: {
			wxFileDialog dlg(this, "Boot Image", "", "",
				"All supported|*.atr;*.atx;*.xfd;*.dcm;*.pro;*.xex;*.obx;*.com;*.car;*.rom;*.bin;*.cas;*.wav;*.atz;*.gz"
				"|All files (*)|*",
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					g_sim.UnloadAll();
					g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
					g_sim.ColdReset();
					MRUAdd(path.c_str());
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Load Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		// ---- Recent Files ----
		case ID_MRU_FILE_0: case ID_MRU_FILE_1: case ID_MRU_FILE_2:
		case ID_MRU_FILE_3: case ID_MRU_FILE_4: case ID_MRU_FILE_5:
		case ID_MRU_FILE_6: case ID_MRU_FILE_7: case ID_MRU_FILE_8:
		case ID_MRU_FILE_9: {
			uint32 idx = id - ID_MRU_FILE_0;
			VDStringW wpath = MRUGet(idx);
			if (!wpath.empty()) {
				try {
					g_sim.Load(wpath.c_str(), kATMediaWriteMode_RO, nullptr);
					MRUAdd(wpath.c_str());
					VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(wpath.c_str())));
					char msg[256];
					snprintf(msg, sizeof(msg), "Loaded: %s", fname.c_str());
					ATImGuiShowToast(msg);
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Load Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_MRU_CLEAR:
			MRUClear();
			break;

		// ---- Save/Load State ----
		case ID_FILE_QUICK_SAVE_STATE:
			try {
				s_pQuickState.clear();
				vdrefptr<IATSerializable> snapInfo;
				g_sim.CreateSnapshot(~s_pQuickState, ~snapInfo);
				ATImGuiShowToast("State saved");
			} catch (...) {
				ATImGuiShowToast("Save state failed");
			}
			break;

		case ID_FILE_QUICK_LOAD_STATE:
			if (s_pQuickState) {
				try {
					ATStateLoadContext ctx {};
					g_sim.ApplySnapshot(*s_pQuickState, &ctx);
					ATImGuiShowToast("State loaded");
				} catch (...) {
					ATImGuiShowToast("Load state failed");
				}
			}
			break;

		case ID_FILE_SAVE_STATE: {
			wxFileDialog dlg(this, "Save State", "", "state.atstate2",
				"Save states (*.atstate2)|*.atstate2|All files (*)|*",
				wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW wpath = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					g_sim.SaveState(wpath.c_str());
					ATImGuiShowToast("State saved to file");
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Save State Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_FILE_LOAD_STATE: {
			wxFileDialog dlg(this, "Load State", "", "",
				"Save states (*.atstate2;*.altstate)|*.atstate2;*.altstate|All files (*)|*",
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW wpath = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					g_sim.Load(wpath.c_str(), kATMediaWriteMode_RO, nullptr);
					ATImGuiShowToast("State loaded");
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Load State Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_FILE_SAVE_SCREENSHOT: {
			VDPixmapBuffer pxbuf;
			VDPixmap px;
			if (!g_sim.GetGTIA().GetLastFrameBuffer(pxbuf, px)) {
				wxMessageBox("No frame available yet.", "Screenshot", wxOK | wxICON_INFORMATION, this);
				break;
			}
			wxFileDialog dlg(this, "Save Screenshot", "", "screenshot.png",
				"PNG files (*.png)|*.png|All files (*)|*",
				wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dlg.ShowModal() == wxID_OK) {
				try {
					VDStringW wpath = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
					ATSaveFrame(px, wpath.c_str());
					ATImGuiShowToast("Screenshot saved");
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Screenshot Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_FILE_SAVE_SCREENSHOT_TRUE_ASPECT: {
			VDPixmapBuffer pxbuf;
			VDPixmap px;
			if (!g_sim.GetGTIA().GetLastFrameBuffer(pxbuf, px)) {
				wxMessageBox("No frame available yet.", "Screenshot", wxOK | wxICON_INFORMATION, this);
				break;
			}
			wxFileDialog dlg(this, "Save Screenshot (True Aspect)", "", "screenshot_aspect.png",
				"PNG files (*.png)|*.png|All files (*)|*",
				wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dlg.ShowModal() == wxID_OK) {
				try {
					VDStringW wpath = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
					// Scale to true aspect ratio (roughly 7:5 PAR for NTSC)
					int srcW = px.w;
					int srcH = px.h;
					int dstW = srcW;
					int dstH = (int)(srcH * 1.2 + 0.5);
					VDPixmapBuffer dstBuf(dstW, dstH, nsVDPixmap::kPixFormat_XRGB8888);
					VDPixmap dst = dstBuf;
					VDPixmapStretchBltBilinear(dst, px);
					ATSaveFrame(dst, wpath.c_str());
					ATImGuiShowToast("True aspect screenshot saved");
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Screenshot Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		// ---- Edit ----
		case ID_EDIT_PASTE_TEXT:
			PasteTextToEmulator();
			break;

		case ID_EDIT_COPY_FRAME: {
			VDPixmapBuffer pxbuf;
			VDPixmap px;
			if (g_sim.GetGTIA().GetLastFrameBuffer(pxbuf, px)) {
				ATCopyFrameToClipboard(px);
				ATImGuiShowToast("Frame copied");
			}
			break;
		}

		// ---- Disk operations (per-drive D1-D15) ----
		case ID_DISK_UNMOUNT_ALL:
			for (int i = 0; i < 15; ++i)
				g_sim.GetDiskInterface(i).UnloadDisk();
			break;

		case ID_DISK_SAVE_ALL_MODIFIED: {
			int saved = 0;
			for (int i = 0; i < 15; ++i) {
				ATDiskInterface& di = g_sim.GetDiskInterface(i);
				if (di.IsDiskLoaded() && di.IsDirty()) {
					try { di.SaveDisk(); ++saved; } catch (...) {}
				}
			}
			char msg[64];
			snprintf(msg, sizeof(msg), "Saved %d disk(s)", saved);
			ATImGuiShowToast(msg);
			break;
		}

		case ID_DISK_ROTATE_NEXT:
		case ID_DISK_ROTATE_PREV: {
			int activeDrives = 0;
			for (int i = 14; i >= 0; --i) {
				if (g_sim.GetDiskDrive(i).IsEnabled() || g_sim.GetDiskInterface(i).GetClientCount() > 1) {
					activeDrives = i + 1;
					break;
				}
			}
			if (activeDrives > 0)
				g_sim.RotateDrives(activeDrives, id == ID_DISK_ROTATE_NEXT ? +1 : -1);
			break;
		}

		// ---- Cartridge ----
		case ID_CART_ATTACH: {
			wxFileDialog dlg(this, "Attach Cartridge", "", "",
				"Cartridge images (*.car;*.rom;*.bin)|*.car;*.rom;*.bin|All files (*)|*",
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					g_sim.LoadCartridge(0, path.c_str(), (ATCartLoadContext *)nullptr);
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Cartridge Load Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_CART_DETACH:
			g_sim.UnloadCartridge(0);
			break;

		case ID_CART_SAVE: {
			ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
			if (cart && cart->GetMode() != 0) {
				wxFileDialog dlg(this, "Save Cartridge", "", "cartridge.car",
					"Cartridge files (*.car;*.bin)|*.car;*.bin|All files (*)|*",
					wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
				if (dlg.ShowModal() == wxID_OK) {
					VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
					try {
						cart->Save(path.c_str(), true);
						ATImGuiShowToast("Cartridge saved");
					} catch (const MyError& e) {
						wxMessageBox(e.c_str(), "Save Error", wxOK | wxICON_ERROR, this);
					}
				}
			}
			break;
		}

		case ID_CART_ATTACH_SECONDARY: {
			wxFileDialog dlg(this, "Attach Secondary Cartridge", "", "",
				"Cartridge images (*.car;*.rom;*.bin)|*.car;*.rom;*.bin|All files (*)|*",
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					g_sim.LoadCartridge(1, path.c_str(), (ATCartLoadContext *)nullptr);
					g_sim.ColdReset();
					ATImGuiShowToast("Secondary cartridge attached");
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Cartridge Load Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_CART_DETACH_SECONDARY:
			g_sim.UnloadCartridge(1);
			g_sim.ColdReset();
			break;

		// ---- Special Cartridges ----
		case ID_SPECIAL_CART_FIRST: case ID_SPECIAL_CART_FIRST+1: case ID_SPECIAL_CART_FIRST+2:
		case ID_SPECIAL_CART_FIRST+3: case ID_SPECIAL_CART_FIRST+4: case ID_SPECIAL_CART_FIRST+5:
		case ID_SPECIAL_CART_FIRST+6: case ID_SPECIAL_CART_FIRST+7: case ID_SPECIAL_CART_FIRST+8:
		case ID_SPECIAL_CART_FIRST+9: case ID_SPECIAL_CART_FIRST+10: case ID_SPECIAL_CART_FIRST+11:
		case ID_SPECIAL_CART_FIRST+12: case ID_SPECIAL_CART_FIRST+13: case ID_SPECIAL_CART_FIRST+14:
		case ID_SPECIAL_CART_FIRST+15: case ID_SPECIAL_CART_FIRST+16: case ID_SPECIAL_CART_FIRST+17:
		case ID_SPECIAL_CART_FIRST+18: {
			int idx = id - ID_SPECIAL_CART_FIRST;
			if (idx >= 0 && idx < (int)std::size(kSpecialCarts)) {
				g_sim.LoadNewCartridge((int)kSpecialCarts[idx].mode);
				g_sim.ColdReset();
				char msg[128];
				snprintf(msg, sizeof(msg), "Attached: %s", kSpecialCarts[idx].name);
				ATImGuiShowToast(msg);
			}
			break;
		}

		// ---- Save Firmware ----
		case ID_SAVE_FW_IDE_MAIN: case ID_SAVE_FW_IDE_SDX:
		case ID_SAVE_FW_U1MB: case ID_SAVE_FW_RAPIDUS: {
			int fwIdx = id - ID_SAVE_FW_IDE_MAIN;
			ATStorageId sid = (ATStorageId)(kATStorageId_Firmware + fwIdx);
			if (g_sim.IsStoragePresent(sid)) {
				wxFileDialog dlg(this, "Save Firmware", "", "firmware.rom",
					"ROM files (*.rom;*.bin)|*.rom;*.bin|All files (*)|*",
					wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
				if (dlg.ShowModal() == wxID_OK) {
					VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
					try {
						g_sim.SaveStorage(sid, path.c_str());
						ATImGuiShowToast("Firmware saved");
					} catch (const MyError& e) {
						wxMessageBox(e.c_str(), "Save Error", wxOK | wxICON_ERROR, this);
					}
				}
			} else {
				ATImGuiShowToast("No firmware present for this device");
			}
			break;
		}

		// ---- Cassette ----
		case ID_CASSETTE_LOAD: {
			wxFileDialog dlg(this, "Load Cassette", "", "",
				"Cassette images (*.cas;*.wav)|*.cas;*.wav|All files (*)|*",
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					g_sim.GetCassette().Load(path.c_str());
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Cassette Load Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_CASSETTE_UNLOAD:
			g_sim.GetCassette().Unload();
			break;

		// ---- View ----
		case ID_VIEW_TOGGLE_FPS:
			ATUISetShowFPS(!ATUIGetShowFPS());
			break;

		case ID_VIEW_TOGGLE_STATUSBAR:
			ATUISetShowStatusBar(!ATUIGetShowStatusBar());
			if (mpStatusBar)
				mpStatusBar->SetVisible(ATUIGetShowStatusBar());
			break;

		case ID_VIEW_TOGGLE_FULLSCREEN:
			ShowFullScreen(!IsFullScreen());
			break;

		case ID_FILTER_POINT:          ATUISetDisplayFilterMode(kATDisplayFilterMode_Point); break;
		case ID_FILTER_BILINEAR:       ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear); break;
		case ID_FILTER_SHARP_BILINEAR: ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear); break;
		case ID_FILTER_BICUBIC:        ATUISetDisplayFilterMode(kATDisplayFilterMode_Bicubic); break;
		case ID_FILTER_DEFAULT:        ATUISetDisplayFilterMode(kATDisplayFilterMode_AnySuitable); break;

		case ID_ENHTEXT_NONE:     ATUISetEnhancedTextMode(kATUIEnhancedTextMode_None); break;
		case ID_ENHTEXT_HARDWARE: ATUISetEnhancedTextMode(kATUIEnhancedTextMode_Hardware); break;
		case ID_ENHTEXT_SOFTWARE: ATUISetEnhancedTextMode(kATUIEnhancedTextMode_Software); break;

		// Overscan mode
		case ID_OVERSCAN_NORMAL:     g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanNormal); break;
		case ID_OVERSCAN_EXTENDED:   g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanExtended); break;
		case ID_OVERSCAN_FULL:       g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanFull); break;
		case ID_OVERSCAN_OS_SCREEN:  g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen); break;
		case ID_OVERSCAN_WIDESCREEN: g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanWidescreen); break;

		// Vertical override
		case ID_VERT_DEFAULT:   g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Default); break;
		case ID_VERT_OS_SCREEN: g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_OSScreen); break;
		case ID_VERT_NORMAL:    g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Normal); break;
		case ID_VERT_EXTENDED:  g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Extended); break;
		case ID_VERT_FULL:      g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Full); break;

		// Artifacting mode
		case ID_ARTIFACT_NONE:    g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None); break;
		case ID_ARTIFACT_NTSC:    g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSC); break;
		case ID_ARTIFACT_PAL:     g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PAL); break;
		case ID_ARTIFACT_NTSC_HI: g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSCHi); break;
		case ID_ARTIFACT_PAL_HI:  g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PALHi); break;
		case ID_ARTIFACT_AUTO:    g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::Auto); break;
		case ID_ARTIFACT_AUTO_HI: g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::AutoHi); break;

		// View toggles
		case ID_VIEW_TOGGLE_VSYNC:
			g_sim.GetGTIA().SetVsyncEnabled(!g_sim.GetGTIA().IsVsyncEnabled());
			break;
		case ID_VIEW_TOGGLE_FRAME_BLENDING:
			g_sim.GetGTIA().SetBlendModeEnabled(!g_sim.GetGTIA().IsBlendModeEnabled());
			break;
		case ID_VIEW_TOGGLE_CONFINE_MOUSE:
			ATUISetConstrainMouseFullScreen(!ATUIGetConstrainMouseFullScreen());
			break;
		case ID_VIEW_TOGGLE_AUTO_HIDE_CURSOR:
			ATUISetPointerAutoHide(!ATUIGetPointerAutoHide());
			break;
		case ID_VIEW_TOGGLE_AUDIO_MONITOR:
			ATShowAudioMonitorWindow(this);
			break;
		case ID_VIEW_TOGGLE_AUDIO_SCOPE:
			ATShowAudioScopeWindow(this);
			break;
		case ID_VIEW_VIDEO_SETTINGS:
			ATShowVideoSettingsDialog(this);
			break;
		case ID_VIEW_COLOR_SETTINGS:
			ATShowColorSettingsDialog(this);
			break;

		case ID_STRETCH_FIT:        ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained); break;
		case ID_STRETCH_ASPECT:     ATUISetDisplayStretchMode(kATDisplayStretchMode_PreserveAspectRatio); break;
		case ID_STRETCH_ASPECT_INT: ATUISetDisplayStretchMode(kATDisplayStretchMode_IntegralPreserveAspectRatio); break;
		case ID_STRETCH_SQUARE:     ATUISetDisplayStretchMode(kATDisplayStretchMode_SquarePixels); break;
		case ID_STRETCH_SQUARE_INT: ATUISetDisplayStretchMode(kATDisplayStretchMode_Integral); break;

		case ID_WINSIZE_1X: SetClientSize(456, 262); break;
		case ID_WINSIZE_2X: SetClientSize(912, 524); break;
		case ID_WINSIZE_3X: SetClientSize(1368, 786); break;
		case ID_WINSIZE_4X: SetClientSize(1824, 1048); break;

		// ---- Speed ----
		case ID_SPEED_TOGGLE_PAUSE:
			if (g_sim.IsPaused()) {
				g_sim.Resume();
				ATImGuiShowToast("Resumed");
			} else {
				g_sim.Pause();
				ATImGuiShowToast("Paused");
			}
			break;

		case ID_SPEED_TOGGLE_TURBO:
			ATUISetTurbo(!ATUIGetTurbo());
			break;

		case ID_SPEED_TOGGLE_SLOW:
			ATUISetSlowMotion(!ATUIGetSlowMotion());
			break;

		case ID_SPEED_TOGGLE_MUTE: {
			IATAudioOutput *audio = g_sim.GetAudioOutput();
			audio->SetMute(!audio->GetMute());
			break;
		}

		case ID_SPEED_50:  ATUISetSpeedModifier(0.5f); break;
		case ID_SPEED_100: ATUISetSpeedModifier(1.0f); break;
		case ID_SPEED_200: ATUISetSpeedModifier(2.0f); break;
		case ID_SPEED_400: ATUISetSpeedModifier(4.0f); break;

		case ID_SPEED_CUSTOM_FIRST: {
			int curPct = (int)(ATUIGetSpeedModifier() * 100.0f + 0.5f);
			wxString val = wxGetTextFromUser("Enter speed percentage (10-800):",
				"Custom Speed", wxString::Format("%d", curPct), this);
			if (!val.empty()) {
				long pct = 0;
				if (val.ToLong(&pct) && pct >= 10 && pct <= 800)
					ATUISetSpeedModifier((float)pct / 100.0f);
				else
					wxMessageBox("Invalid speed. Enter a value between 10 and 800.",
						"Custom Speed", wxOK | wxICON_WARNING, this);
			}
			break;
		}

		case ID_SPEED_PAUSE_INACTIVE: {
			bool cur = ATUIGetPauseWhenInactive();
			ATUISetPauseWhenInactive(!cur);
			break;
		}

		// ---- Debug ----
		case ID_DEBUG_RUN_STOP: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg) {
				if (dbg->IsRunning())
					dbg->Break();
				else
					dbg->Run(kATDebugSrcMode_Disasm);
			}
			break;
		}

		case ID_DEBUG_STEP_INTO: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg && !dbg->IsRunning())
				dbg->StepInto(kATDebugSrcMode_Disasm);
			break;
		}

		case ID_DEBUG_STEP_OVER: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg && !dbg->IsRunning())
				dbg->StepOver(kATDebugSrcMode_Disasm);
			break;
		}

		case ID_DEBUG_STEP_OUT: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg && !dbg->IsRunning())
				dbg->StepOut(kATDebugSrcMode_Disasm);
			break;
		}

		case ID_DEBUG_TOGGLE_DEBUGGER: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg) {
				bool enable = !dbg->IsEnabled();
				dbg->SetEnabled(enable);
				if (enable)
					ATWxDebuggerOpen(this);
				else
					ATWxDebuggerClose();
			}
			break;
		}

		case ID_DEBUG_TOGGLE_BREAK_AT_EXE: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg)
				dbg->SetBreakOnEXERunAddrEnabled(!dbg->IsBreakOnEXERunAddrEnabled());
			break;
		}

		case ID_DEBUG_TOGGLE_AUTO_RELOAD_ROMS:
			g_sim.SetROMAutoReloadEnabled(!g_sim.IsROMAutoReloadEnabled());
			break;

		case ID_DEBUG_LOAD_SYMBOLS: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg) {
				wxFileDialog dlg(this, "Load Symbols", "", "",
					"Symbol files (*.lst;*.lab;*.lbl;*.dbg)|*.lst;*.lab;*.lbl;*.dbg|All files (*)|*",
					wxFD_OPEN | wxFD_FILE_MUST_EXIST);
				if (dlg.ShowModal() == wxID_OK) {
					VDStringA path(dlg.GetPath().utf8_str().data());
					VDStringA cmd = VDStringA(".loadsym \"") + path + "\"";
					dbg->QueueCommand(cmd.c_str(), false);
					ATImGuiShowToast("Loading symbols...");
				}
			}
			break;
		}

		case ID_DEBUG_UNLOAD_ALL_SYMBOLS: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg) {
				dbg->QueueCommand(".unloadsym", false);
				ATImGuiShowToast("All symbols unloaded");
			}
			break;
		}

		case ID_DEBUG_AUTO_LOAD_KERNEL_SYMBOLS:
			g_sim.SetAutoLoadKernelSymbolsEnabled(!g_sim.IsAutoLoadKernelSymbolsEnabled());
			break;

		case ID_DEBUG_AUTO_LOAD_SYSTEM_SYMBOLS: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg)
				dbg->SetAutoLoadSystemSymbols(!dbg->IsAutoLoadSystemSymbolsEnabled());
			break;
		}

		case ID_DEBUG_DEBUG_LINK: {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg)
				dbg->SetDebugLinkEnabled(!dbg->GetDebugLinkEnabled());
			break;
		}

		// ---- Tools ----
		case ID_TOOLS_FIRMWARE_MANAGER:
			ATShowFirmwareManagerDialog(this);
			break;
		case ID_TOOLS_DEVICE_MANAGER:
			ATShowDeviceManagerDialog(this);
			break;
		case ID_TOOLS_CARTRIDGE_BROWSER:
			ATShowCartridgeBrowserDialog(this);
			break;
		case ID_TOOLS_CASSETTE_CONTROL:
			ATShowCassetteControlDialog(this);
			break;
		case ID_TOOLS_TAPE_EDITOR:
			ATShowTapeEditorDialog(this);
			break;
		case ID_TOOLS_PROFILE_MANAGER:
			ATShowProfileManagerDialog(this);
			break;
		case ID_TOOLS_CHEAT_ENGINE:
			ATShowCheaterDialog(this);
			break;
		case ID_TOOLS_COMPAT_BROWSER:
			ATShowCompatBrowserDialog(this);
			break;
		case ID_TOOLS_AUDIO_MONITOR:
			ATShowAudioMonitorWindow(this);
			break;

		case ID_TOOLS_DISK_EXPLORER:
			ATShowDiskExplorerDialog(this);
			break;

		case ID_TOOLS_RECORD_VIDEO:
			ATShowVideoRecordDialog(this);
			break;

		case ID_TOOLS_RECORD_AUDIO_WAV: {
			if (ATIsAnyAudioRecording()) break;
			wxFileDialog dlg(this, "Record Audio (WAV)", "", "recording.wav",
				"WAV files (*.wav)|*.wav|All files (*)|*",
				wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				bool isPAL = g_sim.GetVideoStandard() == kATVideoStandard_PAL;
				bool isStereo = g_sim.IsDualPokeysEnabled();
				try {
					s_pAudioWriter = new ATAudioWriter(path.c_str(), false, isStereo, isPAL, nullptr);
					g_sim.GetAudioOutput()->SetAudioTap(s_pAudioWriter);
					ATImGuiShowToast("Recording WAV audio...");
				} catch (const MyError& e) {
					s_pAudioWriter.reset();
					wxMessageBox(e.c_str(), "Recording Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_TOOLS_RECORD_AUDIO_PCM: {
			if (ATIsAnyAudioRecording()) break;
			wxFileDialog dlg(this, "Record Raw Audio (PCM)", "", "recording.pcm",
				"PCM files (*.pcm)|*.pcm|All files (*)|*",
				wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				bool isPAL = g_sim.GetVideoStandard() == kATVideoStandard_PAL;
				bool isStereo = g_sim.IsDualPokeysEnabled();
				try {
					s_pAudioWriter = new ATAudioWriter(path.c_str(), true, isStereo, isPAL, nullptr);
					g_sim.GetAudioOutput()->SetAudioTap(s_pAudioWriter);
					ATImGuiShowToast("Recording raw PCM audio...");
				} catch (const MyError& e) {
					s_pAudioWriter.reset();
					wxMessageBox(e.c_str(), "Recording Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_TOOLS_RECORD_SAP: {
			if (ATIsAnyAudioRecording()) break;
			wxFileDialog dlg(this, "Record SAP Type-R", "", "recording.sap",
				"SAP files (*.sap)|*.sap|All files (*)|*",
				wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				bool isPAL = g_sim.GetVideoStandard() == kATVideoStandard_PAL;
				try {
					s_pSapWriter = ATCreateSAPWriter();
					s_pSapWriter->Init(
						g_sim.GetEventManager(),
						&g_sim.GetPokey(),
						nullptr,
						path.c_str(),
						isPAL);
					ATImGuiShowToast("Recording SAP Type-R...");
				} catch (const MyError& e) {
					s_pSapWriter.reset();
					wxMessageBox(e.c_str(), "Recording Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_TOOLS_RECORD_VGM: {
			if (ATIsAnyAudioRecording()) break;
			wxFileDialog dlg(this, "Record VGM", "", "recording.vgm",
				"VGM files (*.vgm)|*.vgm|All files (*)|*",
				wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					s_pVgmWriter = ATCreateVgmWriter();
					s_pVgmWriter->Init(path.c_str(), g_sim);
					ATImGuiShowToast("Recording VGM...");
				} catch (const MyError& e) {
					s_pVgmWriter.clear();
					wxMessageBox(e.c_str(), "Recording Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_TOOLS_VIDEO_PAUSE_RESUME:
			if (ATIsVideoRecording()) {
				if (ATIsVideoRecordingPaused()) {
					ATResumeVideoRecording();
					ATImGuiShowToast("Recording resumed");
				} else {
					ATPauseVideoRecording();
					ATImGuiShowToast("Recording paused");
				}
			}
			break;

		case ID_TOOLS_STOP_RECORDING:
			ATStopVideoRecording();
			ATStopAudioRecording();
			ATImGuiShowToast("Recording stopped");
			break;

		case ID_TOOLS_EXPORT_ROM_SET: {
			wxDirDialog dlg(this, "Select Directory for ROM Set Export", "",
				wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW dir = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				int exported = 0;
				for (const auto& rom : kRomExports) {
					try {
						vdfastvector<uint8> data;
						if (ATLoadInternalFirmware(rom.fwId, nullptr, 0, 0, nullptr, nullptr, &data)) {
							if (!data.empty()) {
								VDStringW path = VDMakePath(dir.c_str(), VDTextU8ToW(VDStringSpanA(rom.filename)).c_str());
								VDFile f(path.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways);
								f.write(data.data(), (long)data.size());
								++exported;
							}
						}
					} catch (...) {}
				}
				char msg[64];
				snprintf(msg, sizeof(msg), "Exported %d ROM(s)", exported);
				ATImGuiShowToast(msg);
			}
			break;
		}

		case ID_TOOLS_CONVERT_SAP_TO_EXE: {
			wxFileDialog inDlg(this, "Select SAP File", "", "",
				"SAP files (*.sap)|*.sap|All files (*)|*",
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (inDlg.ShowModal() == wxID_OK) {
				wxFileDialog outDlg(this, "Save EXE File", "", "converted.xex",
					"Executable files (*.xex)|*.xex|All files (*)|*",
					wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
				if (outDlg.ShowModal() == wxID_OK) {
					VDStringW inPath = VDTextU8ToW(VDStringA(inDlg.GetPath().utf8_str().data()));
					VDStringW outPath = VDTextU8ToW(VDStringA(outDlg.GetPath().utf8_str().data()));
					try {
						ATConvertSAPToPlayer(outPath.c_str(), inPath.c_str());
						ATImGuiShowToast("SAP converted to EXE");
					} catch (const MyError& e) {
						wxMessageBox(e.c_str(), "Conversion Error", wxOK | wxICON_ERROR, this);
					}
				}
			}
			break;
		}

		case ID_TOOLS_ANALYZE_TAPE_DECODING: {
			if (!g_sim.GetCassette().IsLoaded()) {
				wxMessageBox("No cassette tape is loaded.", "Tape Decoding", wxOK | wxICON_INFORMATION, this);
				break;
			}
			IATCassetteImage *image = g_sim.GetCassette().GetImage();
			if (!image) {
				wxMessageBox("No tape image available.", "Tape Decoding", wxOK | wxICON_INFORMATION, this);
				break;
			}
			// Show basic tape analysis
			wxDialog dlg(this, wxID_ANY, "Tape Decoding Analysis", wxDefaultPosition,
				wxSize(500, 400), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
			wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
			wxTextCtrl *text = new wxTextCtrl(&dlg, wxID_ANY, "",
				wxDefaultPosition, wxDefaultSize,
				wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
			text->SetFont(wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
			uint32 dataLen = image->GetDataLength();
			text->AppendText(wxString::Format(
				"Tape Information\n"
				"================\n"
				"Data length: %u samples\n"
				"Duration: %.1f seconds\n",
				dataLen, (double)dataLen / 600.0));
			text->SetInsertionPoint(0);
			sizer->Add(text, 1, wxEXPAND | wxALL, 8);
			sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
			dlg.SetSizer(sizer);
			dlg.ShowModal();
			break;
		}

		case ID_TOOLS_OPEN_CONFIG_DIR: {
			VDStringW configDir = VDMakePath(VDGetProgramPath().c_str(), L"");
			const char *xdgConfig = getenv("XDG_CONFIG_HOME");
			VDStringA dir;
			if (xdgConfig && xdgConfig[0])
				dir = VDStringA(xdgConfig) + "/altirra";
			else {
				const char *home = getenv("HOME");
				dir = VDStringA(home ? home : "/tmp") + "/.config/altirra";
			}
			VDStringA cmd = VDStringA("xdg-open \"") + dir + "\" &";
			system(cmd.c_str());
			break;
		}

		case ID_TOOLS_OPEN_FIRMWARE_DIR: {
			const char *xdgConfig = getenv("XDG_CONFIG_HOME");
			VDStringA dir;
			if (xdgConfig && xdgConfig[0])
				dir = VDStringA(xdgConfig) + "/altirra/firmware";
			else {
				const char *home = getenv("HOME");
				dir = VDStringA(home ? home : "/tmp") + "/.config/altirra/firmware";
			}
			VDStringA cmd = VDStringA("xdg-open \"") + dir + "\" &";
			system(cmd.c_str());
			break;
		}

		case ID_TOOLS_ADVANCED_CONFIG:
			ATShowAdvancedConfigDialog(this);
			break;

		// ---- Help ----
		case ID_HELP_KEYBOARD_SHORTCUTS: {
			wxDialog dlg(this, wxID_ANY, "Keyboard Shortcuts", wxDefaultPosition,
				wxSize(450, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
			wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

			wxListCtrl *list = new wxListCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
				wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
			list->AppendColumn("Key", wxLIST_FORMAT_LEFT, 140);
			list->AppendColumn("Action", wxLIST_FORMAT_LEFT, 260);

			static const struct { const char *key; const char *action; } kShortcuts[] = {
				{ "F1 (hold)",      "Turbo / Warp" },
				{ "Shift+F1",       "Cycle Quick Input Maps" },
				{ "Ctrl+F1",        "Cycle Display Filter" },
				{ "F4",             "Toggle Mute" },
				{ "F5",             "Warm Reset" },
				{ "Shift+F5",       "Cold Reset" },
				{ "F7",             "Quick Save State" },
				{ "F8",             "Quick Load State" },
				{ "F9",             "Save Screenshot" },
				{ "F11",            "Toggle Fullscreen" },
				{ "Alt+Return",     "Toggle Fullscreen" },
				{ "Pause",          "Pause / Resume" },
				{ "",               "" },
				{ "Ctrl+O",         "Open Image" },
				{ "Ctrl+Shift+O",   "Boot Image" },
				{ "Ctrl+V",         "Paste Text" },
				{ "Ctrl+S",         "Save Settings" },
				{ "Ctrl+Q",         "Quit" },
				{ "",               "" },
				{ "F5 (debugger)",   "Break / Run" },
				{ "F10",            "Step Over" },
				{ "F11 (debugger)", "Step Into" },
				{ "Shift+F11",      "Step Out" },
			};

			for (int i = 0; i < (int)std::size(kShortcuts); ++i) {
				long idx = list->InsertItem(i, kShortcuts[i].key);
				list->SetItem(idx, 1, kShortcuts[i].action);
			}

			sizer->Add(list, 1, wxEXPAND | wxALL, 8);
			sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
			dlg.SetSizer(sizer);
			dlg.ShowModal();
			break;
		}

		case ID_HELP_HOME_PAGE:
			ATLaunchURL(L"https://www.virtualdub.org/altirra.html");
			break;

		case ID_HELP_CHANGELOG:
			ATLaunchURL(L"https://www.virtualdub.org/altirra-changelog.html");
			break;

		case ID_HELP_CHECK_FOR_UPDATES:
			ATCheckForUpdates(this);
			break;

		case ID_HELP_ABOUT: {
			wxAboutDialogInfo info;
			info.SetName("Altirra");
			info.SetVersion(AT_VERSION);
			info.SetDescription(
				"Atari 800/800XL/5200 Emulator (Linux port)\n\n"
				"Cycle-accurate emulation of 6502/65C02/65C816 CPUs,\n"
				"ANTIC, GTIA, POKEY, and PIA chips.\n\n"
				"Linux port using SDL3, OpenGL, and wxWidgets.");
			info.SetCopyright("Copyright (C) 2008-2024 Avery Lee\nLinux port contributions");
			info.SetWebSite("https://www.virtualdub.org/altirra.html");
			info.AddDeveloper("Avery Lee (original author)");
			info.SetLicence(
				"This program is free software; you can redistribute it and/or modify\n"
				"it under the terms of the GNU General Public License as published by\n"
				"the Free Software Foundation; either version 2 of the License, or\n"
				"(at your option) any later version.");
			wxAboutBox(info, this);
			break;
		}

		// ===== Configure dialogs =====
		case ID_CONFIGURE_SYSTEM:
			ATShowSystemConfigDialog(this);
			break;
		case ID_CONFIGURE_CPU:
			ATShowCPUOptionsDialog(this);
			break;
		case ID_CONFIGURE_BOOT:
			ATShowBootOptionsDialog(this);
			break;
		case ID_CONFIGURE_KEYBOARD:
			ATShowKeyboardSettingsDialog(this);
			break;
		case ID_CONFIGURE_AUDIO:
			ATShowAudioOptionsDialog(this);
			break;
		case ID_CONFIGURE_VIDEO:
			ATShowVideoSettingsDialog(this);
			break;
		case ID_CONFIGURE_INPUT:
			ATShowInputSetupDialog(this);
			break;
		case ID_INPUT_ON_SCREEN_KEYBOARD:
			ATShowOnScreenKeyboard(this);
			break;

		default:
			// Memory mode range
			if (id >= ID_MEMORY_FIRST && id <= ID_MEMORY_LAST) {
				int idx = id - ID_MEMORY_FIRST;
				if (idx >= 0 && idx < (int)std::size(kMemoryModes))
					ATUISwitchMemoryMode(nullptr, kMemoryModes[idx].mode);
				break;
			}

			// Device buttons range
			if (id >= ID_DEVBTN_FIRST && id <= ID_DEVBTN_LAST) {
				int idx = id - ID_DEVBTN_FIRST;
				if (idx >= 0 && idx < (int)std::size(kDeviceButtons)) {
					ATDeviceManager *dm = g_sim.GetDeviceManager();
					if (dm) {
						auto devButtons = dm->GetInterfaces<IATDeviceButtons>(false, false, false);
						for (IATDeviceButtons *p : devButtons) {
							ATDeviceButton btn = kDeviceButtons[idx].id;
							if (p->GetSupportedButtons() & (1U << (uint32)btn))
								p->ActivateButton(btn, true);
						}
					}
				}
				break;
			}

			// Kernel user firmware range
			if (id >= ID_KERNEL_USER_FIRST && id <= ID_KERNEL_USER_LAST) {
				int fwIdx = id - ID_KERNEL_USER_FIRST;
				vdvector<ATFirmwareInfo> fwList;
				g_sim.GetFirmwareManager()->GetFirmwareList(fwList);

				// Collect and sort matching kernel firmware (same order as OnMenuOpen)
				struct KernelEntry { uint64 id; VDStringA name; };
				std::vector<KernelEntry> kernelEntries;
				for (const auto& fw : fwList) {
					if (fw.mType == kATFirmwareType_Kernel800_OSA ||
						fw.mType == kATFirmwareType_Kernel800_OSB ||
						fw.mType == kATFirmwareType_KernelXL ||
						fw.mType == kATFirmwareType_Kernel1200XL ||
						fw.mType == kATFirmwareType_KernelXEGS ||
						fw.mType == kATFirmwareType_Kernel5200) {
						kernelEntries.push_back({ fw.mId, VDTextWToU8(fw.mName) });
					}
				}
				std::sort(kernelEntries.begin(), kernelEntries.end(),
					[](const KernelEntry& a, const KernelEntry& b) {
						bool aNoKernel = (a.id == kATFirmwareId_NoKernel);
						bool bNoKernel = (b.id == kATFirmwareId_NoKernel);
						if (aNoKernel != bNoKernel)
							return aNoKernel;
						return a.name < b.name;
					});

				if (fwIdx >= 0 && fwIdx < (int)kernelEntries.size())
					g_sim.SetKernel(kernelEntries[fwIdx].id);
				break;
			}

			// Profile range
			if (id >= ID_PROFILE_FIRST && id <= ID_PROFILE_LAST) {
				int idx = id - ID_PROFILE_FIRST;
				vdfastvector<uint32> profileIds;
				ATSettingsProfileEnum(profileIds);
				int visIdx = 0;
				for (uint32 pid : profileIds) {
					if (ATSettingsProfileGetVisible(pid)) {
						if (visIdx == idx) {
							ATSettingsSwitchProfile(pid);
							break;
						}
						++visIdx;
					}
				}
				break;
			}

			// Per-drive disk actions range
			if (id >= ID_DISK_DRIVE_FIRST && id <= ID_DISK_DRIVE_LAST) {
				int offset = id - ID_DISK_DRIVE_FIRST;
				int drive = offset / kDiskActionCount;
				int action = offset % kDiskActionCount;
				ATDiskInterface& di = g_sim.GetDiskInterface(drive);

				switch (action) {
					case kDiskAction_Mount: {
						wxFileDialog dlg(this, wxString::Format("Mount D%d", drive + 1), "", "",
							"Disk images (*.atr;*.atx;*.xfd;*.dcm;*.pro)|*.atr;*.atx;*.xfd;*.dcm;*.pro|All files (*)|*",
							wxFD_OPEN | wxFD_FILE_MUST_EXIST);
						if (dlg.ShowModal() == wxID_OK) {
							VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
							try {
								di.LoadDisk(path.c_str());
							} catch (const MyError& e) {
								wxMessageBox(e.c_str(), "Disk Mount Error", wxOK | wxICON_ERROR, this);
							}
						}
						break;
					}
					case kDiskAction_NewDisk: {
						static const struct {
							const char *name;
							uint32 sectorCount;
							uint32 sectorSize;
						} kNewDiskFormats[] = {
							{ "Single Density (90K - 720 sectors, 128 bytes)",   720, 128 },
							{ "Medium Density (130K - 1040 sectors, 128 bytes)", 1040, 128 },
							{ "Double Density (180K - 720 sectors, 256 bytes)",  720, 256 },
							{ "Double-Sided DD (360K - 1440 sectors, 256 bytes)", 1440, 256 },
							{ "DSDD 80 tracks (2880 sectors, 256 bytes)",        2880, 256 },
						};

						wxDialog newDiskDlg(this, wxID_ANY, wxString::Format("New Disk in D%d:", drive + 1),
							wxDefaultPosition, wxSize(400, 280), wxDEFAULT_DIALOG_STYLE);
						wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

						sizer->Add(new wxStaticText(&newDiskDlg, wxID_ANY,
							wxString::Format("Create blank disk image in D%d:", drive + 1)),
							0, wxALL, 8);

						wxRadioButton *fmtRadios[5];
						for (int f = 0; f < 5; ++f) {
							fmtRadios[f] = new wxRadioButton(&newDiskDlg, wxID_ANY,
								kNewDiskFormats[f].name,
								wxDefaultPosition, wxDefaultSize,
								f == 0 ? wxRB_GROUP : 0);
						}
						fmtRadios[0]->SetValue(true);
						for (int f = 0; f < 5; ++f)
							sizer->Add(fmtRadios[f], 0, wxLEFT | wxRIGHT, 16);

						sizer->AddSpacer(8);
						sizer->Add(newDiskDlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL),
							0, wxEXPAND | wxALL, 8);
						newDiskDlg.SetSizer(sizer);
						newDiskDlg.Fit();

						if (newDiskDlg.ShowModal() == wxID_OK) {
							int sel = 0;
							for (int f = 0; f < 5; ++f) {
								if (fmtRadios[f]->GetValue()) { sel = f; break; }
							}
							try {
								di.UnloadDisk();
								di.CreateDisk(kNewDiskFormats[sel].sectorCount, 3,
									kNewDiskFormats[sel].sectorSize);
								di.SetWriteMode(kATMediaWriteMode_VRW);
								char msg[64];
								snprintf(msg, sizeof(msg), "New disk created in D%d", drive + 1);
								ATImGuiShowToast(msg);
							} catch (const MyError& e) {
								wxMessageBox(e.c_str(), "New Disk Error", wxOK | wxICON_ERROR, this);
							}
						}
						break;
					}
					case kDiskAction_Save:
						if (di.IsDiskLoaded()) {
							try {
								di.SaveDisk();
								char msg[64];
								snprintf(msg, sizeof(msg), "D%d saved", drive + 1);
								ATImGuiShowToast(msg);
							} catch (const MyError& e) {
								wxMessageBox(e.c_str(), "Save Error", wxOK | wxICON_ERROR, this);
							}
						}
						break;
					case kDiskAction_SaveAs: {
						if (!di.IsDiskLoaded()) break;
						wxFileDialog dlg(this, wxString::Format("Save D%d As", drive + 1), "", "disk.atr",
							"ATR images (*.atr)|*.atr|XFD images (*.xfd)|*.xfd|All files (*)|*",
							wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
						if (dlg.ShowModal() == wxID_OK) {
							VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
							try {
								di.SaveDiskAs(path.c_str(), kATDiskImageFormat_ATR);
								char msg[64];
								snprintf(msg, sizeof(msg), "D%d saved as file", drive + 1);
								ATImGuiShowToast(msg);
							} catch (const MyError& e) {
								wxMessageBox(e.c_str(), "Save Error", wxOK | wxICON_ERROR, this);
							}
						}
						break;
					}
					case kDiskAction_ReadOnly:
						if (di.IsDiskLoaded()) {
							ATMediaWriteMode wm = di.GetWriteMode();
							if (wm == kATMediaWriteMode_RO)
								di.SetWriteMode(kATMediaWriteMode_RW);
							else
								di.SetWriteMode(kATMediaWriteMode_RO);
						}
						break;
					case kDiskAction_Explore: {
						if (!di.IsDiskLoaded()) break;
						IATDiskImage *image = di.GetDiskImage();
						if (!image) break;
						try {
							bool readOnly = (di.GetWriteMode() == kATMediaWriteMode_RO);
							std::unique_ptr<IATDiskFS> fs(ATDiskMountImage(image, readOnly));
							if (!fs) {
								wxMessageBox("Could not mount filesystem.", "Disk Explorer", wxOK | wxICON_ERROR, this);
								break;
							}
							ATDiskFSInfo fsInfo;
							fs->GetInfo(fsInfo);

							// --- Full-featured Disk Explorer dialog ---
							enum { ID_IMPORT = 10001, ID_EXPORT, ID_DELETE, ID_RENAME, ID_MKDIR, ID_UPDIR };
							wxDialog dlg(this, wxID_ANY,
								wxString::Format("Disk Explorer - D%d: [%s]", drive + 1, fsInfo.mFSType.c_str()),
								wxDefaultPosition, wxSize(600, 500),
								wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
							wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

							// Info + path bar
							wxStaticText *infoLabel = new wxStaticText(&dlg, wxID_ANY,
								wxString::Format("Filesystem: %s   Free: %u sectors   %s",
									fsInfo.mFSType.c_str(), fsInfo.mFreeBlocks,
									readOnly ? "[Read Only]" : "[Read/Write]"));
							mainSizer->Add(infoLabel, 0, wxALL, 8);
							wxStaticText *pathLabel = new wxStaticText(&dlg, wxID_ANY, "Path: /");
							mainSizer->Add(pathLabel, 0, wxLEFT | wxRIGHT, 8);

							// File list
							wxListCtrl *list = new wxListCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
								wxLC_REPORT);
							list->AppendColumn("Filename", wxLIST_FORMAT_LEFT, 180);
							list->AppendColumn("Size", wxLIST_FORMAT_RIGHT, 80);
							list->AppendColumn("Sectors", wxLIST_FORMAT_RIGHT, 70);
							list->AppendColumn("Type", wxLIST_FORMAT_LEFT, 60);
							list->AppendColumn("Date", wxLIST_FORMAT_LEFT, 120);
							list->SetFont(wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
							mainSizer->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

							// Button bar
							wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
							wxButton *btnUp = new wxButton(&dlg, ID_UPDIR, "Up");
							wxButton *btnImport = new wxButton(&dlg, ID_IMPORT, "Import");
							wxButton *btnExport = new wxButton(&dlg, ID_EXPORT, "Export");
							wxButton *btnDelete = new wxButton(&dlg, ID_DELETE, "Delete");
							wxButton *btnRename = new wxButton(&dlg, ID_RENAME, "Rename");
							wxButton *btnMkdir = new wxButton(&dlg, ID_MKDIR, "New Dir");
							if (readOnly) {
								btnImport->Enable(false);
								btnDelete->Enable(false);
								btnRename->Enable(false);
								btnMkdir->Enable(false);
							}
							btnSizer->Add(btnUp, 0, wxRIGHT, 4);
							btnSizer->AddStretchSpacer();
							btnSizer->Add(btnImport, 0, wxRIGHT, 4);
							btnSizer->Add(btnExport, 0, wxRIGHT, 4);
							btnSizer->Add(btnDelete, 0, wxRIGHT, 4);
							btnSizer->Add(btnRename, 0, wxRIGHT, 4);
							btnSizer->Add(btnMkdir, 0);
							mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);
							mainSizer->Add(dlg.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
							dlg.SetSizer(mainSizer);

							// State: current directory key + entry cache
							ATDiskFSKey curDir = ATDiskFSKey::None;
							struct FileEntry { ATDiskFSKey key; ATDiskFSEntryInfo info; };
							std::vector<FileEntry> entries;

							// Populate list from current directory
							auto populateList = [&]() {
								list->DeleteAllItems();
								entries.clear();
								ATDiskFSEntryInfo entry;
								ATDiskFSFindHandle fh = fs->FindFirst(curDir, entry);
								if (fh != ATDiskFSFindHandle::Invalid) {
									int row = 0;
									do {
										entries.push_back({entry.mKey, entry});
										long idx = list->InsertItem(row, wxString(entry.mFileName.c_str()));
										list->SetItem(idx, 1, entry.mbIsDirectory ? "" : wxString::Format("%u", entry.mBytes));
										list->SetItem(idx, 2, wxString::Format("%u", entry.mSectors));
										list->SetItem(idx, 3, entry.mbIsDirectory ? "DIR" : "FILE");
										if (entry.mbDateValid) {
											list->SetItem(idx, 4, wxString::Format("%04d-%02d-%02d %02d:%02d",
												entry.mDate.mYear, entry.mDate.mMonth, entry.mDate.mDay,
												entry.mDate.mHour, entry.mDate.mMinute));
										}
										++row;
									} while (fs->FindNext(fh, entry));
									fs->FindEnd(fh);
								}
								// Update path label
								wxString pathStr = "/";
								ATDiskFSKey k = curDir;
								std::vector<VDStringA> pathParts;
								while (k != ATDiskFSKey::None) {
									ATDiskFSEntryInfo ei;
									fs->GetFileInfo(k, ei);
									pathParts.push_back(ei.mFileName);
									k = fs->GetParentDirectory(k);
								}
								for (int p = (int)pathParts.size() - 1; p >= 0; --p) {
									pathStr += wxString(pathParts[p].c_str()) + "/";
								}
								pathLabel->SetLabel("Path: " + pathStr);
							};

							populateList();

							// Double-click to enter directories
							list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [&](wxListEvent& evt) {
								int sel = evt.GetIndex();
								if (sel >= 0 && sel < (int)entries.size() && entries[sel].info.mbIsDirectory) {
									curDir = entries[sel].key;
									populateList();
								}
							});

							// Up button
							dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
								if (curDir != ATDiskFSKey::None) {
									curDir = fs->GetParentDirectory(curDir);
									populateList();
								}
							}, ID_UPDIR);

							// Export button
							dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
								long sel = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
								if (sel < 0 || sel >= (int)entries.size() || entries[sel].info.mbIsDirectory) {
									wxMessageBox("Select a file to export.", "Export", wxOK | wxICON_INFORMATION, &dlg);
									return;
								}
								wxFileDialog saveDlg(&dlg, "Export File", "",
									wxString(entries[sel].info.mFileName.c_str()),
									"All files (*)|*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
								if (saveDlg.ShowModal() == wxID_OK) {
									try {
										vdfastvector<uint8> data;
										fs->ReadFile(entries[sel].key, data);
										VDStringW path = VDTextU8ToW(VDStringA(saveDlg.GetPath().utf8_str().data()));
										VDFile f(path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
										if (!data.empty())
											f.write(data.data(), (long)data.size());
										f.close();
									} catch (const MyError& e) {
										wxMessageBox(e.c_str(), "Export Error", wxOK | wxICON_ERROR, &dlg);
									}
								}
							}, ID_EXPORT);

							// Import button
							dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
								wxFileDialog openDlg(&dlg, "Import File", "", "",
									"All files (*)|*", wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
								if (openDlg.ShowModal() == wxID_OK) {
									wxArrayString paths;
									openDlg.GetPaths(paths);
									for (const auto& p : paths) {
										try {
											VDStringW wpath = VDTextU8ToW(VDStringA(p.utf8_str().data()));
											VDFile f(wpath.c_str(), nsVDFile::kRead | nsVDFile::kOpenExisting);
											sint64 len = f.size();
											if (len > 16 * 1024 * 1024) {
												wxMessageBox("File too large.", "Import Error", wxOK | wxICON_ERROR, &dlg);
												continue;
											}
											vdfastvector<uint8> data((size_t)len);
											if (len > 0)
												f.read(data.data(), (long)len);
											f.close();
											const char *fname = VDFileSplitPath(VDTextWToA(wpath).c_str());
											// Truncate to 8.3 for Atari DOS compatibility
											VDStringA atariFname(fname);
											if (atariFname.size() > 12)
												atariFname.resize(12);
											// Convert to uppercase
											for (char& c : atariFname)
												c = toupper((unsigned char)c);
											fs->WriteFile(curDir, atariFname.c_str(), data.data(), (uint32)data.size());
										} catch (const MyError& e) {
											wxMessageBox(e.c_str(), "Import Error", wxOK | wxICON_ERROR, &dlg);
										}
									}
									fs->Flush();
									di.OnDiskModified();
									fs->GetInfo(fsInfo);
									infoLabel->SetLabel(wxString::Format("Filesystem: %s   Free: %u sectors   %s",
										fsInfo.mFSType.c_str(), fsInfo.mFreeBlocks,
										readOnly ? "[Read Only]" : "[Read/Write]"));
									populateList();
								}
							}, ID_IMPORT);

							// Delete button
							dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
								long sel = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
								if (sel < 0 || sel >= (int)entries.size()) return;
								wxString msg = wxString::Format("Delete '%s'?", entries[sel].info.mFileName.c_str());
								if (wxMessageBox(msg, "Confirm Delete", wxYES_NO | wxICON_QUESTION, &dlg) == wxYES) {
									try {
										fs->DeleteFile(entries[sel].key);
										fs->Flush();
										di.OnDiskModified();
										fs->GetInfo(fsInfo);
										infoLabel->SetLabel(wxString::Format("Filesystem: %s   Free: %u sectors   %s",
											fsInfo.mFSType.c_str(), fsInfo.mFreeBlocks,
											readOnly ? "[Read Only]" : "[Read/Write]"));
										populateList();
									} catch (const MyError& e) {
										wxMessageBox(e.c_str(), "Delete Error", wxOK | wxICON_ERROR, &dlg);
									}
								}
							}, ID_DELETE);

							// Rename button
							dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
								long sel = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
								if (sel < 0 || sel >= (int)entries.size()) return;
								wxString newName = wxGetTextFromUser("New filename:",
									"Rename", wxString(entries[sel].info.mFileName.c_str()), &dlg);
								if (!newName.empty()) {
									try {
										fs->RenameFile(entries[sel].key, newName.utf8_str().data());
										fs->Flush();
										di.OnDiskModified();
										populateList();
									} catch (const MyError& e) {
										wxMessageBox(e.c_str(), "Rename Error", wxOK | wxICON_ERROR, &dlg);
									}
								}
							}, ID_RENAME);

							// New Dir button
							dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
								wxString dirName = wxGetTextFromUser("Directory name:", "New Directory", "", &dlg);
								if (!dirName.empty()) {
									try {
										fs->CreateDir(curDir, dirName.utf8_str().data());
										fs->Flush();
										di.OnDiskModified();
										populateList();
									} catch (const MyError& e) {
										wxMessageBox(e.c_str(), "Create Directory Error", wxOK | wxICON_ERROR, &dlg);
									}
								}
							}, ID_MKDIR);

							dlg.ShowModal();
						} catch (const MyError& e) {
							wxMessageBox(wxString::Format("Error: %s", e.c_str()),
								"Disk Explorer", wxOK | wxICON_ERROR, this);
						}
						break;
					}
					case kDiskAction_Unmount:
						di.UnloadDisk();
						break;
				}
				break;
			}

			event.Skip();
			break;
	}
}

///////////////////////////////////////////////////////////////////////////
// Menu UI update handler (checkbox/radio/enable state)
///////////////////////////////////////////////////////////////////////////

void ATMainFrame::OnMenuUpdateUI(wxUpdateUIEvent& event) {
	int id = event.GetId();

	switch (id) {
		// System checkboxes
		case ID_SYSTEM_TOGGLE_BASIC:
			event.Check(g_sim.IsBASICEnabled());
			break;
		case ID_SYSTEM_TOGGLE_REWIND:
			event.Check(g_sim.GetAutoSaveManager().GetRewindEnabled());
			break;
		case ID_SYSTEM_REWIND:
			event.Enable(g_sim.GetAutoSaveManager().GetRewindEnabled());
			break;

		// Quick Load only enabled when there's a saved state
		case ID_FILE_QUICK_LOAD_STATE:
			event.Enable(s_pQuickState != nullptr);
			break;

		// Hardware mode radios
		case ID_HW_800:    event.Check(g_sim.GetHardwareMode() == kATHardwareMode_800); break;
		case ID_HW_800XL:  event.Check(g_sim.GetHardwareMode() == kATHardwareMode_800XL); break;
		case ID_HW_1200XL: event.Check(g_sim.GetHardwareMode() == kATHardwareMode_1200XL); break;
		case ID_HW_130XE:  event.Check(g_sim.GetHardwareMode() == kATHardwareMode_130XE); break;
		case ID_HW_XEGS:   event.Check(g_sim.GetHardwareMode() == kATHardwareMode_XEGS); break;
		case ID_HW_5200:   event.Check(g_sim.GetHardwareMode() == kATHardwareMode_5200); break;

		// Video standard radios
		case ID_VIDEOSTD_NTSC:   event.Check(g_sim.GetVideoStandard() == kATVideoStandard_NTSC); break;
		case ID_VIDEOSTD_PAL:    event.Check(g_sim.GetVideoStandard() == kATVideoStandard_PAL); break;
		case ID_VIDEOSTD_SECAM:  event.Check(g_sim.GetVideoStandard() == kATVideoStandard_SECAM); break;
		case ID_VIDEOSTD_NTSC50: event.Check(g_sim.GetVideoStandard() == kATVideoStandard_NTSC50); break;
		case ID_VIDEOSTD_PAL60:  event.Check(g_sim.GetVideoStandard() == kATVideoStandard_PAL60); break;

		// Kernel radios
		case ID_KERNEL_AUTOSELECT:     event.Check(g_sim.GetKernelId() == 0); break;
		case ID_KERNEL_INTERNAL_OSB:   event.Check(g_sim.GetKernelId() == kATFirmwareId_Kernel_LLE); break;
		case ID_KERNEL_INTERNAL_XL:    event.Check(g_sim.GetKernelId() == kATFirmwareId_Kernel_LLEXL); break;
		case ID_KERNEL_INTERNAL_5200:  event.Check(g_sim.GetKernelId() == kATFirmwareId_5200_LLE); break;

		// Power-On Delay radios
		case ID_POWERON_AUTO: event.Check(g_sim.GetPowerOnDelay() < 0); break;
		case ID_POWERON_NONE: event.Check(g_sim.GetPowerOnDelay() == 0); break;
		case ID_POWERON_1SEC: event.Check(g_sim.GetPowerOnDelay() == 10); break;
		case ID_POWERON_2SEC: event.Check(g_sim.GetPowerOnDelay() == 20); break;
		case ID_POWERON_3SEC: event.Check(g_sim.GetPowerOnDelay() == 30); break;

		// System toggles
		case ID_SYSTEM_HOLD_KEYS_FOR_RESET:
			event.Check(g_sim.GetPendingHeldSwitches() != 0);
			break;
		case ID_SYSTEM_AUTO_BOOT_TAPE:
			event.Check(g_sim.IsCassetteAutoBootEnabled());
			break;

		// Disk save-all enable
		case ID_DISK_SAVE_ALL_MODIFIED: {
			bool anyDirty = false;
			for (int i = 0; i < 15 && !anyDirty; ++i) {
				ATDiskInterface& di = g_sim.GetDiskInterface(i);
				if (di.IsDiskLoaded() && di.IsDirty())
					anyDirty = true;
			}
			event.Enable(anyDirty);
			break;
		}

		// Cartridge/cassette
		case ID_CART_DETACH:
			event.Enable(g_sim.IsCartridgeAttached(0));
			break;
		case ID_CART_SAVE: {
			ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
			event.Enable(cart && cart->GetMode() != 0 && cart->GetMode() != kATCartridgeMode_SuperCharger3D);
			break;
		}
		case ID_CART_DETACH_SECONDARY:
			event.Enable(g_sim.IsCartridgeAttached(1));
			break;
		case ID_CASSETTE_UNLOAD:
			event.Enable(g_sim.GetCassette().IsLoaded());
			break;

		// Save firmware enable
		case ID_SAVE_FW_IDE_MAIN:
			event.Enable(g_sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 0)));
			break;
		case ID_SAVE_FW_IDE_SDX:
			event.Enable(g_sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 1)));
			break;
		case ID_SAVE_FW_U1MB:
			event.Enable(g_sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 2)));
			break;
		case ID_SAVE_FW_RAPIDUS:
			event.Enable(g_sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 3)));
			break;

		// Recording state
		case ID_TOOLS_RECORD_VIDEO:
			event.Enable(!ATIsVideoRecording() && !ATIsAnyAudioRecording());
			break;
		case ID_TOOLS_RECORD_AUDIO_WAV:
		case ID_TOOLS_RECORD_AUDIO_PCM:
		case ID_TOOLS_RECORD_SAP:
		case ID_TOOLS_RECORD_VGM:
			event.Enable(!ATIsVideoRecording() && !ATIsAnyAudioRecording());
			break;
		case ID_TOOLS_STOP_RECORDING:
			event.Enable(ATIsVideoRecording() || ATIsAnyAudioRecording());
			break;

		// View checkboxes
		case ID_VIEW_TOGGLE_FPS:
			event.Check(ATUIGetShowFPS());
			break;
		case ID_VIEW_TOGGLE_STATUSBAR:
			event.Check(ATUIGetShowStatusBar());
			break;
		case ID_VIEW_TOGGLE_FULLSCREEN:
			event.Check(IsFullScreen());
			break;

		// Display filter radios
		case ID_FILTER_POINT:          event.Check(ATUIGetDisplayFilterMode() == kATDisplayFilterMode_Point); break;
		case ID_FILTER_BILINEAR:       event.Check(ATUIGetDisplayFilterMode() == kATDisplayFilterMode_Bilinear); break;
		case ID_FILTER_SHARP_BILINEAR: event.Check(ATUIGetDisplayFilterMode() == kATDisplayFilterMode_SharpBilinear); break;
		case ID_FILTER_BICUBIC:        event.Check(ATUIGetDisplayFilterMode() == kATDisplayFilterMode_Bicubic); break;
		case ID_FILTER_DEFAULT:        event.Check(ATUIGetDisplayFilterMode() == kATDisplayFilterMode_AnySuitable); break;

		// Stretch mode radios
		case ID_STRETCH_FIT:        event.Check(ATUIGetDisplayStretchMode() == kATDisplayStretchMode_Unconstrained); break;
		case ID_STRETCH_ASPECT:     event.Check(ATUIGetDisplayStretchMode() == kATDisplayStretchMode_PreserveAspectRatio); break;
		case ID_STRETCH_ASPECT_INT: event.Check(ATUIGetDisplayStretchMode() == kATDisplayStretchMode_IntegralPreserveAspectRatio); break;
		case ID_STRETCH_SQUARE:     event.Check(ATUIGetDisplayStretchMode() == kATDisplayStretchMode_SquarePixels); break;
		case ID_STRETCH_SQUARE_INT: event.Check(ATUIGetDisplayStretchMode() == kATDisplayStretchMode_Integral); break;

		// Enhanced text radios
		case ID_ENHTEXT_NONE:     event.Check(ATUIGetEnhancedTextMode() == kATUIEnhancedTextMode_None); break;
		case ID_ENHTEXT_HARDWARE: event.Check(ATUIGetEnhancedTextMode() == kATUIEnhancedTextMode_Hardware); break;
		case ID_ENHTEXT_SOFTWARE: event.Check(ATUIGetEnhancedTextMode() == kATUIEnhancedTextMode_Software); break;

		// Overscan mode radios
		case ID_OVERSCAN_NORMAL:     event.Check(g_sim.GetGTIA().GetOverscanMode() == ATGTIAEmulator::kOverscanNormal); break;
		case ID_OVERSCAN_EXTENDED:   event.Check(g_sim.GetGTIA().GetOverscanMode() == ATGTIAEmulator::kOverscanExtended); break;
		case ID_OVERSCAN_FULL:       event.Check(g_sim.GetGTIA().GetOverscanMode() == ATGTIAEmulator::kOverscanFull); break;
		case ID_OVERSCAN_OS_SCREEN:  event.Check(g_sim.GetGTIA().GetOverscanMode() == ATGTIAEmulator::kOverscanOSScreen); break;
		case ID_OVERSCAN_WIDESCREEN: event.Check(g_sim.GetGTIA().GetOverscanMode() == ATGTIAEmulator::kOverscanWidescreen); break;

		// Vertical override radios
		case ID_VERT_DEFAULT:   event.Check(g_sim.GetGTIA().GetVerticalOverscanMode() == ATGTIAEmulator::kVerticalOverscan_Default); break;
		case ID_VERT_OS_SCREEN: event.Check(g_sim.GetGTIA().GetVerticalOverscanMode() == ATGTIAEmulator::kVerticalOverscan_OSScreen); break;
		case ID_VERT_NORMAL:    event.Check(g_sim.GetGTIA().GetVerticalOverscanMode() == ATGTIAEmulator::kVerticalOverscan_Normal); break;
		case ID_VERT_EXTENDED:  event.Check(g_sim.GetGTIA().GetVerticalOverscanMode() == ATGTIAEmulator::kVerticalOverscan_Extended); break;
		case ID_VERT_FULL:      event.Check(g_sim.GetGTIA().GetVerticalOverscanMode() == ATGTIAEmulator::kVerticalOverscan_Full); break;

		// Artifacting mode radios
		case ID_ARTIFACT_NONE:    event.Check(g_sim.GetGTIA().GetArtifactingMode() == ATArtifactMode::None); break;
		case ID_ARTIFACT_NTSC:    event.Check(g_sim.GetGTIA().GetArtifactingMode() == ATArtifactMode::NTSC); break;
		case ID_ARTIFACT_PAL:     event.Check(g_sim.GetGTIA().GetArtifactingMode() == ATArtifactMode::PAL); break;
		case ID_ARTIFACT_NTSC_HI: event.Check(g_sim.GetGTIA().GetArtifactingMode() == ATArtifactMode::NTSCHi); break;
		case ID_ARTIFACT_PAL_HI:  event.Check(g_sim.GetGTIA().GetArtifactingMode() == ATArtifactMode::PALHi); break;
		case ID_ARTIFACT_AUTO:    event.Check(g_sim.GetGTIA().GetArtifactingMode() == ATArtifactMode::Auto); break;
		case ID_ARTIFACT_AUTO_HI: event.Check(g_sim.GetGTIA().GetArtifactingMode() == ATArtifactMode::AutoHi); break;

		// View toggles
		case ID_VIEW_TOGGLE_VSYNC:
			event.Check(g_sim.GetGTIA().IsVsyncEnabled());
			break;
		case ID_VIEW_TOGGLE_FRAME_BLENDING:
			event.Check(g_sim.GetGTIA().IsBlendModeEnabled());
			break;
		case ID_VIEW_TOGGLE_CONFINE_MOUSE:
			event.Check(ATUIGetConstrainMouseFullScreen());
			break;
		case ID_VIEW_TOGGLE_AUTO_HIDE_CURSOR:
			event.Check(ATUIGetPointerAutoHide());
			break;

		// Speed checkboxes
		case ID_SPEED_TOGGLE_TURBO:
			event.Check(ATUIGetTurbo());
			break;
		case ID_SPEED_TOGGLE_SLOW:
			event.Check(ATUIGetSlowMotion());
			break;
		case ID_SPEED_TOGGLE_MUTE:
			event.Check(g_sim.GetAudioOutput()->GetMute());
			break;

		case ID_SPEED_PAUSE_INACTIVE:
			event.Check(ATUIGetPauseWhenInactive());
			break;

		// Speed radios
		case ID_SPEED_50:  event.Check(ATUIGetSpeedModifier() == 0.5f); break;
		case ID_SPEED_100: event.Check(ATUIGetSpeedModifier() == 1.0f); break;
		case ID_SPEED_200: event.Check(ATUIGetSpeedModifier() == 2.0f); break;
		case ID_SPEED_400: event.Check(ATUIGetSpeedModifier() == 4.0f); break;

		// Debug
		case ID_DEBUG_TOGGLE_DEBUGGER: {
			IATDebugger *dbg = ATGetDebugger();
			event.Check(dbg && dbg->IsEnabled());
			break;
		}

		case ID_DEBUG_TOGGLE_BREAK_AT_EXE: {
			IATDebugger *dbg = ATGetDebugger();
			event.Check(dbg && dbg->IsBreakOnEXERunAddrEnabled());
			break;
		}

		case ID_DEBUG_TOGGLE_AUTO_RELOAD_ROMS:
			event.Check(g_sim.IsROMAutoReloadEnabled());
			break;

		case ID_DEBUG_STEP_INTO:
		case ID_DEBUG_STEP_OVER:
		case ID_DEBUG_STEP_OUT: {
			IATDebugger *dbg = ATGetDebugger();
			event.Enable(dbg && !dbg->IsRunning());
			break;
		}

		case ID_DEBUG_AUTO_LOAD_KERNEL_SYMBOLS:
			event.Check(g_sim.IsAutoLoadKernelSymbolsEnabled());
			break;

		case ID_DEBUG_AUTO_LOAD_SYSTEM_SYMBOLS: {
			IATDebugger *dbg = ATGetDebugger();
			event.Check(dbg && dbg->IsAutoLoadSystemSymbolsEnabled());
			break;
		}

		case ID_DEBUG_DEBUG_LINK: {
			IATDebugger *dbg = ATGetDebugger();
			event.Check(dbg && dbg->GetDebugLinkEnabled());
			break;
		}

		// Tools recording enable state
		case ID_TOOLS_VIDEO_PAUSE_RESUME:
			event.Enable(ATIsVideoRecording());
			event.SetText(ATIsVideoRecordingPaused() ? "Resume Recording" : "Pause Recording");
			break;

		case ID_TOOLS_ANALYZE_TAPE_DECODING:
			event.Enable(g_sim.GetCassette().IsLoaded());
			break;

		default:
			// Memory mode range
			if (id >= ID_MEMORY_FIRST && id <= ID_MEMORY_LAST) {
				int idx = id - ID_MEMORY_FIRST;
				if (idx >= 0 && idx < (int)std::size(kMemoryModes))
					event.Check(g_sim.GetMemoryMode() == kMemoryModes[idx].mode);
				break;
			}

			// Per-drive disk ReadOnly checkbox
			if (id >= ID_DISK_DRIVE_FIRST && id <= ID_DISK_DRIVE_LAST) {
				int offset = id - ID_DISK_DRIVE_FIRST;
				int drive = offset / kDiskActionCount;
				int action = offset % kDiskActionCount;
				ATDiskInterface& di = g_sim.GetDiskInterface(drive);

				switch (action) {
					case kDiskAction_ReadOnly:
						event.Check(di.GetWriteMode() == kATMediaWriteMode_RO);
						event.Enable(di.IsDiskLoaded());
						break;
					case kDiskAction_Save:
						event.Enable(di.IsDiskLoaded() && di.IsDirty());
						break;
					case kDiskAction_SaveAs:
					case kDiskAction_Explore:
						event.Enable(di.IsDiskLoaded());
						break;
					case kDiskAction_Unmount:
						event.Enable(di.IsDiskLoaded());
						break;
				}
				break;
			}

			// Kernel user firmware range
			if (id >= ID_KERNEL_USER_FIRST && id <= ID_KERNEL_USER_LAST) {
				// Check state computed in OnMenuOpen
				break;
			}

			break;
	}
}

void ATMainFrame::OnMenuOpen(wxMenuEvent& event) {
	event.Skip();

	// ---- Enable/disable cartridge and cassette items ----
	// wxGTK doesn't reliably deliver wxEVT_UPDATE_UI for all items,
	// so do it here where it's guaranteed to run before the menu shows.
	wxMenuBar *mb = GetMenuBar();
	if (mb) {
		wxMenuItem *item;
		if ((item = mb->FindItem(ID_CART_DETACH)))
			item->Enable(g_sim.IsCartridgeAttached(0));
		if ((item = mb->FindItem(ID_CART_DETACH_SECONDARY)))
			item->Enable(g_sim.IsCartridgeAttached(1));
		if ((item = mb->FindItem(ID_CASSETTE_UNLOAD)))
			item->Enable(g_sim.GetCassette().IsLoaded());
	}

	// ---- Rebuild MRU submenu ----
	if (mpMRUMenu) {
		while (mpMRUMenu->GetMenuItemCount() > 0)
			mpMRUMenu->Delete(mpMRUMenu->FindItemByPosition(0));

		uint32 count = MRUCount();
		if (count > 10) count = 10;

		for (uint32 i = 0; i < count; ++i) {
			VDStringW wpath = MRUGet(i);
			if (wpath.empty())
				continue;
			VDStringA u8name = VDTextWToU8(VDStringW(VDFileSplitPath(wpath.c_str())));
			VDStringA u8full = VDTextWToU8(wpath);
			wxString label = wxString::Format("%u. %s", i + 1, u8name.c_str());
			mpMRUMenu->Append(ID_MRU_FILE_0 + i, label, wxString(u8full.c_str()));
		}

		if (count > 0) {
			mpMRUMenu->AppendSeparator();
			mpMRUMenu->Append(ID_MRU_CLEAR, "Clear Recent Files");
		} else {
			mpMRUMenu->Append(wxID_ANY, "(No recent files)")->Enable(false);
		}
	}

	// ---- Rebuild Kernel user firmware items ----
	if (mpKernelMenu) {
		// Remove old user firmware items (keep first 4: Autoselect, OS-B, XL, 5200)
		while (mpKernelMenu->GetMenuItemCount() > 4)
			mpKernelMenu->Delete(mpKernelMenu->FindItemByPosition(mpKernelMenu->GetMenuItemCount() - 1));

		// Collect user firmware items matching kernel types
		vdvector<ATFirmwareInfo> fwList;
		g_sim.GetFirmwareManager()->GetFirmwareList(fwList);
		uint64 curKernel = g_sim.GetKernelId();

		struct KernelEntry { uint64 id; VDStringA name; };
		std::vector<KernelEntry> kernelEntries;
		for (const auto& fw : fwList) {
			if (fw.mType == kATFirmwareType_Kernel800_OSA ||
				fw.mType == kATFirmwareType_Kernel800_OSB ||
				fw.mType == kATFirmwareType_KernelXL ||
				fw.mType == kATFirmwareType_Kernel1200XL ||
				fw.mType == kATFirmwareType_KernelXEGS ||
				fw.mType == kATFirmwareType_Kernel5200) {
				kernelEntries.push_back({ fw.mId, VDTextWToU8(fw.mName) });
			}
		}

		// Sort: NoKernel first, then alphabetical by name
		std::sort(kernelEntries.begin(), kernelEntries.end(),
			[](const KernelEntry& a, const KernelEntry& b) {
				bool aNoKernel = (a.id == kATFirmwareId_NoKernel);
				bool bNoKernel = (b.id == kATFirmwareId_NoKernel);
				if (aNoKernel != bNoKernel)
					return aNoKernel;
				return a.name < b.name;
			});

		int userIdx = 0;
		bool addedSep = false;
		for (const auto& ke : kernelEntries) {
			if (userIdx >= (ID_KERNEL_USER_LAST - ID_KERNEL_USER_FIRST + 1))
				break;
			if (!addedSep) {
				mpKernelMenu->AppendSeparator();
				addedSep = true;
			}
			wxMenuItem *item = mpKernelMenu->AppendRadioItem(
				ID_KERNEL_USER_FIRST + userIdx, wxString(ke.name.c_str()));
			item->Check(curKernel == ke.id);
			++userIdx;
		}

		// Update built-in kernel radio states
		mpKernelMenu->FindItem(ID_KERNEL_AUTOSELECT)->Check(curKernel == 0);
		mpKernelMenu->FindItem(ID_KERNEL_INTERNAL_OSB)->Check(curKernel == kATFirmwareId_Kernel_LLE);
		mpKernelMenu->FindItem(ID_KERNEL_INTERNAL_XL)->Check(curKernel == kATFirmwareId_Kernel_LLEXL);
		mpKernelMenu->FindItem(ID_KERNEL_INTERNAL_5200)->Check(curKernel == kATFirmwareId_5200_LLE);
	}

	// ---- Rebuild Profiles menu ----
	if (mpProfilesMenu) {
		// Remove all dynamic profile items
		while (mpProfilesMenu->GetMenuItemCount() > 0)
			mpProfilesMenu->Delete(mpProfilesMenu->FindItemByPosition(0));

		vdfastvector<uint32> profileIds;
		ATSettingsProfileEnum(profileIds);
		uint32 curProfile = ATSettingsGetCurrentProfileId();
		int visIdx = 0;
		for (uint32 pid : profileIds) {
			if (ATSettingsProfileGetVisible(pid)) {
				if (visIdx >= (ID_PROFILE_LAST - ID_PROFILE_FIRST + 1))
					break;
				VDStringA name = VDTextWToU8(ATSettingsProfileGetName(pid));
				wxMenuItem *item = mpProfilesMenu->InsertRadioItem(
					visIdx, ID_PROFILE_FIRST + visIdx, wxString(name.c_str()));
				item->Check(pid == curProfile);
				++visIdx;
			}
		}
	}

	// ---- Update Disk Drive submenu labels ----
	if (mpDiskDrivesMenu) {
		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			wxString label;
			if (di.IsDiskLoaded()) {
				VDStringW diskPath(di.GetPath());
				if (!diskPath.empty()) {
					VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(diskPath.c_str())));
					label = wxString::Format("D%d: %s", i + 1, fname.c_str());
				} else {
					label = wxString::Format("D%d: [new disk]", i + 1);
				}
			} else {
				label = wxString::Format("D%d: [empty]", i + 1);
			}
			wxMenuItem *menuItem = mpDiskDrivesMenu->FindItemByPosition(i);
			if (menuItem)
				menuItem->SetItemLabel(label);
		}
	}
}
