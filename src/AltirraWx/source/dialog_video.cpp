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
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include "simulator.h"
#include "gtia.h"
#include "uiaccessors.h"
#include "uitypes.h"

extern ATSimulator g_sim;

namespace {

static const char *kFilterNames[] = {
	"Point",
	"Bilinear",
	"Bicubic",
	"Any Suitable",
	"Sharp Bilinear",
};

static const char *kStretchNames[] = {
	"Unconstrained",
	"Preserve Aspect Ratio",
	"Square Pixels",
	"Integral",
	"Integral + Aspect Ratio",
};

static const char *kOverscanNames[] = {
	"Normal (168cc)",
	"Extended (192cc)",
	"Full (228cc)",
	"OS Screen (160cc)",
	"Widescreen (176cc)",
};

static const char *kVertOverscanNames[] = {
	"Default",
	"OS Screen (192 lines)",
	"Normal (224 lines)",
	"Extended (240 lines)",
	"Full",
};

static const char *kArtifactNames[] = {
	"None",
	"NTSC",
	"PAL",
	"NTSC Hi",
	"PAL Hi",
	"Auto",
	"Auto Hi",
};

static const char *kMonitorNames[] = {
	"Color",
	"Peritel",
	"Mono Green",
	"Mono Amber",
	"Mono Bluish White",
	"Mono White",
};

static const char *kColorMatchNames[] = {
	"None",
	"sRGB",
	"Adobe RGB",
	"Gamma 2.2",
	"Gamma 2.4",
};

static const char *kLumaRampNames[] = {
	"Linear",
	"XL",
};

class ATVideoSettingsDialog : public wxDialog {
public:
	ATVideoSettingsDialog(wxWindow *parent);

private:
	void PopulateFromState();
	void ApplyToState();
	void OnOK(wxCommandEvent& event);
	void OnResetColors(wxCommandEvent& event);

	// Display tab
	wxChoice *mpFilter = nullptr;
	wxChoice *mpStretch = nullptr;
	wxChoice *mpHOverscan = nullptr;
	wxChoice *mpVOverscan = nullptr;
	wxCheckBox *mpPALExtended = nullptr;

	// Artifacting tab
	wxChoice *mpArtifactMode = nullptr;
	wxChoice *mpMonitorMode = nullptr;
	wxCheckBox *mpScanlines = nullptr;
	wxCheckBox *mpInterlace = nullptr;

	// Frame blending tab
	wxCheckBox *mpBlend = nullptr;
	wxCheckBox *mpMonoPersist = nullptr;
	wxCheckBox *mpLinearBlend = nullptr;

	// Screen effects tab
	wxCheckBox *mpBloom = nullptr;
	wxSlider *mpBloomRadius = nullptr;
	wxSlider *mpBloomDirect = nullptr;
	wxSlider *mpBloomIndirect = nullptr;
	wxSlider *mpDistortionAngle = nullptr;
	wxSlider *mpDistortionY = nullptr;

	// Color tab
	wxSlider *mpHueStart = nullptr;
	wxSlider *mpHueRange = nullptr;
	wxSlider *mpBrightness = nullptr;
	wxSlider *mpContrast = nullptr;
	wxSlider *mpSaturation = nullptr;
	wxSlider *mpGamma = nullptr;
	wxSlider *mpIntensity = nullptr;
	wxChoice *mpColorMatch = nullptr;
	wxChoice *mpLumaRamp = nullptr;
	wxCheckBox *mpPALQuirks = nullptr;
	wxCheckBox *mpUsePALParams = nullptr;
};

ATVideoSettingsDialog::ATVideoSettingsDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Video Settings", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	wxNotebook *notebook = new wxNotebook(this, wxID_ANY);

	// ==== Display tab ====
	wxPanel *displayPage = new wxPanel(notebook);
	wxFlexGridSizer *dispGrid = new wxFlexGridSizer(2, 5, 10);
	dispGrid->AddGrowableCol(1, 1);

	dispGrid->Add(new wxStaticText(displayPage, wxID_ANY, "Filter:"), 0, wxALIGN_CENTER_VERTICAL);
	mpFilter = new wxChoice(displayPage, wxID_ANY);
	for (auto name : kFilterNames) mpFilter->Append(name);
	dispGrid->Add(mpFilter, 1, wxEXPAND);

