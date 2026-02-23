//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - native file dialog helper
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef AT_FILEDIALOG_LINUX_H
#define AT_FILEDIALOG_LINUX_H

#include <vd2/system/VDString.h>

// Opens a native file dialog using zenity (GTK) or kdialog (KDE) with
// fallback to an ImGui text input popup.
//
// title:   Dialog title text
// filters: Filter string in format "Label|*.ext1;*.ext2|Label2|*.ext3|..."
//
// Returns the selected file path, or empty string if cancelled.
VDStringW ATLinuxOpenFileDialog(const char *title, const char *filters);

// Draws the ImGui fallback dialog. Call every frame while the fallback
// dialog might be open. Returns true and fills 'result' when the user
// confirms a path. Returns false while still open or if cancelled.
bool ATLinuxFileDialogDrawFallback(VDStringW& result);

// Returns true if the ImGui fallback dialog is currently open.
bool ATLinuxFileDialogIsFallbackOpen();

// Opens the ImGui fallback dialog (called internally if no native
// dialog program is available, but can also be used directly).
void ATLinuxFileDialogOpenFallback(const char *title);

#endif
