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
#include <wx/filedlg.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>

#include "simulator.h"
#include "cartridge.h"
#include "cartridge_names.h"
#include <at/atio/cartridgetypes.h>

extern ATSimulator g_sim;
void ATImGuiShowToast(const char *message);
uint32 ATCartridgeAutodetectMode(const void *data, uint32 size, vdfastvector<int>& cartModes);

namespace {

class ATCartridgeBrowserDialog : public wxDialog {
public:
	ATCartridgeBrowserDialog(wxWindow *parent);

private:
	void UpdateSlotDisplay();
	void OnLoadCartridge(wxCommandEvent& event);
	void OnLoadRawBinary(wxCommandEvent& event);
	void OnUnloadSlot0(wxCommandEvent& event);
	void OnUnloadSlot1(wxCommandEvent& event);
	void OnMapperOK(wxCommandEvent& event);
	void OnMapperCancel(wxCommandEvent& event);
	void OnShowAllMappers(wxCommandEvent& event);
	void OnClose(wxCommandEvent& event);

	void ShowMapperSelection();
	void PopulateMapperList();
	void LoadWithMapper(int mapper);

	wxStaticText *mpSlot0Info = nullptr;
	wxStaticText *mpSlot1Info = nullptr;
	wxButton *mpUnloadSlot0 = nullptr;
	wxButton *mpUnloadSlot1 = nullptr;

	// Mapper selection panel
	wxStaticBoxSizer *mpMapperBox = nullptr;
	wxListBox *mpMapperList = nullptr;
	wxCheckBox *mpShowAll = nullptr;
	wxButton *mpMapperOK = nullptr;
	wxButton *mpMapperCancel = nullptr;

	vdfastvector<uint8> mLoadBuffer;
	VDStringW mLoadPath;
	vdfastvector<int> mDetectedModes;
	vdfastvector<int> mDisplayModes;
	int mSelectedMapper = -1;

