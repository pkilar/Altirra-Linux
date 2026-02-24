//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - Dear ImGui emulator configuration UI
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef AT_EMULATOR_IMGUI_H
#define AT_EMULATOR_IMGUI_H

void ATImGuiEmulatorInit();
void ATImGuiEmulatorDraw();      // Call every frame when overlay visible
void ATImGuiEmulatorShutdown();

// Quit confirmation — returns true if quit should proceed
void ATImGuiRequestQuit();
bool ATImGuiIsQuitConfirmed();

// Toast notifications (2.5s fade-out)
void ATImGuiShowToast(const char *message);

// Paste clipboard text to emulator via POKEY
void ATImGuiPasteText();

// Open/Boot image via file dialog
void ATImGuiOpenImage();
void ATImGuiBootImage();

// Open disk explorer for a disk image
class IATDiskImage;
void ATImGuiOpenDiskExplorer(IATDiskImage *image, const wchar_t *imageName, bool readOnly);

// Input capture for binding editor
#include <vd2/system/vdtypes.h>
bool ATImGuiIsCapturingInput();
void ATImGuiOnCapturedInput(uint32 inputCode);

#endif
