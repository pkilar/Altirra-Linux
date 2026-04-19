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
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include "simulator.h"
#include "disk.h"
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>
#include <at/atcore/audiomixer.h>

extern ATSimulator g_sim;

namespace {

class ATAudioOptionsDialog : public wxDialog {
public:
	ATAudioOptionsDialog(wxWindow *parent);

private:
	void PopulateFromState();
	void ApplyToState();
	void OnOK(wxCommandEvent& event);
	void OnSliderUpdate(wxCommandEvent& event);
	void UpdateVolumeLabels();

	// Volume
	wxSlider *mpMasterVolume = nullptr;
	wxStaticText *mpMasterLabel = nullptr;
	wxCheckBox *mpMute = nullptr;
	wxSlider *mpDriveVolume = nullptr;
	wxStaticText *mpDriveLabel = nullptr;
	wxSlider *mpCovoxVolume = nullptr;
	wxStaticText *mpCovoxLabel = nullptr;

	// Buffering
	wxSlider *mpLatency = nullptr;
	wxStaticText *mpLatencyLabel = nullptr;
	wxSlider *mpExtraBuffer = nullptr;
	wxStaticText *mpExtraBufferLabel = nullptr;

	// Drive sounds
	wxCheckBox *mpDriveSounds = nullptr;

	// POKEY
	wxCheckBox *mpDualPokey = nullptr;
	wxCheckBox *mpStereoMono = nullptr;
	wxCheckBox *mpNonlinearMix = nullptr;
	wxCheckBox *mpSpeakerFilter = nullptr;
	wxCheckBox *mpSerialNoise = nullptr;

	// Channel enables
	wxCheckBox *mpPriCh[4] = {};
	wxCheckBox *mpSecCh[4] = {};
};

ATAudioOptionsDialog::ATAudioOptionsDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Audio Options", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// Volume section
	wxStaticBoxSizer *volBox = new wxStaticBoxSizer(wxVERTICAL, this, "Volume");
	wxWindow *vp = volBox->GetStaticBox();

	wxFlexGridSizer *volGrid = new wxFlexGridSizer(3, 5, 10);
	volGrid->AddGrowableCol(1, 1);

	volGrid->Add(new wxStaticText(vp, wxID_ANY, "Master:"), 0, wxALIGN_CENTER_VERTICAL);
	mpMasterVolume = new wxSlider(vp, wxID_ANY, 100, 0, 100, wxDefaultPosition, wxSize(200, -1));
	volGrid->Add(mpMasterVolume, 1, wxEXPAND);
	mpMasterLabel = new wxStaticText(vp, wxID_ANY, "100%", wxDefaultPosition, wxSize(40, -1));
	volGrid->Add(mpMasterLabel, 0, wxALIGN_CENTER_VERTICAL);

	volGrid->Add(new wxStaticText(vp, wxID_ANY, "Drive:"), 0, wxALIGN_CENTER_VERTICAL);
	mpDriveVolume = new wxSlider(vp, wxID_ANY, 100, 0, 100, wxDefaultPosition, wxSize(200, -1));
	volGrid->Add(mpDriveVolume, 1, wxEXPAND);
	mpDriveLabel = new wxStaticText(vp, wxID_ANY, "100%", wxDefaultPosition, wxSize(40, -1));
	volGrid->Add(mpDriveLabel, 0, wxALIGN_CENTER_VERTICAL);

	volGrid->Add(new wxStaticText(vp, wxID_ANY, "Covox:"), 0, wxALIGN_CENTER_VERTICAL);
	mpCovoxVolume = new wxSlider(vp, wxID_ANY, 100, 0, 100, wxDefaultPosition, wxSize(200, -1));
	volGrid->Add(mpCovoxVolume, 1, wxEXPAND);
	mpCovoxLabel = new wxStaticText(vp, wxID_ANY, "100%", wxDefaultPosition, wxSize(40, -1));
	volGrid->Add(mpCovoxLabel, 0, wxALIGN_CENTER_VERTICAL);

	volBox->Add(volGrid, 0, wxEXPAND | wxALL, 3);
	mpMute = new wxCheckBox(vp, wxID_ANY, "Mute");
	volBox->Add(mpMute, 0, wxALL, 3);
	topSizer->Add(volBox, 0, wxEXPAND | wxALL, 5);

	// Buffering section
	wxStaticBoxSizer *bufBox = new wxStaticBoxSizer(wxVERTICAL, this, "Buffering");
	wxWindow *bp = bufBox->GetStaticBox();

