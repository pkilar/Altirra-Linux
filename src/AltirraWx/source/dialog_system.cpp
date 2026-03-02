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

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include "simulator.h"
#include "uiaccessors.h"
#include "options.h"
#include <at/atcore/media.h>

extern ATSimulator g_sim;
extern ATOptions g_ATOptions;

namespace {

// Memory mode display order (sorted by size for UI)
struct MemoryModeEntry {
	ATMemoryMode mode;
	const char *label;
};

static const MemoryModeEntry kMemoryModes[] = {
	{ kATMemoryMode_8K,          "8K" },
	{ kATMemoryMode_16K,         "16K" },
	{ kATMemoryMode_24K,         "24K" },
	{ kATMemoryMode_32K,         "32K" },
	{ kATMemoryMode_40K,         "40K" },
	{ kATMemoryMode_48K,         "48K" },
	{ kATMemoryMode_52K,         "52K" },
	{ kATMemoryMode_64K,         "64K" },
	{ kATMemoryMode_128K,        "128K" },
	{ kATMemoryMode_256K,        "256K" },
	{ kATMemoryMode_320K,        "320K (Rambo)" },
	{ kATMemoryMode_320K_Compy,  "320K (Compy)" },
	{ kATMemoryMode_576K,        "576K (Rambo)" },
	{ kATMemoryMode_576K_Compy,  "576K (Compy)" },
	{ kATMemoryMode_1088K,       "1088K" },
};
static constexpr int kNumMemoryModes = sizeof(kMemoryModes) / sizeof(kMemoryModes[0]);

static const char *kHardwareModeNames[] = {
	"Atari 800",
	"Atari 800XL",
	"Atari 5200",
	"Atari XEGS",
	"Atari 1200XL",
	"Atari 130XE",
	"Atari 1400XL",
};

static const char *kVideoStandardNames[] = {
	"NTSC",
	"PAL",
	"SECAM",
	"PAL-60",
	"NTSC-50",
};

static const char *kMemoryClearModeNames[] = {
	"Zero",
	"Random",
	"DRAM Pattern 1",
	"DRAM Pattern 2",
	"DRAM Pattern 3",
};

static const char *kWriteModeNames[] = {
	"Read Only",
	"VRW Safe",
	"VRW",
	"Read/Write",
};

class ATSystemConfigDialog : public wxDialog {
public:
	ATSystemConfigDialog(wxWindow *parent);

private:
	void PopulateFromState();
	void OnApply(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);

	wxChoice *mpHardware = nullptr;
	wxChoice *mpMemory = nullptr;
	wxChoice *mpVideoStd = nullptr;
	wxCheckBox *mpBASIC = nullptr;
	wxChoice *mpClearMode = nullptr;
	wxChoice *mpWriteMode = nullptr;
};

ATSystemConfigDialog::ATSystemConfigDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "System Settings", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
	grid->AddGrowableCol(1, 1);

	// Hardware
	grid->Add(new wxStaticText(this, wxID_ANY, "Hardware:"), 0, wxALIGN_CENTER_VERTICAL);
	mpHardware = new wxChoice(this, wxID_ANY);
	for (int i = 0; i < (int)kATHardwareModeCount; ++i)
		mpHardware->Append(kHardwareModeNames[i]);
	grid->Add(mpHardware, 1, wxEXPAND);

	// Memory
	grid->Add(new wxStaticText(this, wxID_ANY, "Memory:"), 0, wxALIGN_CENTER_VERTICAL);
	mpMemory = new wxChoice(this, wxID_ANY);
	for (int i = 0; i < kNumMemoryModes; ++i)
		mpMemory->Append(kMemoryModes[i].label);
	grid->Add(mpMemory, 1, wxEXPAND);

	// Video Standard
	grid->Add(new wxStaticText(this, wxID_ANY, "Video:"), 0, wxALIGN_CENTER_VERTICAL);
	mpVideoStd = new wxChoice(this, wxID_ANY);
	for (int i = 0; i < (int)kATVideoStandardCount; ++i)
		mpVideoStd->Append(kVideoStandardNames[i]);
	grid->Add(mpVideoStd, 1, wxEXPAND);

	// BASIC
	grid->Add(new wxStaticText(this, wxID_ANY, "BASIC:"), 0, wxALIGN_CENTER_VERTICAL);
	mpBASIC = new wxCheckBox(this, wxID_ANY, "Enabled");
	grid->Add(mpBASIC, 0);

	// Memory Clear Mode
	grid->Add(new wxStaticText(this, wxID_ANY, "Clear Mode:"), 0, wxALIGN_CENTER_VERTICAL);
	mpClearMode = new wxChoice(this, wxID_ANY);
	for (int i = 0; i < (int)kATMemoryClearModeCount; ++i)
		mpClearMode->Append(kMemoryClearModeNames[i]);
	grid->Add(mpClearMode, 1, wxEXPAND);

