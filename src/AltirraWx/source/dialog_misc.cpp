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
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/listctrl.h>
#include <wx/listbox.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>

#include "simulator.h"
#include "settings.h"
#include "cheatengine.h"
#include "compatdb.h"
#include "oshelper.h"
#include "resource.h"

#include "dialogs_wx.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////
// Profile Manager dialog
///////////////////////////////////////////////////////////////////////////

class ATProfileManagerDialog : public wxDialog {
public:
	ATProfileManagerDialog(wxWindow *parent);

private:
	void PopulateList();
	void OnSwitchTo(wxCommandEvent&);
	void OnNew(wxCommandEvent&);
	void OnRename(wxCommandEvent&);
	void OnDelete(wxCommandEvent&);

	wxListBox *mpList;
	vdfastvector<uint32> mProfileIds;
	vdfastvector<uint32> mVisibleIds;

	enum { ID_SWITCH = 5000, ID_NEW, ID_RENAME, ID_DELETE };
};

ATProfileManagerDialog::ATProfileManagerDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Profile Manager", wxDefaultPosition, wxSize(450, 350))
{
	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

	mpList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	sizer->Add(mpList, 1, wxEXPAND | wxALL, 8);

	wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->Add(new wxButton(this, ID_SWITCH, "Switch To"), 0, wxRIGHT, 4);
	btnSizer->Add(new wxButton(this, ID_NEW, "New..."), 0, wxRIGHT, 4);
	btnSizer->Add(new wxButton(this, ID_RENAME, "Rename..."), 0, wxRIGHT, 4);
	btnSizer->Add(new wxButton(this, ID_DELETE, "Delete"), 0);
	sizer->Add(btnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

	sizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	SetSizer(sizer);

	Bind(wxEVT_BUTTON, &ATProfileManagerDialog::OnSwitchTo, this, ID_SWITCH);
	Bind(wxEVT_BUTTON, &ATProfileManagerDialog::OnNew, this, ID_NEW);
	Bind(wxEVT_BUTTON, &ATProfileManagerDialog::OnRename, this, ID_RENAME);
	Bind(wxEVT_BUTTON, &ATProfileManagerDialog::OnDelete, this, ID_DELETE);

	PopulateList();
}

void ATProfileManagerDialog::PopulateList() {
	mpList->Clear();
	mVisibleIds.clear();

	ATSettingsProfileEnum(mProfileIds);
	uint32 currentId = ATSettingsGetCurrentProfileId();

	for (uint32 id : mProfileIds) {
		if (!ATSettingsProfileGetVisible(id))
			continue;

		VDStringW name = ATSettingsProfileGetName(id);
		VDStringA u8name = VDTextWToU8(name);
		if (id == currentId)
			u8name += " (active)";

		mpList->Append(wxString::FromUTF8(u8name.c_str()));
		mVisibleIds.push_back(id);
	}
}

void ATProfileManagerDialog::OnSwitchTo(wxCommandEvent&) {
	int sel = mpList->GetSelection();
	if (sel == wxNOT_FOUND)
		return;
	uint32 id = mVisibleIds[sel];
	if (id != ATSettingsGetCurrentProfileId()) {
		ATSettingsSwitchProfile(id);
		PopulateList();
	}
}

void ATProfileManagerDialog::OnNew(wxCommandEvent&) {
	wxTextEntryDialog dlg(this, "Profile name:", "New Profile");
	if (dlg.ShowModal() != wxID_OK)
		return;
	wxString name = dlg.GetValue();
	if (name.IsEmpty())
		return;

	uint32 newId = ATSettingsGenerateProfileId();
	VDStringW wname = VDTextU8ToW(VDStringA(name.utf8_str().data()));
	ATSettingsProfileSetName(newId, wname.c_str());
	ATSettingsProfileSetVisible(newId, true);
	ATSettingsProfileSetCategoryMask(newId, kATSettingsCategory_AllCategories);
	ATSettingsSwitchProfile(newId);
	PopulateList();
}

void ATProfileManagerDialog::OnRename(wxCommandEvent&) {
	int sel = mpList->GetSelection();
	if (sel == wxNOT_FOUND)
		return;
	uint32 id = mVisibleIds[sel];

	// Don't rename default profiles
	for (int i = 0; i < kATDefaultProfileCount; ++i) {
		if (ATGetDefaultProfileId((ATDefaultProfile)i) == id)
			return;
	}

	VDStringW curName = ATSettingsProfileGetName(id);
	VDStringA u8cur = VDTextWToU8(curName);
	wxTextEntryDialog dlg(this, "New name:", "Rename Profile",
		wxString::FromUTF8(u8cur.c_str()));
	if (dlg.ShowModal() != wxID_OK)
		return;
	wxString name = dlg.GetValue();
	if (name.IsEmpty())
		return;

	VDStringW wname = VDTextU8ToW(VDStringA(name.utf8_str().data()));
	ATSettingsProfileSetName(id, wname.c_str());
	PopulateList();
}

void ATProfileManagerDialog::OnDelete(wxCommandEvent&) {
	int sel = mpList->GetSelection();
	if (sel == wxNOT_FOUND)
		return;
	uint32 id = mVisibleIds[sel];

	// Don't delete default profiles
	for (int i = 0; i < kATDefaultProfileCount; ++i) {
		if (ATGetDefaultProfileId((ATDefaultProfile)i) == id)
			return;
	}

	VDStringW dname = ATSettingsProfileGetName(id);
	VDStringA u8dname = VDTextWToU8(dname);
	if (wxMessageBox(
		wxString::Format("Delete profile \"%s\"?", u8dname.c_str()),
		"Confirm Delete", wxYES_NO | wxICON_QUESTION, this) != wxYES)
		return;

	ATSettingsProfileDelete(id);
	PopulateList();
}

void ATShowProfileManagerDialog(wxWindow *parent) {
	ATProfileManagerDialog dlg(parent);
	dlg.ShowModal();
}

///////////////////////////////////////////////////////////////////////////
// Video Recording — stub (complex IATVideoWriter Init API requires
// careful integration; recording is handled by the ImGui build for now)
///////////////////////////////////////////////////////////////////////////

void ATStopVideoRecording() {
	// TODO: Implement video recording for wxWidgets build
}

bool ATIsVideoRecording() {
	return false;
}

void ATShowVideoRecordDialog(wxWindow *parent) {
	wxMessageBox("Video recording is not yet available in the wxWidgets build.",
		"Not Implemented", wxOK | wxICON_INFORMATION, parent);
}

///////////////////////////////////////////////////////////////////////////
// Compatibility Browser dialog
///////////////////////////////////////////////////////////////////////////

class ATCompatBrowserDialog : public wxDialog {
public:
	ATCompatBrowserDialog(wxWindow *parent);

private:
	void OnFilterChanged(wxCommandEvent&);
	void PopulateList();

	wxTextCtrl *mpFilter;
	wxListCtrl *mpList;

	const ATCompatDBHeader *mpHeader = nullptr;
	ATCompatDBView mView;
};

ATCompatBrowserDialog::ATCompatBrowserDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Compatibility Database", wxDefaultPosition, wxSize(600, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	// Load compat DB from embedded resources
	size_t len = 0;
	const void *data = ATLockResource(IDR_COMPATDB, len);
	if (data && len >= sizeof(ATCompatDBHeader)) {
		auto *hdr = (const ATCompatDBHeader *)data;
		if (hdr->Validate(len)) {
			mpHeader = hdr;
			mView = ATCompatDBView(hdr);
		}
	}

	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

	if (!mpHeader || !mView.IsValid()) {
		sizer->Add(new wxStaticText(this, wxID_ANY, "Compatibility database not available."),
			0, wxALL, 16);
	} else {
		wxString stats = wxString::Format("Titles: %u  |  Tags: %u",
			(unsigned)mpHeader->mTitleTable.size(),
			(unsigned)mpHeader->mTagTable.size());
		sizer->Add(new wxStaticText(this, wxID_ANY, stats), 0, wxALL, 8);

		mpFilter = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
		mpFilter->SetHint("Filter by title name...");
		sizer->Add(mpFilter, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

		mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
			wxLC_REPORT | wxLC_SINGLE_SEL);
		mpList->AppendColumn("Title", wxLIST_FORMAT_LEFT, 300);
		mpList->AppendColumn("Tags", wxLIST_FORMAT_LEFT, 260);
		sizer->Add(mpList, 1, wxEXPAND | wxALL, 8);

		mpFilter->Bind(wxEVT_TEXT, &ATCompatBrowserDialog::OnFilterChanged, this);
		PopulateList();
	}

	sizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	SetSizer(sizer);
}