	enum { ID_LOAD = 3200, ID_LOAD_RAW, ID_UNLOAD0, ID_UNLOAD1,
	       ID_MAPPER_OK, ID_MAPPER_CANCEL, ID_SHOW_ALL };
};

ATCartridgeBrowserDialog::ATCartridgeBrowserDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Cartridge Browser", wxDefaultPosition,
		wxSize(500, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// Slot display
	wxStaticBoxSizer *slotBox = new wxStaticBoxSizer(wxVERTICAL, this, "Cartridge Slots");
	wxWindow *sp = slotBox->GetStaticBox();

	wxBoxSizer *slot0Row = new wxBoxSizer(wxHORIZONTAL);
	mpSlot0Info = new wxStaticText(sp, wxID_ANY, "Slot 1: (empty)", wxDefaultPosition,
		wxSize(350, -1), wxST_ELLIPSIZE_END);
	slot0Row->Add(mpSlot0Info, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	mpUnloadSlot0 = new wxButton(sp, ID_UNLOAD0, "Unload", wxDefaultPosition, wxSize(65, -1));
	slot0Row->Add(mpUnloadSlot0, 0);
	slotBox->Add(slot0Row, 0, wxEXPAND | wxALL, 3);

	wxBoxSizer *slot1Row = new wxBoxSizer(wxHORIZONTAL);
	mpSlot1Info = new wxStaticText(sp, wxID_ANY, "Slot 2: (empty)", wxDefaultPosition,
		wxSize(350, -1), wxST_ELLIPSIZE_END);
	slot1Row->Add(mpSlot1Info, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	mpUnloadSlot1 = new wxButton(sp, ID_UNLOAD1, "Unload", wxDefaultPosition, wxSize(65, -1));
	slot1Row->Add(mpUnloadSlot1, 0);
	slotBox->Add(slot1Row, 0, wxEXPAND | wxALL, 3);

	topSizer->Add(slotBox, 0, wxEXPAND | wxALL, 5);

	// Load buttons
	wxBoxSizer *loadRow = new wxBoxSizer(wxHORIZONTAL);
	loadRow->Add(new wxButton(this, ID_LOAD, "Load Cartridge..."), 0, wxRIGHT, 3);
	loadRow->Add(new wxButton(this, ID_LOAD_RAW, "Load Raw Binary..."), 0);
	topSizer->Add(loadRow, 0, wxALL, 5);

	// Mapper selection panel (hidden initially)
	mpMapperBox = new wxStaticBoxSizer(wxVERTICAL, this, "Select Mapper Type");
	wxWindow *mp = mpMapperBox->GetStaticBox();

	mpShowAll = new wxCheckBox(mp, ID_SHOW_ALL, "Show all mapper types");
	mpMapperBox->Add(mpShowAll, 0, wxALL, 3);

	mpMapperList = new wxListBox(mp, wxID_ANY, wxDefaultPosition, wxSize(-1, 150));
	mpMapperBox->Add(mpMapperList, 1, wxEXPAND | wxALL, 3);

	wxBoxSizer *mapperBtnRow = new wxBoxSizer(wxHORIZONTAL);
	mpMapperOK = new wxButton(mp, ID_MAPPER_OK, "OK");
	mpMapperCancel = new wxButton(mp, ID_MAPPER_CANCEL, "Cancel");
	mapperBtnRow->Add(mpMapperOK, 0, wxRIGHT, 3);
	mapperBtnRow->Add(mpMapperCancel, 0);
	mpMapperBox->Add(mapperBtnRow, 0, wxALL, 3);

	topSizer->Add(mpMapperBox, 1, wxEXPAND | wxALL, 5);
	mpMapperBox->Show(false);

	// Close button
	topSizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 5);

	SetSizer(topSizer);
	Layout();

	UpdateSlotDisplay();

	Bind(wxEVT_BUTTON, &ATCartridgeBrowserDialog::OnLoadCartridge, this, ID_LOAD);
	Bind(wxEVT_BUTTON, &ATCartridgeBrowserDialog::OnLoadRawBinary, this, ID_LOAD_RAW);
	Bind(wxEVT_BUTTON, &ATCartridgeBrowserDialog::OnUnloadSlot0, this, ID_UNLOAD0);
	Bind(wxEVT_BUTTON, &ATCartridgeBrowserDialog::OnUnloadSlot1, this, ID_UNLOAD1);
	Bind(wxEVT_BUTTON, &ATCartridgeBrowserDialog::OnMapperOK, this, ID_MAPPER_OK);
	Bind(wxEVT_BUTTON, &ATCartridgeBrowserDialog::OnMapperCancel, this, ID_MAPPER_CANCEL);
	Bind(wxEVT_CHECKBOX, &ATCartridgeBrowserDialog::OnShowAllMappers, this, ID_SHOW_ALL);
	Bind(wxEVT_BUTTON, &ATCartridgeBrowserDialog::OnClose, this, wxID_CLOSE);
}

void ATCartridgeBrowserDialog::UpdateSlotDisplay() {
	for (int slot = 0; slot < 2; ++slot) {
		wxStaticText *info = (slot == 0) ? mpSlot0Info : mpSlot1Info;
		wxButton *btn = (slot == 0) ? mpUnloadSlot0 : mpUnloadSlot1;

		if (g_sim.IsCartridgeAttached(slot)) {
			ATCartridgeEmulator *cart = g_sim.GetCartridge(slot);
			if (cart) {
				VDStringA label;
				label.sprintf("Slot %d: %s", slot + 1,
					ATGetCartridgeModeName((int)cart->GetMode()));

				const wchar_t *path = cart->GetPath();
				if (path && path[0]) {
					VDStringA pathStr = VDTextWToU8(VDStringW(VDFileSplitPath(path)));
					label += " - ";
					label += pathStr;
				}
				info->SetLabel(label.c_str());
			}
			btn->Enable(true);
		} else {
			info->SetLabel(wxString::Format("Slot %d: (empty)", slot + 1));
			btn->Enable(false);
		}
	}
}

void ATCartridgeBrowserDialog::OnLoadCartridge(wxCommandEvent&) {
	wxFileDialog dlg(this, "Load Cartridge", "", "",
		"Cartridge images (*.car;*.rom;*.bin)|*.car;*.rom;*.bin|All files (*)|*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() != wxID_OK)
		return;

	VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str()));

	try {
		ATCartLoadContext ctx;
		ctx.mbReturnOnUnknownMapper = true;

		if (!g_sim.LoadCartridge(0, path.c_str(), &ctx)) {
			if (ctx.mLoadStatus == kATCartLoadStatus_UnknownMapper) {
				// Need mapper selection
				mLoadPath = path;
				mDetectedModes.clear();
				if (ctx.mpCaptureBuffer)
					mLoadBuffer = *ctx.mpCaptureBuffer;

				ATCartridgeAutodetectMode(mLoadBuffer.data(),
					(uint32)mLoadBuffer.size(), mDetectedModes);

				ShowMapperSelection();
				return;
			}
		}
		UpdateSlotDisplay();
	} catch (const MyError& e) {
		wxMessageBox(e.c_str(), "Cartridge Load Error", wxOK | wxICON_ERROR, this);
	}
}

void ATCartridgeBrowserDialog::OnLoadRawBinary(wxCommandEvent&) {
	wxFileDialog dlg(this, "Load Raw Binary", "", "",
		"Binary files (*.rom;*.bin)|*.rom;*.bin|All files (*)|*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() != wxID_OK)
		return;

	mLoadPath = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str()));

