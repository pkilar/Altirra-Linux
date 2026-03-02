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
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/vdtypes.h>

#include "simulator.h"
#include "firmwaremanager.h"
#include "firmware_names.h"
#include "firmwaredetect.h"

extern ATSimulator g_sim;
void ATImGuiShowToast(const char *message);
void ATGetFirmwareSearchPaths(vdvector<VDStringW>& paths);

namespace {

struct DefaultFirmwareEntry {
	const char *label;
	ATFirmwareType type;
};

static const DefaultFirmwareEntry kDefaultEntries[] = {
	{ "400/800 Kernel", kATFirmwareType_Kernel800_OSB },
	{ "XL/XE Kernel",   kATFirmwareType_KernelXL },
	{ "BASIC",          kATFirmwareType_Basic },
	{ "5200 OS",        kATFirmwareType_Kernel5200 },
	{ "XEGS OS",        kATFirmwareType_KernelXEGS },
};

class ATFirmwareManagerDialog : public wxDialog {
public:
	ATFirmwareManagerDialog(wxWindow *parent);

private:
	void PopulateFirmwareList();
	void PopulateDefaultChoices();
	void OnScanDirs(wxCommandEvent& event);
	void OnAddFile(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);
	void OnDefaultChanged(wxCommandEvent& event);
	void OnClose(wxCommandEvent& event);

	wxListCtrl *mpFirmwareList = nullptr;
	wxChoice *mpDefaults[5] = {};

	struct FirmwareEntry {
		uint64 id;
		VDStringA name;
		ATFirmwareType type;
		VDStringA path;
	};
	vdvector<FirmwareEntry> mFirmwareEntries;
	bool mDefaultsChanged = false;

	// Store firmware IDs matching each default dropdown
	struct DefaultChoiceData {
		vdvector<uint64> ids;
	};
	DefaultChoiceData mDefaultChoiceData[5];

	enum { ID_SCAN = 3100, ID_ADD, ID_REMOVE };
};

ATFirmwareManagerDialog::ATFirmwareManagerDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Firmware Manager", wxDefaultPosition,
		wxSize(650, 500), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// Toolbar
	wxBoxSizer *toolRow = new wxBoxSizer(wxHORIZONTAL);
	toolRow->Add(new wxButton(this, ID_SCAN, "Scan Directories"), 0, wxRIGHT, 3);
	toolRow->Add(new wxButton(this, ID_ADD, "Add File..."), 0, wxRIGHT, 3);
	toolRow->Add(new wxButton(this, ID_REMOVE, "Remove"), 0);
	topSizer->Add(toolRow, 0, wxALL, 5);

	// Firmware list
	mpFirmwareList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition,
		wxSize(-1, 200), wxLC_REPORT | wxLC_SINGLE_SEL);
	mpFirmwareList->AppendColumn("Name", wxLIST_FORMAT_LEFT, 200);
	mpFirmwareList->AppendColumn("Type", wxLIST_FORMAT_LEFT, 120);
	mpFirmwareList->AppendColumn("Path", wxLIST_FORMAT_LEFT, 280);
	topSizer->Add(mpFirmwareList, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

	// Default firmware section
	wxStaticBoxSizer *defBox = new wxStaticBoxSizer(wxVERTICAL, this, "Default Firmware");
	wxWindow *dp = defBox->GetStaticBox();

	wxFlexGridSizer *defGrid = new wxFlexGridSizer(2, 5, 10);
	defGrid->AddGrowableCol(1, 1);

	for (int i = 0; i < 5; ++i) {
		defGrid->Add(new wxStaticText(dp, wxID_ANY, kDefaultEntries[i].label),
			0, wxALIGN_CENTER_VERTICAL);
		mpDefaults[i] = new wxChoice(dp, wxID_ANY);
		defGrid->Add(mpDefaults[i], 1, wxEXPAND);
	}
	defBox->Add(defGrid, 0, wxEXPAND | wxALL, 3);
	topSizer->Add(defBox, 0, wxEXPAND | wxALL, 5);

	// Close button
	topSizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 5);

	SetSizer(topSizer);

	PopulateFirmwareList();
	PopulateDefaultChoices();

	Bind(wxEVT_BUTTON, &ATFirmwareManagerDialog::OnScanDirs, this, ID_SCAN);
	Bind(wxEVT_BUTTON, &ATFirmwareManagerDialog::OnAddFile, this, ID_ADD);
	Bind(wxEVT_BUTTON, &ATFirmwareManagerDialog::OnRemove, this, ID_REMOVE);
	Bind(wxEVT_BUTTON, &ATFirmwareManagerDialog::OnClose, this, wxID_CLOSE);
	Bind(wxEVT_CHOICE, &ATFirmwareManagerDialog::OnDefaultChanged, this);
}