void ATCompatBrowserDialog::OnFilterChanged(wxCommandEvent&) {
	PopulateList();
}

void ATCompatBrowserDialog::PopulateList() {
	if (!mpHeader || !mpList)
		return;

	mpList->DeleteAllItems();

	VDStringA filterStr;
	if (mpFilter) {
		wxString f = mpFilter->GetValue();
		filterStr = VDStringA(f.utf8_str().data());
		for (char& c : filterStr)
			c = (char)tolower((unsigned char)c);
	}

	auto& titles = mpHeader->mTitleTable;
	for (uint32 i = 0; i < (uint32)titles.size(); ++i) {
		const char *name = titles[i].mName.c_str();

		if (!filterStr.empty()) {
			VDStringA nameLower(name);
			for (char& c : nameLower)
				c = (char)tolower((unsigned char)c);
			if (nameLower.find(filterStr.c_str()) == VDStringA::npos)
				continue;
		}

		long idx = mpList->InsertItem(mpList->GetItemCount(), wxString::FromUTF8(name));

		// Build tags string
		VDStringA tags;
		for (uint32 tagId : titles[i].mTagIds) {
			ATCompatKnownTag knownTag = mView.GetKnownTag(tagId);
			const char *key = nullptr;
			if (knownTag != kATCompatKnownTag_None)
				key = ATCompatGetKeyForKnownTag(knownTag);
			else if (tagId < mpHeader->mTagTable.size())
				key = mpHeader->mTagTable[tagId].mKey.c_str();

			if (key) {
				if (!tags.empty())
					tags += ", ";
				tags += key;
			}
		}
		mpList->SetItem(idx, 1, wxString::FromUTF8(tags.c_str()));
	}
}

