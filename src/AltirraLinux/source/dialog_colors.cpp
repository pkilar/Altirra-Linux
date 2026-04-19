//	Altirra - Atari 800/800XL/5200 emulator
//	Color/Artifacting Adjustment dialog
//	Copyright (C) 2009-2015 Avery Lee
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
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include "simulator.h"
#include "gtia.h"

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/dcclient.h>

extern ATSimulator g_sim;

////////////////////////////////////////////////////////////////////////////////
// Slider parameter descriptor

struct ColorSliderDef {
	const char *label;
	int minVal;
	int maxVal;
	float scale;		// multiply slider value by this to get param value
	float ATColorParams::*pField;
};

static const ColorSliderDef kBasicSliders[] = {
	{ "Hue Start",        -120, 360, 1.0f,     &ATColorParams::mHueStart },
	{ "Hue Range",           0, 540, 1.0f,     &ATColorParams::mHueRange },
	{ "Brightness",        -50,  50, 1.0f/100,  &ATColorParams::mBrightness },
	{ "Contrast",            0, 200, 1.0f/100,  &ATColorParams::mContrast },
	{ "Saturation",          0, 100, 1.0f/100,  &ATColorParams::mSaturation },
	{ "Gamma",              50, 260, 1.0f/100,  &ATColorParams::mGammaCorrect },
	{ "Intensity Scale",    50, 220, 1.0f/100,  &ATColorParams::mIntensityScale },
};

static const ColorSliderDef kArtifactSliders[] = {
	{ "Artifact Hue",      -60, 360, 1.0f,     &ATColorParams::mArtifactHue },
	{ "Artifact Saturation", 0, 400, 1.0f/100,  &ATColorParams::mArtifactSat },
	{ "Artifact Sharpness",  0, 100, 1.0f/100,  &ATColorParams::mArtifactSharpness },
};

static const ColorSliderDef kChannelSliders[] = {
	{ "Red Shift",        -225, 225, 1.0f/10,   &ATColorParams::mRedShift },
	{ "Red Scale",           0, 400, 1.0f/100,  &ATColorParams::mRedScale },
	{ "Green Shift",      -225, 225, 1.0f/10,   &ATColorParams::mGrnShift },
	{ "Green Scale",         0, 400, 1.0f/100,  &ATColorParams::mGrnScale },
	{ "Blue Shift",       -225, 225, 1.0f/10,   &ATColorParams::mBluShift },
	{ "Blue Scale",          0, 400, 1.0f/100,  &ATColorParams::mBluScale },
};

////////////////////////////////////////////////////////////////////////////////
// Palette preview panel (16x16 color swatch grid)

class ATPalettePreview : public wxPanel {
public:
	ATPalettePreview(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(256, 128)) {
		memset(mPalette, 0, sizeof(mPalette));
		Bind(wxEVT_PAINT, &ATPalettePreview::OnPaint, this);
	}

	void UpdatePalette(const uint32 pal[256]) {
		memcpy(mPalette, pal, sizeof(mPalette));
		Refresh();
	}

private:
	void OnPaint(wxPaintEvent&) {
		wxPaintDC dc(this);
		wxSize sz = GetClientSize();
		int cellW = sz.GetWidth() / 16;
		int cellH = sz.GetHeight() / 16;

		for (int row = 0; row < 16; row++) {
			for (int col = 0; col < 16; col++) {
				uint32 c = mPalette[row * 16 + col];
				int r = (c >> 16) & 0xFF;
				int g = (c >> 8) & 0xFF;
				int b = c & 0xFF;
				dc.SetBrush(wxBrush(wxColour(r, g, b)));
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.DrawRectangle(col * cellW, row * cellH, cellW, cellH);
			}
		}
	}

	uint32 mPalette[256];
};

////////////////////////////////////////////////////////////////////////////////
// Color adjustment dialog

class ATColorDialog : public wxDialog {
public:
	ATColorDialog(wxWindow *parent);

private:
	void CreateSliderGroup(wxWindow *parent, wxFlexGridSizer *sizer,
		const ColorSliderDef *defs, int count, int baseIdx);

	void LoadFromGTIA();
	void ApplyToGTIA();
	void UpdatePalettePreview();
	void UpdateValueLabel(int idx);

	void OnSliderChanged(wxCommandEvent& evt);
	void OnPresetChanged(wxCommandEvent& evt);
	void OnSharedChanged(wxCommandEvent& evt);
	void OnPALQuirksChanged(wxCommandEvent& evt);
	void OnLumaRampChanged(wxCommandEvent& evt);
	void OnColorMatchChanged(wxCommandEvent& evt);
	void OnResetDefaults(wxCommandEvent& evt);

