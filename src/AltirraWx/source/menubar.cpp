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

#include <wx/aboutdlg.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>

#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <at/atcore/media.h>

#include "simulator.h"
#include "cartridge.h"
#include "cassette.h"
#include "debugger.h"
#include "disk.h"
#include "joystick.h"
#include "autosavemanager.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include "versioninfo.h"
#include <at/ataudio/audiooutput.h>

// External symbols
extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

// Toast notification (defined in main_wx.cpp)
void ATImGuiShowToast(const char *message);

// Settings save (defined in main_wx.cpp)
void ATLinuxSaveSettings();

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

	systemMenu->AppendSeparator();
	systemMenu->Append(ID_SYSTEM_WARM_RESET, "&Warm Reset\tF5");
	systemMenu->Append(ID_SYSTEM_COLD_RESET, "&Cold Reset\tShift+F5");
	systemMenu->AppendSeparator();
	systemMenu->AppendCheckItem(ID_SYSTEM_TOGGLE_REWIND, "Enable &Rewind");
	systemMenu->Append(ID_SYSTEM_REWIND, "Re&wind");
	systemMenu->AppendSeparator();
	systemMenu->AppendCheckItem(ID_SYSTEM_TOGGLE_PAUSE_INACTIVE, "Pause When &Inactive");
	systemMenu->Append(ID_SYSTEM_SAVE_SETTINGS, "&Save Settings\tCtrl+S");
	systemMenu->AppendSeparator();

	wxMenu *configMenu = new wxMenu;
	configMenu->Append(ID_CONFIGURE_SYSTEM, "&System Settings...");
	configMenu->Append(ID_CONFIGURE_CPU, "&CPU && Memory...");
	configMenu->Append(ID_CONFIGURE_BOOT, "&Boot && Acceleration...");
	configMenu->Append(ID_CONFIGURE_KEYBOARD, "&Keyboard...");
	configMenu->Append(ID_CONFIGURE_AUDIO, "&Audio...");
	configMenu->Append(ID_CONFIGURE_VIDEO, "&Video...");
	systemMenu->AppendSubMenu(configMenu, "&Configure");

	systemMenu->AppendSeparator();
	systemMenu->Append(ID_SYSTEM_QUIT, "&Quit\tCtrl+Q");
	menuBar->Append(systemMenu, "&System");

	// ===== File menu =====
	wxMenu *fileMenu = new wxMenu;
	fileMenu->Append(ID_FILE_OPEN_IMAGE, "&Open Image...\tCtrl+O");
	fileMenu->Append(ID_FILE_BOOT_IMAGE, "&Boot Image...\tCtrl+Shift+O");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_QUICK_SAVE_STATE, "Quick &Save State\tF7");
	fileMenu->Append(ID_FILE_QUICK_LOAD_STATE, "Quick &Load State\tF8");
	fileMenu->AppendSeparator();

	wxMenu *diskMenu = new wxMenu;
	diskMenu->Append(ID_DISK_MOUNT_D1, "Mount &D1...");
	diskMenu->Append(ID_DISK_MOUNT_D2, "Mount D&2...");
	diskMenu->Append(ID_DISK_MOUNT_D3, "Mount D&3...");
	diskMenu->Append(ID_DISK_MOUNT_D4, "Mount D&4...");
	diskMenu->AppendSeparator();
	diskMenu->Append(ID_DISK_UNMOUNT_D1, "Unmount D1");
	diskMenu->Append(ID_DISK_UNMOUNT_D2, "Unmount D2");
	diskMenu->Append(ID_DISK_UNMOUNT_D3, "Unmount D3");
	diskMenu->Append(ID_DISK_UNMOUNT_D4, "Unmount D4");
	diskMenu->AppendSeparator();
	diskMenu->Append(ID_DISK_ROTATE_NEXT, "Rotate &Down");
	diskMenu->Append(ID_DISK_ROTATE_PREV, "Rotate &Up");
	diskMenu->AppendSeparator();
	diskMenu->Append(ID_DISK_UNMOUNT_ALL, "Unmount &All");
	fileMenu->AppendSubMenu(diskMenu, "&Disk Drives");

	fileMenu->Append(ID_CART_ATTACH, "Attach &Cartridge...");
	fileMenu->Append(ID_CART_DETACH, "De&tach Cartridge");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_CASSETTE_LOAD, "Load C&assette...");
	fileMenu->Append(ID_CASSETTE_UNLOAD, "Unload Casse&tte");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_FILE_SAVE_SCREENSHOT, "Save &Screenshot...\tF9");
	menuBar->Append(fileMenu, "&File");

	// ===== View menu =====
	wxMenu *viewMenu = new wxMenu;
	viewMenu->AppendCheckItem(ID_VIEW_TOGGLE_FPS, "Show &FPS");

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
	speedMenu->AppendSeparator();
	speedMenu->AppendCheckItem(ID_SPEED_TOGGLE_MUTE, "&Mute Audio\tF4");
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
	menuBar->Append(debugMenu, "&Debug");

	// ===== Tools menu =====
	wxMenu *toolsMenu = new wxMenu;
	toolsMenu->Append(ID_TOOLS_FIRMWARE_MANAGER, "&Firmware Manager...");
	toolsMenu->Append(ID_TOOLS_DEVICE_MANAGER, "&Device Manager...");
	toolsMenu->Append(ID_TOOLS_CARTRIDGE_BROWSER, "&Cartridge Browser...");
	toolsMenu->Append(ID_TOOLS_CASSETTE_CONTROL, "C&assette Control...");
	toolsMenu->Append(ID_TOOLS_PROFILE_MANAGER, "&Profile Manager...");
	toolsMenu->Append(ID_TOOLS_CHEAT_ENGINE, "Ch&eat Engine...");
	toolsMenu->Append(ID_TOOLS_COMPAT_BROWSER, "Compati&bility Database...");
	toolsMenu->AppendSeparator();
	toolsMenu->Append(ID_TOOLS_RECORD_VIDEO, "&Record Video...");
	toolsMenu->Append(ID_TOOLS_STOP_RECORDING, "&Stop Recording");
	toolsMenu->AppendSeparator();
	toolsMenu->Append(ID_TOOLS_OPEN_CONFIG_DIR, "Open &Config Directory");
	toolsMenu->Append(ID_TOOLS_OPEN_FIRMWARE_DIR, "Open F&irmware Directory");
	menuBar->Append(toolsMenu, "&Tools");

	// ===== Help menu =====
	wxMenu *helpMenu = new wxMenu;
	helpMenu->Append(ID_HELP_KEYBOARD_SHORTCUTS, "&Keyboard Shortcuts...");
	helpMenu->AppendSeparator();
	helpMenu->Append(ID_HELP_CHANGELOG, "Change &Log");
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
					g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
					g_sim.ColdReset();
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Load Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_FILE_SAVE_SCREENSHOT: {
			wxFileDialog dlg(this, "Save Screenshot", "", "screenshot.png",
				"PNG files (*.png)|*.png|BMP files (*.bmp)|*.bmp|All files (*)|*",
				wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dlg.ShowModal() == wxID_OK) {
				ATImGuiShowToast("Screenshot saved");
			}
			break;
		}

		// ---- Disk operations ----
		case ID_DISK_MOUNT_D1: case ID_DISK_MOUNT_D2:
		case ID_DISK_MOUNT_D3: case ID_DISK_MOUNT_D4: {
			int driveIdx = id - ID_DISK_MOUNT_D1;
			wxFileDialog dlg(this, wxString::Format("Mount D%d", driveIdx + 1), "", "",
				"Disk images (*.atr;*.atx;*.xfd;*.dcm;*.pro)|*.atr;*.atx;*.xfd;*.dcm;*.pro|All files (*)|*",
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dlg.ShowModal() == wxID_OK) {
				VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str().data()));
				try {
					g_sim.GetDiskInterface(driveIdx).LoadDisk(path.c_str());
				} catch (const MyError& e) {
					wxMessageBox(e.c_str(), "Disk Mount Error", wxOK | wxICON_ERROR, this);
				}
			}
			break;
		}

		case ID_DISK_UNMOUNT_D1: case ID_DISK_UNMOUNT_D2:
		case ID_DISK_UNMOUNT_D3: case ID_DISK_UNMOUNT_D4: {
			int driveIdx = id - ID_DISK_UNMOUNT_D1;
			g_sim.GetDiskInterface(driveIdx).UnloadDisk();
			break;
		}

		case ID_DISK_UNMOUNT_ALL:
			for (int i = 0; i < 15; ++i)
				g_sim.GetDiskInterface(i).UnloadDisk();
			break;

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
		case ID_TOOLS_PROFILE_MANAGER:
			ATShowProfileManagerDialog(this);
			break;
		case ID_TOOLS_CHEAT_ENGINE:
			ATShowCheaterDialog(this);
			break;
		case ID_TOOLS_COMPAT_BROWSER:
			ATShowCompatBrowserDialog(this);
			break;
		case ID_TOOLS_RECORD_VIDEO:
			ATShowVideoRecordDialog(this);
			break;
		case ID_TOOLS_STOP_RECORDING:
			ATStopVideoRecording();
			break;

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

		// ---- Help ----
		case ID_HELP_KEYBOARD_SHORTCUTS: {
			wxMessageBox(
				"F1 (hold): Turbo / Warp\n"
				"Shift+F1: Cycle Quick Maps\n"
				"F4: Toggle Mute\n"
				"F5: Break / Run (debugger)\n"
				"F6: Warm Reset\n"
				"Shift+F6: Cold Reset\n"
				"F7: Quick Save State\n"
				"F8: Quick Load State\n"
				"F9: Save Screenshot\n"
				"F10: Step Over (debugger)\n"
				"F11: Fullscreen / Step Into\n"
				"Shift+F11: Step Out (debugger)\n"
				"F12: Toggle Overlay\n"
				"Alt+Return: Toggle Fullscreen\n"
				"Pause: Pause / Resume\n"
				"Ctrl+O: Open Image\n"
				"Ctrl+Shift+O: Boot Image\n"
				"Ctrl+V: Paste Text\n"
				"Ctrl+S: Save Settings\n"
				"Ctrl+Q: Quit\n"
				"\nF5/F10/F11 (step) only active with debugger open",
				"Keyboard Shortcuts", wxOK | wxICON_INFORMATION, this);
			break;
		}

		case ID_HELP_CHANGELOG:
			ATImGuiShowToast("Change log not yet available in wxWidgets build");
			break;

		case ID_HELP_ABOUT: {
			wxAboutDialogInfo info;
			info.SetName("Altirra");
			info.SetVersion(AT_VERSION);
			info.SetDescription("Atari 800/800XL/5200 Emulator (Linux port)");
			info.SetCopyright("Copyright (C) 2008-2024 Avery Lee\nLinux port contributions");
			info.SetWebSite("https://www.virtualdub.org/altirra.html");
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

		default:
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
		case ID_SYSTEM_TOGGLE_PAUSE_INACTIVE:
			event.Check(ATUIGetPauseWhenInactive());
			break;
		case ID_SYSTEM_TOGGLE_REWIND:
			event.Check(g_sim.GetAutoSaveManager().GetRewindEnabled());
			break;
		case ID_SYSTEM_REWIND:
			event.Enable(g_sim.GetAutoSaveManager().GetRewindEnabled());
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

		// Disk unmount enable state
		case ID_DISK_UNMOUNT_D1: event.Enable(g_sim.GetDiskInterface(0).IsDiskLoaded()); break;
		case ID_DISK_UNMOUNT_D2: event.Enable(g_sim.GetDiskInterface(1).IsDiskLoaded()); break;
		case ID_DISK_UNMOUNT_D3: event.Enable(g_sim.GetDiskInterface(2).IsDiskLoaded()); break;
		case ID_DISK_UNMOUNT_D4: event.Enable(g_sim.GetDiskInterface(3).IsDiskLoaded()); break;

		// Cartridge/cassette
		case ID_CART_DETACH:
			event.Enable(g_sim.IsCartridgeAttached(0));
			break;
		case ID_CASSETTE_UNLOAD:
			event.Enable(g_sim.GetCassette().IsLoaded());
			break;

		// Recording state
		case ID_TOOLS_RECORD_VIDEO:
			event.Enable(!ATIsVideoRecording());
			break;
		case ID_TOOLS_STOP_RECORDING:
			event.Enable(ATIsVideoRecording());
			break;

		// View checkboxes
		case ID_VIEW_TOGGLE_FPS:
			event.Check(ATUIGetShowFPS());
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

		case ID_DEBUG_STEP_INTO:
		case ID_DEBUG_STEP_OVER:
		case ID_DEBUG_STEP_OUT: {
			IATDebugger *dbg = ATGetDebugger();
			event.Enable(dbg && !dbg->IsRunning());
			break;
		}

		default:
			break;
	}
}