void ATShowCompatBrowserDialog(wxWindow *parent) {
	ATCompatBrowserDialog dlg(parent);
	dlg.ShowModal();
}

///////////////////////////////////////////////////////////////////////////
// Cheat Engine dialog
///////////////////////////////////////////////////////////////////////////

class ATCheaterDialog : public wxDialog {
public:
	ATCheaterDialog(wxWindow *parent);
	~ATCheaterDialog();

private:
	void OnSearch(wxCommandEvent&);
	void OnClearAll(wxCommandEvent&);
	void OnAddCheat(wxCommandEvent&);
	void OnRemoveCheat(wxCommandEvent&);
	void OnLoadCheats(wxCommandEvent&);
	void OnSaveCheats(wxCommandEvent&);
	void RefreshResults();
	void RefreshCheats();

	wxChoice *mpMode;
	wxChoice *mpBitSize;
	wxTextCtrl *mpValue;
	wxListCtrl *mpResultsList;
	wxListCtrl *mpCheatsList;
	wxStaticText *mpResultCount;

	vdfastvector<uint32> mResults;
	bool mInitialized = false;

	enum {
		ID_SEARCH = 5200, ID_CLEAR_ALL, ID_ADD_CHEAT,
		ID_REMOVE_CHEAT, ID_LOAD_CHEATS, ID_SAVE_CHEATS
	};
};

