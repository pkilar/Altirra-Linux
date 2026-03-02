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

#include <stdafx.h>
#include "mainframe.h"
#include "menu_ids.h"
#include <display_wx.h>

#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <time.h>

#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/time.h>
#include <vd2/system/text.h>
#include <at/atcore/device.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/profile.h>

#include "simulator.h"
#include "cartridge.h"
#include "cassette.h"
#include "debugger.h"
#include "disk.h"
#include "inputmanager.h"
#include "joystick.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uienhancedtext.h"
#include "uikeyboard.h"
#include "uiqueue.h"

#include <SDL3/SDL.h>

// External symbols
extern ATSimulator g_sim;

extern sint64 g_frameTicks;
extern uint32 g_frameSubTicks;
extern sint64 g_frameErrorBound;
extern sint64 g_frameTimeout;

extern ATUIKeyboardOptions g_kbdOpts;

IATUIEnhancedTextEngine *ATUIGetEnhancedTextEngine();

// Toast notification (defined in main_wx.cpp)
void ATImGuiShowToast(const char *message);

///////////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ATMainFrame, wxFrame)
	EVT_CLOSE(ATMainFrame::OnClose)
	EVT_IDLE(ATMainFrame::OnIdle)
	EVT_TIMER(ID_GAMEPAD_TIMER_ID, ATMainFrame::OnGamepadTimer)
wxEND_EVENT_TABLE()

ATMainFrame::ATMainFrame()
	: wxFrame(nullptr, wxID_ANY, "Altirra (Linux)")
	, mGamepadTimer(this, ID_GAMEPAD_TIMER_ID)
{
	// Restore saved window geometry, or use defaults (2x NTSC resolution)
	int winW = 912;
	int winH = 524;
	int winX = -1;
	int winY = -1;
	bool winMaximized = false;

	VDRegistryAppKey key("Window", false);
	if (key.getInt("Width", 0) > 0) {
		winW = key.getInt("Width", 912);
		winH = key.getInt("Height", 524);
		winX = key.getInt("X", -1);
		winY = key.getInt("Y", -1);
		winMaximized = key.getBool("Maximized", false);
	}

	SetClientSize(winW, winH);

	if (winX >= 0 && winY >= 0)
		SetPosition(wxPoint(winX, winY));
	else
		Centre();

	if (winMaximized)
		Maximize(true);

	// Create the GL display canvas filling the client area
	mpCanvas = new ATDisplayCanvas(this);
	mpCanvas->InitGL();
	mpDisplay = new ATDisplayWx(mpCanvas);
	mpCanvas->SetDisplay(mpDisplay);

	// Bind keyboard events on the canvas (canvas has focus, frame handles events)
	mpCanvas->Bind(wxEVT_KEY_DOWN, &ATMainFrame::OnKeyDown, this);
	mpCanvas->Bind(wxEVT_KEY_UP, &ATMainFrame::OnKeyUp, this);

	// Bind mouse events on the canvas
	mpCanvas->Bind(wxEVT_MOTION, &ATMainFrame::OnMouseMotion, this);
	mpCanvas->Bind(wxEVT_LEFT_DOWN, &ATMainFrame::OnMouseButton, this);
	mpCanvas->Bind(wxEVT_LEFT_UP, &ATMainFrame::OnMouseButton, this);
	mpCanvas->Bind(wxEVT_MIDDLE_DOWN, &ATMainFrame::OnMouseButton, this);
	mpCanvas->Bind(wxEVT_MIDDLE_UP, &ATMainFrame::OnMouseButton, this);
	mpCanvas->Bind(wxEVT_RIGHT_DOWN, &ATMainFrame::OnMouseButton, this);
	mpCanvas->Bind(wxEVT_RIGHT_UP, &ATMainFrame::OnMouseButton, this);
	mpCanvas->Bind(wxEVT_MOUSEWHEEL, &ATMainFrame::OnMouseWheel, this);

	// Build and attach the menu bar
	SetMenuBar(CreateMenuBar());

	// Bind menu events for our ID range (1000-2000)
	Bind(wxEVT_MENU, &ATMainFrame::OnMenuCommand, this, ID_SYSTEM_WARM_RESET, ID_GAMEPAD_TIMER_ID);
	Bind(wxEVT_UPDATE_UI, &ATMainFrame::OnMenuUpdateUI, this, ID_SYSTEM_WARM_RESET, ID_GAMEPAD_TIMER_ID);

	// Use a sizer so the canvas fills the frame on resize
	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(mpCanvas, 1, wxEXPAND);
	SetSizer(sizer);

	// Give the canvas initial keyboard focus
	mpCanvas->SetFocus();
}

