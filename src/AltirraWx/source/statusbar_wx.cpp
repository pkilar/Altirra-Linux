//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include "statusbar_wx.h"
#include <display_wx.h>

#include <wx/dcbuffer.h>

#include <vd2/system/filesys.h>
#include <vd2/system/text.h>

#include "simulator.h"
#include "cartridge.h"
#include "cassette.h"
#include "disk.h"
#include "uiaccessors.h"
#include <at/ataudio/audiooutput.h>
#include <emulator_imgui.h>

#include <SDL3/SDL.h>

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////
// Color palettes (matching ImGui status bar)
///////////////////////////////////////////////////////////////////////////

// Disk drive dim colors (D1-D8)
static const wxColour kDiskDim[8] = {
	wxColour(145, 161, 0),    // D1: dim yellow
	wxColour(212, 112, 64),   // D2: dim orange
	wxColour(212, 84, 207),   // D3: dim magenta
	wxColour(145, 102, 255),  // D4: dim blue
	wxColour(71, 150, 237),   // D5: dim cyan
	wxColour(54, 186, 97),    // D6: dim green
	wxColour(107, 179, 0),    // D7: dim lime
	wxColour(186, 135, 13),   // D8: dim brown
};

// Disk drive bright colors (active I/O)
static const wxColour kDiskBright[8] = {
	wxColour(255, 255, 102),  // D1: bright yellow
	wxColour(255, 232, 184),  // D2: bright orange
	wxColour(255, 204, 255),  // D3: bright magenta
	wxColour(255, 222, 255),  // D4: bright blue
	wxColour(191, 255, 255),  // D5: bright cyan
	wxColour(171, 255, 217),  // D6: bright green
	wxColour(227, 255, 112),  // D7: bright lime
	wxColour(255, 252, 133),  // D8: bright brown
};

static const wxColour kDiskIdle(128, 128, 128);
static const wxColour kDiskDirty(255, 204, 102);
static const wxColour kDiskError(255, 51, 51);

static const wxColour kColorDefault(204, 204, 204);
static const wxColour kColorHwMode(204, 204, 255);
static const wxColour kColorPaused(255, 255, 77);
static const wxColour kColorTurbo(255, 204, 51);
static const wxColour kColorSpeed(204, 204, 51);
static const wxColour kColorFps(51, 255, 51);
static const wxColour kColorMute(255, 128, 128);

static const wxColour kColorHBright(51, 255, 153);
static const wxColour kColorHDim(26, 153, 89);
static const wxColour kColorPCLBright(102, 204, 255);
static const wxColour kColorPCLDim(51, 128, 179);
static const wxColour kColorHDRead(128, 255, 128);
static const wxColour kColorHDWrite(255, 153, 51);
static const wxColour kColorFlash(255, 77, 26);
static const wxColour kColorCartActive(255, 255, 255);
static const wxColour kColorCartIdle(230, 179, 255);
static const wxColour kColorModem(128, 230, 255);

static const wxColour kColorTapePlaying(51, 255, 51);
static const wxColour kColorTapeRecording(255, 77, 77);
static const wxColour kColorTapeIdle(153, 153, 153);

static const wxColour kColorRecording(255, 51, 51);
static const wxColour kColorRecPaused(255, 153, 51);

///////////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ATStatusBar, wxPanel)
	EVT_PAINT(ATStatusBar::OnPaint)
	EVT_SIZE(ATStatusBar::OnSize)
	EVT_TIMER(ATStatusBar::kTimerId, ATStatusBar::OnTimer)
wxEND_EVENT_TABLE()

ATStatusBar::ATStatusBar(wxWindow *parent)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 24),
		wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE)
	, mRefreshTimer(this, kTimerId)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(wxColour(38, 38, 38));

	wxFont font(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	SetFont(font);

	mRefreshTimer.Start(kRefreshMs);
}

void ATStatusBar::TickFrame() {
	++mFrameCount;

	// Tick hold counters (same logic as ImGui: once per frame)
	auto& ind = ATImGuiGetIndicatorState();
	for (uint32_t f = ind.mStatusHoldFlags; f; f &= f - 1) {
		int idx = __builtin_ctz(f);
		if (idx < 17 && ind.mStatusHoldCounters[idx]) {
			if (!--ind.mStatusHoldCounters[idx])
				ind.mStatusHoldFlags &= ~(1u << idx);
		}
	}

	// Decay activity counters
	if (ind.mHReadCounter) --ind.mHReadCounter;
	if (ind.mHWriteCounter) --ind.mHWriteCounter;
	if (ind.mPCLinkReadCounter) --ind.mPCLinkReadCounter;
	if (ind.mPCLinkWriteCounter) --ind.mPCLinkWriteCounter;
	if (ind.mFlashWriteCounter) --ind.mFlashWriteCounter;
	if (ind.mCartridgeActivityCounter) --ind.mCartridgeActivityCounter;
	if (ind.mHardDiskCounter) {
		if (!--ind.mHardDiskCounter) {
			ind.mbHardDiskRead = false;
			ind.mbHardDiskWrite = false;
		}
	}
}