	ATColorSettings mSettings;

	// All slider defs in order
	static constexpr int kTotalSliders =
		std::size(kBasicSliders) + std::size(kArtifactSliders) + std::size(kChannelSliders);

	wxSlider *mSliders[kTotalSliders] = {};
	wxStaticText *mValueLabels[kTotalSliders] = {};
	const ColorSliderDef *mSliderDefs[kTotalSliders] = {};

	wxChoice *mpPresetChoice = nullptr;
	wxCheckBox *mpSharedCheck = nullptr;
	wxCheckBox *mpPALQuirksCheck = nullptr;
	wxChoice *mpLumaRampChoice = nullptr;
	wxChoice *mpColorMatchChoice = nullptr;
	ATPalettePreview *mpPalettePreview = nullptr;
};

ATColorDialog::ATColorDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Color Settings",
		wxDefaultPosition, wxSize(520, 680),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	auto *mainSizer = new wxBoxSizer(wxVERTICAL);

	// Preset selector
	{
		auto *row = new wxBoxSizer(wxHORIZONTAL);
		row->Add(new wxStaticText(this, wxID_ANY, "Preset:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		mpPresetChoice = new wxChoice(this, wxID_ANY);
		uint32 presetCount = ATGetColorPresetCount();
		mpPresetChoice->Append("(Custom)");
		for (uint32 i = 0; i < presetCount; i++) {
			const wchar_t *name = ATGetColorPresetNameByIndex(i);
			mpPresetChoice->Append(wxString(name));
		}
		mpPresetChoice->Bind(wxEVT_CHOICE, &ATColorDialog::OnPresetChanged, this);
		row->Add(mpPresetChoice, 1, wxEXPAND);
		mainSizer->Add(row, 0, wxEXPAND | wxALL, 5);
	}

	// Shared NTSC/PAL checkbox
	mpSharedCheck = new wxCheckBox(this, wxID_ANY, "Share palettes between NTSC and PAL");
	mpSharedCheck->Bind(wxEVT_CHECKBOX, &ATColorDialog::OnSharedChanged, this);
	mainSizer->Add(mpSharedCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// PAL quirks checkbox
	mpPALQuirksCheck = new wxCheckBox(this, wxID_ANY, "Use PAL quirks");
	mpPALQuirksCheck->Bind(wxEVT_CHECKBOX, &ATColorDialog::OnPALQuirksChanged, this);
	mainSizer->Add(mpPALQuirksCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// Luma ramp + color matching
	{
		auto *row = new wxBoxSizer(wxHORIZONTAL);
		row->Add(new wxStaticText(this, wxID_ANY, "Luma Ramp:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		mpLumaRampChoice = new wxChoice(this, wxID_ANY);
		mpLumaRampChoice->Append("Linear");
		mpLumaRampChoice->Append("XL/XE");
		mpLumaRampChoice->Bind(wxEVT_CHOICE, &ATColorDialog::OnLumaRampChanged, this);
		row->Add(mpLumaRampChoice, 0, wxRIGHT, 15);

		row->Add(new wxStaticText(this, wxID_ANY, "Color Matching:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		mpColorMatchChoice = new wxChoice(this, wxID_ANY);
		mpColorMatchChoice->Append("None");
		mpColorMatchChoice->Append("sRGB");
		mpColorMatchChoice->Append("Adobe RGB");
		mpColorMatchChoice->Append("Gamma 2.2");
		mpColorMatchChoice->Append("Gamma 2.4");
		mpColorMatchChoice->Bind(wxEVT_CHOICE, &ATColorDialog::OnColorMatchChanged, this);
		row->Add(mpColorMatchChoice, 0);
		mainSizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	}

	// Notebook with tabs for slider groups
	auto *notebook = new wxNotebook(this, wxID_ANY);

	// Basic tab
	{
		auto *panel = new wxPanel(notebook);
		auto *sizer = new wxFlexGridSizer(3, 5, 5);
		sizer->AddGrowableCol(1);
		CreateSliderGroup(panel, sizer, kBasicSliders, std::size(kBasicSliders), 0);
		panel->SetSizer(sizer);
		notebook->AddPage(panel, "Basic");
	}

	// Artifacting tab
	{
		auto *panel = new wxPanel(notebook);
		auto *sizer = new wxFlexGridSizer(3, 5, 5);
		sizer->AddGrowableCol(1);
		CreateSliderGroup(panel, sizer, kArtifactSliders, std::size(kArtifactSliders),
			std::size(kBasicSliders));
		panel->SetSizer(sizer);
		notebook->AddPage(panel, "Artifacting");
	}

	// Channel adjust tab
	{
		auto *panel = new wxPanel(notebook);
		auto *sizer = new wxFlexGridSizer(3, 5, 5);
		sizer->AddGrowableCol(1);
		CreateSliderGroup(panel, sizer, kChannelSliders, std::size(kChannelSliders),
			std::size(kBasicSliders) + std::size(kArtifactSliders));
		panel->SetSizer(sizer);
		notebook->AddPage(panel, "Channels");
	}

	mainSizer->Add(notebook, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

	// Palette preview
	mpPalettePreview = new ATPalettePreview(this);
	mainSizer->Add(mpPalettePreview, 0, wxEXPAND | wxALL, 5);

	// Buttons
	{
		auto *btnSizer = new wxBoxSizer(wxHORIZONTAL);
		auto *resetBtn = new wxButton(this, wxID_ANY, "Reset Defaults");
		resetBtn->Bind(wxEVT_BUTTON, &ATColorDialog::OnResetDefaults, this);
		btnSizer->Add(resetBtn, 0, wxRIGHT, 10);
		btnSizer->AddStretchSpacer();
		btnSizer->Add(new wxButton(this, wxID_OK, "OK"), 0, wxRIGHT, 5);
		btnSizer->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0);
		mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 5);
	}

	SetSizerAndFit(mainSizer);
	LoadFromGTIA();
}

void ATColorDialog::CreateSliderGroup(wxWindow *parent, wxFlexGridSizer *sizer,
	const ColorSliderDef *defs, int count, int baseIdx)
{
	for (int i = 0; i < count; i++) {
		int idx = baseIdx + i;
		mSliderDefs[idx] = &defs[i];

		sizer->Add(new wxStaticText(parent, wxID_ANY, defs[i].label),
			0, wxALIGN_CENTER_VERTICAL);

		mSliders[idx] = new wxSlider(parent, 10000 + idx,
			0, defs[i].minVal, defs[i].maxVal,
			wxDefaultPosition, wxSize(250, -1));
		mSliders[idx]->Bind(wxEVT_SLIDER, &ATColorDialog::OnSliderChanged, this);
		sizer->Add(mSliders[idx], 1, wxEXPAND);

		mValueLabels[idx] = new wxStaticText(parent, wxID_ANY, "0",
			wxDefaultPosition, wxSize(60, -1), wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
		sizer->Add(mValueLabels[idx], 0, wxALIGN_CENTER_VERTICAL);
	}
}

void ATColorDialog::LoadFromGTIA() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	mSettings = gtia.GetColorSettings();

	// Determine which params to use based on PAL mode
	bool isPAL = gtia.IsPALMode() && mSettings.mbUsePALParams;
	ATColorParams& params = isPAL ? mSettings.mPALParams : mSettings.mNTSCParams;

	// Update sliders
	for (int i = 0; i < (int)kTotalSliders; i++) {
		if (!mSliderDefs[i]) continue;
		float val = params.*(mSliderDefs[i]->pField);
		int sliderVal = (int)(val / mSliderDefs[i]->scale + 0.5f);
		mSliders[i]->SetValue(std::clamp(sliderVal, mSliderDefs[i]->minVal, mSliderDefs[i]->maxVal));
		UpdateValueLabel(i);
	}

	// Update checkboxes
	mpSharedCheck->SetValue(!mSettings.mbUsePALParams);
	mpPALQuirksCheck->SetValue(params.mbUsePALQuirks);

	// Luma ramp
	mpLumaRampChoice->SetSelection((int)params.mLumaRampMode);

	// Color matching
	mpColorMatchChoice->SetSelection((int)params.mColorMatchingMode);

	// Find matching preset
	mpPresetChoice->SetSelection(0);  // "(Custom)"
	const char *tag = isPAL ? mSettings.mPALParams.mPresetTag.c_str() : mSettings.mNTSCParams.mPresetTag.c_str();
	if (tag && *tag) {
		sint32 idx = ATGetColorPresetIndexByTag(tag);
		if (idx >= 0)
			mpPresetChoice->SetSelection(idx + 1);
	}

	UpdatePalettePreview();
}

void ATColorDialog::ApplyToGTIA() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	bool isPAL = gtia.IsPALMode() && mSettings.mbUsePALParams;
	ATColorParams& params = isPAL ? mSettings.mPALParams : mSettings.mNTSCParams;

	// Read sliders into params
	for (int i = 0; i < (int)kTotalSliders; i++) {
		if (!mSliderDefs[i]) continue;
		params.*(mSliderDefs[i]->pField) = mSliders[i]->GetValue() * mSliderDefs[i]->scale;
	}

	// Checkboxes
	mSettings.mbUsePALParams = !mpSharedCheck->GetValue();
	params.mbUsePALQuirks = mpPALQuirksCheck->GetValue();

	// Luma ramp
	params.mLumaRampMode = (ATLumaRampMode)mpLumaRampChoice->GetSelection();

	// Color matching
	params.mColorMatchingMode = (ATColorMatchingMode)mpColorMatchChoice->GetSelection();

	// If sharing, copy to both
	if (!mSettings.mbUsePALParams) {
		mSettings.mPALParams = mSettings.mNTSCParams;
	}

	gtia.SetColorSettings(mSettings);
}

void ATColorDialog::UpdatePalettePreview() {
	ApplyToGTIA();

	uint32 palette[256];
	g_sim.GetGTIA().GetPalette(palette);
	mpPalettePreview->UpdatePalette(palette);
}

void ATColorDialog::UpdateValueLabel(int idx) {
	if (!mSliderDefs[idx] || !mValueLabels[idx]) return;
	float val = mSliders[idx]->GetValue() * mSliderDefs[idx]->scale;

	wxString text;
	if (mSliderDefs[idx]->scale == 1.0f)
		text.Printf("%d", mSliders[idx]->GetValue());
	else if (mSliderDefs[idx]->scale == 1.0f/10)
		text.Printf("%.1f", val);
	else
		text.Printf("%.0f%%", val * 100);

	mValueLabels[idx]->SetLabel(text);
}

void ATColorDialog::OnSliderChanged(wxCommandEvent& evt) {
	int sliderId = evt.GetId() - 10000;
	if (sliderId >= 0 && sliderId < (int)kTotalSliders) {
		UpdateValueLabel(sliderId);
		// Clear preset selection since user changed manually
		mpPresetChoice->SetSelection(0);
		UpdatePalettePreview();
	}
}

void ATColorDialog::OnPresetChanged(wxCommandEvent&) {
	int sel = mpPresetChoice->GetSelection();
	if (sel <= 0) return;  // "(Custom)" or nothing

	ATColorParams preset = ATGetColorPresetByIndex(sel - 1);
	const char *tag = ATGetColorPresetTagByIndex(sel - 1);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	bool isPAL = gtia.IsPALMode() && mSettings.mbUsePALParams;
	ATNamedColorParams& namedParams = isPAL ? mSettings.mPALParams : mSettings.mNTSCParams;
	static_cast<ATColorParams&>(namedParams) = preset;
	namedParams.mPresetTag = tag ? tag : "";

	// Update sliders from new preset
	for (int i = 0; i < (int)kTotalSliders; i++) {
		if (!mSliderDefs[i]) continue;
		float val = preset.*(mSliderDefs[i]->pField);
		int sliderVal = (int)(val / mSliderDefs[i]->scale + 0.5f);
		mSliders[i]->SetValue(std::clamp(sliderVal, mSliderDefs[i]->minVal, mSliderDefs[i]->maxVal));
		UpdateValueLabel(i);
	}

	mpPALQuirksCheck->SetValue(preset.mbUsePALQuirks);
	mpLumaRampChoice->SetSelection((int)preset.mLumaRampMode);
	mpColorMatchChoice->SetSelection((int)preset.mColorMatchingMode);

	UpdatePalettePreview();
}

void ATColorDialog::OnSharedChanged(wxCommandEvent&) {
	UpdatePalettePreview();
}

void ATColorDialog::OnPALQuirksChanged(wxCommandEvent&) {
	UpdatePalettePreview();
}

void ATColorDialog::OnLumaRampChanged(wxCommandEvent&) {
	mpPresetChoice->SetSelection(0);
	UpdatePalettePreview();
}

void ATColorDialog::OnColorMatchChanged(wxCommandEvent&) {
	mpPresetChoice->SetSelection(0);
	UpdatePalettePreview();
}

void ATColorDialog::OnResetDefaults(wxCommandEvent&) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.ResetColors();
	LoadFromGTIA();
}

////////////////////////////////////////////////////////////////////////////////
// Public entry point

void ATShowColorSettingsDialog(wxWindow *parent) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	ATColorSettings savedSettings = gtia.GetColorSettings();

	ATColorDialog dlg(parent);
	if (dlg.ShowModal() == wxID_OK) {
		// Settings already applied via real-time preview
	} else {
		// Restore original settings on cancel
		gtia.SetColorSettings(savedSettings);
	}
}