ATMainFrame::~ATMainFrame() {
	mEmulationRunning = false;
	mGamepadTimer.Stop();
	mInputWx.Shutdown();
	if (mpCanvas)
		mpCanvas->SetDisplay(nullptr);
	delete mpDisplay;
	mpDisplay = nullptr;
}

void ATMainFrame::InitInput() {
	mInputWx.Init(g_sim.GetInputManager());

	// Start gamepad polling timer (4ms = ~250Hz)
	mGamepadTimer.Start(4);
}

void ATMainFrame::StartEmulation() {
	mLastFrameTime = VDGetPreciseTick();
	mFrameError = 0;
	mFrameTimeErrorAccum = 0;
	mEmulationRunning = true;

	// Kick the idle loop
	wxWakeUpIdle();
}

void ATMainFrame::OnClose(wxCloseEvent& event) {
	// Check for dirty disks before closing
	if (event.CanVeto()) {
		bool hasDirty = false;
		wxString dirtyList;
		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			if (di.IsDiskLoaded() && di.IsDirty()) {
				hasDirty = true;
				const wchar_t *filename = VDFileSplitPath(di.GetPath());
				VDStringA u8 = VDTextWToU8(VDStringW(filename));
				dirtyList += wxString::Format("  D%d: %s\n", i + 1, u8.c_str());
			}
		}

		if (hasDirty) {
			wxString msg = "The following disks have unsaved changes:\n\n" + dirtyList +
				"\nDo you want to save before quitting?";
			int result = wxMessageBox(msg, "Quit Altirra?",
				wxYES_NO | wxCANCEL | wxICON_QUESTION, this);

			if (result == wxCANCEL) {
				event.Veto();
				return;
			}

			if (result == wxYES) {
				for (int i = 0; i < 15; ++i) {
					ATDiskInterface& di = g_sim.GetDiskInterface(i);
					if (di.IsDiskLoaded() && di.IsDirty()) {
						try { di.SaveDisk(); } catch (...) {}
					}
				}
			}
		}
	}

	mEmulationRunning = false;
	mGamepadTimer.Stop();

	// Save window geometry before shutdown
	VDRegistryAppKey wkey("Window", true);
	wkey.setBool("Maximized", IsMaximized());

	if (!IsMaximized() && !IsFullScreen()) {
		wxPoint pos = GetPosition();
		wxSize size = GetClientSize();
		wkey.setInt("X", pos.x);
		wkey.setInt("Y", pos.y);
		wkey.setInt("Width", size.GetWidth());
		wkey.setInt("Height", size.GetHeight());
	}

	Destroy();
}

///////////////////////////////////////////////////////////////////////////
// Keyboard input handling
///////////////////////////////////////////////////////////////////////////

void ATMainFrame::OnKeyDown(wxKeyEvent& event) {
	int keyCode = event.GetKeyCode();

	// Send to input mapping system (for joystick/paddle emulation via keyboard)
	mInputWx.OnKeyDown(keyCode);

	// Send to Atari keyboard processing (for POKEY/console key emulation)
	ProcessAtariKeyDown(keyCode, event);
}

void ATMainFrame::OnKeyUp(wxKeyEvent& event) {
	int keyCode = event.GetKeyCode();

	// Send to input mapping system
	mInputWx.OnKeyUp(keyCode);

	// Release any tracked special keys
	ProcessAtariKeyUp(keyCode);
}

bool ATMainFrame::IsExtendedWxKey(int wxKeyCode) {
	switch (wxKeyCode) {
		case WXK_INSERT:
		case WXK_DELETE:
		case WXK_HOME:
		case WXK_END:
		case WXK_PAGEUP:
		case WXK_PAGEDOWN:
		case WXK_LEFT:
		case WXK_RIGHT:
		case WXK_UP:
		case WXK_DOWN:
		case WXK_NUMPAD_ENTER:
			return true;
		default:
			return false;
	}
}

