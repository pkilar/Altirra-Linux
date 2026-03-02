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
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/listbox.h>
#include <wx/checklst.h>
#include <wx/msgdlg.h>
#include <wx/timer.h>
#include <wx/collpane.h>
#include <wx/statbox.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/refcount.h>

#include "simulator.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "inputdefs.h"
#include "joystick.h"

#include "dialogs_wx.h"
#include "input_wx.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////
// Helper functions — map enums to display strings
///////////////////////////////////////////////////////////////////////////

static const char *ATGetControllerTypeName(ATInputControllerType type) {
	switch (type) {
		case kATInputControllerType_Joystick:		return "Joystick";
		case kATInputControllerType_Paddle:			return "Paddle";
		case kATInputControllerType_STMouse:		return "ST Mouse";
		case kATInputControllerType_Console:		return "Console";
		case kATInputControllerType_5200Controller:	return "5200 Controller";
		case kATInputControllerType_InputState:		return "Input State";
		case kATInputControllerType_LightGun:		return "Light Gun (XG-1)";
		case kATInputControllerType_Tablet:			return "Tablet";
		case kATInputControllerType_KoalaPad:		return "KoalaPad";
		case kATInputControllerType_AmigaMouse:		return "Amiga Mouse";
		case kATInputControllerType_Keypad:			return "Keypad";
		case kATInputControllerType_Trackball_CX80:	return "Trackball CX-80";
		case kATInputControllerType_5200Trackball:	return "5200 Trackball";
		case kATInputControllerType_Driving:		return "Driving Controller";
		case kATInputControllerType_Keyboard:		return "Keyboard";
		case kATInputControllerType_LightPen:		return "Light Pen";
		case kATInputControllerType_PowerPad:		return "Power Pad";
		case kATInputControllerType_LightPenStack:	return "Light Pen Stack";
		default:									return "Unknown";
	}
}

static const char *ATGetInputCodeName(uint32 code) {
	static char buf[32];
	uint32 id = code & kATInputCode_IdMask;

	if (id >= kATInputCode_JoyClass) {
		if (id >= kATInputCode_JoyButton0) {
			snprintf(buf, sizeof(buf), "Joy Btn%d", id - kATInputCode_JoyButton0);
			return buf;
		}
		switch (id) {
			case kATInputCode_JoyStick1Left:	return "Joy Left";
			case kATInputCode_JoyStick1Right:	return "Joy Right";
			case kATInputCode_JoyStick1Up:		return "Joy Up";
			case kATInputCode_JoyStick1Down:	return "Joy Down";
			case kATInputCode_JoyHoriz1:		return "Joy Axis X";
			case kATInputCode_JoyVert1:			return "Joy Axis Y";
			case kATInputCode_JoyPOVLeft:		return "Joy POV Left";
			case kATInputCode_JoyPOVRight:		return "Joy POV Right";
			case kATInputCode_JoyPOVUp:			return "Joy POV Up";
			case kATInputCode_JoyPOVDown:		return "Joy POV Down";
			default: snprintf(buf, sizeof(buf), "Joy 0x%X", id); return buf;
		}
	} else if (id >= kATInputCode_MouseClass) {
		switch (id) {
			case kATInputCode_MouseHoriz:		return "Mouse X";
			case kATInputCode_MouseVert:		return "Mouse Y";
			case kATInputCode_MouseLMB:			return "Mouse LMB";
			case kATInputCode_MouseRMB:			return "Mouse RMB";
			case kATInputCode_MouseMMB:			return "Mouse MMB";
			case kATInputCode_MouseLeft:		return "Mouse Left";
			case kATInputCode_MouseRight:		return "Mouse Right";
			case kATInputCode_MouseUp:			return "Mouse Up";
			case kATInputCode_MouseDown:		return "Mouse Down";
			default: snprintf(buf, sizeof(buf), "Mouse 0x%X", id); return buf;
		}
	} else {
		if (id >= kATInputCode_KeyA && id <= kATInputCode_KeyZ) {
			snprintf(buf, sizeof(buf), "Key %c", (char)id);
			return buf;
		}
		if (id >= kATInputCode_Key0 && id <= kATInputCode_Key9) {
			snprintf(buf, sizeof(buf), "Key %c", (char)id);
			return buf;
		}
		switch (id) {
			case kATInputCode_KeyUp:		return "Up";
			case kATInputCode_KeyDown:		return "Down";
			case kATInputCode_KeyLeft:		return "Left";
			case kATInputCode_KeyRight:		return "Right";
			case kATInputCode_KeySpace:		return "Space";
			case kATInputCode_KeyReturn:	return "Enter";
			case kATInputCode_KeyEscape:	return "Escape";
			case kATInputCode_KeyTab:		return "Tab";
			case kATInputCode_KeyBack:		return "Backspace";
			case kATInputCode_KeyInsert:	return "Insert";
			case kATInputCode_KeyDelete:	return "Delete";
			case kATInputCode_KeyHome:		return "Home";
			case kATInputCode_KeyEnd:		return "End";
			case kATInputCode_KeyPrior:		return "Page Up";
			case kATInputCode_KeyNext:		return "Page Down";
			case kATInputCode_KeyLShift:	return "L.Shift";
			case kATInputCode_KeyRShift:	return "R.Shift";
			case kATInputCode_KeyLControl:	return "L.Ctrl";
			case kATInputCode_KeyRControl:	return "R.Ctrl";
			case kATInputCode_KeyNumpadEnter: return "Numpad Enter";
			default:
				if (id >= kATInputCode_KeyNumpad0 && id <= kATInputCode_KeyNumpad9) {
					snprintf(buf, sizeof(buf), "Numpad %d", id - kATInputCode_KeyNumpad0);
					return buf;
				}
				if (id >= kATInputCode_KeyF1 && id <= kATInputCode_KeyF12) {
					snprintf(buf, sizeof(buf), "F%d", id - kATInputCode_KeyF1 + 1);
					return buf;
				}
				snprintf(buf, sizeof(buf), "Key 0x%X", id);
				return buf;
		}
	}
}

