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
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/statbox.h>
#include <wx/msgdlg.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include <vd2/system/filesys.h>
#include <vd2/system/text.h>

#include "simulator.h"
#include "cassette.h"

extern ATSimulator g_sim;
void ATImGuiShowToast(const char *message);

namespace {

static const char *kTurboModeNames[] = {
	"None",
	"Command Control",
	"Proceed Sense",
	"Interrupt Sense",
	"KSO Turbo 2000",
	"Turbo D",
	"Data Control",
	"Always",
};

static const char *kPolarityNames[] = {
	"Normal",
	"Inverted",
};

static const char *kDirectSenseNames[] = {
	"Normal",
	"Low Speed",
	"High Speed",
	"Max Speed",
};

static const char *kTurboDecodeNames[] = {
	"Slope (No Filter)",
	"Slope (Filter)",
	"Peak (Filter)",
	"Peak (Balance Lo-Hi)",
	"Peak (Balance Hi-Lo)",
};

static void FormatTapeTime(char *buf, size_t bufLen, float seconds) {
	int totalSec = (int)seconds;
	int min = totalSec / 60;
	int sec = totalSec % 60;
	int ms = (int)((seconds - totalSec) * 10.0f);
	snprintf(buf, bufLen, "%d:%02d.%d", min, sec, ms);
}

class ATCassetteControlDialog : public wxDialog {
public:
	ATCassetteControlDialog(wxWindow *parent);

private:
	void OnTimer(wxTimerEvent& event);
	void OnLoadTape(wxCommandEvent& event);
	void OnNewTape(wxCommandEvent& event);
	void OnUnload(wxCommandEvent& event);
	void OnRewind(wxCommandEvent& event);
	void OnPlay(wxCommandEvent& event);
	void OnRecord(wxCommandEvent& event);
	void OnStop(wxCommandEvent& event);
	void OnSkipBack(wxCommandEvent& event);
	void OnSkipFwd(wxCommandEvent& event);
	void OnApplyOptions(wxCommandEvent& event);
	void OnClose(wxCommandEvent& event);
	void UpdateDisplay();

	// Display
	wxStaticText *mpTapeInfo = nullptr;
	wxStaticText *mpPositionText = nullptr;
	wxSlider *mpPositionSlider = nullptr;
	wxStaticText *mpStatusText = nullptr;

	// Transport buttons
	wxButton *mpBtnRewind = nullptr;
	wxButton *mpBtnPlay = nullptr;
	wxButton *mpBtnRecord = nullptr;
	wxButton *mpBtnStop = nullptr;
	wxButton *mpBtnSkipBack = nullptr;
	wxButton *mpBtnSkipFwd = nullptr;

	// Options
	wxChoice *mpTurboMode = nullptr;
	wxChoice *mpPolarity = nullptr;
	wxChoice *mpDirectSense = nullptr;
	wxChoice *mpTurboDecoder = nullptr;
	wxCheckBox *mpAutoRewind = nullptr;
	wxCheckBox *mpLoadAsAudio = nullptr;
	wxCheckBox *mpRandomStart = nullptr;
	wxCheckBox *mpVBIAvoidance = nullptr;
	wxCheckBox *mpFSKCompensation = nullptr;
	wxCheckBox *mpCrosstalkReduction = nullptr;
	wxCheckBox *mpCassetteSIOPatch = nullptr;
	wxCheckBox *mpAutoBootBASIC = nullptr;

	wxTimer mUpdateTimer;
	bool mUpdatingSlider = false;

	enum { ID_TIMER = 3000, ID_LOAD, ID_NEW, ID_UNLOAD, ID_RWD, ID_PLAY, ID_REC,
	       ID_STOP_BTN, ID_SKIPB, ID_SKIPF, ID_APPLY_OPTS };
};

ATCassetteControlDialog::ATCassetteControlDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Cassette Control", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, mUpdateTimer(this, ID_TIMER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// File operations
	wxBoxSizer *fileRow = new wxBoxSizer(wxHORIZONTAL);
	fileRow->Add(new wxButton(this, ID_LOAD, "Load..."), 0, wxRIGHT, 3);
	fileRow->Add(new wxButton(this, ID_NEW, "New"), 0, wxRIGHT, 3);
	fileRow->Add(new wxButton(this, ID_UNLOAD, "Unload"), 0, wxRIGHT, 3);
	topSizer->Add(fileRow, 0, wxALL, 5);

