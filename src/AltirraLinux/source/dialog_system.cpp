//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include "dialogs_wx.h"

#include <algorithm>
#include <cstring>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treebook.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>

#include <at/atui/uicommandmanager.h>

#include "simulator.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "options.h"
#include "firmwaremanager.h"
#include "devicemanager.h"
#include "cartridge.h"
#include "cassette.h"
#include "disk.h"
#include "diskinterface.h"
#include "settings.h"
#include "constants.h"
#include <at/atcore/device.h>
#include <at/atcore/media.h>
#include <at/atcore/profile.h>
#include <at/atio/cartridgetypes.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>

extern ATSimulator g_sim;
extern ATOptions g_ATOptions;
extern ATUICommandManager g_ATUICommandMgr;
extern ATUIKeyboardOptions g_kbdOpts;

///////////////////////////////////////////////////////////////////////////
// Helpers: command-bound controls
///////////////////////////////////////////////////////////////////////////

namespace {

// Execute a command by name
void ExecCmd(const char *cmd) {
	g_ATUICommandMgr.ExecuteCommandNT(cmd);
}

// Query whether a command is checked/radio-checked
bool IsCmdChecked(const char *cmd) {
	const ATUICommand *c = g_ATUICommandMgr.GetCommand(cmd);
	if (!c || !c->mpStateFn)
		return false;
	ATUICmdState st = c->mpStateFn();
	return st == kATUICmdState_Checked || st == kATUICmdState_RadioChecked;
}

// Query whether a command is enabled (testFn returns true or is null)
bool IsCmdEnabled(const char *cmd) {
	const ATUICommand *c = g_ATUICommandMgr.GetCommand(cmd);
	if (!c)
		return false;
	return !c->mpTestFn || c->mpTestFn();
}

// Create a checkbox bound to a toggle command. When clicked, executes
// the command; when the panel is refreshed, reads the command state.
struct CmdCheckbox {
	wxCheckBox *ctrl;
	const char *cmd;
};

wxCheckBox *MakeCmdCheckbox(wxWindow *parent, wxSizer *sizer, const char *label, const char *cmd, std::vector<CmdCheckbox>& bindings) {
	wxCheckBox *cb = new wxCheckBox(parent, wxID_ANY, label);
	sizer->Add(cb, 0, wxLEFT | wxTOP, 4);
	bindings.push_back({cb, cmd});
	return cb;
}

void ReadCmdCheckboxes(std::vector<CmdCheckbox>& bindings) {
	for (auto& b : bindings) {
		b.ctrl->SetValue(IsCmdChecked(b.cmd));
		b.ctrl->Enable(IsCmdEnabled(b.cmd));
	}
}

void BindCmdCheckboxEvents(std::vector<CmdCheckbox>& bindings) {
	for (auto& b : bindings) {
		b.ctrl->Bind(wxEVT_CHECKBOX, [cmd = b.cmd](wxCommandEvent&) {
			ExecCmd(cmd);
		});
	}
}

// CmdCombo: a wxChoice bound to a set of radio-checked commands
struct CmdComboEntry {
	const char *cmd;
	const char *label;
};

struct CmdCombo {
	wxChoice *ctrl;
	const CmdComboEntry *entries;
	int count;
};

wxChoice *MakeCmdCombo(wxWindow *parent, wxFlexGridSizer *grid, const char *label,
	const CmdComboEntry *entries, int count, std::vector<CmdCombo>& bindings) {
	grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
	wxChoice *ch = new wxChoice(parent, wxID_ANY);
	for (int i = 0; i < count; ++i) {
		if (IsCmdEnabled(entries[i].cmd))
			ch->Append(entries[i].label);
	}
	grid->Add(ch, 1, wxEXPAND);
	bindings.push_back({ch, entries, count});
	return ch;
}

void ReadCmdCombos(std::vector<CmdCombo>& bindings) {
	for (auto& b : bindings) {
		b.ctrl->Clear();
		int sel = -1;
		int idx = 0;
		for (int i = 0; i < b.count; ++i) {
			if (IsCmdEnabled(b.entries[i].cmd)) {
				b.ctrl->Append(b.entries[i].label);
				if (IsCmdChecked(b.entries[i].cmd))
					sel = idx;
				++idx;
			}
		}
		if (sel >= 0)
			b.ctrl->SetSelection(sel);
	}
}

void BindCmdComboEvents(std::vector<CmdCombo>& bindings) {
	for (auto& b : bindings) {
		b.ctrl->Bind(wxEVT_CHOICE, [&b](wxCommandEvent&) {
			int sel = b.ctrl->GetSelection();
			if (sel < 0) return;
			// Map visible index back to entry index
			int visIdx = 0;
			for (int i = 0; i < b.count; ++i) {
				if (IsCmdEnabled(b.entries[i].cmd)) {
					if (visIdx == sel) {
						ExecCmd(b.entries[i].cmd);
						return;
					}
					++visIdx;
				}
			}
		});
	}
}

// Refresh all bindings
void RefreshAll(std::vector<CmdCheckbox>& cbs, std::vector<CmdCombo>& combos) {
	ReadCmdCheckboxes(cbs);
	ReadCmdCombos(combos);
}

///////////////////////////////////////////////////////////////////////////
// Base page panel
///////////////////////////////////////////////////////////////////////////

class ConfigPage : public wxPanel {
public:
	ConfigPage(wxWindow *parent) : wxPanel(parent) {}

	virtual void Refresh() {
		RefreshAll(mCheckboxes, mCombos);
	}

protected:
	std::vector<CmdCheckbox> mCheckboxes;
	std::vector<CmdCombo> mCombos;