	VDFile file;
	if (!file.openNT(mLoadPath.c_str())) {
		wxMessageBox("Cannot open file.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	sint64 fileSize = file.size();
	uint32 len = (uint32)std::min<sint64>(fileSize, 1048576);
	mLoadBuffer.resize(len);
	file.read(mLoadBuffer.data(), len);
	file.close();

	mDetectedModes.clear();
	ATCartridgeAutodetectMode(mLoadBuffer.data(), len, mDetectedModes);
	ShowMapperSelection();
}

void ATCartridgeBrowserDialog::OnUnloadSlot0(wxCommandEvent&) {
	g_sim.UnloadCartridge(0);
	UpdateSlotDisplay();
}

void ATCartridgeBrowserDialog::OnUnloadSlot1(wxCommandEvent&) {
	g_sim.UnloadCartridge(1);
	UpdateSlotDisplay();
}

void ATCartridgeBrowserDialog::ShowMapperSelection() {
	mpMapperBox->Show(true);
	mpShowAll->SetValue(false);
	PopulateMapperList();
	Layout();
	Fit();
}

void ATCartridgeBrowserDialog::PopulateMapperList() {
	mpMapperList->Clear();
	mDisplayModes.clear();

	bool showAll = mpShowAll->GetValue();

	if (showAll) {
		// Show all cartridge modes
		for (int i = 1; i < kATCartridgeModeCount; ++i) {
			const char *name = ATGetCartridgeModeName(i);
			if (name && name[0]) {
				mpMapperList->Append(name);
				mDisplayModes.push_back(i);
			}
		}
	} else {
		// Show only detected/recommended modes
		for (int mode : mDetectedModes) {
			const char *name = ATGetCartridgeModeName(mode);
			if (name && name[0]) {
				mpMapperList->Append(name);
				mDisplayModes.push_back(mode);
			}
		}

		if (mDisplayModes.empty()) {
			// No autodetected modes, show all
			mpShowAll->SetValue(true);
			PopulateMapperList();
			return;
		}
	}

	if (!mDisplayModes.empty())
		mpMapperList->SetSelection(0);
}

void ATCartridgeBrowserDialog::OnShowAllMappers(wxCommandEvent&) {
	PopulateMapperList();
}

void ATCartridgeBrowserDialog::OnMapperOK(wxCommandEvent&) {
	int sel = mpMapperList->GetSelection();
	if (sel < 0 || sel >= (int)mDisplayModes.size()) {
		wxMessageBox("Please select a mapper type.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	int mode = mDisplayModes[sel];
	int mapper = ATGetCartridgeMapperForMode((ATCartridgeMode)mode,
		(uint32)mLoadBuffer.size());
	LoadWithMapper(mapper);
}

void ATCartridgeBrowserDialog::OnMapperCancel(wxCommandEvent&) {
	mpMapperBox->Show(false);
	mLoadBuffer.clear();
	mDetectedModes.clear();
	mDisplayModes.clear();
	Layout();
}

void ATCartridgeBrowserDialog::LoadWithMapper(int mapper) {
	try {
		ATCartLoadContext ctx;
		ctx.mCartMapper = mapper;
		g_sim.LoadCartridge(0, mLoadPath.c_str(), &ctx);
		UpdateSlotDisplay();

		mpMapperBox->Show(false);
		mLoadBuffer.clear();
		mDetectedModes.clear();
		mDisplayModes.clear();
		Layout();
	} catch (const MyError& e) {
		wxMessageBox(e.c_str(), "Cartridge Load Error", wxOK | wxICON_ERROR, this);
	}
}

void ATCartridgeBrowserDialog::OnClose(wxCommandEvent&) {
	EndModal(wxID_CLOSE);
}

} // anonymous namespace

void ATShowCartridgeBrowserDialog(wxWindow *parent) {
	ATCartridgeBrowserDialog dlg(parent);
	dlg.ShowModal();
}