static const char *ATGetInputTriggerName(uint32 code) {
	static char buf[32];
	uint32 trigger = code & kATInputTrigger_Mask;
	switch (trigger) {
		case kATInputTrigger_Button0:		return "Button";
		case kATInputTrigger_Up:			return "Up";
		case kATInputTrigger_Down:			return "Down";
		case kATInputTrigger_Left:			return "Left";
		case kATInputTrigger_Right:			return "Right";
		case kATInputTrigger_Start:			return "Start";
		case kATInputTrigger_Select:		return "Select";
		case kATInputTrigger_Option:		return "Option";
		case kATInputTrigger_Turbo:			return "Turbo";
		case kATInputTrigger_ColdReset:		return "Cold Reset";
		case kATInputTrigger_WarmReset:		return "Warm Reset";
		case kATInputTrigger_Axis0:			return "Axis";
		default:
			if (trigger >= kATInputTrigger_5200_0 && trigger <= kATInputTrigger_5200_Pound) {
				const char *k5200Keys[] = {"0","1","2","3","4","5","6","7","8","9","*","#"};
				snprintf(buf, sizeof(buf), "5200 [%s]", k5200Keys[trigger - kATInputTrigger_5200_0]);
			} else if (trigger >= kATInputTrigger_UILeft && trigger <= kATInputTrigger_UIRightShift) {
				const char *kUINames[] = {"UI Left","UI Right","UI Up","UI Down","UI Accept","UI Reject","UI Menu","UI Option","UI Switch L","UI Switch R","UI L.Shift","UI R.Shift"};
				uint32 idx = trigger - kATInputTrigger_UILeft;
				if (idx < 12)
					return kUINames[idx];
				snprintf(buf, sizeof(buf), "UI 0x%X", trigger);
			} else {
				snprintf(buf, sizeof(buf), "Trigger 0x%X", trigger);
			}
			return buf;
	}
}

static const char *ATGetModeName(uint32 code) {
	uint32 mode = code & kATInputTriggerMode_Mask;
	switch (mode) {
		case kATInputTriggerMode_AutoFire:	return "Auto-fire";
		case kATInputTriggerMode_Toggle:	return "Toggle";
		case kATInputTriggerMode_ToggleAF:	return "Toggle AF";
		case kATInputTriggerMode_Relative:	return "Relative";
		case kATInputTriggerMode_Absolute:	return "Absolute";
		case kATInputTriggerMode_Inverted:	return "Inverted";
		default:							return "";
	}
}

///////////////////////////////////////////////////////////////////////////
// Input Capture Dialog — modal popup to capture a key/button/axis
///////////////////////////////////////////////////////////////////////////

class ATInputCaptureDialog : public wxDialog {
public:
	ATInputCaptureDialog(wxWindow *parent);

	uint32 GetCapturedCode() const { return mCapturedCode; }

private:
	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnCancel(wxCommandEvent& event);

	void AcceptInput(uint32 code);

