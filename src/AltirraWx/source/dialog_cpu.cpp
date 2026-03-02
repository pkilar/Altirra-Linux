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

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include "simulator.h"
#include "uiaccessors.h"
#include "cpu.h"

extern ATSimulator g_sim;

namespace {

static const char *kCPUModeNames[] = {
	"6502",
	"65C02",
	"65C816",
};

class ATCPUOptionsDialog : public wxDialog {
public:
	ATCPUOptionsDialog(wxWindow *parent);

private:
	void PopulateFromState();
	void ApplyToState();
	void OnOK(wxCommandEvent& event);

	// CPU
	wxChoice *mpCPUType = nullptr;
	wxCheckBox *mpProfiling = nullptr;
	wxCheckBox *mpVerifier = nullptr;
	wxCheckBox *mpHeatMap = nullptr;
	wxCheckBox *mpHistory = nullptr;
	wxCheckBox *mpPathfinding = nullptr;
	wxCheckBox *mpIllegalInsns = nullptr;
	wxCheckBox *mpStopOnBRK = nullptr;
	wxCheckBox *mpNMIBlocking = nullptr;
	wxCheckBox *mpShadowROMs = nullptr;
	wxCheckBox *mpShadowCarts = nullptr;

	// Memory
	wxChoice *mpAxlonRAM = nullptr;
	wxCheckBox *mpAxlonAliasing = nullptr;
	wxCheckBox *mpMapRAM = nullptr;
	wxCheckBox *mpUltimate1MB = nullptr;
	wxCheckBox *mpFloatingIOBus = nullptr;
	wxCheckBox *mpPreserveExtRAM = nullptr;
};

ATCPUOptionsDialog::ATCPUOptionsDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "CPU & Memory", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// CPU section
	wxStaticBoxSizer *cpuBox = new wxStaticBoxSizer(wxVERTICAL, this, "CPU");
	wxWindow *cp = cpuBox->GetStaticBox();

