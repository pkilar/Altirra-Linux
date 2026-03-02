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

extern ATSimulator g_sim;

namespace {

class ATBootOptionsDialog : public wxDialog {
public:
	ATBootOptionsDialog(wxWindow *parent);

private:
	void PopulateFromState();
	void ApplyToState();
	void OnOK(wxCommandEvent& event);

	// Boot Options
	wxCheckBox *mpFastBoot = nullptr;
	wxCheckBox *mpForceSelfTest = nullptr;
	wxCheckBox *mpKeyboardPresent = nullptr;
	wxCheckBox *mpRandomFillEXE = nullptr;
	wxCheckBox *mpRandomLaunchDelay = nullptr;
	wxChoice *mpLoadMode = nullptr;

	// SIO Acceleration
	wxCheckBox *mpSIOPatch = nullptr;
	wxCheckBox *mpDiskSIOPatch = nullptr;
	wxCheckBox *mpDiskBurst = nullptr;
	wxCheckBox *mpAccurateTiming = nullptr;
	wxCheckBox *mpSIOOverrideDetect = nullptr;

	// Acceleration Patches
	wxCheckBox *mpFPPatch = nullptr;
	wxCheckBox *mpCIOPBIPatch = nullptr;
	wxCheckBox *mpSIOPBIPatch = nullptr;
	wxCheckBox *mpDeviceSIOPatch = nullptr;
	wxCheckBox *mpDeviceCIOBurst = nullptr;
	wxCheckBox *mpDeviceSIOBurst = nullptr;
	wxCheckBox *mpDiskSectorCounter = nullptr;
};

ATBootOptionsDialog::ATBootOptionsDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Boot & Acceleration", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// Boot Options section
	wxStaticBoxSizer *bootBox = new wxStaticBoxSizer(wxVERTICAL, this, "Boot Options");
	wxWindow *bp = bootBox->GetStaticBox();
	mpFastBoot = new wxCheckBox(bp, wxID_ANY, "Fast boot (skip memory test)");
	mpForceSelfTest = new wxCheckBox(bp, wxID_ANY, "Force self-test on cold reset");
	mpKeyboardPresent = new wxCheckBox(bp, wxID_ANY, "Keyboard present");
	mpRandomFillEXE = new wxCheckBox(bp, wxID_ANY, "Randomize memory on EXE load");
	mpRandomLaunchDelay = new wxCheckBox(bp, wxID_ANY, "Random program launch delay");
	bootBox->Add(mpFastBoot, 0, wxALL, 3);
	bootBox->Add(mpForceSelfTest, 0, wxALL, 3);
	bootBox->Add(mpKeyboardPresent, 0, wxALL, 3);
	bootBox->Add(mpRandomFillEXE, 0, wxALL, 3);
	bootBox->Add(mpRandomLaunchDelay, 0, wxALL, 3);