void ATMainFrame::HandleSpecialKey(uint32 scanCode, bool state) {
	switch (scanCode) {
		case kATUIKeyScanCode_Start:
			g_sim.GetGTIA().SetConsoleSwitch(0x01, state);
			break;
		case kATUIKeyScanCode_Select:
			g_sim.GetGTIA().SetConsoleSwitch(0x02, state);
			break;
		case kATUIKeyScanCode_Option:
			g_sim.GetGTIA().SetConsoleSwitch(0x04, state);
			break;
		case kATUIKeyScanCode_Break:
			g_sim.GetPokey().SetBreakKeyState(state, !g_kbdOpts.mbFullRawKeys);
			break;
	}
}

void ATMainFrame::ProcessAtariKeyDown(int wxKeyCode, const wxKeyEvent& event) {
	// Translate wxWidgets key code to virtual key (ATInputCode)
	uint32 vk = mInputWx.TranslateWxKey(wxKeyCode);
	if (vk == kATInputCode_None)
		return;

	// Get modifier state from the event
	bool alt   = event.AltDown();
	bool ctrl  = event.ControlDown();
	bool shift = event.ShiftDown();
	bool ext   = IsExtendedWxKey(wxKeyCode);

	// Map (VK + modifiers) to Atari scan code
	uint32 scanCode;
	if (!ATUIGetScanCodeForVirtualKey(vk, alt, ctrl, shift, ext, scanCode))
		return;

	if (scanCode >= kATUIKeyScanCodeFirst) {
		// Special key (Start/Select/Option/Break)
		HandleSpecialKey(scanCode, true);

		// Track for release on key-up
		bool found = false;
		for (auto& k : mActiveSpecialKeys) {
			if (k.vk == vk) {
				k.scanCode = scanCode;
				found = true;
				break;
			}
		}
		if (!found)
			mActiveSpecialKeys.push_back({vk, scanCode});
	} else if (g_kbdOpts.mbRawKeys) {
		g_sim.GetPokey().PushRawKey(scanCode, !g_kbdOpts.mbFullRawKeys);
	} else {
		g_sim.GetPokey().PushKey(scanCode, event.IsAutoRepeat());
	}
}