	// Tape info
	mpTapeInfo = new wxStaticText(this, wxID_ANY, "No tape loaded");
	topSizer->Add(mpTapeInfo, 0, wxLEFT | wxRIGHT, 5);

	// Position
	mpPositionText = new wxStaticText(this, wxID_ANY, "Position: 0:00.0 / 0:00.0");
	topSizer->Add(mpPositionText, 0, wxALL, 5);
	mpPositionSlider = new wxSlider(this, wxID_ANY, 0, 0, 1000);
	topSizer->Add(mpPositionSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

	// Transport controls
	wxBoxSizer *transportRow = new wxBoxSizer(wxHORIZONTAL);
	mpBtnRewind = new wxButton(this, ID_RWD, "Rewind", wxDefaultPosition, wxSize(65, -1));
	mpBtnPlay = new wxButton(this, ID_PLAY, "Play", wxDefaultPosition, wxSize(55, -1));
	mpBtnRecord = new wxButton(this, ID_REC, "Record", wxDefaultPosition, wxSize(65, -1));
	mpBtnStop = new wxButton(this, ID_STOP_BTN, "Stop", wxDefaultPosition, wxSize(55, -1));
	mpBtnSkipBack = new wxButton(this, ID_SKIPB, "-10s", wxDefaultPosition, wxSize(45, -1));
	mpBtnSkipFwd = new wxButton(this, ID_SKIPF, "+10s", wxDefaultPosition, wxSize(45, -1));
	transportRow->Add(mpBtnRewind, 0, wxRIGHT, 3);
	transportRow->Add(mpBtnPlay, 0, wxRIGHT, 3);
	transportRow->Add(mpBtnRecord, 0, wxRIGHT, 3);
	transportRow->Add(mpBtnStop, 0, wxRIGHT, 3);
	transportRow->Add(mpBtnSkipBack, 0, wxRIGHT, 3);
	transportRow->Add(mpBtnSkipFwd, 0);
	topSizer->Add(transportRow, 0, wxALL, 5);

	// Status
	mpStatusText = new wxStaticText(this, wxID_ANY, "STOP");
	topSizer->Add(mpStatusText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// Options section
	wxStaticBoxSizer *optBox = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
	wxWindow *op = optBox->GetStaticBox();

	wxFlexGridSizer *optGrid = new wxFlexGridSizer(2, 5, 10);
	optGrid->AddGrowableCol(1, 1);

	optGrid->Add(new wxStaticText(op, wxID_ANY, "Turbo Mode:"), 0, wxALIGN_CENTER_VERTICAL);
	mpTurboMode = new wxChoice(op, wxID_ANY);
	for (auto name : kTurboModeNames) mpTurboMode->Append(name);
	optGrid->Add(mpTurboMode, 1, wxEXPAND);

	optGrid->Add(new wxStaticText(op, wxID_ANY, "Polarity:"), 0, wxALIGN_CENTER_VERTICAL);
	mpPolarity = new wxChoice(op, wxID_ANY);
	for (auto name : kPolarityNames) mpPolarity->Append(name);
	optGrid->Add(mpPolarity, 1, wxEXPAND);

	optGrid->Add(new wxStaticText(op, wxID_ANY, "Direct Sense:"), 0, wxALIGN_CENTER_VERTICAL);
	mpDirectSense = new wxChoice(op, wxID_ANY);
	for (auto name : kDirectSenseNames) mpDirectSense->Append(name);
	optGrid->Add(mpDirectSense, 1, wxEXPAND);

	optGrid->Add(new wxStaticText(op, wxID_ANY, "Turbo Decoder:"), 0, wxALIGN_CENTER_VERTICAL);
	mpTurboDecoder = new wxChoice(op, wxID_ANY);
	for (auto name : kTurboDecodeNames) mpTurboDecoder->Append(name);
	optGrid->Add(mpTurboDecoder, 1, wxEXPAND);

	optBox->Add(optGrid, 0, wxEXPAND | wxALL, 3);

	mpAutoRewind = new wxCheckBox(op, wxID_ANY, "Auto-rewind on load");
	mpLoadAsAudio = new wxCheckBox(op, wxID_ANY, "Load data as audio");
	mpRandomStart = new wxCheckBox(op, wxID_ANY, "Randomized start position");
	mpVBIAvoidance = new wxCheckBox(op, wxID_ANY, "VBI avoidance");
	mpFSKCompensation = new wxCheckBox(op, wxID_ANY, "FSK speed compensation");
	mpCrosstalkReduction = new wxCheckBox(op, wxID_ANY, "Crosstalk reduction");
	mpCassetteSIOPatch = new wxCheckBox(op, wxID_ANY, "Cassette SIO patch");
	mpAutoBootBASIC = new wxCheckBox(op, wxID_ANY, "Auto-boot with BASIC");

	optBox->Add(mpAutoRewind, 0, wxALL, 2);
	optBox->Add(mpLoadAsAudio, 0, wxALL, 2);
	optBox->Add(mpRandomStart, 0, wxALL, 2);
	optBox->Add(mpVBIAvoidance, 0, wxALL, 2);
	optBox->Add(mpFSKCompensation, 0, wxALL, 2);
	optBox->Add(mpCrosstalkReduction, 0, wxALL, 2);
	optBox->Add(mpCassetteSIOPatch, 0, wxALL, 2);
	optBox->Add(mpAutoBootBASIC, 0, wxALL, 2);

	topSizer->Add(optBox, 0, wxEXPAND | wxALL, 5);

	// Close button
	topSizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 5);

	SetSizerAndFit(topSizer);

	// Populate options from state
	ATCassetteEmulator& cas = g_sim.GetCassette();
	mpTurboMode->SetSelection((int)cas.GetTurboMode());
	mpPolarity->SetSelection((int)cas.GetPolarityMode());
	mpDirectSense->SetSelection((int)cas.GetDirectSenseMode());
	mpTurboDecoder->SetSelection((int)cas.GetTurboDecodeAlgorithm());
	mpAutoRewind->SetValue(cas.IsAutoRewindEnabled());
	mpLoadAsAudio->SetValue(cas.IsLoadDataAsAudioEnabled());
	mpRandomStart->SetValue(g_sim.IsCassetteRandomizedStartEnabled());
	mpVBIAvoidance->SetValue(cas.IsVBIAvoidanceEnabled());
	mpFSKCompensation->SetValue(cas.GetFSKSpeedCompensationEnabled());
	mpCrosstalkReduction->SetValue(cas.GetCrosstalkReductionEnabled());
	mpCassetteSIOPatch->SetValue(g_sim.IsCassetteSIOPatchEnabled());
	mpAutoBootBASIC->SetValue(g_sim.IsCassetteAutoBasicBootEnabled());

	// Bind events
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnLoadTape, this, ID_LOAD);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnNewTape, this, ID_NEW);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnUnload, this, ID_UNLOAD);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnRewind, this, ID_RWD);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnPlay, this, ID_PLAY);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnRecord, this, ID_REC);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnStop, this, ID_STOP_BTN);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnSkipBack, this, ID_SKIPB);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnSkipFwd, this, ID_SKIPF);
	Bind(wxEVT_BUTTON, &ATCassetteControlDialog::OnClose, this, wxID_CLOSE);
	Bind(wxEVT_TIMER, &ATCassetteControlDialog::OnTimer, this, ID_TIMER);

	// Apply options on any checkbox/choice change
	Bind(wxEVT_CHECKBOX, &ATCassetteControlDialog::OnApplyOptions, this);
	Bind(wxEVT_CHOICE, &ATCassetteControlDialog::OnApplyOptions, this);

	UpdateDisplay();
	mUpdateTimer.Start(200);
}