	wxBoxSizer *loadRow = new wxBoxSizer(wxHORIZONTAL);
	loadRow->Add(new wxStaticText(bp, wxID_ANY, "Load Mode:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	mpLoadMode = new wxChoice(bp, wxID_ANY);
	mpLoadMode->Append("Default");
	mpLoadMode->Append("Type 3 Poll");
	mpLoadMode->Append("Deferred");
	mpLoadMode->Append("Disk Boot");
	loadRow->Add(mpLoadMode, 1, wxEXPAND);
	bootBox->Add(loadRow, 0, wxEXPAND | wxALL, 3);
	topSizer->Add(bootBox, 0, wxEXPAND | wxALL, 5);

	// SIO Acceleration section
	wxStaticBoxSizer *sioBox = new wxStaticBoxSizer(wxVERTICAL, this, "SIO Acceleration");
	wxWindow *sp = sioBox->GetStaticBox();
	mpSIOPatch = new wxCheckBox(sp, wxID_ANY, "SIO patch (OS acceleration)");
	mpDiskSIOPatch = new wxCheckBox(sp, wxID_ANY, "Disk SIO patch");
	mpDiskBurst = new wxCheckBox(sp, wxID_ANY, "Disk burst transfers");
	mpAccurateTiming = new wxCheckBox(sp, wxID_ANY, "Accurate disk timing");
	mpSIOOverrideDetect = new wxCheckBox(sp, wxID_ANY, "SIO override detection");
	sioBox->Add(mpSIOPatch, 0, wxALL, 3);
	sioBox->Add(mpDiskSIOPatch, 0, wxALL, 3);
	sioBox->Add(mpDiskBurst, 0, wxALL, 3);
	sioBox->Add(mpAccurateTiming, 0, wxALL, 3);
	sioBox->Add(mpSIOOverrideDetect, 0, wxALL, 3);
	topSizer->Add(sioBox, 0, wxEXPAND | wxALL, 5);

	// Acceleration Patches section
	wxStaticBoxSizer *patchBox = new wxStaticBoxSizer(wxVERTICAL, this, "Acceleration Patches");
	wxWindow *pp = patchBox->GetStaticBox();
	mpFPPatch = new wxCheckBox(pp, wxID_ANY, "Floating-point acceleration");
	mpCIOPBIPatch = new wxCheckBox(pp, wxID_ANY, "CIO PBI patch");
	mpSIOPBIPatch = new wxCheckBox(pp, wxID_ANY, "SIO PBI patch");
	mpDeviceSIOPatch = new wxCheckBox(pp, wxID_ANY, "Device SIO patch");
	mpDeviceCIOBurst = new wxCheckBox(pp, wxID_ANY, "Device CIO burst transfers");
	mpDeviceSIOBurst = new wxCheckBox(pp, wxID_ANY, "Device SIO burst transfers");
	mpDiskSectorCounter = new wxCheckBox(pp, wxID_ANY, "Disk sector counter");
	patchBox->Add(mpFPPatch, 0, wxALL, 3);
	patchBox->Add(mpCIOPBIPatch, 0, wxALL, 3);
	patchBox->Add(mpSIOPBIPatch, 0, wxALL, 3);
	patchBox->Add(mpDeviceSIOPatch, 0, wxALL, 3);
	patchBox->Add(mpDeviceCIOBurst, 0, wxALL, 3);
	patchBox->Add(mpDeviceSIOBurst, 0, wxALL, 3);
	patchBox->Add(mpDiskSectorCounter, 0, wxALL, 3);
	topSizer->Add(patchBox, 0, wxEXPAND | wxALL, 5);

	// OK/Cancel
	topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 5);

	SetSizerAndFit(topSizer);

	PopulateFromState();
	Bind(wxEVT_BUTTON, &ATBootOptionsDialog::OnOK, this, wxID_OK);
}

void ATBootOptionsDialog::PopulateFromState() {
	mpFastBoot->SetValue(g_sim.IsFastBootEnabled());
	mpForceSelfTest->SetValue(g_sim.IsForcedSelfTest());
	mpKeyboardPresent->SetValue(g_sim.IsKeyboardPresent());
	mpRandomFillEXE->SetValue(g_sim.IsRandomFillEXEEnabled());
	mpRandomLaunchDelay->SetValue(g_sim.IsRandomProgramLaunchDelayEnabled());
	mpLoadMode->SetSelection((int)g_sim.GetHLEProgramLoadMode());

	mpSIOPatch->SetValue(g_sim.IsSIOPatchEnabled());
	mpDiskSIOPatch->SetValue(g_sim.IsDiskSIOPatchEnabled());
	mpDiskBurst->SetValue(g_sim.GetDiskBurstTransfersEnabled());
	mpAccurateTiming->SetValue(g_sim.IsDiskAccurateTimingEnabled());
	mpSIOOverrideDetect->SetValue(g_sim.IsDiskSIOOverrideDetectEnabled());

	mpFPPatch->SetValue(g_sim.IsFPPatchEnabled());
	mpCIOPBIPatch->SetValue(g_sim.IsCIOPBIPatchEnabled());
	mpSIOPBIPatch->SetValue(g_sim.IsSIOPBIPatchEnabled());
	mpDeviceSIOPatch->SetValue(g_sim.GetDeviceSIOPatchEnabled());
	mpDeviceCIOBurst->SetValue(g_sim.GetDeviceCIOBurstTransfersEnabled());
	mpDeviceSIOBurst->SetValue(g_sim.GetDeviceSIOBurstTransfersEnabled());
	mpDiskSectorCounter->SetValue(g_sim.IsDiskSectorCounterEnabled());
}

void ATBootOptionsDialog::ApplyToState() {
	g_sim.SetFastBootEnabled(mpFastBoot->GetValue());
	g_sim.SetForcedSelfTest(mpForceSelfTest->GetValue());
	g_sim.SetKeyboardPresent(mpKeyboardPresent->GetValue());
	g_sim.SetRandomFillEXEEnabled(mpRandomFillEXE->GetValue());
	g_sim.SetRandomProgramLaunchDelayEnabled(mpRandomLaunchDelay->GetValue());
	g_sim.SetHLEProgramLoadMode((ATHLEProgramLoadMode)mpLoadMode->GetSelection());

	g_sim.SetSIOPatchEnabled(mpSIOPatch->GetValue());
	g_sim.SetDiskSIOPatchEnabled(mpDiskSIOPatch->GetValue());
	g_sim.SetDiskBurstTransfersEnabled(mpDiskBurst->GetValue());
	g_sim.SetDiskAccurateTimingEnabled(mpAccurateTiming->GetValue());
	g_sim.SetDiskSIOOverrideDetectEnabled(mpSIOOverrideDetect->GetValue());

	g_sim.SetFPPatchEnabled(mpFPPatch->GetValue());
	g_sim.SetCIOPBIPatchEnabled(mpCIOPBIPatch->GetValue());
	g_sim.SetSIOPBIPatchEnabled(mpSIOPBIPatch->GetValue());
	g_sim.SetDeviceSIOPatchEnabled(mpDeviceSIOPatch->GetValue());
	g_sim.SetDeviceCIOBurstTransfersEnabled(mpDeviceCIOBurst->GetValue());
	g_sim.SetDeviceSIOBurstTransfersEnabled(mpDeviceSIOBurst->GetValue());
	g_sim.SetDiskSectorCounterEnabled(mpDiskSectorCounter->GetValue());
}

void ATBootOptionsDialog::OnOK(wxCommandEvent&) {
	ApplyToState();
	EndModal(wxID_OK);
}

} // anonymous namespace

void ATShowBootOptionsDialog(wxWindow *parent) {
	ATBootOptionsDialog dlg(parent);
	dlg.ShowModal();
}