	void BindAllEvents() {
		BindCmdCheckboxEvents(mCheckboxes);
		BindCmdComboEvents(mCombos);
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Overview
///////////////////////////////////////////////////////////////////////////

class OverviewPage : public ConfigPage {
public:
	OverviewPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

		mpText = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
			wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH);
		mpText->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_MODERN)));

		sizer->Add(mpText, 1, wxEXPAND | wxALL, 5);
		SetSizer(sizer);
	}

	void Refresh() override {
		wxString text;

		// Base system
		const char *hwNames[] = {"800", "800XL", "5200", "XEGS", "1200XL", "130XE", "1400XL"};
		const char *vsNames[] = {"NTSC", "PAL", "SECAM", "PAL-60", "NTSC-50"};

		int hw = (int)g_sim.GetHardwareMode();
		int vs = (int)g_sim.GetVideoStandard();

		text += wxString::Format("Base system:\t%s %s",
			(hw >= 0 && hw < 7) ? vsNames[std::min(vs, 4)] : "?",
			(hw >= 0 && hw < 7) ? hwNames[hw] : "?");

		// Memory
		const char *memStr = "";
		switch (g_sim.GetMemoryMode()) {
			case kATMemoryMode_8K: memStr = "8K"; break;
			case kATMemoryMode_16K: memStr = "16K"; break;
			case kATMemoryMode_24K: memStr = "24K"; break;
			case kATMemoryMode_32K: memStr = "32K"; break;
			case kATMemoryMode_40K: memStr = "40K"; break;
			case kATMemoryMode_48K: memStr = "48K"; break;
			case kATMemoryMode_52K: memStr = "52K"; break;
			case kATMemoryMode_64K: memStr = "64K"; break;
			case kATMemoryMode_128K: memStr = "128K"; break;
			case kATMemoryMode_256K: memStr = "256K"; break;
			case kATMemoryMode_320K: memStr = "320K (Rambo)"; break;
			case kATMemoryMode_320K_Compy: memStr = "320K (Compy)"; break;
			case kATMemoryMode_576K: memStr = "576K (Rambo)"; break;
			case kATMemoryMode_576K_Compy: memStr = "576K (Compy)"; break;
			case kATMemoryMode_1088K: memStr = "1088K"; break;
			default: memStr = "?"; break;
		}
		text += wxString::Format(" (%s)", memStr);

		// Devices
		text += "\n\nAdditional devices:\t";
		bool first = true;
		for (IATDevice *dev : g_sim.GetDeviceManager()->GetDevices(true, true, true)) {
			ATDeviceInfo info;
			dev->GetDeviceInfo(info);
			if (!first) text += ", ";
			text += wxString::FromUTF8(VDTextWToU8(VDStringW(info.mpDef->mpName)).c_str());
			first = false;
		}
		if (first) text += "None";

		// OS firmware
		text += "\n\nOS firmware:\t";
		ATFirmwareInfo fwInfo;
		g_sim.GetFirmwareManager()->GetFirmwareInfo(g_sim.GetActualKernelId(), fwInfo);
		text += wxString::FromUTF8(VDTextWToU8(fwInfo.mName).c_str());
		text += wxString::Format(" [%08X]", g_sim.ComputeKernelCRC32());

		// Mounted images
		text += "\n\nMounted images:\t";
		bool foundImage = false;
		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			if (di.GetDiskImage()) {
				const wchar_t *path = VDFileSplitPath(di.GetPath());
				if (foundImage) text += "\n\t\t";
				text += wxString::Format("Disk: %s", wxString::FromUTF8(VDTextWToU8(VDStringW(path)).c_str()));
				foundImage = true;
			}
		}
		for (uint32 i = 0; i < 2; ++i) {
			ATCartridgeEmulator *ce = g_sim.GetCartridge(i);
			if (ce && ce->GetPath()) {
				const wchar_t *path = VDFileSplitPath(ce->GetPath());
				if (foundImage) text += "\n\t\t";
				text += wxString::Format("Cartridge: %s", wxString::FromUTF8(VDTextWToU8(VDStringW(path)).c_str()));
				foundImage = true;
			}
		}
		if (!foundImage) text += "None";

		// BASIC
		text += wxString::Format("\n\nBASIC:\t\t%s", g_sim.IsBASICEnabled() ? "Enabled" : "Disabled");

		mpText->SetValue(text);
	}

private:
	wxTextCtrl *mpText;
};

///////////////////////////////////////////////////////////////////////////
// Page: System
///////////////////////////////////////////////////////////////////////////

class SystemPage : public ConfigPage {
public:
	SystemPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kHardware[] = {
			{"System.HardwareMode800", "400/800"},
			{"System.HardwareMode800XL", "600XL/800XL"},
			{"System.HardwareMode130XE", "65XE/130XE"},
			{"System.HardwareMode1200XL", "1200XL"},
			{"System.HardwareModeXEGS", "XE Game System (XEGS)"},
			{"System.HardwareMode1400XL", "1400XL/1450XLD"},
			{"System.HardwareMode5200", "5200 SuperSystem"},
		};
		MakeCmdCombo(this, grid, "Hardware type:", kHardware, 7, mCombos);

		static const CmdComboEntry kVideo[] = {
			{"Video.StandardNTSC", "NTSC"},
			{"Video.StandardPAL", "PAL"},
			{"Video.StandardSECAM", "SECAM"},
			{"Video.StandardNTSC50", "NTSC-50"},
			{"Video.StandardPAL60", "PAL-60"},
		};
		MakeCmdCombo(this, grid, "Video standard:", kVideo, 5, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		MakeCmdCheckbox(this, top, "CTIA (no GTIA modes)", "Video.ToggleCTIA", mCheckboxes);

		SetSizer(top);
		BindAllEvents();

		// When hardware changes, refresh combos to update video standard
		for (auto& b : mCombos) {
			b.ctrl->Bind(wxEVT_CHOICE, [this](wxCommandEvent& evt) {
				evt.Skip();
				CallAfter([this] { Refresh(); });
			});
		}
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: CPU
///////////////////////////////////////////////////////////////////////////

class CPUPage : public ConfigPage {
public:
	CPUPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kCPU[] = {
			{"System.CPUMode6502", "6502"},
			{"System.CPUMode65C02", "65C02"},
			{"System.CPUMode65C816", "65C816"},
			{"System.CPUMode65C816x2", "65C816 @ 3.58MHz"},
			{"System.CPUMode65C816x4", "65C816 @ 7.16MHz"},
			{"System.CPUMode65C816x6", "65C816 @ 10.74MHz"},
			{"System.CPUMode65C816x8", "65C816 @ 14.32MHz"},
			{"System.CPUMode65C816x10", "65C816 @ 17.90MHz"},
			{"System.CPUMode65C816x12", "65C816 @ 21.48MHz"},
		};
		MakeCmdCombo(this, grid, "CPU model:", kCPU, 9, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *opts = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
		MakeCmdCheckbox(this, opts, "Enable CPU history tracing", "System.ToggleCPUHistory", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Enable path tracing", "System.ToggleCPUPathTracing", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Enable illegal instructions", "System.ToggleCPUIllegalInstructions", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Stop on BRK instruction", "System.ToggleCPUStopOnBRK", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Allow NMI blocking", "System.ToggleCPUNMIBlocking", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Shadow ROM", "System.ToggleShadowROM", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Shadow cartridges", "System.ToggleShadowCarts", mCheckboxes);
		top->Add(opts, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Firmware
///////////////////////////////////////////////////////////////////////////

class FirmwarePage : public ConfigPage {
public:
	FirmwarePage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		grid->Add(new wxStaticText(this, wxID_ANY, "OS firmware:"), 0, wxALIGN_CENTER_VERTICAL);
		mpOSChoice = new wxChoice(this, wxID_ANY);
		grid->Add(mpOSChoice, 1, wxEXPAND);

		grid->Add(new wxStaticText(this, wxID_ANY, "BASIC:"), 0, wxALIGN_CENTER_VERTICAL);
		mpBasicChoice = new wxChoice(this, wxID_ANY);
		grid->Add(mpBasicChoice, 1, wxEXPAND);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		MakeCmdCheckbox(this, top, "Enable internal BASIC", "System.ToggleBASIC", mCheckboxes);

		wxButton *fwBtn = new wxButton(this, wxID_ANY, "Firmware Manager...");
		fwBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			ATShowFirmwareManagerDialogModal(this);
			PopulateFirmwareLists();
		});
		top->Add(fwBtn, 0, wxALL, 8);

		SetSizer(top);
		BindAllEvents();
		PopulateFirmwareLists();

		mpOSChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
			int sel = mpOSChoice->GetSelection();
			if (sel >= 0 && sel < (int)mKernelIds.size()) {
				g_sim.SetKernel(mKernelIds[sel]);
				g_sim.LoadROMs();
			}
		});

		mpBasicChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
			int sel = mpBasicChoice->GetSelection();
			if (sel >= 0 && sel < (int)mBasicIds.size())
				g_sim.SetBasic(mBasicIds[sel]);
		});
	}

	void Refresh() override {
		ConfigPage::Refresh();
		PopulateFirmwareLists();
	}

