//	Altirra - Atari 800/800XL/5200 emulator
//	Setup wizard for first-run configuration
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
#include <vd2/system/text.h>
#include <at/atcore/device.h>
#include <at/atcore/propertyset.h>

#include "simulator.h"
#include "uiaccessors.h"
#include "firmwaremanager.h"
#include "settings.h"
#include "constants.h"
#include "uitypes.h"
#include "gtia.h"
#include "diskinterface.h"

#include <wx/wizard.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/radiobut.h>
#include <wx/listctrl.h>
#include <wx/font.h>

extern ATSimulator g_sim;

////////////////////////////////////////////////////////////////////////////////
// Page 1: Welcome

class ATWizardPageWelcome : public wxWizardPageSimple {
public:
	ATWizardPageWelcome(wxWizard *parent) : wxWizardPageSimple(parent) {
		auto *sizer = new wxBoxSizer(wxVERTICAL);

		auto *title = new wxStaticText(this, wxID_ANY, "Welcome to Altirra");
		wxFont titleFont = title->GetFont();
		titleFont.SetPointSize(titleFont.GetPointSize() + 4);
		titleFont.SetWeight(wxFONTWEIGHT_BOLD);
		title->SetFont(titleFont);

		sizer->Add(title, 0, wxALL, 10);
		sizer->Add(new wxStaticText(this, wxID_ANY,
			"This wizard will help you set up Altirra for first use.\n\n"
			"Altirra is an emulator for Atari 8-bit computers including\n"
			"the Atari 400/800, XL/XE, and 5200 systems.\n\n"
			"The built-in replacement firmware (AltirraOS) allows you\n"
			"to use the emulator without original ROM images, though\n"
			"original firmware provides better compatibility.\n\n"
			"Click Next to continue."),
			0, wxALL, 10);

		SetSizerAndFit(sizer);
	}
};

////////////////////////////////////////////////////////////////////////////////
// Page 2: Firmware scan

class ATWizardPageFirmware : public wxWizardPageSimple {
public:
	ATWizardPageFirmware(wxWizard *parent) : wxWizardPageSimple(parent) {
		auto *sizer = new wxBoxSizer(wxVERTICAL);

		auto *title = new wxStaticText(this, wxID_ANY, "Firmware Status");
		wxFont titleFont = title->GetFont();
		titleFont.SetPointSize(titleFont.GetPointSize() + 2);
		titleFont.SetWeight(wxFONTWEIGHT_BOLD);
		title->SetFont(titleFont);
		sizer->Add(title, 0, wxALL, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"Altirra has scanned for Atari firmware ROM images.\n"
			"Built-in replacements are used for any missing ROMs."),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition,
			wxSize(-1, 150), wxLC_REPORT | wxLC_SINGLE_SEL);
		mpList->AppendColumn("Firmware", wxLIST_FORMAT_LEFT, 200);
		mpList->AppendColumn("Status", wxLIST_FORMAT_LEFT, 200);
		sizer->Add(mpList, 1, wxEXPAND | wxALL, 10);

		ScanFirmware();

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"You can add firmware later via System > Firmware Manager.\n"
			"Place ROM files in ~/.config/altirra/firmware/"),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		SetSizerAndFit(sizer);
	}

private:
	void ScanFirmware() {
		ATFirmwareManager& fwMgr = *g_sim.GetFirmwareManager();

		struct FirmwareCheck {
			const char *name;
			ATFirmwareType type;
		};

		static const FirmwareCheck checks[] = {
			{ "Atari 800 OS",      kATFirmwareType_Kernel800_OSA },
			{ "Atari XL/XE OS",    kATFirmwareType_KernelXL },
			{ "Atari BASIC",       kATFirmwareType_Basic },
			{ "Atari 5200 OS",     kATFirmwareType_Kernel5200 },
		};

		for (const auto& check : checks) {
			long idx = mpList->InsertItem(mpList->GetItemCount(), check.name);

			uint64 fwId = fwMgr.GetCompatibleFirmware(check.type);
			if (fwId >= kATFirmwareId_Custom) {
				mpList->SetItem(idx, 1, "Found");
			} else {
				mpList->SetItem(idx, 1, "Using built-in replacement");
			}
		}
	}

	wxListCtrl *mpList = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
// Page 3: System selection (800/XL vs 5200)