	uint32 mCapturedCode = kATInputCode_None;
	bool mShiftWasDown = false;
	wxTimer mGamepadTimer;

	enum { ID_CAPTURE_TIMER = 3000 };
};

ATInputCaptureDialog::ATInputCaptureDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Capture Input", wxDefaultPosition, wxSize(350, 150),
		wxDEFAULT_DIALOG_STYLE)
	, mGamepadTimer(this, ID_CAPTURE_TIMER)
{
	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->AddSpacer(16);
	sizer->Add(new wxStaticText(this, wxID_ANY, "Press a key, gamepad button, or move a stick..."),
		0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 16);
	sizer->AddSpacer(8);
	sizer->Add(new wxStaticText(this, wxID_ANY, "Shift+Escape to cancel"),
		0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 16);
	sizer->AddSpacer(16);

	wxButton *cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");
	sizer->Add(cancelBtn, 0, wxALIGN_CENTER | wxBOTTOM, 8);
	SetSizer(sizer);

	// Capture all key events on the dialog itself
	Bind(wxEVT_KEY_DOWN, &ATInputCaptureDialog::OnKeyDown, this);
	Bind(wxEVT_KEY_UP, &ATInputCaptureDialog::OnKeyUp, this);
	Bind(wxEVT_TIMER, &ATInputCaptureDialog::OnTimer, this, ID_CAPTURE_TIMER);
	Bind(wxEVT_BUTTON, &ATInputCaptureDialog::OnCancel, this, wxID_CANCEL);

	// Start polling for gamepad events
	mGamepadTimer.Start(16);  // ~60Hz

	// Set focus to the dialog for keyboard capture
	SetFocus();
}

void ATInputCaptureDialog::OnKeyDown(wxKeyEvent& event) {
	int key = event.GetKeyCode();

	// Track shift state
	if (key == WXK_SHIFT) {
		mShiftWasDown = true;
		return;  // Don't capture bare shift press
	}

	// Shift+Escape = cancel
	if (key == WXK_ESCAPE && mShiftWasDown) {
		EndModal(wxID_CANCEL);
		return;
	}

	// Use the same translation as the normal input system
	ATInputWx inputWx;
	uint32 code = inputWx.TranslateWxKey(key);
	if (code != kATInputCode_None)
		AcceptInput(code);
}

void ATInputCaptureDialog::OnKeyUp(wxKeyEvent& event) {
	int key = event.GetKeyCode();

	// Capture L.Shift / R.Shift on key-up (so Shift alone can be bound)
	if (key == WXK_SHIFT && mShiftWasDown) {
		mShiftWasDown = false;
		// wxWidgets doesn't distinguish L/R shift easily; use L.Shift
		AcceptInput(kATInputCode_KeyLShift);
		return;
	}
	mShiftWasDown = false;
}

void ATInputCaptureDialog::OnTimer(wxTimerEvent&) {
	// Poll SDL3 for gamepad events
	SDL_Event sdlEvent;
	while (SDL_PollEvent(&sdlEvent)) {
		switch (sdlEvent.type) {
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				AcceptInput(kATInputCode_JoyButton0 + sdlEvent.gbutton.button);
				return;

			case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
				int rawValue = sdlEvent.gaxis.value;
				if (rawValue > 24000 || rawValue < -24000) {
					uint32 code = kATInputCode_None;
					switch (sdlEvent.gaxis.axis) {
						case SDL_GAMEPAD_AXIS_LEFTX:		code = kATInputCode_JoyHoriz1; break;
						case SDL_GAMEPAD_AXIS_LEFTY:		code = kATInputCode_JoyVert1; break;
						case SDL_GAMEPAD_AXIS_RIGHTX:		code = kATInputCode_JoyHoriz3; break;
						case SDL_GAMEPAD_AXIS_RIGHTY:		code = kATInputCode_JoyVert3; break;
						case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:	code = kATInputCode_JoyVert2; break;
						case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:code = kATInputCode_JoyVert4; break;
					}
					if (code != kATInputCode_None) {
						AcceptInput(code);
						return;
					}
				}
				break;
			}
			default:
				break;
		}
	}
}

void ATInputCaptureDialog::OnCancel(wxCommandEvent&) {
	EndModal(wxID_CANCEL);
}

void ATInputCaptureDialog::AcceptInput(uint32 code) {
	mGamepadTimer.Stop();
	mCapturedCode = code;
	EndModal(wxID_OK);
}

///////////////////////////////////////////////////////////////////////////
// Input Setup Dialog — main dialog for managing input maps and bindings
///////////////////////////////////////////////////////////////////////////

