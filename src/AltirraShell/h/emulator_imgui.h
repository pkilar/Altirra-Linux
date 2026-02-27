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

// Disk explorer drag-and-drop support
bool ATImGuiIsDiskExplorerActive();
void ATImGuiDiskExplorerImportFile(const wchar_t *hostPath);

// Input capture for binding editor
#include <vd2/system/vdtypes.h>
bool ATImGuiIsCapturingInput();
void ATImGuiOnCapturedInput(uint32 inputCode);

// Activity indicator state — written by ATImGuiUIRenderer in stubs_linux.cpp,
// read by DrawStatusBar() in emulator_imgui.cpp each frame.
struct ATImGuiIndicatorState {
	uint32 mDiskMotorFlags = 0;        // Bitmask: drive motors active (bit 0 = D1)
	uint32 mDiskErrorFlags = 0;        // Bitmask: drive errors (bit 0 = D1)
	uint32 mStatusFlags = 0;           // Bitmask: SIO activity per drive + cassette (bit 16)
	uint32 mStatusHoldFlags = 0;       // Bitmask: held status flags (post-activity linger)
	uint32 mStatusHoldCounters[17] = {};
	uint32 mStatusCounter[15] = {};    // Sector counters per drive
	uint8 mHReadCounter = 0;           // H: device read activity countdown (0-30)
	uint8 mHWriteCounter = 0;          // H: device write activity countdown (0-30)
	uint8 mHardDiskCounter = 0;        // IDE activity timeout
	uint32 mHardDiskLBA = 0;           // Current LBA
	bool mbHardDiskRead = false;
	bool mbHardDiskWrite = false;
	uint8 mPCLinkReadCounter = 0;      // PCLink read activity countdown (0-30)
	uint8 mPCLinkWriteCounter = 0;     // PCLink write activity countdown (0-30)
	uint8 mFlashWriteCounter = 0;      // Flash write activity countdown (0-20)
	char mModemConnection[32] = {};    // Modem connection string (e.g., "9600", "RING")
	uint8 mCartridgeActivityCounter = 0; // Cartridge bank-switch flash countdown (0-20)
	uint32 mDiskLEDFlags = 0;          // Per-drive LED ready state (bit 0 = D1)
	float mRecordingTime = -1.0f;      // Video recording elapsed time (-1 = not recording)
	sint64 mRecordingSize = 0;         // Video recording file size in bytes
	bool mbRecordingPaused = false;    // Video recording paused state
	uint8 mLedStatus = 0;             // LED mask (bit 0 = L1, bit 1 = L2) — 1200XL only
	double mCyclesPerSecond = 0.0;    // CPU master clock rate
};

ATImGuiIndicatorState& ATImGuiGetIndicatorState();

#endif
