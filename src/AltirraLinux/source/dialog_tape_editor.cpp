//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/slider.h>
#include <wx/panel.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/msgdlg.h>

#include <cmath>
#include <vector>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <at/atio/cassetteimage.h>

#include "simulator.h"
#include "cassette.h"

#include "dialogs_wx.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////
// Tape Waveform Panel — custom waveform rendering
///////////////////////////////////////////////////////////////////////////

class ATTapeWaveformPanel : public wxPanel {
public:
	ATTapeWaveformPanel(wxWindow *parent);

	void SetView(float start, float len);
	float GetViewStart() const { return mViewStart; }
	float GetViewLen() const { return mViewLen; }

private:
	void OnPaint(wxPaintEvent& event);
	void OnMouseWheel(wxMouseEvent& event);

	float mViewStart = 0.0f;
	float mViewLen = 1.0f;
};

ATTapeWaveformPanel::ATTapeWaveformPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(600, 250))
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &ATTapeWaveformPanel::OnPaint, this);
	Bind(wxEVT_MOUSEWHEEL, &ATTapeWaveformPanel::OnMouseWheel, this);
}

void ATTapeWaveformPanel::SetView(float start, float len) {
	mViewStart = start;
	mViewLen = len;
	Refresh(false);
}

void ATTapeWaveformPanel::OnMouseWheel(wxMouseEvent& event) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	float totalLen = cas.GetLength();
	if (totalLen <= 0.0f)
		return;

	int rotation = event.GetWheelRotation();
	if (rotation == 0)
		return;

	float factor = rotation > 0 ? 0.8f : 1.25f;
	wxSize sz = GetClientSize();
	float mouseX = (float)event.GetX() / (float)sz.GetWidth();
	float center = mViewStart + mViewLen * mouseX;
	mViewLen *= factor;
	if (mViewLen < 0.001f) mViewLen = 0.001f;
	if (mViewLen > totalLen) mViewLen = totalLen;
	mViewStart = center - mViewLen * mouseX;
	if (mViewStart < 0.0f) mViewStart = 0.0f;
	if (mViewStart + mViewLen > totalLen)
		mViewStart = totalLen > mViewLen ? totalLen - mViewLen : 0.0f;

	Refresh(false);

	// Notify parent to update controls
	wxCommandEvent evt(wxEVT_COMMAND_SLIDER_UPDATED, GetId());
	evt.SetEventObject(this);
	GetParent()->GetEventHandler()->ProcessEvent(evt);
}

void ATTapeWaveformPanel::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	wxSize sz = GetClientSize();
	int w = sz.GetWidth();
	int h = sz.GetHeight();

	// Dark background
	dc.SetBackground(wxBrush(wxColour(20, 20, 30)));
	dc.Clear();

	ATCassetteEmulator& cas = g_sim.GetCassette();
	IATCassetteImage *img = cas.GetImage();
	if (!img)
		return;

	float totalLen = cas.GetLength();
	if (totalLen <= 0.0f)
		return;

	// Center line
	int centerY = h / 2;
	dc.SetPen(wxPen(wxColour(60, 60, 80), 1));
	dc.DrawLine(0, centerY, w, centerY);

	// Read peak data (one sample per pixel)
	int sampleCount = w;
	if (sampleCount <= 0)
		return;

	vdfastvector<float> dataPeaks(sampleCount * 2);
	vdfastvector<float> audioPeaks(sampleCount * 2);
	float dt = mViewLen / (float)sampleCount;

	img->ReadPeakMap(mViewStart, dt, sampleCount, dataPeaks.data(), audioPeaks.data());

	float halfH = (float)h * 0.45f;

	// Draw data track (green)
	dc.SetPen(wxPen(wxColour(80, 220, 80), 1));
	for (int i = 0; i < sampleCount; ++i) {
		float minV = dataPeaks[i * 2];
		float maxV = dataPeaks[i * 2 + 1];
		int y0 = centerY - (int)(maxV * halfH);
		int y1 = centerY - (int)(minV * halfH);
		if (y1 - y0 < 1) y1 = y0 + 1;
		dc.DrawLine(i, y0, i, y1);
	}

	// Draw audio track if present (red, semi-transparent effect via lighter color)
	if (img->IsAudioPresent()) {
		dc.SetPen(wxPen(wxColour(220, 80, 80), 1));
		for (int i = 0; i < sampleCount; ++i) {
			float minV = audioPeaks[i * 2];
			float maxV = audioPeaks[i * 2 + 1];
			int y0 = centerY - (int)(maxV * halfH);
			int y1 = centerY - (int)(minV * halfH);
			if (y1 - y0 < 1) y1 = y0 + 1;
			dc.DrawLine(i, y0, i, y1);
		}
	}

	// Draw current position marker (yellow)
	float curPos = cas.GetPosition();
	if (curPos >= mViewStart && curPos <= mViewStart + mViewLen) {
		float posX = (curPos - mViewStart) / mViewLen * (float)w;
		dc.SetPen(wxPen(wxColour(255, 255, 0), 2));
		dc.DrawLine((int)posX, 0, (int)posX, h);
	}
}