private:
	wxChoice *mpOSChoice;
	wxChoice *mpBasicChoice;
	std::vector<uint64> mKernelIds;
	std::vector<uint64> mBasicIds;

	struct FwSortEntry {
		uint64 id;
		VDStringA name;
	};

	void PopulateFirmwareLists() {
		ATFirmwareManager *fwm = g_sim.GetFirmwareManager();

		vdvector<ATFirmwareInfo> fws;
		fwm->GetFirmwareList(fws);

		bool is5200 = (g_sim.GetHardwareMode() == kATHardwareMode_5200);

		// --- OS firmware ---
		mpOSChoice->Clear();
		mKernelIds.clear();

		mpOSChoice->Append("Internal (default)");
		mKernelIds.push_back(0);

		std::vector<FwSortEntry> osEntries;
		for (const auto& fw : fws) {
			bool compatible = false;
			if (is5200 && fw.mType == kATFirmwareType_Kernel5200)
				compatible = true;
			else if (!is5200) {
				if (fw.mType == kATFirmwareType_Kernel800_OSA ||
					fw.mType == kATFirmwareType_Kernel800_OSB ||
					fw.mType == kATFirmwareType_KernelXL ||
					fw.mType == kATFirmwareType_KernelXEGS ||
					fw.mType == kATFirmwareType_Kernel1200XL)
					compatible = true;
			}
			if (compatible)
				osEntries.push_back({ fw.mId, VDTextWToU8(fw.mName) });
		}

		std::sort(osEntries.begin(), osEntries.end(),
			[](const FwSortEntry& a, const FwSortEntry& b) {
				return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
			});

		int selOS = 0;
		uint64 curKernel = g_sim.GetKernelId();
		for (const auto& e : osEntries) {
			mpOSChoice->Append(wxString::FromUTF8(e.name.c_str()));
			mKernelIds.push_back(e.id);
			if (e.id == curKernel)
				selOS = (int)mKernelIds.size() - 1;
		}
		mpOSChoice->SetSelection(selOS);

		// --- BASIC firmware ---
		mpBasicChoice->Clear();
		mBasicIds.clear();

		mpBasicChoice->Append("Internal (default)");
		mBasicIds.push_back(0);

		std::vector<FwSortEntry> basicEntries;
		for (const auto& fw : fws) {
			if (fw.mType == kATFirmwareType_Basic)
				basicEntries.push_back({ fw.mId, VDTextWToU8(fw.mName) });
		}

		std::sort(basicEntries.begin(), basicEntries.end(),
			[](const FwSortEntry& a, const FwSortEntry& b) {
				return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
			});

		int selBasic = 0;
		uint64 curBasic = g_sim.GetBasicId();
		for (const auto& e : basicEntries) {
			mpBasicChoice->Append(wxString::FromUTF8(e.name.c_str()));
			mBasicIds.push_back(e.id);
			if (e.id == curBasic)
				selBasic = (int)mBasicIds.size() - 1;
		}
		mpBasicChoice->SetSelection(selBasic);
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Memory
///////////////////////////////////////////////////////////////////////////

class MemoryPage : public ConfigPage {
public:
	MemoryPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kMemType[] = {
			{"System.MemoryMode8K", "8K"},
			{"System.MemoryMode16K", "16K"},
			{"System.MemoryMode24K", "24K"},
			{"System.MemoryMode32K", "32K"},
			{"System.MemoryMode40K", "40K"},
			{"System.MemoryMode48K", "48K (800)"},
			{"System.MemoryMode52K", "52K"},
			{"System.MemoryMode64K", "64K (800XL/1200XL)"},
			{"System.MemoryMode128K", "128K (130XE)"},
			{"System.MemoryMode256K", "256K (Rambo)"},
			{"System.MemoryMode320K", "320K (Rambo)"},
			{"System.MemoryMode320KCompy", "320K (Compy)"},
			{"System.MemoryMode576K", "576K (Rambo)"},
			{"System.MemoryMode576KCompy", "576K (Compy)"},
			{"System.MemoryMode1088K", "1088K"},
		};
		MakeCmdCombo(this, grid, "Memory size:", kMemType, 15, mCombos);

		static const CmdComboEntry kClear[] = {
			{"System.MemoryClearDRAM1", "DRAM 1 (default)"},
			{"System.MemoryClearDRAM2", "DRAM 2"},
			{"System.MemoryClearDRAM3", "DRAM 3"},
			{"System.MemoryClearRandom", "SRAM (random)"},
			{"System.MemoryClearZero", "Cleared"},
		};
		MakeCmdCombo(this, grid, "Power-up pattern:", kClear, 5, mCombos);

		static const CmdComboEntry kAxlon[] = {
			{"System.AxlonMemoryNone", "None"},
			{"System.AxlonMemory64K", "64K (4 banks)"},
			{"System.AxlonMemory128K", "128K (8 banks)"},
			{"System.AxlonMemory256K", "256K (16 banks)"},
			{"System.AxlonMemory512K", "512K (32 banks)"},
			{"System.AxlonMemory1024K", "1024K (64 banks)"},
			{"System.AxlonMemory2048K", "2048K (128 banks)"},
			{"System.AxlonMemory4096K", "4096K (256 banks)"},
		};
		MakeCmdCombo(this, grid, "Axlon RAM disk:", kAxlon, 8, mCombos);

		static const CmdComboEntry kHighMem[] = {
			{"System.HighMemoryNA", "None (16-bit addressing)"},
			{"System.HighMemoryNone", "None (24-bit addressing)"},
			{"System.HighMemory64K", "64K (bank $01)"},
			{"System.HighMemory192K", "192K (banks $01-03)"},
			{"System.HighMemory960K", "1MB (banks $01-0F)"},
			{"System.HighMemory4032K", "4MB (banks $01-3F)"},
			{"System.HighMemory16320K", "16MB (banks $01-FF)"},
		};
		MakeCmdCombo(this, grid, "High memory:", kHighMem, 7, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *opts = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
		MakeCmdCheckbox(this, opts, "Enable MapRAM (XL/XE only)", "System.ToggleMapRAM", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Preserve extended memory on cold reset", "System.TogglePreserveExtRAM", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Enable Axlon bank register aliasing", "System.ToggleAxlonAliasing", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Enable floating I/O bus (800 only)", "System.ToggleFloatingIOBus", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Enable Ultimate1MB", "System.ToggleUltimate1MB", mCheckboxes);
		top->Add(opts, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Acceleration
///////////////////////////////////////////////////////////////////////////

class AccelerationPage : public ConfigPage {
public:
	AccelerationPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		wxStaticBoxSizer *sio = new wxStaticBoxSizer(wxVERTICAL, this, "SIO Acceleration");
		MakeCmdCheckbox(this, sio, "Fast boot", "System.ToggleFastBoot", mCheckboxes);
		MakeCmdCheckbox(this, sio, "Fast floating-point math", "System.ToggleFPPatch", mCheckboxes);
		MakeCmdCheckbox(this, sio, "SIO C: patch (cassette)", "Cassette.ToggleSIOPatch", mCheckboxes);
		MakeCmdCheckbox(this, sio, "SIO D: patch (disk)", "Disk.ToggleSIOPatch", mCheckboxes);
		MakeCmdCheckbox(this, sio, "SIO PRT: patch (devices)", "Devices.ToggleSIOPatch", mCheckboxes);
		MakeCmdCheckbox(this, sio, "SIO D: burst I/O", "Disk.ToggleBurstTransfers", mCheckboxes);
		MakeCmdCheckbox(this, sio, "SIO PRT: burst I/O", "Devices.ToggleSIOBurstTransfers", mCheckboxes);
		MakeCmdCheckbox(this, sio, "SIO override detection", "Disk.ToggleSIOOverrideDetection", mCheckboxes);
		top->Add(sio, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *cio = new wxStaticBoxSizer(wxVERTICAL, this, "CIO Acceleration");
		MakeCmdCheckbox(this, cio, "CIO H: patch", "Devices.ToggleCIOPatchH", mCheckboxes);
		MakeCmdCheckbox(this, cio, "CIO P: patch", "Devices.ToggleCIOPatchP", mCheckboxes);
		MakeCmdCheckbox(this, cio, "CIO R: patch", "Devices.ToggleCIOPatchR", mCheckboxes);
		MakeCmdCheckbox(this, cio, "CIO T: patch", "Devices.ToggleCIOPatchT", mCheckboxes);
		MakeCmdCheckbox(this, cio, "CIO burst transfers", "Devices.ToggleCIOBurstTransfers", mCheckboxes);
		top->Add(cio, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Speed
///////////////////////////////////////////////////////////////////////////

class SpeedPage : public ConfigPage {
public:
	SpeedPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kFrameRate[] = {
			{"System.SpeedMatchHardware", "Match hardware"},
			{"System.SpeedMatchBroadcast", "Broadcast"},
			{"System.SpeedMatchInteger", "Integral"},
		};
		MakeCmdCombo(this, grid, "Frame rate:", kFrameRate, 3, mCombos);

		// Speed slider
		grid->Add(new wxStaticText(this, wxID_ANY, "Speed:"), 0, wxALIGN_CENTER_VERTICAL);
		wxBoxSizer *speedRow = new wxBoxSizer(wxHORIZONTAL);
		mpSpeedSlider = new wxSlider(this, wxID_ANY, 100, 10, 800);
		mpSpeedLabel = new wxStaticText(this, wxID_ANY, "100%");
		mpSpeedLabel->SetMinSize(wxSize(50, -1));
		speedRow->Add(mpSpeedSlider, 1, wxEXPAND);
		speedRow->Add(mpSpeedLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
		grid->Add(speedRow, 1, wxEXPAND);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *opts = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
		MakeCmdCheckbox(this, opts, "Run as fast as possible (warp)", "System.ToggleWarpSpeed", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Pause when inactive", "System.TogglePauseWhenInactive", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Lock to refresh rate (adaptive VSync)", "System.ToggleVSyncAdaptiveSpeed", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Enable rewind recording", "System.ToggleRewindRecording", mCheckboxes);
		top->Add(opts, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();

		mpSpeedSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
			int val = mpSpeedSlider->GetValue();
			mpSpeedLabel->SetLabel(wxString::Format("%d%%", val));
			ATUISetSpeedModifier((float)val / 100.0f);
		});
	}

	void Refresh() override {
		ConfigPage::Refresh();
		int pct = (int)(ATUIGetSpeedModifier() * 100.0f + 0.5f);
		pct = std::clamp(pct, 10, 800);
		mpSpeedSlider->SetValue(pct);
		mpSpeedLabel->SetLabel(wxString::Format("%d%%", pct));
	}

private:
	wxSlider *mpSpeedSlider;
	wxStaticText *mpSpeedLabel;
};

///////////////////////////////////////////////////////////////////////////
// Page: Boot
///////////////////////////////////////////////////////////////////////////

class BootPage : public ConfigPage {
public:
	BootPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kLoadMode[] = {
			{"System.ProgramLoadModeDefault", "Default"},
			{"System.ProgramLoadModeDiskBoot", "Disk boot"},
			{"System.ProgramLoadModeType3Poll", "Type 3 poll"},
			{"System.ProgramLoadModeDeferred", "Deferred"},
		};
		MakeCmdCombo(this, grid, "Program load mode:", kLoadMode, 4, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *opts = new wxStaticBoxSizer(wxVERTICAL, this, "Boot behavior");
		MakeCmdCheckbox(this, opts, "Unload cartridges when booting program", "Options.ToggleBootUnloadCartridges", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Unload disks when booting program", "Options.ToggleBootUnloadDisks", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Unload tapes when booting program", "Options.ToggleBootUnloadTapes", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Randomize launch delay", "System.ToggleProgramLaunchDelayRandomization", mCheckboxes);
		top->Add(opts, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Video
///////////////////////////////////////////////////////////////////////////

class VideoPage : public ConfigPage {
public:
	VideoPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kArtifact[] = {
			{"Video.ArtifactingNone", "None"},
			{"Video.ArtifactingNTSC", "NTSC artifacting"},
			{"Video.ArtifactingNTSCHi", "NTSC high artifacting"},
			{"Video.ArtifactingPAL", "PAL artifacting"},
			{"Video.ArtifactingPALHi", "PAL high artifacting"},
			{"Video.ArtifactingAuto", "NTSC/PAL auto-switch"},
			{"Video.ArtifactingAutoHi", "NTSC/PAL high auto-switch"},
		};
		MakeCmdCombo(this, grid, "Artifacting:", kArtifact, 7, mCombos);

		static const CmdComboEntry kMonitor[] = {
			{"Video.MonitorModeColor", "Color"},
			{"Video.MonitorModeMonoGreen", "Monochrome (green phosphor)"},
			{"Video.MonitorModeMonoAmber", "Monochrome (amber phosphor)"},
			{"Video.MonitorModeMonoBluishWhite", "Monochrome (bluish white phosphor)"},
			{"Video.MonitorModeMonoWhite", "Monochrome (white phosphor)"},
			{"Video.MonitorModePERITEL", "RGB through PERITEL/SCART adapter"},
		};
		MakeCmdCombo(this, grid, "Monitor mode:", kMonitor, 6, mCombos);

		static const CmdComboEntry kDeint[] = {
			{"Video.DeinterlaceModeNone", "No deinterlacing"},
			{"Video.DeinterlaceModeAdaptiveBob", "Deinterlace (adaptive bob)"},
		};
		MakeCmdCombo(this, grid, "Deinterlace:", kDeint, 2, mCombos);

		static const CmdComboEntry kPALPhase[] = {
			{"Video.PALPhase0", "Phase 0"},
			{"Video.PALPhase1", "Phase 1"},
		};
		MakeCmdCombo(this, grid, "PAL phase:", kPALPhase, 2, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *opts = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
		MakeCmdCheckbox(this, opts, "Frame blending", "Video.ToggleFrameBlending", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Blend in linear color space", "Video.ToggleLinearFrameBlending", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Monochrome persistence", "Video.ToggleMonoPersistence", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Interlace support", "Video.ToggleInterlace", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Scanlines", "Video.ToggleScanlines", mCheckboxes);
		top->Add(opts, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Enhanced Text
///////////////////////////////////////////////////////////////////////////

class EnhancedTextPage : public ConfigPage {
public:
	EnhancedTextPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kMode[] = {
			{"Video.EnhancedModeNone", "Disabled"},
			{"Video.EnhancedModeHardware", "Hardware accelerated"},
			{"Video.EnhancedModeCIO", "Software (CIO intercept)"},
		};
		MakeCmdCombo(this, grid, "Enhanced text:", kMode, 3, mCombos);
		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Audio
///////////////////////////////////////////////////////////////////////////

class AudioPage : public ConfigPage {
public:
	AudioPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		wxStaticBoxSizer *gen = new wxStaticBoxSizer(wxVERTICAL, this, "General");
		MakeCmdCheckbox(this, gen, "Stereo (dual POKEY)", "Audio.ToggleStereo", mCheckboxes);
		MakeCmdCheckbox(this, gen, "Downmix stereo to mono", "Audio.ToggleStereoAsMono", mCheckboxes);
		MakeCmdCheckbox(this, gen, "Non-linear mixing", "Audio.ToggleNonlinearMixing", mCheckboxes);
		MakeCmdCheckbox(this, gen, "Console speaker simulation", "Audio.ToggleSpeakerFilter", mCheckboxes);
		MakeCmdCheckbox(this, gen, "Serial noise", "Audio.ToggleSerialNoise", mCheckboxes);
		MakeCmdCheckbox(this, gen, "Mute all", "Audio.ToggleMute", mCheckboxes);
		top->Add(gen, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *ch = new wxStaticBoxSizer(wxVERTICAL, this, "POKEY Channels");
		MakeCmdCheckbox(this, ch, "Channel 1", "Audio.ToggleChannel1", mCheckboxes);
		MakeCmdCheckbox(this, ch, "Channel 2", "Audio.ToggleChannel2", mCheckboxes);
		MakeCmdCheckbox(this, ch, "Channel 3", "Audio.ToggleChannel3", mCheckboxes);
		MakeCmdCheckbox(this, ch, "Channel 4", "Audio.ToggleChannel4", mCheckboxes);
		top->Add(ch, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *sch = new wxStaticBoxSizer(wxVERTICAL, this, "Secondary POKEY Channels");
		MakeCmdCheckbox(this, sch, "Channel 1", "Audio.ToggleSecondaryChannel1", mCheckboxes);
		MakeCmdCheckbox(this, sch, "Channel 2", "Audio.ToggleSecondaryChannel2", mCheckboxes);
		MakeCmdCheckbox(this, sch, "Channel 3", "Audio.ToggleSecondaryChannel3", mCheckboxes);
		MakeCmdCheckbox(this, sch, "Channel 4", "Audio.ToggleSecondaryChannel4", mCheckboxes);
		top->Add(sch, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Devices
///////////////////////////////////////////////////////////////////////////

class DevicesPage : public ConfigPage {
public:
	DevicesPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		mpDeviceList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 200),
			wxLC_REPORT | wxLC_SINGLE_SEL);
		mpDeviceList->AppendColumn("Device", wxLIST_FORMAT_LEFT, 200);
		mpDeviceList->AppendColumn("Type", wxLIST_FORMAT_LEFT, 150);
		top->Add(mpDeviceList, 1, wxEXPAND | wxALL, 8);

		wxBoxSizer *btnRow = new wxBoxSizer(wxHORIZONTAL);
		wxButton *addBtn = new wxButton(this, wxID_ANY, "Add...");
		wxButton *removeBtn = new wxButton(this, wxID_ANY, "Remove");
		wxButton *removeAllBtn = new wxButton(this, wxID_ANY, "Remove All");
		wxButton *settingsBtn = new wxButton(this, wxID_ANY, "Settings...");
		btnRow->Add(addBtn, 0, wxRIGHT, 4);
		btnRow->Add(removeBtn, 0, wxRIGHT, 4);
		btnRow->Add(removeAllBtn, 0, wxRIGHT, 4);
		btnRow->Add(settingsBtn, 0);
		top->Add(btnRow, 0, wxALL, 8);

		SetSizer(top);

		addBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			ATShowDeviceManagerDialog(this);
			RefreshDevices();
		});
		removeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			long sel = mpDeviceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if (sel >= 0 && sel < (long)mDevicePtrs.size()) {
				g_sim.GetDeviceManager()->RemoveDevice(mDevicePtrs[sel]);
				RefreshDevices();
			}
		});
		removeAllBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			g_sim.GetDeviceManager()->RemoveAllDevices(true);
			RefreshDevices();
		});
		settingsBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			ATShowDeviceManagerDialog(this);
			RefreshDevices();
		});
	}

	void Refresh() override {
		ConfigPage::Refresh();
		RefreshDevices();
	}

private:
	wxListCtrl *mpDeviceList;
	std::vector<IATDevice *> mDevicePtrs;

	void RefreshDevices() {
		mpDeviceList->DeleteAllItems();
		mDevicePtrs.clear();

		int idx = 0;
		for (IATDevice *dev : g_sim.GetDeviceManager()->GetDevices(true, true, true)) {
			ATDeviceInfo info;
			dev->GetDeviceInfo(info);

			VDStringA name = VDTextWToU8(VDStringW(info.mpDef->mpName));
			VDStringA tag(info.mpDef->mpTag);

			mpDeviceList->InsertItem(idx, wxString::FromUTF8(name.c_str()));
			mpDeviceList->SetItem(idx, 1, wxString::FromUTF8(tag.c_str()));
			mDevicePtrs.push_back(dev);
			++idx;
		}
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Keyboard
///////////////////////////////////////////////////////////////////////////

class KeyboardPage : public ConfigPage {
public:
	KeyboardPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kLayout[] = {
			{"Input.KeyboardLayoutNatural", "Natural: Map by typed character"},
			{"Input.KeyboardLayoutDirect", "Direct: Map by key location"},
			{"Input.KeyboardLayoutCustom", "Custom layout"},
		};
		MakeCmdCombo(this, grid, "Layout:", kLayout, 3, mCombos);

		static const CmdComboEntry kMode[] = {
			{"Input.KeyModeCooked", "Cooked keys"},
			{"Input.KeyModeRaw", "Raw keys"},
			{"Input.KeyModeFull", "Full raw keyboard scan"},
		};
		MakeCmdCombo(this, grid, "Key mode:", kMode, 3, mCombos);

		static const CmdComboEntry kArrow[] = {
			{"Input.ArrowKeyModeDefault", "Arrows by default; Ctrl inverted"},
			{"Input.ArrowKeyModeMapped", "Arrows; Ctrl/Shift mapped"},
			{"Input.ArrowKeyModeDirect", "Map directly to -/=/+/*"},
		};
		MakeCmdCombo(this, grid, "Arrow keys:", kArrow, 3, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *opts = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
		MakeCmdCheckbox(this, opts, "Enable 1200XL function keys", "Input.Toggle1200XLFunctionKeys", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Allow SHIFT key on reset", "Input.ToggleAllowShiftOnReset", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Share non-modifier keys with input maps", "Input.ToggleAllowInputMapKeyboardOverlap", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Share modifier keys with input maps", "Input.ToggleAllowInputMapKeyboardModifierOverlap", mCheckboxes);
		top->Add(opts, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Defaults (Media)
///////////////////////////////////////////////////////////////////////////

class DefaultsPage : public ConfigPage {
public:
	DefaultsPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kWriteMode[] = {
			{"Options.MediaDefaultModeRO", "Read only"},
			{"Options.MediaDefaultModeVRWSafe", "Virtual read/write (prohibit format)"},
			{"Options.MediaDefaultModeVRW", "Virtual read/write"},
			{"Options.MediaDefaultModeRW", "Read/write"},
		};
		MakeCmdCombo(this, grid, "Default write mode:", kWriteMode, 4, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Disk
///////////////////////////////////////////////////////////////////////////

class DiskPage : public ConfigPage {
public:
	DiskPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		MakeCmdCheckbox(this, top, "Accurate sector timing", "Disk.ToggleAccurateSectorTiming", mCheckboxes);
		MakeCmdCheckbox(this, top, "Show sector counter", "Disk.ToggleSectorCounter", mCheckboxes);
		MakeCmdCheckbox(this, top, "Drive sounds", "Disk.ToggleDriveSounds", mCheckboxes);

		top->AddSpacer(8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Cassette
///////////////////////////////////////////////////////////////////////////

class CassettePage : public ConfigPage {
public:
	CassettePage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kTurbo[] = {
			{"Cassette.TurboModeNone", "Disabled"},
			{"Cassette.TurboModeAlways", "Always on"},
			{"Cassette.TurboModeCommandControl", "SIO command (Turbo 2000)"},
			{"Cassette.TurboModeDataControl", "SIO data out (Turbo Blizzard)"},
			{"Cassette.TurboModeProceedSense", "SIO proceed (Turbo 6000)"},
			{"Cassette.TurboModeInterruptSense", "SIO interrupt (Rambit Turbo Tape)"},
			{"Cassette.TurboModeKSOTurbo2000", "Joystick port 2 (KSO Turbo 2000)"},
			{"Cassette.TurboModeTurboD", "Joystick port 2 (Turbo D)"},
		};
		MakeCmdCombo(this, grid, "Turbo mode:", kTurbo, 8, mCombos);

		static const CmdComboEntry kDecoder[] = {
			{"Cassette.TurboDecoderPeakHPF", "Peak + HPF (default)"},
			{"Cassette.TurboDecoderPeakHPFBalLo", "Peak + HPF + balance lo-hi"},
			{"Cassette.TurboDecoderPeakHPFBalHi", "Peak + HPF + balance hi-lo"},
			{"Cassette.TurboDecoderSlopeHPF", "Slope + HPF (old 3.x default)"},
			{"Cassette.TurboDecoderSlope", "Slope"},
		};
		MakeCmdCombo(this, grid, "Turbo decoder:", kDecoder, 5, mCombos);

		static const CmdComboEntry kDirect[] = {
			{"Cassette.DirectSenseNormal", "Normal (~2000 baud)"},
			{"Cassette.DirectSenseLowSpeed", "Low speed (~1000 baud)"},
			{"Cassette.DirectSenseHighSpeed", "High speed (~4000 baud)"},
			{"Cassette.DirectSenseMaxSpeed", "Max speed"},
		};
		MakeCmdCombo(this, grid, "Direct read filter:", kDirect, 4, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		wxStaticBoxSizer *opts = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
		MakeCmdCheckbox(this, opts, "Auto-boot on startup", "Cassette.ToggleAutoBoot", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Auto-boot BASIC", "Cassette.ToggleAutoBasicBoot", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Auto-rewind", "Cassette.ToggleAutoRewind", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Load data as audio", "Cassette.ToggleLoadDataAsAudio", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Randomize start position", "Cassette.ToggleRandomizeStartPosition", mCheckboxes);
		MakeCmdCheckbox(this, opts, "Invert turbo data polarity", "Cassette.ToggleTurboPreFilterInvert", mCheckboxes);
		MakeCmdCheckbox(this, opts, "VBI avoidance", "Cassette.ToggleVBIAvoidance", mCheckboxes);
		top->Add(opts, 0, wxEXPAND | wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Flash
///////////////////////////////////////////////////////////////////////////

class FlashPage : public ConfigPage {
public:
	FlashPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		// Flash chip selections use direct simulator API since there are
		// no command bindings for these in the shared command system.
		grid->Add(new wxStaticText(this, wxID_ANY, "SIC! flash chip:"), 0, wxALIGN_CENTER_VERTICAL);
		mpSICFlash = new wxChoice(this, wxID_ANY);
		mpSICFlash->Append("Am29F040B");
		mpSICFlash->Append("SST39SF040");
		mpSICFlash->Append("MX29F040");
		mpSICFlash->SetSelection(0);
		grid->Add(mpSICFlash, 1, wxEXPAND);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);

		top->Add(new wxStaticText(this, wxID_ANY,
			"Flash chip settings are used when creating new flash\n"
			"firmware images. They do not affect existing images."),
			0, wxALL, 8);

		SetSizer(top);
	}

private:
	wxChoice *mpSICFlash;
};

///////////////////////////////////////////////////////////////////////////
// Page: File Types (Linux-specific: no file associations on Linux)
///////////////////////////////////////////////////////////////////////////

class FileTypesPage : public ConfigPage {
public:
	FileTypesPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		top->Add(new wxStaticText(this, wxID_ANY,
			"File type associations are managed through your desktop\n"
			"environment's settings on Linux. Altirra registers its\n"
			"MIME types via the .desktop file during installation."),
			0, wxALL, 10);

		SetSizer(top);
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Accessibility
///////////////////////////////////////////////////////////////////////////

class AccessibilityPage : public ConfigPage {
public:
	AccessibilityPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		MakeCmdCheckbox(this, top, "Enable screen reader support", "View.ToggleReaderEnabled", mCheckboxes);
		top->AddSpacer(8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Compat DB
///////////////////////////////////////////////////////////////////////////

class CompatDBPage : public ConfigPage {
public:
	CompatDBPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		MakeCmdCheckbox(this, top, "Show compatibility warnings", "Options.ToggleCompatEnable", mCheckboxes);
		MakeCmdCheckbox(this, top, "Use internal database", "Options.ToggleCompatInternal", mCheckboxes);
		MakeCmdCheckbox(this, top, "Use external database", "Options.ToggleCompatExternal", mCheckboxes);

		top->AddSpacer(8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Debugger
///////////////////////////////////////////////////////////////////////////

class DebuggerPage : public ConfigPage {
public:
	DebuggerPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kSymPre[] = {
			{"Debug.SymbolPreStartDisabled", "Disabled"},
			{"Debug.SymbolPreStartDeferred", "Deferred"},
			{"Debug.SymbolPreStartEnabled", "Enabled"},
		};
		MakeCmdCombo(this, grid, "Pre-start symbols:", kSymPre, 3, mCombos);

		static const CmdComboEntry kSymPost[] = {
			{"Debug.SymbolPostStartDisabled", "Disabled"},
			{"Debug.SymbolPostStartDeferred", "Deferred"},
			{"Debug.SymbolPostStartEnabled", "Enabled"},
		};
		MakeCmdCombo(this, grid, "Post-start symbols:", kSymPost, 3, mCombos);

		static const CmdComboEntry kScript[] = {
			{"Debug.ScriptAutoLoadDisabled", "Disabled"},
			{"Debug.ScriptAutoLoadAsk", "Ask to load"},
			{"Debug.ScriptAutoLoadEnabled", "Enabled"},
		};
		MakeCmdCombo(this, grid, "Script auto-load:", kScript, 3, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Display 1
///////////////////////////////////////////////////////////////////////////

class Display1Page : public ConfigPage {
public:
	Display1Page(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		MakeCmdCheckbox(this, top, "Show on-screen indicators", "View.ToggleIndicators", mCheckboxes);
		MakeCmdCheckbox(this, top, "Pad indicators margin", "View.ToggleIndicatorMargin", mCheckboxes);
		MakeCmdCheckbox(this, top, "Auto-hide mouse pointer", "View.ToggleAutoHidePointer", mCheckboxes);
		MakeCmdCheckbox(this, top, "Hide target pointer", "View.ToggleTargetPointer", mCheckboxes);
		MakeCmdCheckbox(this, top, "Show pad bounds", "View.TogglePadBounds", mCheckboxes);
		MakeCmdCheckbox(this, top, "Show pad pointers", "View.TogglePadPointers", mCheckboxes);
		MakeCmdCheckbox(this, top, "Constrain pointer in fullscreen", "View.ToggleConstrainPointerFullScreen", mCheckboxes);

		top->AddSpacer(8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Display 2
///////////////////////////////////////////////////////////////////////////

class Display2Page : public ConfigPage {
public:
	Display2Page(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		MakeCmdCheckbox(this, top, "Hardware acceleration for screen effects", "View.ToggleAccelScreenFX", mCheckboxes);

		top->Add(new wxStaticText(this, wxID_ANY,
			"\nFullscreen and display mode settings are handled by your\n"
			"window manager on Linux. Use F11 or Alt+Enter for fullscreen."),
			0, wxALL, 8);

		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Ease of Use
///////////////////////////////////////////////////////////////////////////

class EaseOfUsePage : public ConfigPage {
public:
	EaseOfUsePage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		MakeCmdCheckbox(this, top, "Auto-reset when changing cartridges", "Options.ToggleAutoResetCartridge", mCheckboxes);
		MakeCmdCheckbox(this, top, "Auto-reset when toggling BASIC", "Options.ToggleAutoResetBasic", mCheckboxes);
		MakeCmdCheckbox(this, top, "Auto-reset when changing video standard", "Options.ToggleAutoResetVideoStandard", mCheckboxes);

		top->AddSpacer(8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Error Handling
///////////////////////////////////////////////////////////////////////////

class ErrorHandlingPage : public ConfigPage {
public:
	ErrorHandlingPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
		grid->AddGrowableCol(1, 1);

		static const CmdComboEntry kErrorMode[] = {
			{"Options.ErrorModeDialog", "Show error dialog (default)"},
			{"Options.ErrorModeDebugger", "Break into the debugger"},
			{"Options.ErrorModePause", "Pause the emulation"},
			{"Options.ErrorModeColdReset", "Cold reset the emulation"},
		};
		MakeCmdCombo(this, grid, "Error handling:", kErrorMode, 4, mCombos);

		top->Add(grid, 0, wxEXPAND | wxALL, 8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Input
///////////////////////////////////////////////////////////////////////////

class InputPage : public ConfigPage {
public:
	InputPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		MakeCmdCheckbox(this, top, "Immediate analog pot update", "Input.ToggleImmediatePotUpdate", mCheckboxes);
		MakeCmdCheckbox(this, top, "Immediate light pen update", "Input.ToggleImmediateLightPenUpdate", mCheckboxes);
		MakeCmdCheckbox(this, top, "Enable paddle noise", "Input.TogglePotNoise", mCheckboxes);

		top->AddSpacer(8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Settings
///////////////////////////////////////////////////////////////////////////

class SettingsPage : public ConfigPage {
public:
	SettingsPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		top->Add(new wxStaticText(this, wxID_ANY,
			"Settings are stored in:\n"
			"  ~/.config/altirra/Altirra.ini"),
			0, wxALL, 10);

		wxButton *resetBtn = new wxButton(this, wxID_ANY, "Reset All Settings");
		resetBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if (wxMessageBox("Reset all settings to defaults?", "Confirm",
				wxYES_NO | wxICON_QUESTION, this) == wxYES) {
				ExecCmd("Options.ResetAllSettings");
			}
		});
		top->Add(resetBtn, 0, wxALL, 10);

		SetSizer(top);
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: UI
///////////////////////////////////////////////////////////////////////////

class UIPage : public ConfigPage {
public:
	UIPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		MakeCmdCheckbox(this, top, "Auto-hide menu bar", "View.ToggleAutoHideMenu", mCheckboxes);
		MakeCmdCheckbox(this, top, "Pause when menus open", "Options.PauseDuringMenu", mCheckboxes);
		MakeCmdCheckbox(this, top, "Use dark theme", "Options.UseDarkTheme", mCheckboxes);

		top->AddSpacer(8);
		SetSizer(top);
		BindAllEvents();
	}
};

///////////////////////////////////////////////////////////////////////////
// Page: Window Caption
///////////////////////////////////////////////////////////////////////////

class CaptionPage : public ConfigPage {
public:
	CaptionPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		top->Add(new wxStaticText(this, wxID_ANY,
			"Custom window caption template. Available variables:\n"
			"  basic, fps, frame, hardwareType, kernelName, memoryType,\n"
			"  profile, speed, videoStandard\n"
			"Use ~ prefix to include only when non-default."),
			0, wxALL, 8);

		mpTemplate = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 60),
			wxTE_MULTILINE);
		mpTemplate->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_MODERN)));
		top->Add(mpTemplate, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

		wxBoxSizer *btnRow = new wxBoxSizer(wxHORIZONTAL);
		wxButton *applyBtn = new wxButton(this, wxID_ANY, "Apply");
		wxButton *clearBtn = new wxButton(this, wxID_ANY, "Clear");
		btnRow->Add(applyBtn, 0, wxRIGHT, 4);
		btnRow->Add(clearBtn, 0);
		top->Add(btnRow, 0, wxALL, 8);

		SetSizer(top);

		applyBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			std::string tmpl = mpTemplate->GetValue().utf8_string();
			ATUISetWindowCaptionTemplate(tmpl.c_str());
		});
		clearBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			mpTemplate->Clear();
			ATUISetWindowCaptionTemplate("");
		});
	}

	void Refresh() override {
		ConfigPage::Refresh();
		const char *tmpl = ATUIGetWindowCaptionTemplate();
		mpTemplate->SetValue(wxString::FromUTF8(tmpl));
	}

private:
	wxTextCtrl *mpTemplate;
};

///////////////////////////////////////////////////////////////////////////
// Page: Workarounds
///////////////////////////////////////////////////////////////////////////

class WorkaroundsPage : public ConfigPage {
public:
	WorkaroundsPage(wxWindow *parent) : ConfigPage(parent) {
		wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

		top->Add(new wxStaticText(this, wxID_ANY,
			"No workarounds are currently needed on Linux."),
			0, wxALL, 10);

		SetSizer(top);
	}
};

///////////////////////////////////////////////////////////////////////////
// Main dialog: wxTreebook with all pages
///////////////////////////////////////////////////////////////////////////

class ATConfigureSystemDialog : public wxDialog {
public:
	ATConfigureSystemDialog(wxWindow *parent);

private:
	void OnPageChanged(wxBookCtrlEvent& evt);

	wxTreebook *mpBook;
	std::vector<ConfigPage *> mPages;
};

ATConfigureSystemDialog::ATConfigureSystemDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Configure System", wxDefaultPosition, wxSize(700, 550),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	mpBook = new wxTreebook(this, wxID_ANY);

	// Top-level pages
	auto *overviewPage = new OverviewPage(mpBook);
	mpBook->AddPage(overviewPage, "Overview");
	mPages.push_back(overviewPage);

	// Category: Computer
	auto *systemPage = new SystemPage(mpBook);
	mpBook->AddPage(systemPage, "Computer");
	mPages.push_back(systemPage);

	auto *cpuPage = new CPUPage(mpBook);
	mpBook->AddSubPage(cpuPage, "CPU");
	mPages.push_back(cpuPage);

	auto *fwPage = new FirmwarePage(mpBook);
	mpBook->AddSubPage(fwPage, "Firmware");
	mPages.push_back(fwPage);

	auto *memPage = new MemoryPage(mpBook);
	mpBook->AddSubPage(memPage, "Memory");
	mPages.push_back(memPage);

	auto *accelPage = new AccelerationPage(mpBook);
	mpBook->AddSubPage(accelPage, "Acceleration");
	mPages.push_back(accelPage);

	auto *speedPage = new SpeedPage(mpBook);
	mpBook->AddSubPage(speedPage, "Speed");
	mPages.push_back(speedPage);

	auto *bootPage = new BootPage(mpBook);
	mpBook->AddSubPage(bootPage, "Boot");
	mPages.push_back(bootPage);

	// Category: Outputs
	auto *videoPage = new VideoPage(mpBook);
	mpBook->AddPage(videoPage, "Outputs");
	mPages.push_back(videoPage);

	auto *enhTextPage = new EnhancedTextPage(mpBook);
	mpBook->AddSubPage(enhTextPage, "Enhanced Text");
	mPages.push_back(enhTextPage);

	auto *audioPage = new AudioPage(mpBook);
	mpBook->AddSubPage(audioPage, "Audio");
	mPages.push_back(audioPage);

	// Category: Peripherals
	auto *devicesPage = new DevicesPage(mpBook);
	mpBook->AddPage(devicesPage, "Peripherals");
	mPages.push_back(devicesPage);

	auto *kbdPage = new KeyboardPage(mpBook);
	mpBook->AddSubPage(kbdPage, "Keyboard");
	mPages.push_back(kbdPage);

	// Category: Media
	auto *defaultsPage = new DefaultsPage(mpBook);
	mpBook->AddPage(defaultsPage, "Media");
	mPages.push_back(defaultsPage);

	auto *diskPage = new DiskPage(mpBook);
	mpBook->AddSubPage(diskPage, "Disk");
	mPages.push_back(diskPage);

	auto *cassettePage = new CassettePage(mpBook);
	mpBook->AddSubPage(cassettePage, "Cassette");
	mPages.push_back(cassettePage);

	auto *flashPage = new FlashPage(mpBook);
	mpBook->AddSubPage(flashPage, "Flash");
	mPages.push_back(flashPage);

	auto *ftPage = new FileTypesPage(mpBook);
	mpBook->AddSubPage(ftPage, "File Types");
	mPages.push_back(ftPage);

	// Category: Emulator
	auto *eouPage = new EaseOfUsePage(mpBook);
	mpBook->AddPage(eouPage, "Emulator");
	mPages.push_back(eouPage);

	auto *accessPage = new AccessibilityPage(mpBook);
	mpBook->AddSubPage(accessPage, "Accessibility");
	mPages.push_back(accessPage);

	auto *compatPage = new CompatDBPage(mpBook);
	mpBook->AddSubPage(compatPage, "Compat DB");
	mPages.push_back(compatPage);

	auto *dbgPage = new DebuggerPage(mpBook);
	mpBook->AddSubPage(dbgPage, "Debugger");
	mPages.push_back(dbgPage);

	auto *disp1Page = new Display1Page(mpBook);
	mpBook->AddSubPage(disp1Page, "Display 1");
	mPages.push_back(disp1Page);

	auto *disp2Page = new Display2Page(mpBook);
	mpBook->AddSubPage(disp2Page, "Display 2");
	mPages.push_back(disp2Page);

	auto *errPage = new ErrorHandlingPage(mpBook);
	mpBook->AddSubPage(errPage, "Error Handling");
	mPages.push_back(errPage);

	auto *inputPage = new InputPage(mpBook);
	mpBook->AddSubPage(inputPage, "Input");
	mPages.push_back(inputPage);

	auto *settingsPage = new SettingsPage(mpBook);
	mpBook->AddSubPage(settingsPage, "Settings");
	mPages.push_back(settingsPage);

	auto *uiPage = new UIPage(mpBook);
	mpBook->AddSubPage(uiPage, "UI");
	mPages.push_back(uiPage);

	auto *capPage = new CaptionPage(mpBook);
	mpBook->AddSubPage(capPage, "Window Caption");
	mPages.push_back(capPage);

	auto *waPage = new WorkaroundsPage(mpBook);
	mpBook->AddSubPage(waPage, "Workarounds");
	mPages.push_back(waPage);

	topSizer->Add(mpBook, 1, wxEXPAND | wxALL, 5);

	// Close button
	topSizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 5);

	SetSizer(topSizer);

	// Refresh the initial page
	if (!mPages.empty())
		mPages[0]->Refresh();

	// Refresh pages when switching
	mpBook->Bind(wxEVT_TREEBOOK_PAGE_CHANGED, &ATConfigureSystemDialog::OnPageChanged, this);
}

void ATConfigureSystemDialog::OnPageChanged(wxBookCtrlEvent& evt) {
	int sel = evt.GetSelection();
	if (sel >= 0 && sel < (int)mPages.size())
		mPages[sel]->Refresh();
	evt.Skip();
}

} // anonymous namespace

///////////////////////////////////////////////////////////////////////////

void ATShowSystemConfigDialog(wxWindow *parent) {
	ATConfigureSystemDialog dlg(parent);
	dlg.ShowModal();
}