	dispGrid->Add(new wxStaticText(displayPage, wxID_ANY, "Stretch:"), 0, wxALIGN_CENTER_VERTICAL);
	mpStretch = new wxChoice(displayPage, wxID_ANY);
	for (auto name : kStretchNames) mpStretch->Append(name);
	dispGrid->Add(mpStretch, 1, wxEXPAND);

	dispGrid->Add(new wxStaticText(displayPage, wxID_ANY, "H Overscan:"), 0, wxALIGN_CENTER_VERTICAL);
	mpHOverscan = new wxChoice(displayPage, wxID_ANY);
	for (auto name : kOverscanNames) mpHOverscan->Append(name);
	dispGrid->Add(mpHOverscan, 1, wxEXPAND);

	dispGrid->Add(new wxStaticText(displayPage, wxID_ANY, "V Overscan:"), 0, wxALIGN_CENTER_VERTICAL);
	mpVOverscan = new wxChoice(displayPage, wxID_ANY);
	for (auto name : kVertOverscanNames) mpVOverscan->Append(name);
	dispGrid->Add(mpVOverscan, 1, wxEXPAND);

	dispGrid->AddSpacer(0);
	mpPALExtended = new wxCheckBox(displayPage, wxID_ANY, "PAL extended height");
	dispGrid->Add(mpPALExtended, 0);

	wxBoxSizer *dispSizer = new wxBoxSizer(wxVERTICAL);
	dispSizer->Add(dispGrid, 0, wxEXPAND | wxALL, 10);
	displayPage->SetSizer(dispSizer);
	notebook->AddPage(displayPage, "Display");

	// ==== Artifacting tab ====
	wxPanel *artPage = new wxPanel(notebook);
	wxBoxSizer *artSizer = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer *artGrid = new wxFlexGridSizer(2, 5, 10);
	artGrid->AddGrowableCol(1, 1);

	artGrid->Add(new wxStaticText(artPage, wxID_ANY, "Artifacting:"), 0, wxALIGN_CENTER_VERTICAL);
	mpArtifactMode = new wxChoice(artPage, wxID_ANY);
	for (auto name : kArtifactNames) mpArtifactMode->Append(name);
	artGrid->Add(mpArtifactMode, 1, wxEXPAND);

	artGrid->Add(new wxStaticText(artPage, wxID_ANY, "Monitor:"), 0, wxALIGN_CENTER_VERTICAL);
	mpMonitorMode = new wxChoice(artPage, wxID_ANY);
	for (auto name : kMonitorNames) mpMonitorMode->Append(name);
	artGrid->Add(mpMonitorMode, 1, wxEXPAND);

	artSizer->Add(artGrid, 0, wxEXPAND | wxALL, 10);

	mpScanlines = new wxCheckBox(artPage, wxID_ANY, "Scanlines");
	mpInterlace = new wxCheckBox(artPage, wxID_ANY, "Interlace");
	artSizer->Add(mpScanlines, 0, wxLEFT | wxRIGHT, 10);
	artSizer->Add(mpInterlace, 0, wxALL, 10);

	artPage->SetSizer(artSizer);
	notebook->AddPage(artPage, "Artifacting");

	// ==== Frame Blending tab ====
	wxPanel *blendPage = new wxPanel(notebook);
	wxBoxSizer *blendSizer = new wxBoxSizer(wxVERTICAL);

	mpBlend = new wxCheckBox(blendPage, wxID_ANY, "Enable frame blending");
	mpMonoPersist = new wxCheckBox(blendPage, wxID_ANY, "Mono persistence");
	mpLinearBlend = new wxCheckBox(blendPage, wxID_ANY, "Linear blend");

	blendSizer->Add(mpBlend, 0, wxALL, 10);
	blendSizer->Add(mpMonoPersist, 0, wxLEFT | wxRIGHT, 10);
	blendSizer->Add(mpLinearBlend, 0, wxALL, 10);

	blendPage->SetSizer(blendSizer);
	notebook->AddPage(blendPage, "Blending");

	// ==== Screen Effects tab ====
	wxPanel *fxPage = new wxPanel(notebook);
	wxBoxSizer *fxSizer = new wxBoxSizer(wxVERTICAL);