void ATFirmwareManagerDialog::PopulateFirmwareList() {
	mpFirmwareList->DeleteAllItems();
	mFirmwareEntries.clear();

	ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();
	vdvector<ATFirmwareInfo> fwList;
	fwMgr->GetFirmwareList(fwList);

	int row = 0;
	for (const auto& fw : fwList) {
		if (!fw.mbVisible)
			continue;

		FirmwareEntry entry;
		entry.id = fw.mId;
		entry.name = VDTextWToU8(fw.mName);
		entry.type = fw.mType;
		entry.path = VDTextWToU8(fw.mPath);

		long idx = mpFirmwareList->InsertItem(row, entry.name.c_str());
		mpFirmwareList->SetItem(idx, 1, ATGetFirmwareTypeDisplayName(fw.mType));
		mpFirmwareList->SetItem(idx, 2, entry.path.c_str());

		mFirmwareEntries.push_back(std::move(entry));
		++row;
	}
}

void ATFirmwareManagerDialog::PopulateDefaultChoices() {
	ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();
	vdvector<ATFirmwareInfo> fwList;
	fwMgr->GetFirmwareList(fwList);

	for (int i = 0; i < 5; ++i) {
		mpDefaults[i]->Clear();
		mDefaultChoiceData[i].ids.clear();

		// First entry is always "Built-in HLE"
		mpDefaults[i]->Append("Built-in HLE");
		mDefaultChoiceData[i].ids.push_back(0);

		ATFirmwareType targetType = kDefaultEntries[i].type;
		uint64 currentDefault = fwMgr->GetDefaultFirmware(targetType);

		int sel = 0;
		int choiceIdx = 1;
		for (const auto& fw : fwList) {
			if (!fw.mbVisible || fw.mType != targetType)
				continue;

			VDStringA name = VDTextWToU8(fw.mName);
			mpDefaults[i]->Append(name.c_str());
			mDefaultChoiceData[i].ids.push_back(fw.mId);

			if (fw.mId == currentDefault)
				sel = choiceIdx;
			++choiceIdx;
		}

		mpDefaults[i]->SetSelection(sel);
	}
}

