//	Altirra - Atari 800/800XL/5200 emulator
//	On-screen keyboard for touchscreen/accessibility use
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
#include "simulator.h"
#include "inputmanager.h"

#include <wx/frame.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/tglbtn.h>

extern ATSimulator g_sim;

////////////////////////////////////////////////////////////////////////////////
// On-screen keyboard key definitions
// Each key maps to a VK-style input code for OnButtonDown/Up.

struct OSKKeyDef {
	const char *label;
	int vkCode;
	int spanCols;	// how many grid columns this key spans
};

// Atari keyboard layout rows (matching 800XL layout)
// VK codes: A-Z = 0x41-0x5A, 0-9 = 0x30-0x39, etc.
static const OSKKeyDef s_row0[] = {
	{ "Esc",    0x1B, 1 }, { "1",   '1', 1 }, { "2",   '2', 1 }, { "3",   '3', 1 },
	{ "4",      '4',  1 }, { "5",   '5', 1 }, { "6",   '6', 1 }, { "7",   '7', 1 },
	{ "8",      '8',  1 }, { "9",   '9', 1 }, { "0",   '0', 1 }, { "<",   0xBC, 1 },
	{ ">",      0xBE, 1 }, { "BkSp", 0x08, 1 },
};

static const OSKKeyDef s_row1[] = {
	{ "Tab",  0x09, 1 }, { "Q", 'Q', 1 }, { "W", 'W', 1 }, { "E", 'E', 1 },
	{ "R",    'R',  1 }, { "T", 'T', 1 }, { "Y", 'Y', 1 }, { "U", 'U', 1 },
	{ "I",    'I',  1 }, { "O", 'O', 1 }, { "P", 'P', 1 }, { "-", 0xBD, 1 },
	{ "=",    0xBB, 1 }, { "Ret", 0x0D, 1 },
};

static const OSKKeyDef s_row2[] = {
	{ "Ctrl", 0x11, 1 }, { "A", 'A', 1 }, { "S", 'S', 1 }, { "D", 'D', 1 },
	{ "F",    'F',  1 }, { "G", 'G', 1 }, { "H", 'H', 1 }, { "J", 'J', 1 },
	{ "K",    'K',  1 }, { "L", 'L', 1 }, { ";", 0xBA, 1 }, { "+", 0xBB, 1 },
	{ "*",    '8',  1 }, { "Caps", 0x14, 1 },
};

static const OSKKeyDef s_row3[] = {
	{ "Shift", 0x10, 1 }, { "Z", 'Z', 1 }, { "X", 'X', 1 }, { "C", 'C', 1 },
	{ "V",     'V',  1 }, { "B", 'B', 1 }, { "N", 'N', 1 }, { "M", 'M', 1 },
	{ ",",     0xBC, 1 }, { ".", 0xBE, 1 }, { "/", 0xBF, 1 },
	{ "Inv",   0x00, 1 }, // Atari Inverse Video key
	{ "Space", 0x20, 2 },
};

// Special Atari function keys
static const OSKKeyDef s_rowFn[] = {
	{ "Help",    0x70, 1 }, // F1
	{ "Start",   0x71, 1 }, // F2
	{ "Select",  0x72, 1 }, // F3
	{ "Option",  0x73, 1 }, // F4
	{ "Reset",   0x74, 1 }, // F5
	{ "Break",   0x13, 1 }, // Pause/Break
};

////////////////////////////////////////////////////////////////////////////////

static wxFrame *s_pOSKWindow = nullptr;

class ATOnScreenKeyboard : public wxFrame {
public:
	ATOnScreenKeyboard(wxWindow *parent);
	~ATOnScreenKeyboard();

private:
	void AddKeyRow(wxFlexGridSizer *sizer, const OSKKeyDef *keys, int count);
	void OnKeyPress(wxCommandEvent& evt);
	void OnKeyRelease(wxCommandEvent& evt);
	void OnToggleKey(wxCommandEvent& evt);
	void OnClose(wxCloseEvent& evt);

