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

#include <algorithm>
#include <cstring>
#include <set>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>

#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/vdtypes.h>

#include "simulator.h"
#include "disk.h"
#include <at/atio/diskfs.h>

extern ATSimulator g_sim;
void ATImGuiShowToast(const char *message);

namespace {

// Sanitize a host filename to Atari 8.3 format: uppercase, A-Z/0-9/_ only.
VDStringA SanitizeAtari83Name(const char *hostName) {
	const char *lastSlash = strrchr(hostName, '/');
	if (lastSlash) hostName = lastSlash + 1;
	const char *bslash = strrchr(hostName, '\\');
	if (bslash) hostName = bslash + 1;

	VDStringA base, ext;
	const char *dot = strrchr(hostName, '.');
	if (dot) {
		base.assign(hostName, dot);
		ext.assign(dot + 1);
	} else {
		base = hostName;
	}

	auto sanitize = [](VDStringA& s) {
		VDStringA out;
		for (char c : s) {
			c = (char)toupper((unsigned char)c);
			if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
				out += c;
		}
		s = out;
	};

	sanitize(base);
	sanitize(ext);

	if (base.size() > 8) base.resize(8);
	if (ext.size() > 3) ext.resize(3);
	if (base.empty()) base = "FILE";

	return ext.empty() ? base : base + "." + ext;
}

class ATDiskExplorerDialog : public wxDialog {
public:
	ATDiskExplorerDialog(wxWindow *parent, int driveIdx, IATDiskImage *image, bool readOnly);
	ATDiskExplorerDialog(wxWindow *parent, const wxString& title, IATDiskImage *image, bool readOnly);

private:
	void RefreshListing();
	void OnNavigateUp(wxCommandEvent& event);
	void OnExtract(wxCommandEvent& event);
	void OnImport(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void OnRename(wxCommandEvent& event);
	void OnMkdir(wxCommandEvent& event);
	void OnItemActivated(wxListEvent& event);
	void OnClose(wxCommandEvent& event);

	std::unique_ptr<IATDiskFS> mpFS;
	bool mbReadOnly = false;
	int mDriveIdx = 0;

	ATDiskFSKey mCurDir = ATDiskFSKey::None;
	std::vector<ATDiskFSKey> mDirStack;

	struct Entry {
		ATDiskFSEntryInfo info;
	};
	std::vector<Entry> mEntries;

	wxStaticText *mpInfoText = nullptr;
	wxStaticText *mpPathText = nullptr;
	wxListCtrl *mpList = nullptr;
	wxCheckBox *mpTextMode = nullptr;
	wxButton *mpUpBtn = nullptr;
	wxButton *mpExtractBtn = nullptr;
	wxButton *mpImportBtn = nullptr;
	wxButton *mpDeleteBtn = nullptr;
	wxButton *mpRenameBtn = nullptr;
	wxButton *mpMkdirBtn = nullptr;

	enum {
		ID_NAV_UP = 3500, ID_EXTRACT, ID_IMPORT,
		ID_DELETE, ID_RENAME, ID_MKDIR, ID_TEXT_MODE
	};
};

ATDiskExplorerDialog::ATDiskExplorerDialog(wxWindow *parent, int driveIdx, IATDiskImage *image, bool readOnly)
	: ATDiskExplorerDialog(parent, wxString::Format("Disk Explorer - D%d:", driveIdx + 1), image, readOnly)
{
	mDriveIdx = driveIdx;
}

ATDiskExplorerDialog::ATDiskExplorerDialog(wxWindow *parent, const wxString& title, IATDiskImage *image, bool readOnly)
	: wxDialog(parent, wxID_ANY, title,
		wxDefaultPosition, wxSize(650, 500),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, mbReadOnly(readOnly)
	, mDriveIdx(-1)
{
	mpFS.reset(ATDiskMountImage(image, readOnly));
	if (!mpFS) {
		wxMessageBox("Could not mount filesystem on disk image.",
			"Disk Explorer", wxOK | wxICON_ERROR, parent);
		return;
	}

	ATDiskFSInfo fsInfo;
	mpFS->GetInfo(fsInfo);

	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// Info row
	mpInfoText = new wxStaticText(this, wxID_ANY,
		wxString::Format("Filesystem: %s   Free: %u sectors (%u bytes)   %s",
			fsInfo.mFSType.c_str(), fsInfo.mFreeBlocks,
			fsInfo.mFreeBlocks * fsInfo.mBlockSize,
			readOnly ? "[Read-Only]" : "[Read-Write]"));
	topSizer->Add(mpInfoText, 0, wxALL, 5);

	// Path / navigation row
	wxBoxSizer *navRow = new wxBoxSizer(wxHORIZONTAL);
	mpUpBtn = new wxButton(this, ID_NAV_UP, "Up");
	navRow->Add(mpUpBtn, 0, wxRIGHT, 4);
	mpPathText = new wxStaticText(this, wxID_ANY, "/");
	navRow->Add(mpPathText, 1, wxALIGN_CENTER_VERTICAL);
	topSizer->Add(navRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

	// File list
	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT);
	mpList->AppendColumn("Name", wxLIST_FORMAT_LEFT, 180);
	mpList->AppendColumn("Size", wxLIST_FORMAT_RIGHT, 80);
	mpList->AppendColumn("Sectors", wxLIST_FORMAT_RIGHT, 70);
	mpList->AppendColumn("Type", wxLIST_FORMAT_LEFT, 60);
	mpList->AppendColumn("Date", wxLIST_FORMAT_LEFT, 120);

	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mpList->SetFont(mono);
	topSizer->Add(mpList, 1, wxEXPAND | wxALL, 5);

	// Action buttons row
	wxBoxSizer *actionRow = new wxBoxSizer(wxHORIZONTAL);
	mpExtractBtn = new wxButton(this, ID_EXTRACT, "Extract...");
	mpImportBtn = new wxButton(this, ID_IMPORT, "Import...");
	mpDeleteBtn = new wxButton(this, ID_DELETE, "Delete");
	mpRenameBtn = new wxButton(this, ID_RENAME, "Rename...");
	mpMkdirBtn = new wxButton(this, ID_MKDIR, "New Dir...");
	mpTextMode = new wxCheckBox(this, ID_TEXT_MODE, "Text Mode (EOL conversion)");

	actionRow->Add(mpExtractBtn, 0, wxRIGHT, 3);
	actionRow->Add(mpImportBtn, 0, wxRIGHT, 3);
	actionRow->Add(mpDeleteBtn, 0, wxRIGHT, 3);
	actionRow->Add(mpRenameBtn, 0, wxRIGHT, 3);
	actionRow->Add(mpMkdirBtn, 0, wxRIGHT, 8);
	actionRow->Add(mpTextMode, 0, wxALIGN_CENTER_VERTICAL);
	topSizer->Add(actionRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	if (readOnly) {
		mpImportBtn->Enable(false);
		mpDeleteBtn->Enable(false);
		mpRenameBtn->Enable(false);
		mpMkdirBtn->Enable(false);
	}

	// Close button
	topSizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 5);

	SetSizer(topSizer);

	RefreshListing();

	Bind(wxEVT_BUTTON, &ATDiskExplorerDialog::OnNavigateUp, this, ID_NAV_UP);
	Bind(wxEVT_BUTTON, &ATDiskExplorerDialog::OnExtract, this, ID_EXTRACT);
	Bind(wxEVT_BUTTON, &ATDiskExplorerDialog::OnImport, this, ID_IMPORT);
	Bind(wxEVT_BUTTON, &ATDiskExplorerDialog::OnDelete, this, ID_DELETE);
	Bind(wxEVT_BUTTON, &ATDiskExplorerDialog::OnRename, this, ID_RENAME);
	Bind(wxEVT_BUTTON, &ATDiskExplorerDialog::OnMkdir, this, ID_MKDIR);
	Bind(wxEVT_BUTTON, &ATDiskExplorerDialog::OnClose, this, wxID_CLOSE);
	Bind(wxEVT_LIST_ITEM_ACTIVATED, &ATDiskExplorerDialog::OnItemActivated, this);
}

void ATDiskExplorerDialog::RefreshListing() {
	mpList->DeleteAllItems();
	mEntries.clear();

	if (!mpFS) return;

	ATDiskFSEntryInfo info;
	ATDiskFSFindHandle fh = mpFS->FindFirst(mCurDir, info);
	if (fh != ATDiskFSFindHandle::Invalid) {
		do {
			mEntries.push_back({info});
		} while (mpFS->FindNext(fh, info));
		mpFS->FindEnd(fh);
	}

	// Sort: directories first, then alphabetical
	std::sort(mEntries.begin(), mEntries.end(),
		[](const Entry& a, const Entry& b) {
			if (a.info.mbIsDirectory != b.info.mbIsDirectory)
				return a.info.mbIsDirectory > b.info.mbIsDirectory;
			return strcasecmp(a.info.mFileName.c_str(), b.info.mFileName.c_str()) < 0;
		});

	for (int i = 0; i < (int)mEntries.size(); i++) {
		const auto& e = mEntries[i].info;

		wxString name = e.mbIsDirectory
			? wxString::Format("[%s]", e.mFileName.c_str())
			: wxString(e.mFileName.c_str());

		long idx = mpList->InsertItem(i, name);
		mpList->SetItem(idx, 1, wxString::Format("%u", e.mBytes));
		mpList->SetItem(idx, 2, wxString::Format("%u", e.mSectors));
		mpList->SetItem(idx, 3, e.mbIsDirectory ? "DIR" : "FILE");

		if (e.mbDateValid) {
			char dateBuf[32];
			snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d %02d:%02d",
				e.mDate.mYear, e.mDate.mMonth, e.mDate.mDay,
				e.mDate.mHour, e.mDate.mMinute);
			mpList->SetItem(idx, 4, dateBuf);
		}
	}

	// Update path display
	wxString pathStr = "/";
	if (!mDirStack.empty())
		pathStr += ".../" + wxString::Format("%d levels deep", (int)mDirStack.size());
	mpPathText->SetLabel(pathStr);

	mpUpBtn->Enable(!mDirStack.empty());

	// Update free space info
	ATDiskFSInfo fsInfo;
	mpFS->GetInfo(fsInfo);
	mpInfoText->SetLabel(wxString::Format("Filesystem: %s   Free: %u sectors (%u bytes)   %s",
		fsInfo.mFSType.c_str(), fsInfo.mFreeBlocks,
		fsInfo.mFreeBlocks * fsInfo.mBlockSize,
		mbReadOnly ? "[Read-Only]" : "[Read-Write]"));
}

void ATDiskExplorerDialog::OnNavigateUp(wxCommandEvent&) {
	if (mDirStack.empty()) return;
	mCurDir = mDirStack.back();
	mDirStack.pop_back();
	RefreshListing();
}

void ATDiskExplorerDialog::OnItemActivated(wxListEvent& event) {
	int idx = event.GetIndex();
	if (idx < 0 || idx >= (int)mEntries.size()) return;

	if (mEntries[idx].info.mbIsDirectory) {
		mDirStack.push_back(mCurDir);
		mCurDir = mEntries[idx].info.mKey;
		RefreshListing();
	}
}

void ATDiskExplorerDialog::OnExtract(wxCommandEvent&) {
	// Collect selected items
	std::vector<int> selected;
	long item = -1;
	while ((item = mpList->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (item < (long)mEntries.size() && !mEntries[item].info.mbIsDirectory)
			selected.push_back((int)item);
	}

	if (selected.empty()) {
		wxMessageBox("Select one or more files to extract.", "Extract", wxOK | wxICON_INFORMATION, this);
		return;
	}

	bool textMode = mpTextMode->IsChecked();

	if (selected.size() == 1) {
		// Single file: save as dialog
		const auto& entry = mEntries[selected[0]].info;
		wxFileDialog dlg(this, "Extract File", "", entry.mFileName.c_str(),
			"All files (*)|*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dlg.ShowModal() != wxID_OK) return;

		try {
			vdfastvector<uint8> data;
			mpFS->ReadFile(entry.mKey, data);

			if (textMode) {
				for (auto& b : data) {
					if (b == 0x9B) b = 0x0A;
				}
			}

			VDStringW path = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str()));
			VDFile f(path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways);
			if (!data.empty())
				f.write(data.data(), (long)data.size());
			f.close();

			ATImGuiShowToast("File extracted");
		} catch (const MyError& e) {
			wxMessageBox(wxString::Format("Extract failed: %s", e.c_str()),
				"Error", wxOK | wxICON_ERROR, this);
		}
	} else {
		// Multiple files: pick directory
		wxDirDialog dlg(this, "Extract Files to Directory");
		if (dlg.ShowModal() != wxID_OK) return;

		VDStringW destDir = VDTextU8ToW(VDStringA(dlg.GetPath().utf8_str()));
		int ok = 0, fail = 0;

		for (int sel : selected) {
			const auto& entry = mEntries[sel].info;
			try {
				vdfastvector<uint8> data;
				mpFS->ReadFile(entry.mKey, data);

				if (textMode) {
					for (auto& b : data) {
						if (b == 0x9B) b = 0x0A;
					}
				}

				VDStringW path = VDMakePath(destDir.c_str(),
					VDTextU8ToW(VDStringA(entry.mFileName.c_str())).c_str());
				VDFile f(path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways);
				if (!data.empty())
					f.write(data.data(), (long)data.size());
				f.close();
				++ok;
			} catch (...) {
				++fail;
			}
		}

		ATImGuiShowToast(VDStringA().sprintf("Extracted %d file(s), %d failed", ok, fail).c_str());
	}
}

void ATDiskExplorerDialog::OnImport(wxCommandEvent&) {
	if (mbReadOnly) return;

	wxFileDialog dlg(this, "Import Files", "", "",
		"All files (*)|*", wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
	if (dlg.ShowModal() != wxID_OK) return;

	wxArrayString paths;
	dlg.GetPaths(paths);

	bool textMode = mpTextMode->IsChecked();
	int ok = 0, fail = 0;

	for (size_t i = 0; i < paths.GetCount(); i++) {
		try {
			VDStringW hostPath = VDTextU8ToW(VDStringA(paths[i].utf8_str()));

			VDFile f(hostPath.c_str(), nsVDFile::kRead | nsVDFile::kOpenExisting);
			sint64 len = f.size();
			if (len > 0x1000000) {
				++fail;
				continue;
			}

			vdfastvector<uint8> data((size_t)len);
			if (len > 0)
				f.read(data.data(), (long)len);
			f.close();

			if (textMode) {
				for (auto& b : data) {
					if (b == 0x0A) b = 0x9B;
				}
			}

			VDStringA atariName = SanitizeAtari83Name(
				VDTextWToU8(VDStringW(VDFileSplitPath(hostPath.c_str()))).c_str());

			mpFS->WriteFile(mCurDir, atariName.c_str(), data.data(), (uint32)data.size());
			++ok;
		} catch (...) {
			++fail;
		}
	}

	if (ok > 0)
		mpFS->Flush();

	RefreshListing();
	ATImGuiShowToast(VDStringA().sprintf("Imported %d file(s), %d failed", ok, fail).c_str());
}

void ATDiskExplorerDialog::OnDelete(wxCommandEvent&) {
	if (mbReadOnly) return;

	std::vector<int> selected;
	long item = -1;
	while ((item = mpList->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (item < (long)mEntries.size())
			selected.push_back((int)item);
	}

	if (selected.empty()) {
		wxMessageBox("Select items to delete.", "Delete", wxOK | wxICON_INFORMATION, this);
		return;
	}

	if (wxMessageBox(wxString::Format("Delete %d item(s)?", (int)selected.size()),
			"Confirm Delete", wxYES_NO | wxICON_QUESTION, this) != wxYES)
		return;

	int deleted = 0;
	// Delete in reverse order to preserve indices
	for (int i = (int)selected.size() - 1; i >= 0; --i) {
		try {
			mpFS->DeleteFile(mEntries[selected[i]].info.mKey);
			++deleted;
		} catch (const MyError& e) {
			wxMessageBox(wxString::Format("Delete failed: %s", e.c_str()),
				"Error", wxOK | wxICON_ERROR, this);
		}
	}

	if (deleted > 0)
		mpFS->Flush();

	RefreshListing();
	ATImGuiShowToast(VDStringA().sprintf("Deleted %d item(s)", deleted).c_str());
}

void ATDiskExplorerDialog::OnRename(wxCommandEvent&) {
	if (mbReadOnly) return;

	long sel = mpList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= (long)mEntries.size()) {
		wxMessageBox("Select a file to rename.", "Rename", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const auto& entry = mEntries[sel].info;
	wxTextEntryDialog dlg(this, "New filename:", "Rename File", entry.mFileName.c_str());
	if (dlg.ShowModal() != wxID_OK) return;

	VDStringA newName = SanitizeAtari83Name(dlg.GetValue().utf8_str().data());
	if (newName.empty()) return;

	try {
		mpFS->RenameFile(entry.mKey, newName.c_str());
		mpFS->Flush();
		RefreshListing();
		ATImGuiShowToast("File renamed");
	} catch (const MyError& e) {
		wxMessageBox(wxString::Format("Rename failed: %s", e.c_str()),
			"Error", wxOK | wxICON_ERROR, this);
	}
}

void ATDiskExplorerDialog::OnMkdir(wxCommandEvent&) {
	if (mbReadOnly) return;

	wxTextEntryDialog dlg(this, "Directory name:", "Create Directory");
	if (dlg.ShowModal() != wxID_OK) return;

	VDStringA dirName = SanitizeAtari83Name(dlg.GetValue().utf8_str().data());
	if (dirName.empty()) return;

	try {
		mpFS->CreateDir(mCurDir, dirName.c_str());
		mpFS->Flush();
		RefreshListing();
		ATImGuiShowToast("Directory created");
	} catch (const MyError& e) {
		wxMessageBox(wxString::Format("Create directory failed: %s", e.c_str()),
			"Error", wxOK | wxICON_ERROR, this);
	}
}

void ATDiskExplorerDialog::OnClose(wxCommandEvent&) {
	EndModal(wxID_CLOSE);
}

} // anonymous namespace

void ATShowDiskExplorerDialog(wxWindow *parent) {
	// Find first loaded disk
	int driveIdx = -1;
	for (int i = 0; i < 15; ++i) {
		if (g_sim.GetDiskInterface(i).IsDiskLoaded()) {
			driveIdx = i;
			break;
		}
	}
	if (driveIdx < 0) {
		wxMessageBox("No disk image is loaded.", "Disk Explorer", wxOK | wxICON_INFORMATION, parent);
		return;
	}

	ATDiskInterface& di = g_sim.GetDiskInterface(driveIdx);
	IATDiskImage *image = di.GetDiskImage();
	if (!image) {
		wxMessageBox("No disk image available.", "Disk Explorer", wxOK | wxICON_INFORMATION, parent);
		return;
	}

	bool readOnly = !di.IsDiskWritable();

	try {
		ATDiskExplorerDialog dlg(parent, driveIdx, image, readOnly);
		dlg.ShowModal();
	} catch (const MyError& e) {
		wxMessageBox(wxString::Format("Error opening disk: %s", e.c_str()),
			"Disk Explorer", wxOK | wxICON_ERROR, parent);
	}
}

void ATShowDiskExplorerForImage(wxWindow *parent, IATDiskImage *image, const wchar_t *title, bool readOnly) {
	if (!image)
		return;

	VDStringA u8title = VDTextWToU8(VDStringW(title));
	wxString wxTitle = wxString::Format("Disk Explorer - %s", u8title.c_str());

	try {
		ATDiskExplorerDialog dlg(parent, wxTitle, image, readOnly);
		dlg.ShowModal();
	} catch (const MyError& e) {
		wxMessageBox(wxString::Format("Error opening disk: %s", e.c_str()),
			"Disk Explorer", wxOK | wxICON_ERROR, parent);
	}
}