	mpBloom = new wxCheckBox(fxPage, wxID_ANY, "Enable bloom");
	fxSizer->Add(mpBloom, 0, wxALL, 10);

	wxFlexGridSizer *fxGrid = new wxFlexGridSizer(2, 5, 10);
	fxGrid->AddGrowableCol(1, 1);

	fxGrid->Add(new wxStaticText(fxPage, wxID_ANY, "Bloom Radius:"), 0, wxALIGN_CENTER_VERTICAL);
	mpBloomRadius = new wxSlider(fxPage, wxID_ANY, 0, 0, 320, wxDefaultPosition, wxSize(200, -1));
	fxGrid->Add(mpBloomRadius, 1, wxEXPAND);

	fxGrid->Add(new wxStaticText(fxPage, wxID_ANY, "Bloom Direct:"), 0, wxALIGN_CENTER_VERTICAL);
	mpBloomDirect = new wxSlider(fxPage, wxID_ANY, 0, 0, 200, wxDefaultPosition, wxSize(200, -1));
	fxGrid->Add(mpBloomDirect, 1, wxEXPAND);

	fxGrid->Add(new wxStaticText(fxPage, wxID_ANY, "Bloom Indirect:"), 0, wxALIGN_CENTER_VERTICAL);
	mpBloomIndirect = new wxSlider(fxPage, wxID_ANY, 0, 0, 200, wxDefaultPosition, wxSize(200, -1));
	fxGrid->Add(mpBloomIndirect, 1, wxEXPAND);

	fxGrid->Add(new wxStaticText(fxPage, wxID_ANY, "Distortion Angle:"), 0, wxALIGN_CENTER_VERTICAL);
	mpDistortionAngle = new wxSlider(fxPage, wxID_ANY, 0, 0, 179, wxDefaultPosition, wxSize(200, -1));
	fxGrid->Add(mpDistortionAngle, 1, wxEXPAND);

	fxGrid->Add(new wxStaticText(fxPage, wxID_ANY, "Distortion Y:"), 0, wxALIGN_CENTER_VERTICAL);
	mpDistortionY = new wxSlider(fxPage, wxID_ANY, 0, 0, 100, wxDefaultPosition, wxSize(200, -1));
	fxGrid->Add(mpDistortionY, 1, wxEXPAND);