	int mKeyboardUnit = -1;
	ATInputManager *mpInputMgr = nullptr;

	// Toggle keys (Shift, Ctrl) stay pressed until clicked again
	wxToggleButton *mpShiftBtn = nullptr;
	wxToggleButton *mpCtrlBtn = nullptr;
};

ATOnScreenKeyboard::ATOnScreenKeyboard(wxWindow *parent)
	: wxFrame(parent, wxID_ANY, "On-Screen Keyboard",
		wxDefaultPosition, wxDefaultSize,
		wxFRAME_TOOL_WINDOW | wxCAPTION | wxCLOSE_BOX | wxFRAME_FLOAT_ON_PARENT)
{
	mpInputMgr = g_sim.GetInputManager();
	if (mpInputMgr) {
		ATInputUnitIdentifier oskId;
		memset(&oskId, 0, sizeof(oskId));
		memcpy(oskId.buf, "osk_keyboard\0\0", 14);
		mKeyboardUnit = mpInputMgr->RegisterInputUnit(oskId, L"On-Screen Keyboard", nullptr);
	}

	auto *mainSizer = new wxBoxSizer(wxVERTICAL);

	// Function key row
	auto *fnSizer = new wxFlexGridSizer(6, 2, 2);
	AddKeyRow(fnSizer, s_rowFn, sizeof(s_rowFn) / sizeof(s_rowFn[0]));
	mainSizer->Add(fnSizer, 0, wxALL | wxEXPAND, 4);

	// Main keyboard rows
	int cols = 14;
	auto *gridSizer = new wxFlexGridSizer(cols, 2, 2);
	for (int i = 0; i < cols; i++)
		gridSizer->AddGrowableCol(i);

	AddKeyRow(gridSizer, s_row0, sizeof(s_row0) / sizeof(s_row0[0]));
	AddKeyRow(gridSizer, s_row1, sizeof(s_row1) / sizeof(s_row1[0]));
	AddKeyRow(gridSizer, s_row2, sizeof(s_row2) / sizeof(s_row2[0]));
	AddKeyRow(gridSizer, s_row3, sizeof(s_row3) / sizeof(s_row3[0]));

	mainSizer->Add(gridSizer, 1, wxALL | wxEXPAND, 4);

	SetSizerAndFit(mainSizer);
	Bind(wxEVT_CLOSE_WINDOW, &ATOnScreenKeyboard::OnClose, this);
}

ATOnScreenKeyboard::~ATOnScreenKeyboard() {
	if (mpInputMgr && mKeyboardUnit >= 0) {
		mpInputMgr->UnregisterInputUnit(mKeyboardUnit);
	}
	s_pOSKWindow = nullptr;
}