	wxFlexGridSizer *bufGrid = new wxFlexGridSizer(3, 5, 10);
	bufGrid->AddGrowableCol(1, 1);

	bufGrid->Add(new wxStaticText(bp, wxID_ANY, "Latency:"), 0, wxALIGN_CENTER_VERTICAL);
	mpLatency = new wxSlider(bp, wxID_ANY, 80, 10, 500, wxDefaultPosition, wxSize(200, -1));
	bufGrid->Add(mpLatency, 1, wxEXPAND);
	mpLatencyLabel = new wxStaticText(bp, wxID_ANY, "80 ms", wxDefaultPosition, wxSize(50, -1));
	bufGrid->Add(mpLatencyLabel, 0, wxALIGN_CENTER_VERTICAL);

	bufGrid->Add(new wxStaticText(bp, wxID_ANY, "Extra Buffer:"), 0, wxALIGN_CENTER_VERTICAL);
	mpExtraBuffer = new wxSlider(bp, wxID_ANY, 100, 20, 500, wxDefaultPosition, wxSize(200, -1));
	bufGrid->Add(mpExtraBuffer, 1, wxEXPAND);
	mpExtraBufferLabel = new wxStaticText(bp, wxID_ANY, "100 ms", wxDefaultPosition, wxSize(50, -1));
	bufGrid->Add(mpExtraBufferLabel, 0, wxALIGN_CENTER_VERTICAL);

	bufBox->Add(bufGrid, 0, wxEXPAND | wxALL, 3);
	topSizer->Add(bufBox, 0, wxEXPAND | wxALL, 5);

	// Drive sounds
	mpDriveSounds = new wxCheckBox(this, wxID_ANY, "Enable drive sounds");
	topSizer->Add(mpDriveSounds, 0, wxALL, 5);

	// POKEY section
	wxStaticBoxSizer *pokeyBox = new wxStaticBoxSizer(wxVERTICAL, this, "POKEY Options");
	wxWindow *pp = pokeyBox->GetStaticBox();
	mpDualPokey = new wxCheckBox(pp, wxID_ANY, "Dual POKEY (stereo)");
	mpStereoMono = new wxCheckBox(pp, wxID_ANY, "Downmix stereo to mono");
	mpNonlinearMix = new wxCheckBox(pp, wxID_ANY, "Non-linear mixing");
	mpSpeakerFilter = new wxCheckBox(pp, wxID_ANY, "Speaker filter");
	mpSerialNoise = new wxCheckBox(pp, wxID_ANY, "Serial noise");
	pokeyBox->Add(mpDualPokey, 0, wxALL, 3);
	pokeyBox->Add(mpStereoMono, 0, wxALL, 3);
	pokeyBox->Add(mpNonlinearMix, 0, wxALL, 3);
	pokeyBox->Add(mpSpeakerFilter, 0, wxALL, 3);
	pokeyBox->Add(mpSerialNoise, 0, wxALL, 3);
	topSizer->Add(pokeyBox, 0, wxEXPAND | wxALL, 5);

	// Channel enables
	wxStaticBoxSizer *chBox = new wxStaticBoxSizer(wxVERTICAL, this, "Channel Enables");
	wxWindow *cp = chBox->GetStaticBox();

	chBox->Add(new wxStaticText(cp, wxID_ANY, "Primary POKEY:"), 0, wxALL, 3);
	wxBoxSizer *priRow = new wxBoxSizer(wxHORIZONTAL);
	for (int i = 0; i < 4; i++) {
		mpPriCh[i] = new wxCheckBox(cp, wxID_ANY, wxString::Format("Ch %d", i + 1));
		priRow->Add(mpPriCh[i], 0, wxRIGHT, 10);
	}
	chBox->Add(priRow, 0, wxLEFT, 10);

	chBox->Add(new wxStaticText(cp, wxID_ANY, "Secondary POKEY:"), 0, wxALL, 3);
	wxBoxSizer *secRow = new wxBoxSizer(wxHORIZONTAL);
	for (int i = 0; i < 4; i++) {
		mpSecCh[i] = new wxCheckBox(cp, wxID_ANY, wxString::Format("Ch %d", i + 1));
		secRow->Add(mpSecCh[i], 0, wxRIGHT, 10);
	}
	chBox->Add(secRow, 0, wxLEFT, 10);

	topSizer->Add(chBox, 0, wxEXPAND | wxALL, 5);

