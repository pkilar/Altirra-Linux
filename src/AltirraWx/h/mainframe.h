//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#pragma once

#include <wx/frame.h>
#include <wx/timer.h>
#include <vd2/system/vdtypes.h>
#include <input_wx.h>
#include <vector>

class ATDisplayCanvas;
class ATDisplayWx;

class ATMainFrame : public wxFrame {
public:
	ATMainFrame();
	~ATMainFrame();

	ATDisplayWx *GetDisplay() const { return mpDisplay; }

	// Start the emulation idle loop
	void StartEmulation();

	// Initialize input system (called after input manager is ready)
	void InitInput();

private:
	void OnClose(wxCloseEvent& event);
	void OnIdle(wxIdleEvent& event);

	// Keyboard event handlers (bound to canvas)
	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	// Mouse event handlers (bound to canvas)
	void OnMouseMotion(wxMouseEvent& event);
	void OnMouseButton(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);

	// Gamepad polling timer
	void OnGamepadTimer(wxTimerEvent& event);

	// Atari keyboard processing pipeline
	void ProcessAtariKeyDown(int wxKeyCode, const wxKeyEvent& event);
	void ProcessAtariKeyUp(int wxKeyCode);
	static bool IsExtendedWxKey(int wxKeyCode);
	static void HandleSpecialKey(uint32 scanCode, bool state);

	// Menu bar (implemented in menubar.cpp)
	wxMenuBar *CreateMenuBar();
	void OnMenuCommand(wxCommandEvent& event);
	void OnMenuUpdateUI(wxUpdateUIEvent& event);

	void UpdateWindowTitle();
	void RenderAndPresent();

	ATDisplayCanvas *mpCanvas = nullptr;
	ATDisplayWx *mpDisplay = nullptr;
	ATInputWx mInputWx;

	// Gamepad polling timer (4ms = ~250Hz)
	wxTimer mGamepadTimer;

	// Track active special keys for proper release on key-up
	struct ActiveSpecialKey {
		uint32 vk;
		uint32 scanCode;
	};
	std::vector<ActiveSpecialKey> mActiveSpecialKeys;

	// Mouse tracking for relative motion
	int mLastMouseX = -1;
	int mLastMouseY = -1;

	// Frame pacing state
	sint64 mFrameError = 0;
	uint32 mFrameTimeErrorAccum = 0;
	uint64 mLastFrameTime = 0;
	bool mEmulationRunning = false;

	wxDECLARE_EVENT_TABLE();
};