void ATMainFrame::ProcessAtariKeyUp(int wxKeyCode) {
	uint32 vk = mInputWx.TranslateWxKey(wxKeyCode);
	if (vk == kATInputCode_None)
		return;

	// Release any special key tracked for this VK
	for (auto it = mActiveSpecialKeys.begin(); it != mActiveSpecialKeys.end(); ++it) {
		if (it->vk == vk) {
			HandleSpecialKey(it->scanCode, false);
			mActiveSpecialKeys.erase(it);
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// Mouse input handling
///////////////////////////////////////////////////////////////////////////

void ATMainFrame::OnMouseMotion(wxMouseEvent& event) {
	int x = event.GetX();
	int y = event.GetY();

	if (mLastMouseX >= 0 && mLastMouseY >= 0) {
		int dx = x - mLastMouseX;
		int dy = y - mLastMouseY;
		if (dx != 0 || dy != 0)
			mInputWx.OnMouseMove(dx, dy);
	}

	mLastMouseX = x;
	mLastMouseY = y;
}

void ATMainFrame::OnMouseButton(wxMouseEvent& event) {
	// Ensure canvas has focus when clicked
	if (event.ButtonDown())
		mpCanvas->SetFocus();

	int button = 0;
	if (event.GetButton() == wxMOUSE_BTN_LEFT)
		button = 1;
	else if (event.GetButton() == wxMOUSE_BTN_MIDDLE)
		button = 2;
	else if (event.GetButton() == wxMOUSE_BTN_RIGHT)
		button = 3;

	if (button == 0)
		return;

	if (event.ButtonDown())
		mInputWx.OnMouseButtonDown(button);
	else if (event.ButtonUp())
		mInputWx.OnMouseButtonUp(button);
}

void ATMainFrame::OnMouseWheel(wxMouseEvent& event) {
	int delta = event.GetWheelRotation();
	if (delta != 0)
		mInputWx.OnMouseWheel(delta);
}

///////////////////////////////////////////////////////////////////////////
// Gamepad polling
///////////////////////////////////////////////////////////////////////////

void ATMainFrame::OnGamepadTimer(wxTimerEvent&) {
	// Poll SDL3 for gamepad events (SDL was initialized with SDL_INIT_GAMEPAD)
	SDL_Event sdlEvent;
	while (SDL_PollEvent(&sdlEvent)) {
		switch (sdlEvent.type) {
			case SDL_EVENT_GAMEPAD_ADDED:
			case SDL_EVENT_GAMEPAD_REMOVED:
			case SDL_EVENT_GAMEPAD_REMAPPED:
			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
			case SDL_EVENT_JOYSTICK_ADDED:
			case SDL_EVENT_JOYSTICK_REMOVED:
			case SDL_EVENT_JOYSTICK_AXIS_MOTION:
			case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
			case SDL_EVENT_JOYSTICK_BUTTON_UP:
			case SDL_EVENT_JOYSTICK_HAT_MOTION:
				// These events are consumed by the joystick manager
				// through its SDL3 integration (joystick_sdl3.cpp).
				// SDL_PollEvent removes them from the queue, and the
				// joystick manager processes them on its next poll.
				break;
			default:
				// Ignore non-gamepad SDL events
				break;
		}
	}

	// Poll the joystick manager to update controller state
	IATJoystickManager *jm = g_sim.GetJoystickManager();
	if (jm)
		jm->Poll();
}

///////////////////////////////////////////////////////////////////////////
// Emulation loop
///////////////////////////////////////////////////////////////////////////

void ATMainFrame::OnIdle(wxIdleEvent& event) {
	if (!mEmulationRunning)
		return;

	// Process UI step queue (custom device scripts, deferred actions)
	while (ATUIGetQueue().Run()) {}

	// Tick debugger (processes queued commands)
	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->Tick();

	// Advance emulation with exception recovery
	ATSimulator::AdvanceResult result;
	try {
		result = g_sim.Advance(false);
	} catch (const MyError& e) {
		ATUIShowError(e);
		g_sim.ColdReset();
		g_sim.Resume();
		RenderAndPresent();
		event.RequestMore(true);
		return;
	} catch (const std::exception& e) {
		ATUIShowError2(nullptr,
			VDTextU8ToW(VDStringA(e.what())).c_str(),
			L"Emulation Error");
		g_sim.ColdReset();
		g_sim.Resume();
		RenderAndPresent();
		event.RequestMore(true);
		return;
	} catch (...) {
		ATUIShowError2(nullptr,
			L"An unknown error occurred during emulation.",
			L"Emulation Error");
		g_sim.ColdReset();
		g_sim.Resume();
		RenderAndPresent();
		event.RequestMore(true);
		return;
	}

	bool frameRendered = false;

	if (result == ATSimulator::kAdvanceResult_WaitingForFrame) {
		RenderAndPresent();
		frameRendered = true;
	} else if (result == ATSimulator::kAdvanceResult_Stopped) {
		// Emulation stopped — still render but at reduced rate
		RenderAndPresent();

		// Sleep 16ms to avoid spinning at full CPU when stopped
		struct timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 16000000;
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);

		// Reset pacing state while stopped
		mLastFrameTime = VDGetPreciseTick();
		mFrameError = 0;
		mFrameTimeErrorAccum = 0;
	} else if (result == ATSimulator::kAdvanceResult_Running) {
		// Check if display has a new frame ready
		if (mpDisplay && mpDisplay->IsFramePending()) {
			RenderAndPresent();
			frameRendered = true;
		}
	}

	// Frame pacing — error accumulation feedback loop
	if (frameRendered) {
		uint64 curTime = VDGetPreciseTick();
		sint64 lastFrameDuration = curTime - mLastFrameTime;
		mLastFrameTime = curTime;

		mFrameError += lastFrameDuration - g_frameTicks;
		mFrameTimeErrorAccum += g_frameSubTicks;

		if (mFrameTimeErrorAccum >= 0x10000) {
			mFrameTimeErrorAccum &= 0xFFFF;
			--mFrameError;
		}

		if (mFrameError > g_frameErrorBound || mFrameError < -g_frameErrorBound)
			mFrameError = -g_frameTicks;

		// In turbo mode, don't pace
		if (g_sim.IsTurboModeEnabled()) {
			mFrameError = 0;
		} else if (mFrameError < 0) {
			// We're ahead of schedule — sleep to maintain target frame rate
			sint64 nsToSleep = -mFrameError;

			if (nsToSleep > 0 && nsToSleep < (sint64)g_frameTimeout) {
				struct timespec ts;
				ts.tv_sec = nsToSleep / 1000000000LL;
				ts.tv_nsec = nsToSleep % 1000000000LL;
				clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
			}
		}
	}

	// Keep the idle loop running
	event.RequestMore(true);
}

///////////////////////////////////////////////////////////////////////////
// Window title and rendering
///////////////////////////////////////////////////////////////////////////

void ATMainFrame::UpdateWindowTitle() {
	const char *hwName = "";
	switch (g_sim.GetHardwareMode()) {
		case kATHardwareMode_800:    hwName = "800"; break;
		case kATHardwareMode_800XL:  hwName = "800XL"; break;
		case kATHardwareMode_5200:   hwName = "5200"; break;
		case kATHardwareMode_XEGS:   hwName = "XEGS"; break;
		case kATHardwareMode_1200XL: hwName = "1200XL"; break;
		case kATHardwareMode_130XE:  hwName = "130XE"; break;
		default: hwName = "Atari"; break;
	}

	char title[512];
	int off = snprintf(title, sizeof(title), "Altirra %s", hwName);
	int baseOff = off;

	// Show first loaded disk
	for (int i = 0; i < 4; ++i) {
		ATDiskInterface& di = g_sim.GetDiskInterface(i);
		if (di.IsDiskLoaded()) {
			VDStringA u8 = VDTextWToU8(VDStringW(VDFileSplitPath(di.GetPath())));
			off += snprintf(title + off, sizeof(title) - off, " - %s", u8.c_str());
			break;
		}
	}

	// Show cartridge (if no disk shown)
	if (off == baseOff && g_sim.IsCartridgeAttached(0)) {
		ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
		if (cart && cart->GetPath() && *cart->GetPath()) {
			VDStringA u8 = VDTextWToU8(VDStringW(VDFileSplitPath(cart->GetPath())));
			off += snprintf(title + off, sizeof(title) - off, " - %s", u8.c_str());
		}
	}

	// Show cassette (if no disk or cartridge shown)
	if (off == baseOff && g_sim.GetCassette().IsLoaded()) {
		const wchar_t *tapePath = g_sim.GetCassette().GetPath();
		if (tapePath && *tapePath) {
			VDStringA u8 = VDTextWToU8(VDStringW(VDFileSplitPath(tapePath)));
			off += snprintf(title + off, sizeof(title) - off, " - %s", u8.c_str());
		}
	}

	// Show turbo/paused indicator
	if (ATUIGetTurbo())
		off += snprintf(title + off, sizeof(title) - off, " [TURBO]");
	else if (g_sim.IsPaused())
		off += snprintf(title + off, sizeof(title) - off, " [PAUSED]");

	SetTitle(title);
}

void ATMainFrame::RenderAndPresent() {
	if (!mpDisplay)
		return;

	// Update window title
	UpdateWindowTitle();

	// Update pixel aspect ratio from GTIA
	mpDisplay->SetPixelAspectRatio(g_sim.GetGTIA().GetPixelAspectRatio());

	// Update enhanced text engine if active
	IATUIEnhancedTextEngine *enhText = ATUIGetEnhancedTextEngine();
	if (enhText) {
		enhText->Update(false);
		IATDeviceVideoOutput *vo = enhText->GetVideoOutput();
		if (vo) {
			const VDPixmap& fb = vo->GetFrameBuffer();
			if (fb.data && fb.w > 0 && fb.h > 0) {
				mpDisplay->SetSourcePersistent(true, fb, true, nullptr, nullptr);
			}
		}
	}

	// Render emulation frame and swap buffers
	mpDisplay->PresentFrame();
}