	// Default Write Mode
	grid->Add(new wxStaticText(this, wxID_ANY, "Write Mode:"), 0, wxALIGN_CENTER_VERTICAL);
	mpWriteMode = new wxChoice(this, wxID_ANY);
	for (int i = 0; i < 4; ++i)
		mpWriteMode->Append(kWriteModeNames[i]);
	grid->Add(mpWriteMode, 1, wxEXPAND);

	topSizer->Add(grid, 0, wxEXPAND | wxALL, 10);

	// Buttons: Apply & Cold Reset + OK + Cancel
	wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton *applyBtn = new wxButton(this, wxID_APPLY, "Apply && Cold Reset");
	btnSizer->Add(applyBtn, 0, wxRIGHT, 5);
	btnSizer->AddStretchSpacer();
	btnSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0);
	topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 5);

	SetSizerAndFit(topSizer);

	PopulateFromState();
	Bind(wxEVT_BUTTON, &ATSystemConfigDialog::OnApply, this, wxID_APPLY);
	Bind(wxEVT_BUTTON, &ATSystemConfigDialog::OnOK, this, wxID_OK);
}

void ATSystemConfigDialog::PopulateFromState() {
	mpHardware->SetSelection((int)g_sim.GetHardwareMode());

	// Find current memory mode in sorted list
	ATMemoryMode curMem = g_sim.GetMemoryMode();
	for (int i = 0; i < kNumMemoryModes; ++i) {
		if (kMemoryModes[i].mode == curMem) {
			mpMemory->SetSelection(i);
			break;
		}
	}

	mpVideoStd->SetSelection((int)g_sim.GetVideoStandard());
	mpBASIC->SetValue(g_sim.IsBASICEnabled());
	mpClearMode->SetSelection((int)g_sim.GetMemoryClearMode());

	// Map write mode to index
	int wmIdx = 0;
	switch (g_ATOptions.mDefaultWriteMode) {
		case kATMediaWriteMode_RO: wmIdx = 0; break;
		case kATMediaWriteMode_VRWSafe: wmIdx = 1; break;
		case kATMediaWriteMode_VRW: wmIdx = 2; break;
		case kATMediaWriteMode_RW: wmIdx = 3; break;
		default: wmIdx = 0; break;
	}
	mpWriteMode->SetSelection(wmIdx);
}

void ATSystemConfigDialog::OnApply(wxCommandEvent&) {
	ATHardwareMode hw = (ATHardwareMode)mpHardware->GetSelection();
	ATUISwitchHardwareMode(nullptr, hw, true);

	int memIdx = mpMemory->GetSelection();
	if (memIdx >= 0 && memIdx < kNumMemoryModes)
		ATUISwitchMemoryMode(nullptr, kMemoryModes[memIdx].mode);

	g_sim.SetVideoStandard((ATVideoStandard)mpVideoStd->GetSelection());
	g_sim.SetBASICEnabled(mpBASIC->GetValue());
	g_sim.SetMemoryClearMode((ATMemoryClearMode)mpClearMode->GetSelection());

	static const ATMediaWriteMode kWriteModes[] = {
		kATMediaWriteMode_RO,
		kATMediaWriteMode_VRWSafe,
		kATMediaWriteMode_VRW,
		kATMediaWriteMode_RW,
	};
	int wmIdx = mpWriteMode->GetSelection();
	if (wmIdx >= 0 && wmIdx < 4)
		g_ATOptions.mDefaultWriteMode = kWriteModes[wmIdx];

	g_sim.LoadROMs();
	g_sim.ColdReset();

	EndModal(wxID_OK);
}

void ATSystemConfigDialog::OnOK(wxCommandEvent& event) {
	// OK without cold reset — just apply settings that don't require restart
	g_sim.SetVideoStandard((ATVideoStandard)mpVideoStd->GetSelection());
	g_sim.SetBASICEnabled(mpBASIC->GetValue());
	g_sim.SetMemoryClearMode((ATMemoryClearMode)mpClearMode->GetSelection());

	static const ATMediaWriteMode kWriteModes[] = {
		kATMediaWriteMode_RO,
		kATMediaWriteMode_VRWSafe,
		kATMediaWriteMode_VRW,
		kATMediaWriteMode_RW,
	};
	int wmIdx = mpWriteMode->GetSelection();
	if (wmIdx >= 0 && wmIdx < 4)
		g_ATOptions.mDefaultWriteMode = kWriteModes[wmIdx];

	EndModal(wxID_OK);
}

} // anonymous namespace

void ATShowSystemConfigDialog(wxWindow *parent) {
	ATSystemConfigDialog dlg(parent);
	dlg.ShowModal();
}
