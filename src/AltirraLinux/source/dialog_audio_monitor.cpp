//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>

#include <wx/frame.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/panel.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>

#include <cmath>
#include <vector>

#include <cstring>

#include <vd2/system/vdtypes.h>
#include <at/ataudio/pokey.h>
#include <at/ataudio/audiooutput.h>

#include "simulator.h"
#include "audiomonitor.h"
#include "emulator_imgui.h"

#include "dialogs_wx.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////
// Scope mix tap — captures post-mix audio (POKEY + Covox + all sources)
///////////////////////////////////////////////////////////////////////////

static struct ScopeMixTapState {
	std::vector<float> mLeft;
	std::vector<float> mRight;
	uint32 mMaxSamples = 0;
	uint32 mNumSamples = 0;
	bool mbStereo = false;

	void Resize(uint32 n) {
		mLeft.resize(n);
		mRight.resize(n);
		mMaxSamples = n;
		mNumSamples = 0;
	}
} s_scopeMixTap;

static void ScopeMixTapCallback(void *ctx, const float *left, const float *right, uint32 count) {
	auto& tap = s_scopeMixTap;
	if (tap.mNumSamples < tap.mMaxSamples) {
		uint32 n = std::min(count, tap.mMaxSamples - tap.mNumSamples);
		memcpy(tap.mLeft.data() + tap.mNumSamples, left, n * sizeof(float));
		if (right) {
			memcpy(tap.mRight.data() + tap.mNumSamples, right, n * sizeof(float));
			tap.mbStereo = true;
		}
		tap.mNumSamples += n;
	}
}

///////////////////////////////////////////////////////////////////////////
// Waveform Panel — custom drawing for per-channel POKEY waveforms
///////////////////////////////////////////////////////////////////////////

class ATAudioMonitorPanel : public wxPanel {
public:
	ATAudioMonitorPanel(wxWindow *parent, int pokeyIdx);

private:
	void OnPaint(wxPaintEvent& event);
	void OnTimer(wxTimerEvent& event);

	int mPokeyIdx;
	wxTimer mRefreshTimer;
	enum { ID_REFRESH = 4000 };
};

ATAudioMonitorPanel::ATAudioMonitorPanel(wxWindow *parent, int pokeyIdx)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(400, 250))
	, mPokeyIdx(pokeyIdx)
	, mRefreshTimer(this, ID_REFRESH)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &ATAudioMonitorPanel::OnPaint, this);
	Bind(wxEVT_TIMER, &ATAudioMonitorPanel::OnTimer, this, ID_REFRESH);
	mRefreshTimer.Start(33);  // ~30 Hz refresh
}

void ATAudioMonitorPanel::OnTimer(wxTimerEvent&) {
	Refresh(false);
}