	fxSizer->Add(fxGrid, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

	fxPage->SetSizer(fxSizer);
	notebook->AddPage(fxPage, "Effects");

	// ==== Color tab ====
	wxPanel *colorPage = new wxPanel(notebook);
	wxBoxSizer *colorSizer = new wxBoxSizer(wxVERTICAL);

	mpUsePALParams = new wxCheckBox(colorPage, wxID_ANY, "Use separate PAL parameters");
	colorSizer->Add(mpUsePALParams, 0, wxALL, 10);

	wxFlexGridSizer *colorGrid = new wxFlexGridSizer(2, 5, 10);
	colorGrid->AddGrowableCol(1, 1);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Hue Start:"), 0, wxALIGN_CENTER_VERTICAL);
	mpHueStart = new wxSlider(colorPage, wxID_ANY, 0, -600, 600, wxDefaultPosition, wxSize(200, -1));
	colorGrid->Add(mpHueStart, 1, wxEXPAND);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Hue Range:"), 0, wxALIGN_CENTER_VERTICAL);
	mpHueRange = new wxSlider(colorPage, wxID_ANY, 2600, 1600, 3600, wxDefaultPosition, wxSize(200, -1));
	colorGrid->Add(mpHueRange, 1, wxEXPAND);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Brightness:"), 0, wxALIGN_CENTER_VERTICAL);
	mpBrightness = new wxSlider(colorPage, wxID_ANY, 0, -500, 500, wxDefaultPosition, wxSize(200, -1));
	colorGrid->Add(mpBrightness, 1, wxEXPAND);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Contrast:"), 0, wxALIGN_CENTER_VERTICAL);
	mpContrast = new wxSlider(colorPage, wxID_ANY, 1000, 0, 2000, wxDefaultPosition, wxSize(200, -1));
	colorGrid->Add(mpContrast, 1, wxEXPAND);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Saturation:"), 0, wxALIGN_CENTER_VERTICAL);
	mpSaturation = new wxSlider(colorPage, wxID_ANY, 500, 0, 1000, wxDefaultPosition, wxSize(200, -1));
	colorGrid->Add(mpSaturation, 1, wxEXPAND);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Gamma:"), 0, wxALIGN_CENTER_VERTICAL);
	mpGamma = new wxSlider(colorPage, wxID_ANY, 100, 50, 300, wxDefaultPosition, wxSize(200, -1));
	colorGrid->Add(mpGamma, 1, wxEXPAND);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Intensity:"), 0, wxALIGN_CENTER_VERTICAL);
	mpIntensity = new wxSlider(colorPage, wxID_ANY, 100, 50, 200, wxDefaultPosition, wxSize(200, -1));
	colorGrid->Add(mpIntensity, 1, wxEXPAND);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Color Match:"), 0, wxALIGN_CENTER_VERTICAL);
	mpColorMatch = new wxChoice(colorPage, wxID_ANY);
	for (auto name : kColorMatchNames) mpColorMatch->Append(name);
	colorGrid->Add(mpColorMatch, 1, wxEXPAND);

	colorGrid->Add(new wxStaticText(colorPage, wxID_ANY, "Luma Ramp:"), 0, wxALIGN_CENTER_VERTICAL);
	mpLumaRamp = new wxChoice(colorPage, wxID_ANY);
	for (auto name : kLumaRampNames) mpLumaRamp->Append(name);
	colorGrid->Add(mpLumaRamp, 1, wxEXPAND);

	colorSizer->Add(colorGrid, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

	mpPALQuirks = new wxCheckBox(colorPage, wxID_ANY, "PAL quirks");
	colorSizer->Add(mpPALQuirks, 0, wxALL, 10);

	wxButton *resetBtn = new wxButton(colorPage, wxID_ANY, "Reset to Defaults");
	colorSizer->Add(resetBtn, 0, wxLEFT | wxBOTTOM, 10);
	resetBtn->Bind(wxEVT_BUTTON, &ATVideoSettingsDialog::OnResetColors, this);

	colorPage->SetSizer(colorSizer);
	notebook->AddPage(colorPage, "Color");

	topSizer->Add(notebook, 1, wxEXPAND | wxALL, 5);

	// OK/Cancel
	topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 5);

	SetSizerAndFit(topSizer);

	PopulateFromState();
	Bind(wxEVT_BUTTON, &ATVideoSettingsDialog::OnOK, this, wxID_OK);
}

void ATVideoSettingsDialog::PopulateFromState() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	mpFilter->SetSelection((int)ATUIGetDisplayFilterMode());
	mpStretch->SetSelection((int)ATUIGetDisplayStretchMode());
	mpHOverscan->SetSelection((int)gtia.GetOverscanMode());
	mpVOverscan->SetSelection((int)gtia.GetVerticalOverscanMode());
	mpPALExtended->SetValue(gtia.IsOverscanPALExtended());

	mpArtifactMode->SetSelection((int)gtia.GetArtifactingMode());
	mpMonitorMode->SetSelection((int)gtia.GetMonitorMode());
	mpScanlines->SetValue(gtia.AreScanlinesEnabled());
	mpInterlace->SetValue(gtia.IsInterlaceEnabled());

	mpBlend->SetValue(gtia.IsBlendModeEnabled());
	mpMonoPersist->SetValue(gtia.IsBlendMonoPersistenceEnabled());
	mpLinearBlend->SetValue(gtia.IsLinearBlendEnabled());

	ATArtifactingParams artParams = gtia.GetArtifactingParams();
	mpBloom->SetValue(artParams.mbEnableBloom);
	mpBloomRadius->SetValue((int)(artParams.mBloomRadius * 10.0f));
	mpBloomDirect->SetValue((int)(artParams.mBloomDirectIntensity * 100.0f));
	mpBloomIndirect->SetValue((int)(artParams.mBloomIndirectIntensity * 100.0f));
	mpDistortionAngle->SetValue((int)artParams.mDistortionViewAngleX);
	mpDistortionY->SetValue((int)(artParams.mDistortionYRatio * 100.0f));

	ATColorSettings cs = gtia.GetColorSettings();
	bool isPAL = gtia.IsPALMode();
	const ATColorParams& params = isPAL ? cs.mPALParams : cs.mNTSCParams;