void ATCassetteControlDialog::OnTimer(wxTimerEvent&) {
	UpdateDisplay();
}

void ATCassetteControlDialog::UpdateDisplay() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	bool loaded = cas.IsLoaded();

	// Tape info
	if (loaded) {
		VDStringA info = VDTextWToU8(VDStringW(cas.GetPath()));
		if (info.empty())
			info = "[new tape]";
		else
			info = VDTextWToU8(VDStringW(VDFileSplitPath(cas.GetPath())));
		if (cas.IsImageDirty())
			info += " (modified)";
		mpTapeInfo->SetLabel(info.c_str());
	} else {
		mpTapeInfo->SetLabel("No tape loaded");
	}

	// Position
	if (loaded) {
		char posBuf[32], lenBuf[32];
		FormatTapeTime(posBuf, sizeof(posBuf), cas.GetPosition());
		FormatTapeTime(lenBuf, sizeof(lenBuf), cas.GetLength());
		mpPositionText->SetLabel(wxString::Format("Position: %s / %s", posBuf, lenBuf));

		float len = cas.GetLength();
		if (len > 0 && !mUpdatingSlider) {
			int sliderPos = (int)(cas.GetPosition() / len * 1000.0f);
			mpPositionSlider->SetValue(sliderPos);
		}
	} else {
		mpPositionText->SetLabel("Position: --");
		mpPositionSlider->SetValue(0);
	}

	// Status
	wxString status;
	if (!loaded)
		status = "NO TAPE";
	else if (cas.IsPlayEnabled() && !cas.IsPaused())
		status = "PLAY";
	else if (cas.IsRecordEnabled() && !cas.IsPaused())
		status = "REC";
	else if (cas.IsPaused())
		status = "PAUSED";
	else
		status = "STOP";

	if (loaded && cas.IsMotorRunning())
		status += "  MOTOR";

	mpStatusText->SetLabel(status);

	// Button states
	mpBtnRewind->Enable(loaded);
	mpBtnPlay->Enable(loaded);
	mpBtnRecord->Enable(loaded);
	mpBtnStop->Enable(loaded && !cas.IsStopped());
	mpBtnSkipBack->Enable(loaded);
	mpBtnSkipFwd->Enable(loaded);
}