///////////////////////////////////////////////////////////////////////////
// Tape Editor Dialog
///////////////////////////////////////////////////////////////////////////

static const float kZoomLevels[] = { 0.01f, 0.05f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 30.0f };
static const char *kZoomLabels[] = { "10ms", "50ms", "100ms", "500ms", "1s", "5s", "10s", "30s" };
static const int kNumZoomLevels = 8;

class ATTapeEditorDialog : public wxDialog {
public:
	ATTapeEditorDialog(wxWindow *parent);

private:
	void OnZoom(wxCommandEvent& event);
	void OnGoToPosition(wxCommandEvent& event);
	void OnNavSlider(wxCommandEvent& event);
	void OnWaveformChanged(wxCommandEvent& event);
	void OnRefreshTimer(wxTimerEvent& event);

	void UpdateInfoBar();
	void UpdateSlider();

	ATTapeWaveformPanel *mpWaveform = nullptr;
	wxStaticText *mpInfoLabel = nullptr;
	wxSlider *mpNavSlider = nullptr;

	wxTimer mRefreshTimer;

	enum {
		ID_ZOOM_BASE = 5000,
		ID_GO_TO_POS = 5100,
		ID_NAV_SLIDER = 5101,
		ID_WAVEFORM = 5102,
		ID_REFRESH_TIMER = 5103,
	};
};