static const struct { uint32 code; const char *name; } kCommonTriggers[] = {
	{ kATInputTrigger_Up,		"Up" },
	{ kATInputTrigger_Down,		"Down" },
	{ kATInputTrigger_Left,		"Left" },
	{ kATInputTrigger_Right,	"Right" },
	{ kATInputTrigger_Button0,	"Button" },
	{ kATInputTrigger_Start,	"Start" },
	{ kATInputTrigger_Select,	"Select" },
	{ kATInputTrigger_Option,	"Option" },
	{ kATInputTrigger_Axis0,	"Axis" },
};
static const int kNumCommonTriggers = (int)(sizeof(kCommonTriggers) / sizeof(kCommonTriggers[0]));

class ATInputSetupDialog : public wxDialog {
public:
	ATInputSetupDialog(wxWindow *parent);

private:
	void PopulateMapList();
	void PopulateBindings();
	void PopulatePresets();
	void PopulateControllers();

	void OnMapSelected(wxCommandEvent& event);
	void OnMapEnabled(wxCommandEvent& event);
	void OnRemoveMap(wxCommandEvent& event);
	void OnRebind(wxCommandEvent& event);
	void OnDeleteBinding(wxCommandEvent& event);
	void OnAddCapture(wxCommandEvent& event);
	void OnAddPreset(wxCommandEvent& event);
	void OnResetDefaults(wxCommandEvent& event);
	void OnRescan(wxCommandEvent& event);

	void RefreshMapAfterChange();

	// Translate display index to input manager index
	uint32 MapDisplayToManager(int displayIdx) const;
	uint32 PresetDisplayToManager(int displayIdx) const;

	ATInputManager *mpInputMgr = nullptr;

	wxCheckListBox *mpMapList;
	wxListCtrl *mpBindingList;
	wxChoice *mpControllerChoice;
	wxChoice *mpTriggerChoice;
	wxListBox *mpPresetList;
	wxStaticText *mpControllerSummary;

	int mSelectedMapIdx = -1;  // display index into sorted map list
	std::vector<uint32> mMapIndexMap;    // display index → manager index
	std::vector<uint32> mPresetIndexMap; // display index → preset index

	enum {
		ID_MAP_LIST = 3100,
		ID_REMOVE_MAP,
		ID_REBIND,
		ID_DEL_BINDING,
		ID_ADD_CAPTURE,
		ID_ADD_PRESET,
		ID_RESET_DEFAULTS,
		ID_RESCAN,
		ID_CONTROLLER_CHOICE,
		ID_TRIGGER_CHOICE,
	};
};