void ATAudioMonitorPanel::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	wxSize sz = GetClientSize();
	int w = sz.GetWidth();
	int h = sz.GetHeight();

	dc.SetBackground(*wxBLACK_BRUSH);
	dc.Clear();

	ATImGuiIndicatorState& ind = ATImGuiGetIndicatorState();
	ATAudioMonitor *mon = ind.mpAudioMonitors[mPokeyIdx];
	if (!mon) {
		dc.SetTextForeground(*wxWHITE);
		dc.DrawText("Audio monitor not active. Enable via simulator.", 10, h / 2 - 8);
		return;
	}

	ATPokeyAudioLog *log = nullptr;
	ATPokeyRegisterState *rstate = nullptr;
	uint8 chanMask = mon->Update(&log, &rstate);

	if (!log || log->mLastFrameSampleCount == 0)
		return;

	uint32 sampleCount = log->mLastFrameSampleCount;
	float fullScale = (float)log->mFullScaleValue;
	if (fullScale < 1.0f) fullScale = 1.0f;

	// Layout: 4 channels stacked vertically
	int channelH = h / 4;

	for (int ch = 0; ch < 4; ++ch) {
		int yBase = ch * channelH;
		bool active = (chanMask & (1 << ch)) != 0;

		// Channel label
		dc.SetTextForeground(active ? wxColour(0, 200, 0) : wxColour(80, 80, 80));
		dc.SetFont(wxFont(8, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

		// Decode register info
		uint8 audf = rstate->mReg[ch * 2];
		uint8 audc = rstate->mReg[ch * 2 + 1];
		int vol = audc & 0x0F;

		dc.DrawText(wxString::Format("Ch%d  AUDF=%02X AUDC=%02X V=%d", ch + 1, audf, audc, vol),
			4, yBase + 2);

		// Volume bar (left side, 4px wide)
		if (vol > 0) {
			int volH = (channelH - 20) * vol / 15;
			dc.SetBrush(wxBrush(wxColour(255, 255, 0)));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle(4, yBase + channelH - volH - 2, 4, volH);
		}

		// Waveform area
		int waveX = 14;
		int waveW = w - waveX - 4;
		int waveY = yBase + 16;
		int waveH = channelH - 20;

		if (waveW < 2 || waveH < 2)
			continue;

		// Draw waveform
		if (sampleCount > 1 && active) {
			dc.SetPen(wxPen(wxColour(0, 255, 0), 1));

			float xStep = (float)waveW / (float)(sampleCount - 1);
			float yScale = -(float)(waveH - 2) / fullScale;
			float yMid = (float)(waveY + waveH / 2);

			int prevX = waveX;
			int prevY = (int)(log->mpStates[0].mChannelOutputs[ch] * yScale + yMid);

			for (uint32 s = 1; s < sampleCount; ++s) {
				int px = waveX + (int)(s * xStep);
				int py = (int)(log->mpStates[s].mChannelOutputs[ch] * yScale + yMid);
				dc.DrawLine(prevX, prevY, px, py);
				prevX = px;
				prevY = py;
			}
		}

		// Separator line
		if (ch < 3) {
			dc.SetPen(wxPen(wxColour(50, 50, 50), 1));
			dc.DrawLine(0, yBase + channelH, w, yBase + channelH);
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// Audio Scope Panel — mixed waveform scope display
///////////////////////////////////////////////////////////////////////////

static constexpr float kScopeUsPerDiv[] = {
	100.0f, 200.0f, 500.0f,
	1000.0f, 2000.0f, 5000.0f,
	10000.0f, 20000.0f, 50000.0f,
	100000.0f, 200000.0f,
};
static const int kScopeRateCount = (int)(sizeof(kScopeUsPerDiv) / sizeof(kScopeUsPerDiv[0]));

class ATAudioScopePanel : public wxPanel {
public:
	ATAudioScopePanel(wxWindow *parent);

	void ZoomIn();
	void ZoomOut();
	int GetRateIndex() const { return mRateIndex; }

private:
	void OnPaint(wxPaintEvent& event);
	void OnTimer(wxTimerEvent& event);
	void UpdateSampleCounts();

	int mRateIndex = 3;
	uint32 mSamplesRequested = 0;
	uint32 mSampleScale = 1;
	std::vector<float> mWaveforms[2];   // Left (red), Right (green)

	wxTimer mRefreshTimer;
	enum { ID_REFRESH = 4100 };
};

ATAudioScopePanel::ATAudioScopePanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(500, 200))
	, mRefreshTimer(this, ID_REFRESH)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &ATAudioScopePanel::OnPaint, this);
	Bind(wxEVT_TIMER, &ATAudioScopePanel::OnTimer, this, ID_REFRESH);
	mRefreshTimer.Start(33);
	UpdateSampleCounts();
}

void ATAudioScopePanel::ZoomIn() {
	if (mRateIndex > 0) {
		--mRateIndex;
		UpdateSampleCounts();
	}
}

void ATAudioScopePanel::ZoomOut() {
	if (mRateIndex < kScopeRateCount - 1) {
		++mRateIndex;
		UpdateSampleCounts();
	}
}

void ATAudioScopePanel::UpdateSampleCounts() {
	float usPerDiv = kScopeUsPerDiv[mRateIndex];
	float usPerView = usPerDiv * 10.0f;
	float secsPerView = usPerView / 1000000.0f;
	float samplesPerSec = 63920.8f;
	float samplesPerViewF = samplesPerSec * secsPerView;

	uint32 n = (uint32)ceilf(samplesPerViewF);
	mSamplesRequested = n;

	s_scopeMixTap.Resize(n);
}

void ATAudioScopePanel::OnTimer(wxTimerEvent&) {
	Refresh(false);
}

void ATAudioScopePanel::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	wxSize sz = GetClientSize();
	int w = sz.GetWidth();
	int h = sz.GetHeight();

	dc.SetBackground(*wxBLACK_BRUSH);
	dc.Clear();

	if (s_scopeMixTap.mMaxSamples == 0) {
		dc.SetTextForeground(*wxWHITE);
		dc.DrawText("Audio scope not active.", 10, h / 2 - 8);
		return;
	}

	// Draw grid
	wxPen gridPen(wxColour(128, 128, 128, 128), 1);
	dc.SetPen(gridPen);

	for (int i = 1; i < 10; ++i) {
		int x = w * i / 10;
		dc.DrawLine(x, 0, x, h);
	}

	int ymid = h / 2;
	dc.DrawLine(0, ymid, w, ymid);

	// Downsample when buffer is full
	bool tapReady = s_scopeMixTap.mNumSamples >= s_scopeMixTap.mMaxSamples;

	if (tapReady) {
		sint32 n = (sint32)mSamplesRequested;
		mSampleScale = 1;

		while (mSampleScale < 64 && n > (sint32)w * 2) {
			mSampleScale += mSampleScale;
			n >>= 1;
		}

		uint32 step = mSampleScale;
		float scale = 1.0f / (float)step;

		// Left channel (always present)
		mWaveforms[0].resize(n);
		{
			const float *src = s_scopeMixTap.mLeft.data();
			float *dst = mWaveforms[0].data();
			for (sint32 j = 0; j < n; ++j) {
				float v = 0;
				for (uint32 k = 0; k < step; ++k)
					v += src[k];
				dst[j] = v * scale;
				src += step;
			}
		}

		// Right channel (only when stereo mixing is active)
		if (s_scopeMixTap.mbStereo) {
			mWaveforms[1].resize(n);
			const float *src = s_scopeMixTap.mRight.data();
			float *dst = mWaveforms[1].data();
			for (sint32 j = 0; j < n; ++j) {
				float v = 0;
				for (uint32 k = 0; k < step; ++k)
					v += src[k];
				dst[j] = v * scale;
				src += step;
			}
		} else {
			mWaveforms[1].clear();
		}

		s_scopeMixTap.mNumSamples = 0;
		s_scopeMixTap.mbStereo = false;
	}

	// Draw waveforms: Red=Left, Green=Right
	float usPerDiv = kScopeUsPerDiv[mRateIndex];
	float usPerView = usPerDiv * 10.0f;
	float secsPerView = usPerView / 1000000.0f;
	float samplesPerSec = 63920.8f;
	float samplesPerViewF = samplesPerSec * secsPerView;

	float yscale = -(float)(h / 2);
	float yoffset = (float)ymid;
	float xscale = (float)w / samplesPerViewF * (float)mSampleScale;

	for (int i = 0; i < 2; ++i) {
		const auto& wf = mWaveforms[i];
		if (wf.empty())
			continue;

		size_t n = wf.size();
		wxColour color = i ? wxColour(0, 180, 0) : wxColour(255, 0, 0);
		dc.SetPen(wxPen(color, 1));

		int prevX = (int)(0.5f * xscale);
		int prevY = (int)(wf[0] * yscale + yoffset);

		for (size_t j = 1; j < n; ++j) {
			int px = (int)((float)j * xscale + xscale * 0.5f);
			int py = (int)(wf[j] * yscale + yoffset);
			dc.DrawLine(prevX, prevY, px, py);
			prevX = px;
			prevY = py;
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// Audio Monitor Frame — POKEY channel waveforms (non-modal)
///////////////////////////////////////////////////////////////////////////

class ATAudioMonitorFrame : public wxFrame {
public:
	ATAudioMonitorFrame(wxWindow *parent);
	~ATAudioMonitorFrame();

private:
	void OnClose(wxCloseEvent& event);
	bool mWasMonitorEnabled = false;
};

static ATAudioMonitorFrame *spAudioMonitorFrame = nullptr;

ATAudioMonitorFrame::ATAudioMonitorFrame(wxWindow *parent)
	: wxFrame(parent, wxID_ANY, "Audio Monitor", wxDefaultPosition,
		wxSize(500, g_sim.IsDualPokeysEnabled() ? 600 : 350),
		wxDEFAULT_FRAME_STYLE)
{
	mWasMonitorEnabled = g_sim.IsAudioMonitorEnabled();
	g_sim.SetAudioMonitorEnabled(true);

	wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

	// POKEY 1 channels
	mainSizer->Add(new ATAudioMonitorPanel(this, 0), 1, wxEXPAND);

	// POKEY 2 if dual POKEYs
	if (g_sim.IsDualPokeysEnabled()) {
		mainSizer->Add(new wxStaticText(this, wxID_ANY, "POKEY 2:"), 0, wxLEFT | wxTOP, 4);
		mainSizer->Add(new ATAudioMonitorPanel(this, 1), 1, wxEXPAND);
	}

	SetSizer(mainSizer);

	Bind(wxEVT_CLOSE_WINDOW, &ATAudioMonitorFrame::OnClose, this);
}

ATAudioMonitorFrame::~ATAudioMonitorFrame() {
	spAudioMonitorFrame = nullptr;
}

void ATAudioMonitorFrame::OnClose(wxCloseEvent&) {
	g_sim.SetAudioMonitorEnabled(mWasMonitorEnabled);
	Destroy();
}

///////////////////////////////////////////////////////////////////////////
// Audio Scope Frame — mixed signal oscilloscope (non-modal)
///////////////////////////////////////////////////////////////////////////

class ATAudioScopeFrame : public wxFrame {
public:
	ATAudioScopeFrame(wxWindow *parent);
	~ATAudioScopeFrame();

private:
	void OnZoomIn(wxCommandEvent& event);
	void OnZoomOut(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);
	void UpdateTimebaseLabel();

	ATAudioScopePanel *mpScopePanel = nullptr;
	wxStaticText *mpTimebaseLabel = nullptr;
	bool mWasScopeEnabled = false;

	enum { ID_ZOOM_IN = 4200, ID_ZOOM_OUT };
};

static ATAudioScopeFrame *spAudioScopeFrame = nullptr;

ATAudioScopeFrame::ATAudioScopeFrame(wxWindow *parent)
	: wxFrame(parent, wxID_ANY, "Audio Scope", wxDefaultPosition, wxSize(600, 300),
		wxDEFAULT_FRAME_STYLE)
{
	mWasScopeEnabled = g_sim.IsAudioScopeEnabled();
	g_sim.SetAudioMonitorEnabled(true);
	g_sim.SetAudioScopeEnabled(true);

	// Install post-mix scope tap to capture Covox and other non-POKEY audio
	g_sim.GetAudioOutput()->SetScopeTap(ScopeMixTapCallback, nullptr);

	wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

	mpScopePanel = new ATAudioScopePanel(this);
	mainSizer->Add(mpScopePanel, 1, wxEXPAND);

	// Scope controls
	wxBoxSizer *ctrlRow = new wxBoxSizer(wxHORIZONTAL);
	ctrlRow->Add(new wxButton(this, ID_ZOOM_IN, "-", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 4);
	mpTimebaseLabel = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(120, -1), wxALIGN_CENTRE_HORIZONTAL);
	ctrlRow->Add(mpTimebaseLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	ctrlRow->Add(new wxButton(this, ID_ZOOM_OUT, "+", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
	ctrlRow->AddStretchSpacer();
	ctrlRow->Add(new wxStaticText(this, wxID_ANY, "Red=Left  Green=Right"), 0, wxALIGN_CENTER_VERTICAL);
	mainSizer->Add(ctrlRow, 0, wxEXPAND | wxALL, 4);

	SetSizer(mainSizer);

	Bind(wxEVT_BUTTON, &ATAudioScopeFrame::OnZoomIn, this, ID_ZOOM_IN);
	Bind(wxEVT_BUTTON, &ATAudioScopeFrame::OnZoomOut, this, ID_ZOOM_OUT);
	Bind(wxEVT_CLOSE_WINDOW, &ATAudioScopeFrame::OnClose, this);

	UpdateTimebaseLabel();
}

ATAudioScopeFrame::~ATAudioScopeFrame() {
	spAudioScopeFrame = nullptr;
}

void ATAudioScopeFrame::OnZoomIn(wxCommandEvent&) {
	mpScopePanel->ZoomIn();
	UpdateTimebaseLabel();
}

void ATAudioScopeFrame::OnZoomOut(wxCommandEvent&) {
	mpScopePanel->ZoomOut();
	UpdateTimebaseLabel();
}

void ATAudioScopeFrame::OnClose(wxCloseEvent&) {
	// Remove post-mix scope tap
	g_sim.GetAudioOutput()->SetScopeTap(nullptr, nullptr);

	// Only disable scope if the monitor window isn't also open
	if (!spAudioMonitorFrame)
		g_sim.SetAudioMonitorEnabled(false);
	g_sim.SetAudioScopeEnabled(mWasScopeEnabled);
	Destroy();
}

void ATAudioScopeFrame::UpdateTimebaseLabel() {
	float usPerDiv = kScopeUsPerDiv[mpScopePanel->GetRateIndex()];
	wxString label;
	if (usPerDiv < 1000.0f)
		label = wxString::Format("%.0f us/div", usPerDiv);
	else
		label = wxString::Format("%.0f ms/div", usPerDiv / 1000.0f);
	mpTimebaseLabel->SetLabel(label);
}

///////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////

void ATShowAudioMonitorWindow(wxWindow *parent) {
	if (spAudioMonitorFrame) {
		spAudioMonitorFrame->Raise();
		return;
	}
	spAudioMonitorFrame = new ATAudioMonitorFrame(parent);
	spAudioMonitorFrame->Show();
}

void ATShowAudioScopeWindow(wxWindow *parent) {
	if (spAudioScopeFrame) {
		spAudioScopeFrame->Raise();
		return;
	}
	spAudioScopeFrame = new ATAudioScopeFrame(parent);
	spAudioScopeFrame->Show();
}

void ATCloseAudioWindows() {
	if (spAudioMonitorFrame) {
		spAudioMonitorFrame->Destroy();
		spAudioMonitorFrame = nullptr;
	}
	if (spAudioScopeFrame) {
		spAudioScopeFrame->Destroy();
		spAudioScopeFrame = nullptr;
	}
}
