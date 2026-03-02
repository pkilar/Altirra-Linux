//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#pragma once

#include <wx/panel.h>
#include <wx/timer.h>

class ATDisplayWx;

class ATStatusBar : public wxPanel {
public:
	ATStatusBar(wxWindow *parent);

	void SetDisplay(ATDisplayWx *display) { mpDisplay = display; }

	// Called each emulation frame to tick hold counters and count frames
	void TickFrame();

	// Show or hide, updating display bottom margin
	void SetVisible(bool visible);

private:
	void OnPaint(wxPaintEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnSize(wxSizeEvent& event);

	void UpdateBottomMargin();

	// Draw a colored text segment, advancing xPos
	void DrawSegment(wxDC& dc, int& xPos, int y, const wxString& text, const wxColour& color);

	ATDisplayWx *mpDisplay = nullptr;
	wxTimer mRefreshTimer;

	// FPS calculation
	uint64_t mFrameCount = 0;
	uint64_t mLastFpsFrameCount = 0;
	double mLastFpsTime = 0;
	float mCurrentFps = 0;

	static const int kTimerId = 2100;
	static const int kRefreshMs = 100;
	static const int kSegmentSpacing = 16;

	wxDECLARE_EVENT_TABLE();
};