ATInputSetupDialog::ATInputSetupDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Input Setup", wxDefaultPosition, wxSize(700, 600),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	mpInputMgr = g_sim.GetInputManager();
	if (!mpInputMgr) {
		Close();
		return;
	}

	wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

	// --- Detected Controllers ---
	{
		wxStaticBoxSizer *ctrlBox = new wxStaticBoxSizer(wxVERTICAL, this, "Detected Controllers");
		wxBoxSizer *ctrlRow = new wxBoxSizer(wxHORIZONTAL);

		int numJoysticks = 0;
		SDL_JoystickID *joysticks = SDL_GetJoysticks(&numJoysticks);

		wxString ctrlText;
		if (joysticks && numJoysticks > 0) {
			for (int i = 0; i < numJoysticks; ++i) {
				const char *name = SDL_GetJoystickNameForID(joysticks[i]);
				if (i > 0) ctrlText += "  |  ";
				if (SDL_IsGamepad(joysticks[i]))
					ctrlText += wxString::Format("Gamepad %d: %s", i + 1, name ? name : "(unknown)");
				else
					ctrlText += wxString::Format("Joystick %d: %s", i + 1, name ? name : "(unknown)");
			}
		} else {
			ctrlText = "No joysticks/gamepads detected";
		}
		SDL_free(joysticks);

		ctrlRow->Add(new wxStaticText(ctrlBox->GetStaticBox(), wxID_ANY, ctrlText), 1, wxALIGN_CENTER_VERTICAL);
		ctrlRow->Add(new wxButton(ctrlBox->GetStaticBox(), ID_RESCAN, "Rescan"), 0, wxLEFT, 8);
		ctrlBox->Add(ctrlRow, 0, wxEXPAND | wxALL, 4);
		mainSizer->Add(ctrlBox, 0, wxEXPAND | wxALL, 8);
	}

	// --- Input Maps ---
	{
		wxStaticBoxSizer *mapBox = new wxStaticBoxSizer(wxVERTICAL, this, "Input Maps");
		wxWindow *mapParent = mapBox->GetStaticBox();

		mapBox->Add(new wxStaticText(mapParent, wxID_ANY,
			"Input maps bind host inputs (keyboard, mouse, gamepad) to Atari controllers."),
			0, wxALL, 4);

		wxBoxSizer *mapRow = new wxBoxSizer(wxHORIZONTAL);

		// Map list with checkboxes
		mpMapList = new wxCheckListBox(mapParent, ID_MAP_LIST, wxDefaultPosition, wxSize(250, 120));
		mapRow->Add(mpMapList, 1, wxEXPAND | wxRIGHT, 4);

		wxBoxSizer *mapBtnCol = new wxBoxSizer(wxVERTICAL);
		mapBtnCol->Add(new wxButton(mapParent, ID_REMOVE_MAP, "Remove"), 0, wxBOTTOM, 4);
		mapRow->Add(mapBtnCol, 0);

		mapBox->Add(mapRow, 0, wxEXPAND | wxALL, 4);

		// Controller summary for selected map
		mpControllerSummary = new wxStaticText(mapParent, wxID_ANY, "");
		mapBox->Add(mpControllerSummary, 0, wxLEFT | wxBOTTOM, 4);

		mainSizer->Add(mapBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
	}

	// --- Bindings for selected map ---
	{
		wxStaticBoxSizer *bindBox = new wxStaticBoxSizer(wxVERTICAL, this, "Bindings");
		wxWindow *bindParent = bindBox->GetStaticBox();

		mpBindingList = new wxListCtrl(bindParent, wxID_ANY, wxDefaultPosition, wxSize(-1, 200),
			wxLC_REPORT | wxLC_SINGLE_SEL);
		mpBindingList->AppendColumn("Input", wxLIST_FORMAT_LEFT, 130);
		mpBindingList->AppendColumn("Controller", wxLIST_FORMAT_LEFT, 140);
		mpBindingList->AppendColumn("Target", wxLIST_FORMAT_LEFT, 110);
		mpBindingList->AppendColumn("Mode", wxLIST_FORMAT_LEFT, 80);
		bindBox->Add(mpBindingList, 1, wxEXPAND | wxALL, 4);

		// Binding action buttons
		wxBoxSizer *bindBtnRow = new wxBoxSizer(wxHORIZONTAL);
		bindBtnRow->Add(new wxButton(bindParent, ID_REBIND, "Rebind"), 0, wxRIGHT, 4);
		bindBtnRow->Add(new wxButton(bindParent, ID_DEL_BINDING, "Delete"), 0, wxRIGHT, 16);

		// Add new binding controls
		mpControllerChoice = new wxChoice(bindParent, ID_CONTROLLER_CHOICE, wxDefaultPosition, wxSize(160, -1));
		bindBtnRow->Add(mpControllerChoice, 0, wxRIGHT, 4);

		wxArrayString triggerNames;
		for (int i = 0; i < kNumCommonTriggers; ++i)
			triggerNames.Add(kCommonTriggers[i].name);
		mpTriggerChoice = new wxChoice(bindParent, ID_TRIGGER_CHOICE, wxDefaultPosition, wxSize(80, -1), triggerNames);
		mpTriggerChoice->SetSelection(0);
		bindBtnRow->Add(mpTriggerChoice, 0, wxRIGHT, 4);

		bindBtnRow->Add(new wxButton(bindParent, ID_ADD_CAPTURE, "Add && Capture"), 0);

		bindBox->Add(bindBtnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
		mainSizer->Add(bindBox, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
	}

	// --- Presets ---
	{
		wxStaticBoxSizer *presetBox = new wxStaticBoxSizer(wxVERTICAL, this, "Add Preset Map");
		wxWindow *presetParent = presetBox->GetStaticBox();

		wxBoxSizer *presetRow = new wxBoxSizer(wxHORIZONTAL);
		mpPresetList = new wxListBox(presetParent, wxID_ANY, wxDefaultPosition, wxSize(-1, 100));
		presetRow->Add(mpPresetList, 1, wxEXPAND | wxRIGHT, 4);
		presetRow->Add(new wxButton(presetParent, ID_ADD_PRESET, "Add Selected\nPreset"), 0);

		presetBox->Add(presetRow, 1, wxEXPAND | wxALL, 4);
		mainSizer->Add(presetBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
	}

	// --- Bottom buttons ---
	{
		wxBoxSizer *bottomRow = new wxBoxSizer(wxHORIZONTAL);
		bottomRow->Add(new wxButton(this, ID_RESET_DEFAULTS, "Reset to Defaults"), 0, wxRIGHT, 8);
		bottomRow->AddStretchSpacer();
		bottomRow->Add(new wxButton(this, wxID_CLOSE, "Close"), 0);
		mainSizer->Add(bottomRow, 0, wxEXPAND | wxALL, 8);
	}

	SetSizer(mainSizer);

	// Event bindings
	Bind(wxEVT_LISTBOX, &ATInputSetupDialog::OnMapSelected, this, ID_MAP_LIST);
	Bind(wxEVT_CHECKLISTBOX, &ATInputSetupDialog::OnMapEnabled, this, ID_MAP_LIST);
	Bind(wxEVT_BUTTON, &ATInputSetupDialog::OnRemoveMap, this, ID_REMOVE_MAP);
	Bind(wxEVT_BUTTON, &ATInputSetupDialog::OnRebind, this, ID_REBIND);
	Bind(wxEVT_BUTTON, &ATInputSetupDialog::OnDeleteBinding, this, ID_DEL_BINDING);
	Bind(wxEVT_BUTTON, &ATInputSetupDialog::OnAddCapture, this, ID_ADD_CAPTURE);
	Bind(wxEVT_BUTTON, &ATInputSetupDialog::OnAddPreset, this, ID_ADD_PRESET);
	Bind(wxEVT_BUTTON, &ATInputSetupDialog::OnResetDefaults, this, ID_RESET_DEFAULTS);
	Bind(wxEVT_BUTTON, &ATInputSetupDialog::OnRescan, this, ID_RESCAN);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);

	PopulateMapList();
	PopulatePresets();
}

uint32 ATInputSetupDialog::MapDisplayToManager(int displayIdx) const {
	if (displayIdx >= 0 && (size_t)displayIdx < mMapIndexMap.size())
		return mMapIndexMap[displayIdx];
	return (uint32)-1;
}

uint32 ATInputSetupDialog::PresetDisplayToManager(int displayIdx) const {
	if (displayIdx >= 0 && (size_t)displayIdx < mPresetIndexMap.size())
		return mPresetIndexMap[displayIdx];
	return (uint32)-1;
}

void ATInputSetupDialog::PopulateMapList() {
	mpMapList->Clear();
	mMapIndexMap.clear();

	// Collect maps with their original indices
	struct MapEntry { uint32 idx; VDStringA name; wxString label; bool enabled; };
	std::vector<MapEntry> entries;

	uint32 mapCount = mpInputMgr->GetInputMapCount();
	for (uint32 i = 0; i < mapCount; ++i) {
		ATInputMap *imap = nullptr;
		if (!mpInputMgr->GetInputMapByIndex(i, &imap))
			continue;

		VDStringA u8name = VDTextWToU8(VDStringW(imap->GetName()));

		char portInfo[128] = "";
		uint32 ctrlCount = imap->GetControllerCount();
		int pos = 0;
		for (uint32 c = 0; c < ctrlCount && pos < 100; ++c) {
			const ATInputMap::Controller& ctrl = imap->GetController(c);
			if (c > 0)
				pos += snprintf(portInfo + pos, sizeof(portInfo) - pos, ", ");
			pos += snprintf(portInfo + pos, sizeof(portInfo) - pos, "%s P%u",
				ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1);
		}

		wxString label = wxString::Format("%-28s  %s", u8name.c_str(), portInfo);
		entries.push_back({i, u8name, label, mpInputMgr->IsInputMapEnabled(imap)});
	}

	// Sort alphabetically by map name (case-insensitive)
	std::sort(entries.begin(), entries.end(),
		[](const MapEntry& a, const MapEntry& b) {
			return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
		});

	for (auto& e : entries) {
		int idx = mpMapList->Append(e.label);
		mpMapList->Check(idx, e.enabled);
		mMapIndexMap.push_back(e.idx);
	}

	if (mSelectedMapIdx >= 0 && mSelectedMapIdx < (int)mpMapList->GetCount())
		mpMapList->SetSelection(mSelectedMapIdx);

	PopulateBindings();
}

void ATInputSetupDialog::PopulateBindings() {
	mpBindingList->DeleteAllItems();
	mpControllerSummary->SetLabel("");
	PopulateControllers();

	ATInputMap *selMap = nullptr;
	uint32 mgrIdx = MapDisplayToManager(mSelectedMapIdx);
	if (mgrIdx != (uint32)-1)
		mpInputMgr->GetInputMapByIndex(mgrIdx, &selMap);

	if (!selMap)
		return;

	// Controller summary
	uint32 ctrlCount = selMap->GetControllerCount();
	if (ctrlCount > 0) {
		wxString summary = "Controllers: ";
		for (uint32 c = 0; c < ctrlCount; ++c) {
			const ATInputMap::Controller& ctrl = selMap->GetController(c);
			if (c > 0) summary += ", ";
			summary += wxString::Format("%s P%u", ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1);
		}
		mpControllerSummary->SetLabel(summary);
	}

	// Populate binding rows
	uint32 mappingCount = selMap->GetMappingCount();
	for (uint32 m = 0; m < mappingCount; ++m) {
		const ATInputMap::Mapping& mapping = selMap->GetMapping(m);

		long idx = mpBindingList->InsertItem(m, ATGetInputCodeName(mapping.mInputCode));

		uint32 cid = mapping.mControllerId;
		if (cid < ctrlCount) {
			const ATInputMap::Controller& ctrl = selMap->GetController(cid);
			mpBindingList->SetItem(idx, 1,
				wxString::Format("%s P%u", ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1));
		} else {
			mpBindingList->SetItem(idx, 1, "?");
		}

		mpBindingList->SetItem(idx, 2, ATGetInputTriggerName(mapping.mCode));
		mpBindingList->SetItem(idx, 3, ATGetModeName(mapping.mCode));

		// Store original mapping index in item data
		mpBindingList->SetItemData(idx, m);
	}
}

void ATInputSetupDialog::PopulatePresets() {
	mpPresetList->Clear();
	mPresetIndexMap.clear();

	struct PresetEntry { uint32 idx; VDStringA name; };
	std::vector<PresetEntry> entries;

	uint32 presetCount = mpInputMgr->GetPresetInputMapCount();
	for (uint32 i = 0; i < presetCount; ++i) {
		vdrefptr<ATInputMap> preset;
		if (!mpInputMgr->GetPresetInputMapByIndex(i, ~preset))
			continue;
		VDStringA u8name = VDTextWToU8(VDStringW(preset->GetName()));
		entries.push_back({i, u8name});
	}

	std::sort(entries.begin(), entries.end(),
		[](const PresetEntry& a, const PresetEntry& b) {
			return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
		});

	for (auto& e : entries) {
		mpPresetList->Append(wxString::FromUTF8(e.name.c_str()));
		mPresetIndexMap.push_back(e.idx);
	}
}

void ATInputSetupDialog::PopulateControllers() {
	mpControllerChoice->Clear();

	ATInputMap *selMap = nullptr;
	uint32 mgrIdx = MapDisplayToManager(mSelectedMapIdx);
	if (mgrIdx != (uint32)-1)
		mpInputMgr->GetInputMapByIndex(mgrIdx, &selMap);

	if (!selMap)
		return;

	uint32 ctrlCount = selMap->GetControllerCount();
	for (uint32 c = 0; c < ctrlCount; ++c) {
		const ATInputMap::Controller& ctrl = selMap->GetController(c);
		mpControllerChoice->Append(
			wxString::Format("%s P%u", ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1));
	}
	if (ctrlCount > 0)
		mpControllerChoice->SetSelection(0);
}

void ATInputSetupDialog::OnMapSelected(wxCommandEvent& event) {
	mSelectedMapIdx = event.GetInt();
	PopulateBindings();
}

void ATInputSetupDialog::OnMapEnabled(wxCommandEvent& event) {
	int displayIdx = event.GetInt();
	uint32 mgrIdx = MapDisplayToManager(displayIdx);
	if (mgrIdx == (uint32)-1)
		return;

	ATInputMap *imap = nullptr;
	if (mpInputMgr->GetInputMapByIndex(mgrIdx, &imap)) {
		bool checked = mpMapList->IsChecked(displayIdx);
		mpInputMgr->ActivateInputMap(imap, checked);
	}
}

void ATInputSetupDialog::OnRemoveMap(wxCommandEvent&) {
	uint32 mgrIdx = MapDisplayToManager(mSelectedMapIdx);
	if (mgrIdx == (uint32)-1)
		return;

	ATInputMap *imap = nullptr;
	if (mpInputMgr->GetInputMapByIndex(mgrIdx, &imap)) {
		mpInputMgr->RemoveInputMap(imap);
		mSelectedMapIdx = -1;
		PopulateMapList();
	}
}

void ATInputSetupDialog::OnRebind(wxCommandEvent&) {
	long sel = mpBindingList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0)
		return;

	ATInputMap *selMap = nullptr;
	uint32 mgrIdx = MapDisplayToManager(mSelectedMapIdx);
	if (mgrIdx == (uint32)-1)
		return;
	mpInputMgr->GetInputMapByIndex(mgrIdx, &selMap);
	if (!selMap)
		return;

	uint32 mappingIdx = (uint32)mpBindingList->GetItemData(sel);

	ATInputCaptureDialog captureDlg(this);
	if (captureDlg.ShowModal() == wxID_OK) {
		uint32 newCode = captureDlg.GetCapturedCode();
		if (newCode != kATInputCode_None) {
			selMap->SetMappingInputCode(mappingIdx, newCode);
			RefreshMapAfterChange();
		}
	}
}

void ATInputSetupDialog::OnDeleteBinding(wxCommandEvent&) {
	long sel = mpBindingList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0)
		return;

	ATInputMap *selMap = nullptr;
	uint32 mgrIdx = MapDisplayToManager(mSelectedMapIdx);
	if (mgrIdx == (uint32)-1)
		return;
	mpInputMgr->GetInputMapByIndex(mgrIdx, &selMap);
	if (!selMap)
		return;

	uint32 mappingIdx = (uint32)mpBindingList->GetItemData(sel);
	selMap->RemoveMapping(mappingIdx);
	RefreshMapAfterChange();
}

