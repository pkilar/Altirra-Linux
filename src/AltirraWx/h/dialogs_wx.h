//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#pragma once

#include <wx/dialog.h>

class wxCheckBox;
class wxChoice;
class wxRadioBox;
class wxSlider;
class wxSpinCtrl;
class wxStaticText;

// Show configuration dialogs (modal). Each reads current state on open
// and applies changes on OK.
void ATShowSystemConfigDialog(wxWindow *parent);
void ATShowCPUOptionsDialog(wxWindow *parent);
void ATShowBootOptionsDialog(wxWindow *parent);
void ATShowKeyboardSettingsDialog(wxWindow *parent);
void ATShowAudioOptionsDialog(wxWindow *parent);
void ATShowVideoSettingsDialog(wxWindow *parent);

// Show manager / tool dialogs (modal).
void ATShowFirmwareManagerDialog(wxWindow *parent);
void ATShowDeviceManagerDialog(wxWindow *parent);
void ATShowCartridgeBrowserDialog(wxWindow *parent);
void ATShowCassetteControlDialog(wxWindow *parent);

// Show Phase 8 dialogs (modal).
void ATShowProfileManagerDialog(wxWindow *parent);
void ATShowVideoRecordDialog(wxWindow *parent);
void ATShowCompatBrowserDialog(wxWindow *parent);
void ATShowCheaterDialog(wxWindow *parent);

// Video recording control (used by menu stop command).
void ATStopVideoRecording();
bool ATIsVideoRecording();