ATCheaterDialog::ATCheaterDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Cheat Engine", wxDefaultPosition, wxSize(700, 500),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	// Enable cheat engine
	if (!g_sim.GetCheatEngine())
		g_sim.SetCheatEngineEnabled(true);

	wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

	// Controls row
	wxBoxSizer *ctrlSizer = new wxBoxSizer(wxHORIZONTAL);

	wxArrayString modes;
	modes.Add("New Snapshot");
	modes.Add("= Unchanged");
	modes.Add("!= Changed");
	modes.Add("< Less Than Previous");
	modes.Add("<= Less or Equal");
	modes.Add("> Greater Than Previous");
	modes.Add(">= Greater or Equal");
	modes.Add("=X Equal to Value");
	mpMode = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(180, -1), modes);
	mpMode->SetSelection(0);
	ctrlSizer->Add(mpMode, 0, wxRIGHT, 4);

	wxArrayString bits;
	bits.Add("8-bit");
	bits.Add("16-bit");
	mpBitSize = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, bits);
	mpBitSize->SetSelection(0);
	ctrlSizer->Add(mpBitSize, 0, wxRIGHT, 4);

	mpValue = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(60, -1));
	mpValue->SetHint("Value");
	ctrlSizer->Add(mpValue, 0, wxRIGHT, 4);

	ctrlSizer->Add(new wxButton(this, ID_SEARCH, "Search"), 0, wxRIGHT, 4);
	ctrlSizer->Add(new wxButton(this, ID_CLEAR_ALL, "Clear All"), 0);

	mainSizer->Add(ctrlSizer, 0, wxALL, 8);

	// Two-pane layout: Results | Active Cheats
	wxBoxSizer *paneSizer = new wxBoxSizer(wxHORIZONTAL);

	// Results pane
	wxBoxSizer *resultSizer = new wxBoxSizer(wxVERTICAL);
	mpResultCount = new wxStaticText(this, wxID_ANY, "Results: 0");
	resultSizer->Add(mpResultCount, 0, wxBOTTOM, 4);

	mpResultsList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	mpResultsList->AppendColumn("Address", wxLIST_FORMAT_LEFT, 80);
	mpResultsList->AppendColumn("Value", wxLIST_FORMAT_LEFT, 80);
	resultSizer->Add(mpResultsList, 1, wxEXPAND);

	resultSizer->Add(new wxButton(this, ID_ADD_CHEAT, "Add as Cheat"), 0, wxTOP, 4);

	paneSizer->Add(resultSizer, 1, wxEXPAND | wxRIGHT, 4);

	// Cheats pane
	wxBoxSizer *cheatSizer = new wxBoxSizer(wxVERTICAL);
	cheatSizer->Add(new wxStaticText(this, wxID_ANY, "Active Cheats:"), 0, wxBOTTOM, 4);

	mpCheatsList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	mpCheatsList->AppendColumn("Address", wxLIST_FORMAT_LEFT, 70);
	mpCheatsList->AppendColumn("Value", wxLIST_FORMAT_LEFT, 70);
	mpCheatsList->AppendColumn("Enabled", wxLIST_FORMAT_LEFT, 60);
	cheatSizer->Add(mpCheatsList, 1, wxEXPAND);

	wxBoxSizer *cheatBtnSizer = new wxBoxSizer(wxHORIZONTAL);
	cheatBtnSizer->Add(new wxButton(this, ID_REMOVE_CHEAT, "Remove"), 0, wxRIGHT, 4);
	cheatBtnSizer->Add(new wxButton(this, ID_LOAD_CHEATS, "Load..."), 0, wxRIGHT, 4);
	cheatBtnSizer->Add(new wxButton(this, ID_SAVE_CHEATS, "Save..."), 0);
	cheatSizer->Add(cheatBtnSizer, 0, wxTOP, 4);

	paneSizer->Add(cheatSizer, 1, wxEXPAND);

	mainSizer->Add(paneSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	mainSizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	SetSizer(mainSizer);

	Bind(wxEVT_BUTTON, &ATCheaterDialog::OnSearch, this, ID_SEARCH);
	Bind(wxEVT_BUTTON, &ATCheaterDialog::OnClearAll, this, ID_CLEAR_ALL);
	Bind(wxEVT_BUTTON, &ATCheaterDialog::OnAddCheat, this, ID_ADD_CHEAT);
	Bind(wxEVT_BUTTON, &ATCheaterDialog::OnRemoveCheat, this, ID_REMOVE_CHEAT);
	Bind(wxEVT_BUTTON, &ATCheaterDialog::OnLoadCheats, this, ID_LOAD_CHEATS);
	Bind(wxEVT_BUTTON, &ATCheaterDialog::OnSaveCheats, this, ID_SAVE_CHEATS);

	RefreshCheats();
}

ATCheaterDialog::~ATCheaterDialog() {
	// Leave cheat engine enabled — cheats may still be active
}

void ATCheaterDialog::OnSearch(wxCommandEvent&) {
	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;

	int mode = mpMode->GetSelection();
	bool bit16 = mpBitSize->GetSelection() != 0;

	if (mode == 0) {
		// New snapshot
		ce->Snapshot(kATCheatSnapMode_Replace, 0, false);
		mInitialized = true;
	} else {
		uint32 value = 0;
		if (mode == 7) { // Equal to value
			wxString v = mpValue->GetValue().Trim();
			VDStringA vs(v.utf8_str().data());
			const char *p = vs.c_str();
			if (*p == '$')
				sscanf(p + 1, "%x", &value);
			else
				sscanf(p, "%u", &value);
		}
		ce->Snapshot((ATCheatSnapshotMode)mode, value, bit16);
	}
	RefreshResults();
}

