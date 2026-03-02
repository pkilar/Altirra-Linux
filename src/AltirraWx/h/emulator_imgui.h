// wxWidgets shim — provides the same API surface as the ImGui version
// so stubs_linux.cpp and other shared code compiles without ImGui.

#ifndef AT_EMULATOR_IMGUI_H
#define AT_EMULATOR_IMGUI_H

#include <vd2/system/vdtypes.h>

// These are no-ops or stubs in the wxWidgets build for now.
// Real implementations will be added in later phases.
inline void ATImGuiEmulatorInit() {}
inline void ATImGuiEmulatorDraw() {}
inline void ATImGuiEmulatorShutdown() {}

inline void ATImGuiRequestQuit() {}
inline bool ATImGuiIsQuitConfirmed() { return false; }

void ATImGuiShowToast(const char *message);

inline void ATImGuiPasteText() {}
inline void ATImGuiOpenImage() {}
inline void ATImGuiBootImage() {}

class IATDiskImage;
inline void ATImGuiOpenDiskExplorer(IATDiskImage *, const wchar_t *, bool) {}
inline bool ATImGuiIsDiskExplorerActive() { return false; }
inline void ATImGuiDiskExplorerImportFile(const wchar_t *) {}

inline bool ATImGuiIsCapturingInput() { return false; }
inline void ATImGuiOnCapturedInput(uint32) {}

class ATAudioMonitor;

struct ATImGuiIndicatorState {
	uint32 mDiskMotorFlags = 0;
	uint32 mDiskErrorFlags = 0;
	uint32 mStatusFlags = 0;
	uint32 mStatusHoldFlags = 0;
	uint32 mStatusHoldCounters[17] = {};
	uint32 mStatusCounter[15] = {};
	uint8 mHReadCounter = 0;
	uint8 mHWriteCounter = 0;
	uint8 mHardDiskCounter = 0;
	uint32 mHardDiskLBA = 0;
	bool mbHardDiskRead = false;
	bool mbHardDiskWrite = false;
	uint8 mPCLinkReadCounter = 0;
	uint8 mPCLinkWriteCounter = 0;
	uint8 mFlashWriteCounter = 0;
	char mModemConnection[32] = {};
	uint8 mCartridgeActivityCounter = 0;
	uint32 mDiskLEDFlags = 0;
	float mRecordingTime = -1.0f;
	sint64 mRecordingSize = 0;
	bool mbRecordingPaused = false;
	uint8 mLedStatus = 0;
	double mCyclesPerSecond = 0.0;

	uint8 mHeldButtonMask = 0;
	bool mbPendingHoldMode = false;
	int mPendingHeldKey = -1;
	uint8 mPendingHeldButtons = 0;

	sint64 mTracingSize = -1;

	char mStatusMessages[3][128] = {};
	uint32 mStatusMessageTimestamp = 0;

	struct WatchSlot {
		bool active = false;
		uint32 value = 0;
		int format = 0;
	};
	WatchSlot mWatchSlots[8] = {};

	ATAudioMonitor *mpAudioMonitors[2] = {};
	bool mbAudioDisplayEnabled[2] = {};
	bool mbAudioScopeEnabled = false;
};

ATImGuiIndicatorState& ATImGuiGetIndicatorState();

// Toast-only draw (used when overlay is hidden but toasts are active)
inline void ATImGuiDrawToastsOnly() {}

#endif
