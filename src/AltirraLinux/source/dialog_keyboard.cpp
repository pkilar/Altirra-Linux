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
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/statbox.h>

#include "uikeyboard.h"

extern ATUIKeyboardOptions g_kbdOpts;
void ATUIInitVirtualKeyMap(const ATUIKeyboardOptions& opts);

namespace {

class ATKeyboardSettingsDialog : public wxDialog {
public:
	ATKeyboardSettingsDialog(wxWindow *parent);

private:
	void PopulateFromState();
	void ApplyToState();
	void OnOK(wxCommandEvent& event);

	wxRadioBox *mpLayoutMode = nullptr;
	wxRadioBox *mpKeyboardMode = nullptr;
	wxRadioBox *mpArrowKeyMode = nullptr;
	wxCheckBox *mpFunctionKeys = nullptr;
	wxCheckBox *mpShiftOnColdReset = nullptr;
	wxCheckBox *mpInputMapOverlap = nullptr;
	wxCheckBox *mpInputMapModOverlap = nullptr;
};

ATKeyboardSettingsDialog::ATKeyboardSettingsDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Keyboard Settings", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// Layout Mode
	wxString layoutChoices[] = { "Natural", "Direct (Raw)", "Custom" };
	mpLayoutMode = new wxRadioBox(this, wxID_ANY, "Layout Mode",
		wxDefaultPosition, wxDefaultSize, 3, layoutChoices, 1, wxRA_SPECIFY_COLS);
	topSizer->Add(mpLayoutMode, 0, wxEXPAND | wxALL, 5);

	// Keyboard Mode
	wxString kbdChoices[] = { "Cooked", "Raw", "Full Raw" };
	mpKeyboardMode = new wxRadioBox(this, wxID_ANY, "Keyboard Mode",
		wxDefaultPosition, wxDefaultSize, 3, kbdChoices, 1, wxRA_SPECIFY_COLS);
	topSizer->Add(mpKeyboardMode, 0, wxEXPAND | wxALL, 5);

	// Arrow Key Mode
	wxString arrowChoices[] = { "Invert Ctrl", "Auto Ctrl", "Default Ctrl" };
	mpArrowKeyMode = new wxRadioBox(this, wxID_ANY, "Arrow Key Mode",
		wxDefaultPosition, wxDefaultSize, 3, arrowChoices, 1, wxRA_SPECIFY_COLS);
	topSizer->Add(mpArrowKeyMode, 0, wxEXPAND | wxALL, 5);

	// Options
	wxStaticBoxSizer *optBox = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
	mpFunctionKeys = new wxCheckBox(optBox->GetStaticBox(), wxID_ANY, "1200XL function keys (F1-F4)");
	mpShiftOnColdReset = new wxCheckBox(optBox->GetStaticBox(), wxID_ANY, "Allow Shift on cold reset");
	mpInputMapOverlap = new wxCheckBox(optBox->GetStaticBox(), wxID_ANY, "Allow input map keyboard overlap");
	mpInputMapModOverlap = new wxCheckBox(optBox->GetStaticBox(), wxID_ANY, "Allow input map modifier overlap");
	optBox->Add(mpFunctionKeys, 0, wxALL, 3);
	optBox->Add(mpShiftOnColdReset, 0, wxALL, 3);
	optBox->Add(mpInputMapOverlap, 0, wxALL, 3);
	optBox->Add(mpInputMapModOverlap, 0, wxALL, 3);
	topSizer->Add(optBox, 0, wxEXPAND | wxALL, 5);

	// OK/Cancel
	topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 5);

	SetSizerAndFit(topSizer);

	PopulateFromState();

	Bind(wxEVT_BUTTON, &ATKeyboardSettingsDialog::OnOK, this, wxID_OK);
}

void ATKeyboardSettingsDialog::PopulateFromState() {
	mpLayoutMode->SetSelection((int)g_kbdOpts.mLayoutMode);

	if (g_kbdOpts.mbFullRawKeys)
		mpKeyboardMode->SetSelection(2);
	else if (g_kbdOpts.mbRawKeys)
		mpKeyboardMode->SetSelection(1);
	else
		mpKeyboardMode->SetSelection(0);

	mpArrowKeyMode->SetSelection((int)g_kbdOpts.mArrowKeyMode);
	mpFunctionKeys->SetValue(g_kbdOpts.mbEnableFunctionKeys);
	mpShiftOnColdReset->SetValue(g_kbdOpts.mbAllowShiftOnColdReset);
	mpInputMapOverlap->SetValue(g_kbdOpts.mbAllowInputMapOverlap);
	mpInputMapModOverlap->SetValue(g_kbdOpts.mbAllowInputMapModifierOverlap);
}

void ATKeyboardSettingsDialog::ApplyToState() {
	g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)mpLayoutMode->GetSelection();

	int kbdSel = mpKeyboardMode->GetSelection();
	g_kbdOpts.mbRawKeys = (kbdSel >= 1);
	g_kbdOpts.mbFullRawKeys = (kbdSel == 2);

	g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)mpArrowKeyMode->GetSelection();
	g_kbdOpts.mbEnableFunctionKeys = mpFunctionKeys->GetValue();
	g_kbdOpts.mbAllowShiftOnColdReset = mpShiftOnColdReset->GetValue();
	g_kbdOpts.mbAllowInputMapOverlap = mpInputMapOverlap->GetValue();
	g_kbdOpts.mbAllowInputMapModifierOverlap = mpInputMapModOverlap->GetValue();

	ATUIInitVirtualKeyMap(g_kbdOpts);
}

void ATKeyboardSettingsDialog::OnOK(wxCommandEvent&) {
	ApplyToState();
	EndModal(wxID_OK);
}

} // anonymous namespace

void ATShowKeyboardSettingsDialog(wxWindow *parent) {
	ATKeyboardSettingsDialog dlg(parent);
	dlg.ShowModal();
}
