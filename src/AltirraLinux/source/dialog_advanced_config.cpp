//	Altirra - Atari 800/800XL/5200 emulator
//	Advanced Configuration Editor
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
#include <vd2/system/VDString.h>
#include <at/atcore/configvar.h>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/font.h>

#include <algorithm>
#include <vector>

////////////////////////////////////////////////////////////////////////////////
// Item representing either a defined or undefined config variable

struct ConfigVarItem {
	ATConfigVar *pVar;			// non-null for defined variables
	VDStringA undefinedName;	// name for undefined variables

	const char *GetName() const {
		return pVar ? pVar->mpVarName : undefinedName.c_str();
	}

	bool IsOverridden() const {
		return pVar ? pVar->mbOverridden : true;  // undefined vars are always "overridden"
	}

	VDStringA GetValueString() const {
		if (pVar)
			return pVar->ToString();
		return VDStringA("(unknown cvar)");
	}

	const char *GetTypeString() const {
		if (!pVar) return "undefined";
		switch (pVar->GetVarType()) {
			case ATConfigVarType::Bool:     return "bool";
			case ATConfigVarType::Int32:    return "int32";
			case ATConfigVarType::Float:    return "float";
			case ATConfigVarType::RGBColor: return "color";
			default: return "unknown";
		}
	}
};

////////////////////////////////////////////////////////////////////////////////

class ATAdvancedConfigDialog : public wxDialog {
public:
	ATAdvancedConfigDialog(wxWindow *parent);

private:
	void PopulateList();
	void RefreshItem(long idx);
	void OnItemActivated(wxListEvent& evt);
	void OnResetSelected(wxCommandEvent& evt);
	void OnResetAll(wxCommandEvent& evt);

	wxListCtrl *mpList = nullptr;
	std::vector<ConfigVarItem> mItems;
};

ATAdvancedConfigDialog::ATAdvancedConfigDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Advanced Configuration",
		wxDefaultPosition, wxSize(640, 500),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	auto *mainSizer = new wxBoxSizer(wxVERTICAL);

	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->AppendColumn("Variable", wxLIST_FORMAT_LEFT, 250);
	mpList->AppendColumn("Type", wxLIST_FORMAT_LEFT, 60);
	mpList->AppendColumn("Value", wxLIST_FORMAT_LEFT, 280);
	mpList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ATAdvancedConfigDialog::OnItemActivated, this);
	mainSizer->Add(mpList, 1, wxEXPAND | wxALL, 5);

	// Buttons
	auto *btnSizer = new wxBoxSizer(wxHORIZONTAL);
	auto *resetSelBtn = new wxButton(this, wxID_ANY, "Reset Selected");
	resetSelBtn->Bind(wxEVT_BUTTON, &ATAdvancedConfigDialog::OnResetSelected, this);
	btnSizer->Add(resetSelBtn, 0, wxRIGHT, 5);

	auto *resetAllBtn = new wxButton(this, wxID_ANY, "Reset All");
	resetAllBtn->Bind(wxEVT_BUTTON, &ATAdvancedConfigDialog::OnResetAll, this);
	btnSizer->Add(resetAllBtn, 0, wxRIGHT, 15);

	btnSizer->AddStretchSpacer();
	btnSizer->Add(new wxButton(this, wxID_CLOSE, "Close"), 0);
	mainSizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	SetSizerAndFit(mainSizer);

	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);

	PopulateList();
}