	wxBoxSizer *cpuRow = new wxBoxSizer(wxHORIZONTAL);
	cpuRow->Add(new wxStaticText(cp, wxID_ANY, "CPU Type:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	mpCPUType = new wxChoice(cp, wxID_ANY);
	for (auto name : kCPUModeNames)
		mpCPUType->Append(name);
	cpuRow->Add(mpCPUType, 1, wxEXPAND);
	cpuBox->Add(cpuRow, 0, wxEXPAND | wxALL, 3);

	mpProfiling = new wxCheckBox(cp, wxID_ANY, "CPU profiling");
	mpVerifier = new wxCheckBox(cp, wxID_ANY, "CPU verifier");
	mpHeatMap = new wxCheckBox(cp, wxID_ANY, "Heat map");
	mpHistory = new wxCheckBox(cp, wxID_ANY, "Record instruction history");
	mpPathfinding = new wxCheckBox(cp, wxID_ANY, "Track code paths");
	mpIllegalInsns = new wxCheckBox(cp, wxID_ANY, "Enable illegal instructions");
	mpStopOnBRK = new wxCheckBox(cp, wxID_ANY, "Stop on BRK instruction");
	mpNMIBlocking = new wxCheckBox(cp, wxID_ANY, "Allow BRK/IRQ to block NMI");
	mpShadowROMs = new wxCheckBox(cp, wxID_ANY, "Shadow ROMs in fast RAM");
	mpShadowCarts = new wxCheckBox(cp, wxID_ANY, "Shadow cartridges in fast RAM");

	cpuBox->Add(mpProfiling, 0, wxALL, 3);
	cpuBox->Add(mpVerifier, 0, wxALL, 3);
	cpuBox->Add(mpHeatMap, 0, wxALL, 3);
	cpuBox->Add(mpHistory, 0, wxALL, 3);
	cpuBox->Add(mpPathfinding, 0, wxALL, 3);
	cpuBox->Add(mpIllegalInsns, 0, wxALL, 3);
	cpuBox->Add(mpStopOnBRK, 0, wxALL, 3);
	cpuBox->Add(mpNMIBlocking, 0, wxALL, 3);
	cpuBox->Add(mpShadowROMs, 0, wxALL, 3);
	cpuBox->Add(mpShadowCarts, 0, wxALL, 3);
	topSizer->Add(cpuBox, 0, wxEXPAND | wxALL, 5);

	// Memory section
	wxStaticBoxSizer *memBox = new wxStaticBoxSizer(wxVERTICAL, this, "Memory");
	wxWindow *mp = memBox->GetStaticBox();

	wxBoxSizer *axlonRow = new wxBoxSizer(wxHORIZONTAL);
	axlonRow->Add(new wxStaticText(mp, wxID_ANY, "Axlon RAM:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	mpAxlonRAM = new wxChoice(mp, wxID_ANY);
	mpAxlonRAM->Append("Disabled");
	mpAxlonRAM->Append("64K");
	mpAxlonRAM->Append("128K");
	mpAxlonRAM->Append("256K");
	mpAxlonRAM->Append("512K");
	mpAxlonRAM->Append("1024K");
	mpAxlonRAM->Append("2048K");
	mpAxlonRAM->Append("4096K");
	axlonRow->Add(mpAxlonRAM, 1, wxEXPAND);
	memBox->Add(axlonRow, 0, wxEXPAND | wxALL, 3);

	mpAxlonAliasing = new wxCheckBox(mp, wxID_ANY, "Axlon aliasing");
	mpMapRAM = new wxCheckBox(mp, wxID_ANY, "MapRAM");
	mpUltimate1MB = new wxCheckBox(mp, wxID_ANY, "Ultimate1MB");
	mpFloatingIOBus = new wxCheckBox(mp, wxID_ANY, "Floating I/O bus");
	mpPreserveExtRAM = new wxCheckBox(mp, wxID_ANY, "Preserve extended RAM on reset");

	memBox->Add(mpAxlonAliasing, 0, wxALL, 3);
	memBox->Add(mpMapRAM, 0, wxALL, 3);
	memBox->Add(mpUltimate1MB, 0, wxALL, 3);
	memBox->Add(mpFloatingIOBus, 0, wxALL, 3);
	memBox->Add(mpPreserveExtRAM, 0, wxALL, 3);
	topSizer->Add(memBox, 0, wxEXPAND | wxALL, 5);

	// OK/Cancel
	topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 5);

	SetSizerAndFit(topSizer);

	PopulateFromState();
	Bind(wxEVT_BUTTON, &ATCPUOptionsDialog::OnOK, this, wxID_OK);
}

void ATCPUOptionsDialog::PopulateFromState() {
	ATCPUMode cpuMode = g_sim.GetCPUMode();
	int cpuIdx = 0;
	switch (cpuMode) {
		case kATCPUMode_6502:  cpuIdx = 0; break;
		case kATCPUMode_65C02: cpuIdx = 1; break;
		case kATCPUMode_65C816: cpuIdx = 2; break;
		default: cpuIdx = 0; break;
	}
	mpCPUType->SetSelection(cpuIdx);

	mpProfiling->SetValue(g_sim.IsProfilingEnabled());
	mpVerifier->SetValue(g_sim.IsVerifierEnabled());
	mpHeatMap->SetValue(g_sim.IsHeatMapEnabled());

	ATCPUEmulator& cpu = g_sim.GetCPU();
	mpHistory->SetValue(cpu.IsHistoryEnabled());
	mpPathfinding->SetValue(cpu.IsPathfindingEnabled());
	mpIllegalInsns->SetValue(cpu.AreIllegalInsnsEnabled());
	mpStopOnBRK->SetValue(cpu.GetStopOnBRK());
	mpNMIBlocking->SetValue(cpu.IsNMIBlockingEnabled());

	mpShadowROMs->SetValue(g_sim.GetShadowROMEnabled());
	mpShadowCarts->SetValue(g_sim.GetShadowCartridgeEnabled());

	// Axlon memory mode
	int axlonMode = g_sim.GetAxlonMemoryMode();
	int axlonIdx = 0;
	if (axlonMode == 0) axlonIdx = 0;
	else {
		// Axlon modes: 0=disabled, 3=64K, 4=128K, 5=256K, ...
		axlonIdx = axlonMode - 2;
		if (axlonIdx < 1) axlonIdx = 0;
		if (axlonIdx > 7) axlonIdx = 7;
	}
	mpAxlonRAM->SetSelection(axlonIdx);

	mpAxlonAliasing->SetValue(g_sim.GetAxlonAliasingEnabled());
	mpMapRAM->SetValue(g_sim.IsMapRAMEnabled());
	mpUltimate1MB->SetValue(g_sim.IsUltimate1MBEnabled());
	mpFloatingIOBus->SetValue(g_sim.IsFloatingIoBusEnabled());
	mpPreserveExtRAM->SetValue(g_sim.IsPreserveExtRAMEnabled());
}

void ATCPUOptionsDialog::ApplyToState() {
	static const ATCPUMode kCPUModes[] = {
		kATCPUMode_6502, kATCPUMode_65C02, kATCPUMode_65C816
	};
	int cpuIdx = mpCPUType->GetSelection();
	if (cpuIdx >= 0 && cpuIdx < 3)
		g_sim.SetCPUMode(kCPUModes[cpuIdx], g_sim.GetCPUSubCycles());

	g_sim.SetProfilingEnabled(mpProfiling->GetValue());
	g_sim.SetVerifierEnabled(mpVerifier->GetValue());
	g_sim.SetHeatMapEnabled(mpHeatMap->GetValue());

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetHistoryEnabled(mpHistory->GetValue());
	cpu.SetPathfindingEnabled(mpPathfinding->GetValue());
	cpu.SetIllegalInsnsEnabled(mpIllegalInsns->GetValue());
	cpu.SetStopOnBRK(mpStopOnBRK->GetValue());
	cpu.SetNMIBlockingEnabled(mpNMIBlocking->GetValue());

	g_sim.SetShadowROMEnabled(mpShadowROMs->GetValue());
	g_sim.SetShadowCartridgeEnabled(mpShadowCarts->GetValue());

	// Axlon memory mode: 0=disabled, 3=64K, 4=128K, ...
	int axlonIdx = mpAxlonRAM->GetSelection();
	int axlonMode = (axlonIdx == 0) ? 0 : (axlonIdx + 2);
	g_sim.SetAxlonMemoryMode(axlonMode);

	g_sim.SetAxlonAliasingEnabled(mpAxlonAliasing->GetValue());
	g_sim.SetMapRAMEnabled(mpMapRAM->GetValue());
	g_sim.SetUltimate1MBEnabled(mpUltimate1MB->GetValue());
	g_sim.SetFloatingIoBusEnabled(mpFloatingIOBus->GetValue());
	g_sim.SetPreserveExtRAMEnabled(mpPreserveExtRAM->GetValue());
}

void ATCPUOptionsDialog::OnOK(wxCommandEvent&) {
	ApplyToState();
	EndModal(wxID_OK);
}

} // anonymous namespace

void ATShowCPUOptionsDialog(wxWindow *parent) {
	ATCPUOptionsDialog dlg(parent);
	dlg.ShowModal();
}