void ATStatusBar::SetVisible(bool visible) {
	Show(visible);
	UpdateBottomMargin();
	GetParent()->Layout();
}

void ATStatusBar::UpdateBottomMargin() {
	if (!mpDisplay)
		return;

	if (IsShown()) {
		wxSize sz = GetSize();
		mpDisplay->SetBottomMargin(sz.GetHeight());
	} else {
		mpDisplay->SetBottomMargin(0);
	}
}

void ATStatusBar::OnSize(wxSizeEvent& event) {
	event.Skip();
	UpdateBottomMargin();
}

void ATStatusBar::OnTimer(wxTimerEvent&) {
	if (IsShown())
		Refresh(false);
}

void ATStatusBar::DrawSegment(wxDC& dc, int& xPos, int y, const wxString& text, const wxColour& color) {
	dc.SetTextForeground(color);
	dc.DrawText(text, xPos, y);
	wxSize ext = dc.GetTextExtent(text);
	xPos += ext.GetWidth() + kSegmentSpacing;
}

void ATStatusBar::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	wxSize sz = GetSize();

	// Dark background
	dc.SetBackground(wxBrush(wxColour(38, 38, 38)));
	dc.Clear();

	dc.SetFont(GetFont());

	int xPos = 8;
	int yPos = (sz.GetHeight() - dc.GetCharHeight()) / 2;

	auto& ind = ATImGuiGetIndicatorState();

	// Hardware mode + video standard
	{
		static const char *kHwNames[] = { "800", "800XL", "5200", "XEGS", "1200XL", "130XE", "1400XL" };
		static const char *kVsNames[] = { "NTSC", "PAL", "SECAM", "PAL-60", "NTSC-50" };

		int hw = (int)g_sim.GetHardwareMode();
		int vs = (int)g_sim.GetVideoStandard();
		const char *hwName = (hw >= 0 && hw < (int)(sizeof(kHwNames)/sizeof(kHwNames[0]))) ? kHwNames[hw] : "?";
		const char *vsName = (vs >= 0 && vs < (int)(sizeof(kVsNames)/sizeof(kVsNames[0]))) ? kVsNames[vs] : "?";

		DrawSegment(dc, xPos, yPos, wxString::Format("%s %s", hwName, vsName), kColorHwMode);
	}

	// Disk drive indicators (always show D1-D4; higher only when loaded/active)
	{
		uint32_t statusFlags = ind.mStatusFlags | ind.mStatusHoldFlags;
		bool errorBlink = (SDL_GetTicks() % 1000) >= 500;
		uint32_t diskErrorVis = errorBlink ? ind.mDiskErrorFlags : 0;

		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			uint32_t flag = 1u << i;
			bool motorOn = (ind.mDiskMotorFlags & flag) != 0;
			bool sioActive = (statusFlags & flag) != 0;
			bool hasError = (diskErrorVis & flag) != 0;
			bool ledOn = (ind.mDiskLEDFlags & flag) != 0;
			bool isActive = sioActive || hasError;

			if (i >= 4 && !di.IsDiskLoaded() && !motorOn && !sioActive && !ledOn)
				continue;

			const wxColour *color = nullptr;
			if (hasError)
				color = &kDiskError;
			else if (isActive)
				color = &kDiskBright[i & 7];
			else if (motorOn)
				color = &kDiskDim[i & 7];
			else if (di.IsDiskLoaded() && di.IsDirty())
				color = &kDiskDirty;
			else if (ledOn)
				color = &kDiskDim[i & 7];

			if (di.IsDiskLoaded()) {
				VDStringA u8 = VDTextWToU8(VDStringW(VDFileSplitPath(di.GetPath())));
				const char *dirty = di.IsDirty() ? "*" : "";

				uint32_t sector = ind.mStatusCounter[i];
				uint32_t track = 0;
				uint32_t secInTrack = 0;
				IATDiskImage *img = di.GetDiskImage();
				if (img && sector > 0) {
					ATDiskGeometryInfo geo = img->GetGeometry();
					uint32_t spt = geo.mSectorsPerTrack ? geo.mSectorsPerTrack : 18;
					track = (sector - 1) / spt;
					secInTrack = (sector - 1) % spt + 1;
				}

				wxString label = wxString::Format("D%d: %s%s [T%02u S%02u]",
					i + 1, u8.c_str(), dirty, track, secInTrack);
				DrawSegment(dc, xPos, yPos, label, color ? *color : kColorDefault);
			} else if (motorOn || sioActive) {
				wxString label = wxString::Format("D%d:", i + 1);
				DrawSegment(dc, xPos, yPos, label, isActive ? kDiskBright[i & 7] : kDiskDim[i & 7]);
			} else {
				DrawSegment(dc, xPos, yPos, wxString::Format("D%d: -", i + 1), kDiskIdle);
			}
		}
	}

	// H: device indicator
	if (ind.mHReadCounter || ind.mHWriteCounter) {
		bool bright = ind.mHReadCounter > 24 || ind.mHWriteCounter > 24;
		const char *suffix = (ind.mHReadCounter && ind.mHWriteCounter) ? "H:RW"
			: ind.mHWriteCounter ? "H:W" : "H:R";
		DrawSegment(dc, xPos, yPos, suffix, bright ? kColorHBright : kColorHDim);
	}

	// PCLink indicator
	if (ind.mPCLinkReadCounter || ind.mPCLinkWriteCounter) {
		bool bright = ind.mPCLinkReadCounter > 24 || ind.mPCLinkWriteCounter > 24;
		const char *suffix = (ind.mPCLinkReadCounter && ind.mPCLinkWriteCounter) ? "PCL:RW"
			: ind.mPCLinkWriteCounter ? "PCL:W" : "PCL:R";
		DrawSegment(dc, xPos, yPos, suffix, bright ? kColorPCLBright : kColorPCLDim);
	}

	// IDE/Hard disk indicator
	if (ind.mbHardDiskRead || ind.mbHardDiskWrite) {
		wxString label = wxString::Format("HD:%c%u",
			ind.mbHardDiskWrite ? 'W' : 'R', ind.mHardDiskLBA);
		DrawSegment(dc, xPos, yPos, label,
			ind.mbHardDiskWrite ? kColorHDWrite : kColorHDRead);
	}

	// Flash write indicator
	if (ind.mFlashWriteCounter) {
		DrawSegment(dc, xPos, yPos, "FL", kColorFlash);
	}

	// Cartridge indicator
	if (g_sim.IsCartridgeAttached(0)) {
		DrawSegment(dc, xPos, yPos, "CART",
			ind.mCartridgeActivityCounter ? kColorCartActive : kColorCartIdle);
	}

	// Modem connection
	if (ind.mModemConnection[0]) {
		DrawSegment(dc, xPos, yPos,
			wxString::Format("MDM:%s", ind.mModemConnection), kColorModem);
	}

	// Tape indicator
	{
		ATCassetteEmulator& cas = g_sim.GetCassette();
		if (cas.IsLoaded()) {
			float pos = cas.GetPosition();
			int posMin = (int)(pos / 60.0f);
			int posSec = (int)pos % 60;

			if (cas.IsPlayEnabled() && !cas.IsPaused()) {
				DrawSegment(dc, xPos, yPos,
					wxString::Format("TAPE> %d:%02d", posMin, posSec), kColorTapePlaying);
			} else if (cas.IsRecordEnabled() && !cas.IsPaused()) {
				DrawSegment(dc, xPos, yPos,
					wxString::Format("TAPE* %d:%02d", posMin, posSec), kColorTapeRecording);
			} else {
				DrawSegment(dc, xPos, yPos,
					wxString::Format("TAPE %d:%02d", posMin, posSec), kColorTapeIdle);
			}
		}
	}

	// 1200XL LED indicators
	if (ind.mLedStatus) {
		static const wxColour kLedGreen(51, 255, 51);
		static const wxColour kLedDim(51, 102, 51);
		wxString ledStr;
		if (ind.mLedStatus & 1) ledStr += "L1 ";
		if (ind.mLedStatus & 2) ledStr += "L2";
		ledStr.Trim();
		DrawSegment(dc, xPos, yPos, ledStr, kLedGreen);
	}

	// Held console buttons indicator
	if (ind.mHeldButtonMask) {
		static const wxColour kHeldColor(255, 255, 77);
		wxString held = "HELD:";
		if (ind.mHeldButtonMask & 1) held += " Start";
		if (ind.mHeldButtonMask & 2) held += " Select";
		if (ind.mHeldButtonMask & 4) held += " Option";
		DrawSegment(dc, xPos, yPos, held, kHeldColor);
	}

	// Pending hold mode indicator
	if (ind.mbPendingHoldMode) {
		static const wxColour kPendingColor(51, 255, 153);
		wxString pendStr = "Hold:";
		if (ind.mPendingHeldButtons & 1) pendStr += " Start";
		if (ind.mPendingHeldButtons & 2) pendStr += " Select";
		if (ind.mPendingHeldButtons & 4) pendStr += " Option";
		if (ind.mPendingHeldKey >= 0) pendStr += wxString::Format(" Key%d", ind.mPendingHeldKey);
		DrawSegment(dc, xPos, yPos, pendStr, kPendingColor);
	}

	// Tracing indicator
	if (ind.mTracingSize >= 0) {
		static const wxColour kTracingColor(153, 204, 255);
		double mb = ind.mTracingSize / 1048576.0;
		DrawSegment(dc, xPos, yPos, wxString::Format("Tracing %.1fM", mb), kTracingColor);
	}

	// Recording indicator (with type)
	if (ind.mRecordingTime >= 0) {
		int secs = (int)ind.mRecordingTime;
		int mins = secs / 60;
		secs %= 60;
		const wxColour& recColor = ind.mbRecordingPaused ? kColorRecPaused : kColorRecording;
		if (ind.mRecordingSize >= 1048576)
			DrawSegment(dc, xPos, yPos,
				wxString::Format("REC %d:%02d %.1fMB", mins, secs, ind.mRecordingSize / 1048576.0), recColor);
		else
			DrawSegment(dc, xPos, yPos,
				wxString::Format("REC %d:%02d %lldKB", mins, secs, (long long)(ind.mRecordingSize / 1024)), recColor);
	}

	// Status messages (transient)
	{
		uint32_t now = SDL_GetTicks();
		for (int i = 0; i < 3; ++i) {
			if (ind.mStatusMessages[i][0]) {
				uint32_t age = now - ind.mStatusMessageTimestamp;
				// Auto-expire priority 0 after 1500ms, others after 3000ms
				uint32_t timeout = (i == 0) ? 1500 : 3000;
				if (age < timeout) {
					static const wxColour kMsgColor(255, 255, 77);
					DrawSegment(dc, xPos, yPos, ind.mStatusMessages[i], kMsgColor);
				}
			}
		}
	}

	// Mute indicator
	{
		IATAudioOutput *audioOut = g_sim.GetAudioOutput();
		if (audioOut && audioOut->GetMute()) {
			DrawSegment(dc, xPos, yPos, "MUTE", kColorMute);
		}
	}

	// Pause indicator
	if (g_sim.IsPaused()) {
		DrawSegment(dc, xPos, yPos, "PAUSED", kColorPaused);
	}

	// Turbo / speed indicator
	if (ATUIGetTurbo()) {
		DrawSegment(dc, xPos, yPos, "[TURBO]", kColorTurbo);
	} else {
		float speed = ATUIGetSpeedModifier();
		int pct = (int)(speed * 100.0f + 0.5f);
		if (pct != 100) {
			DrawSegment(dc, xPos, yPos, wxString::Format("%d%%", pct), kColorSpeed);
		}
	}

	// Watched values (debugger)
	{
		static const wxColour kWatchColor(153, 255, 153);
		for (int i = 0; i < 8; ++i) {
			if (ind.mWatchSlots[i].active) {
				uint32_t val = ind.mWatchSlots[i].value;
				wxString ws;
				if (ind.mWatchSlots[i].format == 0)
					ws = wxString::Format("W%d=$%04X", i, val);
				else
					ws = wxString::Format("W%d=%u", i, val);
				DrawSegment(dc, xPos, yPos, ws, kWatchColor);
			}
		}
	}

	// FPS counter
	if (ATUIGetShowFPS()) {
		// Calculate FPS from frame count
		double now = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
		if (mLastFpsTime == 0)
			mLastFpsTime = now;
		double elapsed = now - mLastFpsTime;
		if (elapsed >= 0.5) {
			uint64_t frameDelta = mFrameCount - mLastFpsFrameCount;
			mCurrentFps = (float)(frameDelta / elapsed);
			mLastFpsFrameCount = mFrameCount;
			mLastFpsTime = now;
		}

		DrawSegment(dc, xPos, yPos,
			wxString::Format("%.1f fps", mCurrentFps), kColorFps);
	}
}