void ATAdvancedConfigDialog::PopulateList() {
	mpList->DeleteAllItems();
	mItems.clear();

	// Collect defined variables
	ATConfigVar **vars = nullptr;
	size_t numVars = 0;
	ATGetConfigVars(vars, numVars);

	for (size_t i = 0; i < numVars; i++) {
		ConfigVarItem item;
		item.pVar = vars[i];
		mItems.push_back(item);
	}

	// Collect undefined variables (legacy overrides)
	const VDStringA *uvars = nullptr;
	size_t numUVars = 0;
	ATGetUndefinedConfigVars(uvars, numUVars);

	for (size_t i = 0; i < numUVars; i++) {
		ConfigVarItem item;
		item.pVar = nullptr;
		item.undefinedName = uvars[i];
		mItems.push_back(item);
	}

	// Sort by name
	std::sort(mItems.begin(), mItems.end(), [](const ConfigVarItem& a, const ConfigVarItem& b) {
		return strcmp(a.GetName(), b.GetName()) < 0;
	});

	// Populate list
	wxFont boldFont = mpList->GetFont();
	boldFont.SetWeight(wxFONTWEIGHT_BOLD);

	for (size_t i = 0; i < mItems.size(); i++) {
		const auto& item = mItems[i];
		long idx = mpList->InsertItem(mpList->GetItemCount(), wxString::FromUTF8(item.GetName()));
		mpList->SetItem(idx, 1, wxString::FromUTF8(item.GetTypeString()));
		mpList->SetItem(idx, 2, wxString::FromUTF8(item.GetValueString().c_str()));

		if (item.IsOverridden()) {
			wxListItem li;
			li.SetId(idx);
			li.SetFont(boldFont);
			mpList->SetItem(li);
		}
	}
}

void ATAdvancedConfigDialog::RefreshItem(long idx) {
	if (idx < 0 || idx >= (long)mItems.size()) return;
	const auto& item = mItems[idx];
	mpList->SetItem(idx, 2, wxString::FromUTF8(item.GetValueString().c_str()));

	wxFont font = mpList->GetFont();
	if (item.IsOverridden())
		font.SetWeight(wxFONTWEIGHT_BOLD);
	else
		font.SetWeight(wxFONTWEIGHT_NORMAL);

	wxListItem li;
	li.SetId(idx);
	li.SetFont(font);
	mpList->SetItem(li);
}

void ATAdvancedConfigDialog::OnItemActivated(wxListEvent& evt) {
	long idx = evt.GetIndex();
	if (idx < 0 || idx >= (long)mItems.size()) return;

	auto& item = mItems[idx];
	if (!item.pVar) {
		wxMessageBox("This is an undefined variable from a previous version.\n"
			"Use 'Reset Selected' to remove it.",
			"Cannot Edit", wxOK | wxICON_INFORMATION, this);
		return;
	}

	VDStringA currentVal = item.pVar->ToString();
	wxString prompt;
	prompt.Printf("Edit '%s' (%s):", item.pVar->mpVarName, item.GetTypeString());

	// For bools, toggle directly instead of text entry
	if (item.pVar->GetVarType() == ATConfigVarType::Bool) {
		const char *newVal = (currentVal == "true") ? "false" : "true";
		item.pVar->FromString(newVal);
		RefreshItem(idx);
		return;
	}

	wxTextEntryDialog dlg(this, prompt, "Edit Configuration Variable",
		wxString::FromUTF8(currentVal.c_str()));

	if (dlg.ShowModal() == wxID_OK) {
		VDStringA newVal = VDStringA(dlg.GetValue().ToUTF8().data());
		if (!item.pVar->FromString(newVal.c_str())) {
			wxMessageBox("Invalid value for this variable type.", "Error",
				wxOK | wxICON_ERROR, this);
		} else {
			RefreshItem(idx);
		}
	}
}

void ATAdvancedConfigDialog::OnResetSelected(wxCommandEvent&) {
	long idx = mpList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (idx < 0) return;

	auto& item = mItems[idx];
	if (item.pVar) {
		item.pVar->Unset();
		RefreshItem(idx);
	} else {
		ATUnsetUndefinedConfigVar(item.undefinedName.c_str());
		PopulateList();  // re-enumerate since undefined list changed
	}
}

void ATAdvancedConfigDialog::OnResetAll(wxCommandEvent&) {
	int result = wxMessageBox(
		"Reset ALL configuration variables to defaults?\n\n"
		"This cannot be undone.", "Confirm Reset",
		wxYES_NO | wxICON_WARNING, this);

	if (result == wxYES) {
		ATResetConfigVars();
		PopulateList();
	}
}

////////////////////////////////////////////////////////////////////////////////
// Public entry point

void ATShowAdvancedConfigDialog(wxWindow *parent) {
	ATAdvancedConfigDialog dlg(parent);
	dlg.ShowModal();
}