void ATInputSetupDialog::OnAddCapture(wxCommandEvent&) {
	ATInputMap *selMap = nullptr;
	uint32 mgrIdx = MapDisplayToManager(mSelectedMapIdx);
	if (mgrIdx == (uint32)-1)
		return;
	mpInputMgr->GetInputMapByIndex(mgrIdx, &selMap);
	if (!selMap || selMap->GetControllerCount() == 0)
		return;

	int ctrlIdx = mpControllerChoice->GetSelection();
	if (ctrlIdx < 0)
		ctrlIdx = 0;

	int trigIdx = mpTriggerChoice->GetSelection();
	if (trigIdx < 0 || trigIdx >= kNumCommonTriggers)
		trigIdx = 0;

	ATInputCaptureDialog captureDlg(this);
	if (captureDlg.ShowModal() == wxID_OK) {
		uint32 newCode = captureDlg.GetCapturedCode();
		if (newCode != kATInputCode_None) {
			selMap->AddMapping(newCode, (uint32)ctrlIdx, kCommonTriggers[trigIdx].code);
			RefreshMapAfterChange();
		}
	}
}

void ATInputSetupDialog::OnAddPreset(wxCommandEvent&) {
	int sel = mpPresetList->GetSelection();
	if (sel < 0)
		return;

	uint32 presetMgrIdx = PresetDisplayToManager(sel);
	if (presetMgrIdx == (uint32)-1)
		return;

	vdrefptr<ATInputMap> preset;
	if (mpInputMgr->GetPresetInputMapByIndex(presetMgrIdx, ~preset)) {
		mpInputMgr->AddInputMap(preset);
		PopulateMapList();
	}
}