void ATOnScreenKeyboard::AddKeyRow(wxFlexGridSizer *sizer, const OSKKeyDef *keys, int count) {
	int totalCols = 0;
	for (int i = 0; i < count; i++)
		totalCols += keys[i].spanCols;

	for (int i = 0; i < count; i++) {
		const OSKKeyDef& k = keys[i];

		// Shift and Ctrl are toggle buttons
		if (k.vkCode == 0x10) {
			mpShiftBtn = new wxToggleButton(this, wxID_ANY, k.label, wxDefaultPosition, wxSize(50, 36));
			mpShiftBtn->SetClientData(reinterpret_cast<void *>(static_cast<intptr_t>(k.vkCode)));
			mpShiftBtn->Bind(wxEVT_TOGGLEBUTTON, &ATOnScreenKeyboard::OnToggleKey, this);
			if (k.spanCols > 1)
				sizer->Add(mpShiftBtn, 0, wxEXPAND, 0);
			else
				sizer->Add(mpShiftBtn, 0, wxEXPAND, 0);
			continue;
		}
		if (k.vkCode == 0x11) {
			mpCtrlBtn = new wxToggleButton(this, wxID_ANY, k.label, wxDefaultPosition, wxSize(50, 36));
			mpCtrlBtn->SetClientData(reinterpret_cast<void *>(static_cast<intptr_t>(k.vkCode)));
			mpCtrlBtn->Bind(wxEVT_TOGGLEBUTTON, &ATOnScreenKeyboard::OnToggleKey, this);
			sizer->Add(mpCtrlBtn, 0, wxEXPAND, 0);
			continue;
		}

		auto *btn = new wxButton(this, wxID_ANY, k.label, wxDefaultPosition, wxSize(50, 36));
		btn->SetClientData(reinterpret_cast<void *>(static_cast<intptr_t>(k.vkCode)));
		btn->Bind(wxEVT_LEFT_DOWN, [this, btn](wxMouseEvent&) {
			int code = static_cast<int>(reinterpret_cast<intptr_t>(btn->GetClientData()));
			if (mpInputMgr && mKeyboardUnit >= 0 && code > 0)
				mpInputMgr->OnButtonDown(mKeyboardUnit, code);
		});
		btn->Bind(wxEVT_LEFT_UP, [this, btn](wxMouseEvent&) {
			int code = static_cast<int>(reinterpret_cast<intptr_t>(btn->GetClientData()));
			if (mpInputMgr && mKeyboardUnit >= 0 && code > 0)
				mpInputMgr->OnButtonUp(mKeyboardUnit, code);

			// Auto-release Shift/Ctrl after a key press
			if (mpShiftBtn && mpShiftBtn->GetValue()) {
				mpShiftBtn->SetValue(false);
				if (mpInputMgr && mKeyboardUnit >= 0)
					mpInputMgr->OnButtonUp(mKeyboardUnit, 0x10);
			}
			if (mpCtrlBtn && mpCtrlBtn->GetValue()) {
				mpCtrlBtn->SetValue(false);
				if (mpInputMgr && mKeyboardUnit >= 0)
					mpInputMgr->OnButtonUp(mKeyboardUnit, 0x11);
			}
		});

		if (k.spanCols > 1)
			sizer->Add(btn, 0, wxEXPAND, 0);
		else
			sizer->Add(btn, 0, wxEXPAND, 0);
	}

	// Fill remaining columns with spacers
	int gridCols = sizer->GetCols();
	int usedCols = totalCols;
	for (int i = usedCols; i < gridCols; i++)
		sizer->AddSpacer(0);
}

void ATOnScreenKeyboard::OnKeyPress(wxCommandEvent& evt) {
}

void ATOnScreenKeyboard::OnKeyRelease(wxCommandEvent& evt) {
}

void ATOnScreenKeyboard::OnToggleKey(wxCommandEvent& evt) {
	auto *btn = static_cast<wxToggleButton *>(evt.GetEventObject());
	int code = static_cast<int>(reinterpret_cast<intptr_t>(btn->GetClientData()));
	if (!mpInputMgr || mKeyboardUnit < 0)
		return;

	if (btn->GetValue())
		mpInputMgr->OnButtonDown(mKeyboardUnit, code);
	else
		mpInputMgr->OnButtonUp(mKeyboardUnit, code);
}

void ATOnScreenKeyboard::OnClose(wxCloseEvent& evt) {
	// Release any held toggle keys
	if (mpInputMgr && mKeyboardUnit >= 0) {
		if (mpShiftBtn && mpShiftBtn->GetValue())
			mpInputMgr->OnButtonUp(mKeyboardUnit, 0x10);
		if (mpCtrlBtn && mpCtrlBtn->GetValue())
			mpInputMgr->OnButtonUp(mKeyboardUnit, 0x11);
	}
	Destroy();
}

////////////////////////////////////////////////////////////////////////////////
// Public entry point

void ATShowOnScreenKeyboard(wxWindow *parent) {
	if (s_pOSKWindow) {
		s_pOSKWindow->Raise();
		return;
	}

	s_pOSKWindow = new ATOnScreenKeyboard(parent);
	s_pOSKWindow->Show();
}

void ATCloseOnScreenKeyboard() {
	if (s_pOSKWindow) {
		s_pOSKWindow->Close();
		s_pOSKWindow = nullptr;
	}
}