void ATCheaterDialog::OnClearAll(wxCommandEvent&) {
	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;
	ce->Clear();
	mInitialized = false;
	mResults.clear();
	RefreshResults();
	RefreshCheats();
}

void ATCheaterDialog::OnAddCheat(wxCommandEvent&) {
	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;

	long sel = mpResultsList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= (long)mResults.size())
		return;

	bool bit16 = mpBitSize->GetSelection() != 0;
	ce->AddCheat(mResults[sel], bit16);
	RefreshCheats();
}

void ATCheaterDialog::OnRemoveCheat(wxCommandEvent&) {
	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;

	long sel = mpCheatsList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= (long)ce->GetCheatCount())
		return;

	ce->RemoveCheatByIndex(sel);
	RefreshCheats();
}

void ATCheaterDialog::OnLoadCheats(wxCommandEvent&) {
	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;

	wxFileDialog fdlg(this, "Load Cheats", "", "",
		"Cheat Files (*.atcheats)|*.atcheats|All Files (*)|*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (fdlg.ShowModal() != wxID_OK)
		return;

	VDStringW wpath = VDTextU8ToW(VDStringA(fdlg.GetPath().utf8_str().data()));
	try {
		ce->Load(wpath.c_str());
		RefreshResults();
		RefreshCheats();
	} catch (const std::exception& e) {
		wxMessageBox(wxString::Format("Load failed: %s", e.what()),
			"Error", wxOK | wxICON_ERROR, this);
	}
}

void ATCheaterDialog::OnSaveCheats(wxCommandEvent&) {
	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;

	wxFileDialog fdlg(this, "Save Cheats", "", "",
		"Cheat Files (*.atcheats)|*.atcheats|All Files (*)|*",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (fdlg.ShowModal() != wxID_OK)
		return;

	VDStringW wpath = VDTextU8ToW(VDStringA(fdlg.GetPath().utf8_str().data()));
	try {
		ce->Save(wpath.c_str());
	} catch (const std::exception& e) {
		wxMessageBox(wxString::Format("Save failed: %s", e.what()),
			"Error", wxOK | wxICON_ERROR, this);
	}
}

void ATCheaterDialog::RefreshResults() {
	mpResultsList->DeleteAllItems();

	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;

	enum { kMaxResults = 1000 };
	mResults.resize(kMaxResults);
	uint32 count = ce->GetValidOffsets(mResults.data(), kMaxResults);
	mResults.resize(count);

	mpResultCount->SetLabel(wxString::Format("Results: %u", count));

	bool bit16 = mpBitSize->GetSelection() != 0;
	uint32 displayCount = std::min(count, (uint32)500);
	for (uint32 i = 0; i < displayCount; ++i) {
		uint32 offset = mResults[i];
		uint32 val = ce->GetOffsetCurrentValue(offset, bit16);

		char addrBuf[16];
		snprintf(addrBuf, sizeof(addrBuf), "$%04X", offset);
		long idx = mpResultsList->InsertItem(i, addrBuf);

		char valBuf[16];
		if (bit16)
			snprintf(valBuf, sizeof(valBuf), "$%04X (%u)", val, val);
		else
			snprintf(valBuf, sizeof(valBuf), "$%02X (%u)", val, val);
		mpResultsList->SetItem(idx, 1, valBuf);
	}
}

void ATCheaterDialog::RefreshCheats() {
	mpCheatsList->DeleteAllItems();

	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;

	for (uint32 i = 0; i < ce->GetCheatCount(); ++i) {
		const ATCheatEngine::Cheat& cheat = ce->GetCheatByIndex(i);

		char addrBuf[16];
		snprintf(addrBuf, sizeof(addrBuf), "$%04X", cheat.mAddress);
		long idx = mpCheatsList->InsertItem(i, addrBuf);

		char valBuf[16];
		if (cheat.mb16Bit)
			snprintf(valBuf, sizeof(valBuf), "$%04X", cheat.mValue);
		else
			snprintf(valBuf, sizeof(valBuf), "$%02X", cheat.mValue);
		mpCheatsList->SetItem(idx, 1, valBuf);

		mpCheatsList->SetItem(idx, 2, cheat.mbEnabled ? "Yes" : "No");
	}
}

void ATShowCheaterDialog(wxWindow *parent) {
	ATCheaterDialog dlg(parent);
	dlg.ShowModal();
}