	mpUsePALParams->SetValue(cs.mbUsePALParams);
	mpHueStart->SetValue((int)(params.mHueStart * 10.0f));
	mpHueRange->SetValue((int)(params.mHueRange * 10.0f));
	mpBrightness->SetValue((int)(params.mBrightness * 1000.0f));
	mpContrast->SetValue((int)(params.mContrast * 1000.0f));
	mpSaturation->SetValue((int)(params.mSaturation * 1000.0f));
	mpGamma->SetValue((int)(params.mGammaCorrect * 100.0f));
	mpIntensity->SetValue((int)(params.mIntensityScale * 100.0f));
	mpColorMatch->SetSelection((int)params.mColorMatchingMode);
	mpLumaRamp->SetSelection((int)params.mLumaRampMode);
	mpPALQuirks->SetValue(params.mbUsePALQuirks);
}

void ATVideoSettingsDialog::ApplyToState() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	ATUISetDisplayFilterMode((ATDisplayFilterMode)mpFilter->GetSelection());
	ATUISetDisplayStretchMode((ATDisplayStretchMode)mpStretch->GetSelection());
	gtia.SetOverscanMode((ATGTIAEmulator::OverscanMode)mpHOverscan->GetSelection());
	gtia.SetVerticalOverscanMode((ATGTIAEmulator::VerticalOverscanMode)mpVOverscan->GetSelection());
	gtia.SetOverscanPALExtended(mpPALExtended->GetValue());

	gtia.SetArtifactingMode((ATArtifactMode)mpArtifactMode->GetSelection());
	gtia.SetMonitorMode((ATMonitorMode)mpMonitorMode->GetSelection());
	gtia.SetScanlinesEnabled(mpScanlines->GetValue());
	gtia.SetInterlaceEnabled(mpInterlace->GetValue());

	gtia.SetBlendModeEnabled(mpBlend->GetValue());
	gtia.SetBlendMonoPersistenceEnabled(mpMonoPersist->GetValue());
	gtia.SetLinearBlendEnabled(mpLinearBlend->GetValue());

	ATArtifactingParams artParams = gtia.GetArtifactingParams();
	artParams.mbEnableBloom = mpBloom->GetValue();
	artParams.mBloomRadius = mpBloomRadius->GetValue() / 10.0f;
	artParams.mBloomDirectIntensity = mpBloomDirect->GetValue() / 100.0f;
	artParams.mBloomIndirectIntensity = mpBloomIndirect->GetValue() / 100.0f;
	artParams.mDistortionViewAngleX = (float)mpDistortionAngle->GetValue();
	artParams.mDistortionYRatio = mpDistortionY->GetValue() / 100.0f;
	gtia.SetArtifactingParams(artParams);

	ATColorSettings cs = gtia.GetColorSettings();
	bool isPAL = gtia.IsPALMode();
	ATColorParams& params = isPAL ? cs.mPALParams : cs.mNTSCParams;

	cs.mbUsePALParams = mpUsePALParams->GetValue();
	params.mHueStart = mpHueStart->GetValue() / 10.0f;
	params.mHueRange = mpHueRange->GetValue() / 10.0f;
	params.mBrightness = mpBrightness->GetValue() / 1000.0f;
	params.mContrast = mpContrast->GetValue() / 1000.0f;
	params.mSaturation = mpSaturation->GetValue() / 1000.0f;
	params.mGammaCorrect = mpGamma->GetValue() / 100.0f;
	params.mIntensityScale = mpIntensity->GetValue() / 100.0f;
	params.mColorMatchingMode = (ATColorMatchingMode)mpColorMatch->GetSelection();
	params.mLumaRampMode = (ATLumaRampMode)mpLumaRamp->GetSelection();
	params.mbUsePALQuirks = mpPALQuirks->GetValue();
	gtia.SetColorSettings(cs);
}

void ATVideoSettingsDialog::OnOK(wxCommandEvent&) {
	ApplyToState();
	EndModal(wxID_OK);
}

void ATVideoSettingsDialog::OnResetColors(wxCommandEvent&) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	ATColorSettings defaults = gtia.GetDefaultColorSettings();
	gtia.SetColorSettings(defaults);
	PopulateFromState();
}

} // anonymous namespace

void ATShowVideoSettingsDialog(wxWindow *parent) {
	ATVideoSettingsDialog dlg(parent);
	dlg.ShowModal();
}