void ATCassetteControlDialog::OnLoadTape(wxCommandEvent&) {
	wxFileDialog dlg(this, "Load Tape Image", "", "",
		"Cassette Images (*.cas;*.wav)|*.cas;*.wav|All Files (*.*)|*.*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		try {
			VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str()));
			g_sim.GetCassette().Load(path.c_str());
			ATImGuiShowToast("Tape loaded");
		} catch (const MyError& e) {
			wxMessageBox(e.c_str(), "Error", wxOK | wxICON_ERROR, this);
		}
	}
}

void ATCassetteControlDialog::OnNewTape(wxCommandEvent&) {
	g_sim.GetCassette().LoadNew();
}

void ATCassetteControlDialog::OnUnload(wxCommandEvent&) {
	g_sim.GetCassette().Unload();
}

void ATCassetteControlDialog::OnRewind(wxCommandEvent&) {
	g_sim.GetCassette().RewindToStart();
}

void ATCassetteControlDialog::OnPlay(wxCommandEvent&) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	if (cas.IsPlayEnabled() || cas.IsRecordEnabled()) {
		cas.SetPaused(!cas.IsPaused());
	} else {
		cas.Play();
	}
}

void ATCassetteControlDialog::OnRecord(wxCommandEvent&) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	if (cas.IsRecordEnabled()) {
		cas.SetPaused(!cas.IsPaused());
	} else {
		cas.Record();
	}
}

void ATCassetteControlDialog::OnStop(wxCommandEvent&) {
	g_sim.GetCassette().Stop();
}

void ATCassetteControlDialog::OnSkipBack(wxCommandEvent&) {
	g_sim.GetCassette().SkipBackward(10.0f);
}

void ATCassetteControlDialog::OnSkipFwd(wxCommandEvent&) {
	g_sim.GetCassette().SkipForward(10.0f);
}

void ATCassetteControlDialog::OnApplyOptions(wxCommandEvent&) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	cas.SetTurboMode((ATCassetteTurboMode)mpTurboMode->GetSelection());
	cas.SetPolarityMode((ATCassettePolarityMode)mpPolarity->GetSelection());
	cas.SetDirectSenseMode((ATCassetteDirectSenseMode)mpDirectSense->GetSelection());
	cas.SetTurboDecodeAlgorithm((ATCassetteTurboDecodeAlgorithm)mpTurboDecoder->GetSelection());
	cas.SetAutoRewindEnabled(mpAutoRewind->GetValue());
	cas.SetLoadDataAsAudioEnable(mpLoadAsAudio->GetValue());
	g_sim.SetCassetteRandomizedStartEnabled(mpRandomStart->GetValue());
	cas.SetVBIAvoidanceEnabled(mpVBIAvoidance->GetValue());
	cas.SetFSKSpeedCompensationEnabled(mpFSKCompensation->GetValue());
	cas.SetCrosstalkReductionEnabled(mpCrosstalkReduction->GetValue());
	g_sim.SetCassetteSIOPatchEnabled(mpCassetteSIOPatch->GetValue());
	g_sim.SetCassetteAutoBasicBootEnabled(mpAutoBootBASIC->GetValue());
}

void ATCassetteControlDialog::OnClose(wxCommandEvent&) {
	mUpdateTimer.Stop();
	EndModal(wxID_CLOSE);
}

} // anonymous namespace

void ATShowCassetteControlDialog(wxWindow *parent) {
	ATCassetteControlDialog dlg(parent);
	dlg.ShowModal();
}
