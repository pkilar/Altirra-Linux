//	Altirra - Atari 800/800XL/5200 emulator
//	Update checker for Linux (uses curl to query GitHub releases API)
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
#include <stdio.h>
#include <string.h>
#include <string>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>

#include "version.h"

////////////////////////////////////////////////////////////////////////////////

static bool RunCurlQuery(std::string& output) {
	FILE *fp = popen(
		"curl -s -m 10 -H 'Accept: application/vnd.github.v3+json' "
		"'https://api.github.com/repos/pkilar/Altirra-Linux/releases/latest' 2>/dev/null",
		"r");

	if (!fp)
		return false;

	char buf[4096];
	output.clear();
	while (size_t n = fread(buf, 1, sizeof(buf), fp))
		output.append(buf, n);

	int status = pclose(fp);
	return status == 0 && !output.empty();
}

// Minimal JSON string extraction — finds "key": "value" pairs.
static bool ExtractJsonString(const std::string& json, const char *key, std::string& value) {
	std::string pattern = std::string("\"") + key + "\"";
	size_t pos = json.find(pattern);
	if (pos == std::string::npos)
		return false;

	pos = json.find(':', pos + pattern.size());
	if (pos == std::string::npos)
		return false;

	// Skip whitespace and opening quote
	pos = json.find('"', pos + 1);
	if (pos == std::string::npos)
		return false;
	++pos;

	size_t end = json.find('"', pos);
	if (end == std::string::npos)
		return false;

	value = json.substr(pos, end - pos);
	return true;
}

////////////////////////////////////////////////////////////////////////////////

void ATCheckForUpdates(wxWindow *parent) {
	wxBusyCursor wait;

	std::string json;
	if (!RunCurlQuery(json)) {
		wxMessageBox(
			"Could not check for updates.\n\n"
			"Make sure 'curl' is installed and you have an internet connection.",
			"Update Check", wxOK | wxICON_WARNING, parent);
		return;
	}

	// Check for API error (e.g. no releases yet)
	if (json.find("\"tag_name\"") == std::string::npos) {
		wxMessageBox(
			"No releases found on GitHub.\n\n"
			"This may be a development build.",
			"Update Check", wxOK | wxICON_INFORMATION, parent);
		return;
	}

	std::string tagName, htmlUrl, body;
	ExtractJsonString(json, "tag_name", tagName);
	ExtractJsonString(json, "html_url", htmlUrl);
	ExtractJsonString(json, "name", body);

	// Compare versions — strip leading 'v' from tag
	std::string remoteVer = tagName;
	if (!remoteVer.empty() && remoteVer[0] == 'v')
		remoteVer = remoteVer.substr(1);

	const char *localVer = AT_VERSION;

#if AT_VERSION_DEV
	// Dev builds always show the latest release info
	wxDialog dlg(parent, wxID_ANY, wxString("Update Check"),
		wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);

	auto *sizer = new wxBoxSizer(wxVERTICAL);

	sizer->Add(new wxStaticText(&dlg, wxID_ANY,
		wxString::Format("You are running a development build (%s).", localVer)),
		0, wxALL, 10);

	sizer->Add(new wxStaticText(&dlg, wxID_ANY,
		wxString::Format("Latest release: %s", remoteVer.c_str())),
		0, wxLEFT | wxRIGHT, 10);

	if (!body.empty()) {
		sizer->Add(new wxStaticText(&dlg, wxID_ANY,
			wxString::FromUTF8(body.c_str())),
			0, wxLEFT | wxRIGHT | wxTOP, 10);
	}

	if (!htmlUrl.empty()) {
		sizer->AddSpacer(5);
		sizer->Add(new wxHyperlinkCtrl(&dlg, wxID_ANY,
			"View release on GitHub", wxString::FromUTF8(htmlUrl.c_str())),
			0, wxLEFT | wxRIGHT, 10);
	}

	sizer->AddSpacer(10);
	sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, 10);
	dlg.SetSizerAndFit(sizer);
	dlg.ShowModal();
#else
	// Release builds: compare version strings
	if (remoteVer == localVer) {
		wxMessageBox(
			wxString::Format("You are running the latest version (%s).", localVer),
			"Update Check", wxOK | wxICON_INFORMATION, parent);
	} else {
		wxDialog dlg(parent, wxID_ANY, wxString("Update Available"),
			wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);

		auto *sizer = new wxBoxSizer(wxVERTICAL);

		sizer->Add(new wxStaticText(&dlg, wxID_ANY,
			wxString::Format("A new version is available!\n\n"
				"Current: %s\nLatest: %s", localVer, remoteVer.c_str())),
			0, wxALL, 10);

		if (!htmlUrl.empty()) {
			sizer->Add(new wxHyperlinkCtrl(&dlg, wxID_ANY,
				"Download from GitHub", wxString::FromUTF8(htmlUrl.c_str())),
				0, wxLEFT | wxRIGHT, 10);
		}

		sizer->AddSpacer(10);
		sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, 10);
		dlg.SetSizerAndFit(sizer);
		dlg.ShowModal();
	}
#endif
}