class ATWizardPageSystem : public wxWizardPageSimple {
public:
	ATWizardPageSystem(wxWizard *parent) : wxWizardPageSimple(parent) {
		auto *sizer = new wxBoxSizer(wxVERTICAL);

		auto *title = new wxStaticText(this, wxID_ANY, "Select System Type");
		wxFont titleFont = title->GetFont();
		titleFont.SetPointSize(titleFont.GetPointSize() + 2);
		titleFont.SetWeight(wxFONTWEIGHT_BOLD);
		title->SetFont(titleFont);
		sizer->Add(title, 0, wxALL, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"Choose which Atari system to emulate:"),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		mpRadio800 = new wxRadioButton(this, wxID_ANY,
			"Atari 800/XL/XE (home computer)",
			wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		sizer->Add(mpRadio800, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"    General-purpose 8-bit home computer with keyboard.\n"
			"    Runs cartridges, disk software, cassette programs."),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		mpRadio5200 = new wxRadioButton(this, wxID_ANY,
			"Atari 5200 SuperSystem (game console)");
		sizer->Add(mpRadio5200, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"    Dedicated game console with analog joystick.\n"
			"    Runs 5200 cartridge games only."),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		// Default to 800/XL
		mpRadio800->SetValue(true);

		SetSizerAndFit(sizer);
	}

	bool Is5200() const { return mpRadio5200->GetValue(); }

private:
	wxRadioButton *mpRadio800 = nullptr;
	wxRadioButton *mpRadio5200 = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
// Page 4: Video standard (NTSC/PAL)

class ATWizardPageVideo : public wxWizardPageSimple {
public:
	ATWizardPageVideo(wxWizard *parent) : wxWizardPageSimple(parent) {
		auto *sizer = new wxBoxSizer(wxVERTICAL);

		auto *title = new wxStaticText(this, wxID_ANY, "Select Video Standard");
		wxFont titleFont = title->GetFont();
		titleFont.SetPointSize(titleFont.GetPointSize() + 2);
		titleFont.SetWeight(wxFONTWEIGHT_BOLD);
		title->SetFont(titleFont);
		sizer->Add(title, 0, wxALL, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"Choose the video standard for the emulated system:"),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		mpRadioNTSC = new wxRadioButton(this, wxID_ANY,
			"NTSC (North America, Japan)",
			wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		sizer->Add(mpRadioNTSC, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"    60 Hz, 262 scanlines. Most common for US software."),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		mpRadioPAL = new wxRadioButton(this, wxID_ANY,
			"PAL (Europe, Australia)");
		sizer->Add(mpRadioPAL, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"    50 Hz, 312 scanlines. Required for some European software."),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		// Default to NTSC
		mpRadioNTSC->SetValue(true);

		SetSizerAndFit(sizer);
	}

	bool IsPAL() const { return mpRadioPAL->GetValue(); }

private:
	wxRadioButton *mpRadioNTSC = nullptr;
	wxRadioButton *mpRadioPAL = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
// Page 5: Experience mode

class ATWizardPageExperience : public wxWizardPageSimple {
public:
	ATWizardPageExperience(wxWizard *parent) : wxWizardPageSimple(parent) {
		auto *sizer = new wxBoxSizer(wxVERTICAL);

		auto *title = new wxStaticText(this, wxID_ANY, "Experience Level");
		wxFont titleFont = title->GetFont();
		titleFont.SetPointSize(titleFont.GetPointSize() + 2);
		titleFont.SetWeight(wxFONTWEIGHT_BOLD);
		title->SetFont(titleFont);
		sizer->Add(title, 0, wxALL, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"Choose how you want the emulator to behave:"),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		mpRadioFriendly = new wxRadioButton(this, wxID_ANY,
			"User-friendly (recommended)",
			wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		sizer->Add(mpRadioFriendly, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"    Faster disk I/O, clean display, simpler operation.\n"
			"    Best for playing games and running software."),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		mpRadioAuthentic = new wxRadioButton(this, wxID_ANY,
			"Authentic");
		sizer->Add(mpRadioAuthentic, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		sizer->Add(new wxStaticText(this, wxID_ANY,
			"    Accurate disk timing, NTSC artifacts, drive sounds.\n"
			"    Best for developers and preserving original experience."),
			0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

		// Default to user-friendly
		mpRadioFriendly->SetValue(true);

		SetSizerAndFit(sizer);
	}

	bool IsAuthentic() const { return mpRadioAuthentic->GetValue(); }

private:
	wxRadioButton *mpRadioFriendly = nullptr;
	wxRadioButton *mpRadioAuthentic = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
// Public entry point

bool ATShowSetupWizard(wxWindow *parent) {
	wxWizard wizard(parent, wxID_ANY, "Altirra Setup",
		wxNullBitmap, wxDefaultPosition,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	wizard.SetPageSize(wxSize(480, 360));

	ATWizardPageWelcome pageWelcome(&wizard);
	ATWizardPageFirmware pageFirmware(&wizard);
	ATWizardPageSystem pageSystem(&wizard);
	ATWizardPageVideo pageVideo(&wizard);
	ATWizardPageExperience pageExperience(&wizard);

	// Chain pages: Welcome -> Firmware -> System -> Video -> Experience
	wxWizardPageSimple::Chain(&pageWelcome, &pageFirmware);
	wxWizardPageSimple::Chain(&pageFirmware, &pageSystem);
	wxWizardPageSimple::Chain(&pageSystem, &pageVideo);
	wxWizardPageSimple::Chain(&pageVideo, &pageExperience);

	if (!wizard.RunWizard(&pageWelcome))
		return false;

	// Apply settings

	// System type
	if (pageSystem.Is5200()) {
		ATSettingsSwitchProfile(kATDefaultProfile_5200);
	} else {
		ATSettingsSwitchProfile(kATDefaultProfile_XL);
	}

	// Video standard (only meaningful for 800/XL)
	if (!pageSystem.Is5200()) {
		g_sim.SetVideoStandard(pageVideo.IsPAL()
			? kATVideoStandard_PAL
			: kATVideoStandard_NTSC);
	}

	// Experience mode
	if (pageExperience.IsAuthentic()) {
		// Authentic: enable artifacts, accurate timing, drive sounds
		g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::AutoHi);
		g_sim.SetCassetteSIOPatchEnabled(false);
		g_sim.SetDiskSIOPatchEnabled(false);
		g_sim.SetDiskAccurateTimingEnabled(true);
		for (int i = 0; i < 15; ++i)
			g_sim.GetDiskInterface(i).SetDriveSoundsEnabled(true);
		ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
	} else {
		// User-friendly: fast I/O, clean display
		g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
		g_sim.SetCassetteSIOPatchEnabled(true);
		g_sim.SetDiskSIOPatchEnabled(true);
		g_sim.SetDiskAccurateTimingEnabled(false);
		for (int i = 0; i < 15; ++i)
			g_sim.GetDiskInterface(i).SetDriveSoundsEnabled(false);
		ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
	}

	// Reload ROMs and cold reset with new settings
	g_sim.LoadROMs();
	g_sim.ColdReset();

	return true;
}