	// OK/Cancel
	topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 5);

	SetSizerAndFit(topSizer);

	PopulateFromState();

	Bind(wxEVT_BUTTON, &ATAudioOptionsDialog::OnOK, this, wxID_OK);
	Bind(wxEVT_SLIDER, &ATAudioOptionsDialog::OnSliderUpdate, this);
}

void ATAudioOptionsDialog::PopulateFromState() {
	IATAudioOutput *audioOut = g_sim.GetAudioOutput();

	if (audioOut) {
		mpMasterVolume->SetValue((int)(audioOut->GetVolume() * 100.0f));
		mpMute->SetValue(audioOut->GetMute());
		mpDriveVolume->SetValue((int)(audioOut->GetMixLevel(kATAudioMix_Drive) * 100.0f));
		mpCovoxVolume->SetValue((int)(audioOut->GetMixLevel(kATAudioMix_Covox) * 100.0f));
		mpLatency->SetValue(audioOut->GetLatency());
		mpExtraBuffer->SetValue(audioOut->GetExtraBuffer());
	}

	mpDriveSounds->SetValue(g_sim.GetDiskInterface(0).AreDriveSoundsEnabled());

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	mpDualPokey->SetValue(g_sim.IsDualPokeysEnabled());
	mpStereoMono->SetValue(pokey.IsStereoAsMonoEnabled());
	mpNonlinearMix->SetValue(pokey.IsNonlinearMixingEnabled());
	mpSpeakerFilter->SetValue(pokey.IsSpeakerFilterEnabled());
	mpSerialNoise->SetValue(pokey.IsSerialNoiseEnabled());

	for (int i = 0; i < 4; i++) {
		mpPriCh[i]->SetValue(pokey.IsChannelEnabled(i));
		mpSecCh[i]->SetValue(pokey.IsSecondaryChannelEnabled(i));
	}

	UpdateVolumeLabels();
}

void ATAudioOptionsDialog::ApplyToState() {
	IATAudioOutput *audioOut = g_sim.GetAudioOutput();

	if (audioOut) {
		audioOut->SetVolume(mpMasterVolume->GetValue() / 100.0f);
		audioOut->SetMute(mpMute->GetValue());
		audioOut->SetMixLevel(kATAudioMix_Drive, mpDriveVolume->GetValue() / 100.0f);
		audioOut->SetMixLevel(kATAudioMix_Covox, mpCovoxVolume->GetValue() / 100.0f);
		audioOut->SetLatency(mpLatency->GetValue());
		audioOut->SetExtraBuffer(mpExtraBuffer->GetValue());
	}

	bool driveSounds = mpDriveSounds->GetValue();
	for (int i = 0; i < 15; ++i)
		g_sim.GetDiskInterface(i).SetDriveSoundsEnabled(driveSounds);

	g_sim.SetDualPokeysEnabled(mpDualPokey->GetValue());

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetStereoAsMonoEnabled(mpStereoMono->GetValue());
	pokey.SetNonlinearMixingEnabled(mpNonlinearMix->GetValue());
	pokey.SetSpeakerFilterEnabled(mpSpeakerFilter->GetValue());
	pokey.SetSerialNoiseEnabled(mpSerialNoise->GetValue());

	for (int i = 0; i < 4; i++) {
		pokey.SetChannelEnabled(i, mpPriCh[i]->GetValue());
		pokey.SetSecondaryChannelEnabled(i, mpSecCh[i]->GetValue());
	}
}

void ATAudioOptionsDialog::OnOK(wxCommandEvent&) {
	ApplyToState();
	EndModal(wxID_OK);
}

void ATAudioOptionsDialog::OnSliderUpdate(wxCommandEvent&) {
	UpdateVolumeLabels();
}

void ATAudioOptionsDialog::UpdateVolumeLabels() {
	mpMasterLabel->SetLabel(wxString::Format("%d%%", mpMasterVolume->GetValue()));
	mpDriveLabel->SetLabel(wxString::Format("%d%%", mpDriveVolume->GetValue()));
	mpCovoxLabel->SetLabel(wxString::Format("%d%%", mpCovoxVolume->GetValue()));
	mpLatencyLabel->SetLabel(wxString::Format("%d ms", mpLatency->GetValue()));
	mpExtraBufferLabel->SetLabel(wxString::Format("%d ms", mpExtraBuffer->GetValue()));
}

} // anonymous namespace

void ATShowAudioOptionsDialog(wxWindow *parent) {
	ATAudioOptionsDialog dlg(parent);
	dlg.ShowModal();
}