ATTapeEditorDialog::ATTapeEditorDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Tape Editor", wxDefaultPosition, wxSize(800, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, mRefreshTimer(this, ID_REFRESH_TIMER)
{
	ATCassetteEmulator& cas = g_sim.GetCassette();
	if (!cas.GetImage()) {
		wxMessageBox("No tape image loaded.", "Tape Editor", wxOK | wxICON_INFORMATION, parent);
		// Will show empty dialog; user can close it
	}

	wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

	// Info bar
	mpInfoLabel = new wxStaticText(this, wxID_ANY, "");
	wxBoxSizer *infoRow = new wxBoxSizer(wxHORIZONTAL);
	infoRow->Add(mpInfoLabel, 1, wxALIGN_CENTER_VERTICAL);
	infoRow->Add(new wxButton(this, ID_GO_TO_POS, "Go to Position"), 0, wxLEFT, 8);
	mainSizer->Add(infoRow, 0, wxEXPAND | wxALL, 8);

	// Zoom buttons
	wxBoxSizer *zoomRow = new wxBoxSizer(wxHORIZONTAL);
	zoomRow->Add(new wxStaticText(this, wxID_ANY, "View:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	for (int i = 0; i < kNumZoomLevels; ++i) {
		wxButton *btn = new wxButton(this, ID_ZOOM_BASE + i, kZoomLabels[i],
			wxDefaultPosition, wxSize(50, -1));
		zoomRow->Add(btn, 0, wxRIGHT, 2);
	}
	mainSizer->Add(zoomRow, 0, wxLEFT | wxRIGHT, 8);

	// Navigation slider
	mpNavSlider = new wxSlider(this, ID_NAV_SLIDER, 0, 0, 10000,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	mainSizer->Add(mpNavSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

	// Waveform panel
	mpWaveform = new ATTapeWaveformPanel(this);
	mpWaveform->SetId(ID_WAVEFORM);
	mainSizer->Add(mpWaveform, 1, wxEXPAND | wxALL, 8);

	// Close button
	mainSizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	SetSizer(mainSizer);

	// Bind events
	for (int i = 0; i < kNumZoomLevels; ++i)
		Bind(wxEVT_BUTTON, &ATTapeEditorDialog::OnZoom, this, ID_ZOOM_BASE + i);
	Bind(wxEVT_BUTTON, &ATTapeEditorDialog::OnGoToPosition, this, ID_GO_TO_POS);
	Bind(wxEVT_SLIDER, &ATTapeEditorDialog::OnNavSlider, this, ID_NAV_SLIDER);
	Bind(wxEVT_COMMAND_SLIDER_UPDATED, &ATTapeEditorDialog::OnWaveformChanged, this, ID_WAVEFORM);
	Bind(wxEVT_TIMER, &ATTapeEditorDialog::OnRefreshTimer, this, ID_REFRESH_TIMER);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);

	// Set initial view to show the entire tape (or 30s if very long)
	{
		ATCassetteEmulator& cas2 = g_sim.GetCassette();
		float totalLen = cas2.GetLength();
		if (totalLen > 0.0f) {
			float viewLen = totalLen < 30.0f ? totalLen : 30.0f;
			mpWaveform->SetView(0.0f, viewLen);
		}
	}

	UpdateInfoBar();
	UpdateSlider();

	// Periodic refresh for position marker
	mRefreshTimer.Start(200);
}

void ATTapeEditorDialog::OnZoom(wxCommandEvent& event) {
	int idx = event.GetId() - ID_ZOOM_BASE;
	if (idx >= 0 && idx < kNumZoomLevels) {
		ATCassetteEmulator& cas = g_sim.GetCassette();
		float totalLen = cas.GetLength();
		float center = mpWaveform->GetViewStart() + mpWaveform->GetViewLen() * 0.5f;
		float newLen = kZoomLevels[idx];
		float newStart = center - newLen * 0.5f;
		if (newStart < 0.0f) newStart = 0.0f;
		if (newStart + newLen > totalLen)
			newStart = totalLen > newLen ? totalLen - newLen : 0.0f;
		mpWaveform->SetView(newStart, newLen);
		UpdateSlider();
		UpdateInfoBar();
	}
}

void ATTapeEditorDialog::OnGoToPosition(wxCommandEvent&) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	float curPos = cas.GetPosition();
	float viewLen = mpWaveform->GetViewLen();
	float newStart = curPos - viewLen * 0.5f;
	float totalLen = cas.GetLength();
	if (newStart < 0.0f) newStart = 0.0f;
	if (newStart + viewLen > totalLen)
		newStart = totalLen > viewLen ? totalLen - viewLen : 0.0f;
	mpWaveform->SetView(newStart, viewLen);
	UpdateSlider();
	UpdateInfoBar();
}

void ATTapeEditorDialog::OnNavSlider(wxCommandEvent&) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	float totalLen = cas.GetLength();
	float viewLen = mpWaveform->GetViewLen();
	float maxStart = totalLen > viewLen ? totalLen - viewLen : 0.0f;
	float newStart = maxStart * (float)mpNavSlider->GetValue() / 10000.0f;
	mpWaveform->SetView(newStart, viewLen);
	UpdateInfoBar();
}

void ATTapeEditorDialog::OnWaveformChanged(wxCommandEvent&) {
	// Called when waveform panel zoomed via mouse wheel
	UpdateSlider();
	UpdateInfoBar();
}

void ATTapeEditorDialog::OnRefreshTimer(wxTimerEvent&) {
	UpdateInfoBar();
	mpWaveform->Refresh(false);  // Update position marker
}

void ATTapeEditorDialog::UpdateInfoBar() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	float totalLen = cas.GetLength();
	float curPos = cas.GetPosition();
	float viewStart = mpWaveform->GetViewStart();
	float viewEnd = viewStart + mpWaveform->GetViewLen();
	uint32 samples = cas.GetSampleLen();

	mpInfoLabel->SetLabel(wxString::Format(
		"Length: %.1f s  |  Playback: %.1f s  |  View: %.2f - %.2f s  |  Samples: %u",
		totalLen, curPos, viewStart, viewEnd, samples));
}

void ATTapeEditorDialog::UpdateSlider() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	float totalLen = cas.GetLength();
	float viewLen = mpWaveform->GetViewLen();
	float viewStart = mpWaveform->GetViewStart();
	float maxStart = totalLen > viewLen ? totalLen - viewLen : 0.0f;

	int sliderVal = maxStart > 0.0f ? (int)(viewStart / maxStart * 10000.0f) : 0;
	mpNavSlider->SetValue(sliderVal);
}

///////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////

void ATShowTapeEditorDialog(wxWindow *parent) {
	ATTapeEditorDialog dlg(parent);
	dlg.ShowModal();
}