void ATFirmwareManagerDialog::OnScanDirs(wxCommandEvent&) {
	ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();
	vdvector<VDStringW> searchPaths;
	ATGetFirmwareSearchPaths(searchPaths);

	int added = 0;
	for (const auto& dirPath : searchPaths) {
		VDDirectoryIterator it(VDMakePath(dirPath.c_str(), L"*").c_str());
		while (it.Next()) {
			if (it.IsDirectory())
				continue;

			sint64 fileSize = it.GetSize();
			if (!ATFirmwareAutodetectCheckSize(fileSize))
				continue;

			VDStringW fullPath = VDMakePath(dirPath.c_str(), it.GetName());

			// Check if already registered
			bool duplicate = false;
			vdvector<ATFirmwareInfo> existing;
			fwMgr->GetFirmwareList(existing);
			for (const auto& fw : existing) {
				if (fw.mPath == fullPath) {
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;

			// Read and autodetect
			VDFile file;
			if (!file.openNT(fullPath.c_str()))
				continue;

			uint32 len = (uint32)std::min<sint64>(fileSize, 1048576);
			vdfastvector<uint8> buf(len);
			file.read(buf.data(), len);
			file.close();

			ATFirmwareInfo info;
			ATSpecificFirmwareType specificType;
			sint32 knownIdx;
			ATFirmwareDetection detection = ATFirmwareAutodetect(
				buf.data(), len, info, specificType, knownIdx);

			if (detection != ATFirmwareDetection::None) {
				info.mPath = fullPath;
				info.mId = kATFirmwareId_Custom + (uint64)(rand() & 0xFFFF) + ((uint64)rand() << 16);
				info.mbVisible = true;
				info.mbAutoselect = true;
				if (info.mName.empty())
					info.mName = it.GetName();
				fwMgr->AddFirmware(info);
				++added;
			}
		}
	}

	PopulateFirmwareList();
	PopulateDefaultChoices();

	if (added > 0)
		ATImGuiShowToast(VDStringA().sprintf("%d firmware file(s) added", added).c_str());
	else
		ATImGuiShowToast("No new firmware found");
}

void ATFirmwareManagerDialog::OnAddFile(wxCommandEvent&) {
	wxFileDialog dlg(this, "Add Firmware File", "", "",
		"Firmware files (*.rom;*.bin;*.epr;*.epm)|*.rom;*.bin;*.epr;*.epm|All files (*)|*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() != wxID_OK)
		return;

	VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str()));

	VDFile file;
	if (!file.openNT(path.c_str())) {
		wxMessageBox("Cannot open file.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	sint64 fileSize = file.size();
	if (!ATFirmwareAutodetectCheckSize(fileSize)) {
		wxMessageBox("File size is not valid for firmware.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	uint32 len = (uint32)std::min<sint64>(fileSize, 1048576);
	vdfastvector<uint8> buf(len);
	file.read(buf.data(), len);
	file.close();

	ATFirmwareInfo info;
	ATSpecificFirmwareType specificType;
	sint32 knownIdx;
	ATFirmwareAutodetect(buf.data(), len, info, specificType, knownIdx);

	info.mPath = path;
	info.mId = kATFirmwareId_Custom + (uint64)(rand() & 0xFFFF) + ((uint64)rand() << 16);
	info.mbVisible = true;
	info.mbAutoselect = true;
	if (info.mName.empty())
		info.mName = VDFileSplitPath(path.c_str());

	g_sim.GetFirmwareManager()->AddFirmware(info);

	PopulateFirmwareList();
	PopulateDefaultChoices();
	ATImGuiShowToast("Firmware added");
}

void ATFirmwareManagerDialog::OnRemove(wxCommandEvent&) {
	long sel = mpFirmwareList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= (long)mFirmwareEntries.size())
		return;

	uint64 id = mFirmwareEntries[sel].id;
	if (id < kATFirmwareId_Custom) {
		wxMessageBox("Cannot remove built-in firmware.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	g_sim.GetFirmwareManager()->RemoveFirmware(id);
	PopulateFirmwareList();
	PopulateDefaultChoices();
}

void ATFirmwareManagerDialog::OnDefaultChanged(wxCommandEvent&) {
	ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();

	for (int i = 0; i < 5; ++i) {
		int sel = mpDefaults[i]->GetSelection();
		if (sel >= 0 && sel < (int)mDefaultChoiceData[i].ids.size()) {
			uint64 id = mDefaultChoiceData[i].ids[sel];
			fwMgr->SetDefaultFirmware(kDefaultEntries[i].type, id);
		}
	}
	mDefaultsChanged = true;
}

void ATFirmwareManagerDialog::OnClose(wxCommandEvent&) {
	if (mDefaultsChanged) {
		if (g_sim.LoadROMs()) {
			g_sim.ColdReset();
			g_sim.Resume();
		}
	}
	EndModal(wxID_CLOSE);
}

} // anonymous namespace

void ATShowFirmwareManagerDialog(wxWindow *parent) {
	ATFirmwareManagerDialog dlg(parent);
	dlg.ShowModal();
}