void ATInputSetupDialog::OnResetDefaults(wxCommandEvent&) {
	int result = wxMessageBox(
		"Remove all maps and add back the default set?\n"
		"Includes arrow keys, numpad, mouse, and gamepad presets.",
		"Reset to Defaults", wxYES_NO | wxICON_QUESTION, this);

	if (result == wxYES) {
		mpInputMgr->ResetToDefaults();
		mSelectedMapIdx = -1;
		PopulateMapList();
	}
}

void ATInputSetupDialog::OnRescan(wxCommandEvent&) {
	IATJoystickManager *jm = g_sim.GetJoystickManager();
	if (jm)
		jm->RescanForDevices();
}

void ATInputSetupDialog::RefreshMapAfterChange() {
	ATInputMap *selMap = nullptr;
	uint32 mgrIdx = MapDisplayToManager(mSelectedMapIdx);
	if (mgrIdx != (uint32)-1)
		mpInputMgr->GetInputMapByIndex(mgrIdx, &selMap);

	// Re-register to refresh internal state
	if (selMap) {
		bool wasEnabled = mpInputMgr->IsInputMapEnabled(selMap);
		if (wasEnabled) {
			mpInputMgr->ActivateInputMap(selMap, false);
			mpInputMgr->ActivateInputMap(selMap, true);
		}
	}

	PopulateBindings();
}

///////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////

void ATShowInputSetupDialog(wxWindow *parent) {
	ATInputSetupDialog dlg(parent);
	dlg.ShowModal();
}
