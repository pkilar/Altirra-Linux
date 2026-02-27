//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - command handlers for ATUICommandManager
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

// Linux command handlers registered with ATUICommandManager. These cover
// the subset of commands that work without Win32 UI dialogs, enabling
// custom device VM scripts to execute UI commands via
// ATUIExecuteCommandStringAndShowErrors().

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/refcount.h>
#include <at/atui/uicommandmanager.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>

class ATIRQController;

#include "uitypes.h"
#include "simulator.h"
#include "autosavemanager.h"
#include "uiaccessors.h"
#include "debugger.h"
#include "console.h"
#include "cassette.h"
#include "devicemanager.h"
#include "gtia.h"
#include "disk.h"
#include "diskinterface.h"

extern ATSimulator g_sim;

// Global command manager instance (referenced by stubs_linux.cpp)
ATUICommandManager g_ATUICommandMgr;

///////////////////////////////////////////////////////////////////////////
// System commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdSystemTogglePause() {
	if (g_sim.IsRunning())
		g_sim.Pause();
	else
		g_sim.Resume();
}

static void OnCmdSystemWarmReset() {
	g_sim.WarmReset();
	g_sim.Resume();
}

static void OnCmdSystemColdReset() {
	g_sim.ColdReset();
	g_sim.Resume();
}

static void OnCmdSystemColdResetComputerOnly() {
	g_sim.ColdResetComputerOnly();
	g_sim.Resume();
}

static void OnCmdSystemTogglePauseWhenInactive() {
	ATUISetPauseWhenInactive(!ATUIGetPauseWhenInactive());
}

static void OnCmdSystemToggleWarpSpeed() {
	ATUISetTurbo(!ATUIGetTurbo());
}

static void OnCmdSystemToggleFPPatch() {
	g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled());
}

static void OnCmdSystemToggleKeyboardPresent() {
	g_sim.SetKeyboardPresent(!g_sim.IsKeyboardPresent());
}

static void OnCmdSystemToggleForcedSelfTest() {
	g_sim.SetForcedSelfTest(!g_sim.IsForcedSelfTest());
}

static void OnCmdSystemToggleBASIC() {
	g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled());
	g_sim.ColdReset();
}

static void OnCmdSystemToggleFastBoot() {
	g_sim.SetFastBootEnabled(!g_sim.IsFastBootEnabled());
}

static void OnCmdSystemToggleRTime8() {
	g_sim.GetDeviceManager()->ToggleDevice("rtime8");
}

static void OnCmdSystemSpeedMatchHardware() {
	ATUISetFrameRateMode(kATFrameRateMode_Hardware);
}

static void OnCmdSystemSpeedMatchBroadcast() {
	ATUISetFrameRateMode(kATFrameRateMode_Broadcast);
}

static void OnCmdSystemSpeedInteger() {
	ATUISetFrameRateMode(kATFrameRateMode_Integral);
}

static void OnCmdSystemToggleVSyncAdaptive() {
	ATUISetFrameRateVSyncAdaptive(!ATUIGetFrameRateVSyncAdaptive());
}

static void OnCmdRewind() {
	g_sim.GetAutoSaveManager().Rewind();
}

///////////////////////////////////////////////////////////////////////////
// Cartridge commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdCartDetach() {
	g_sim.UnloadCartridge(0);
}

static void OnCmdCartDetachSecond() {
	g_sim.UnloadCartridge(1);
}

///////////////////////////////////////////////////////////////////////////
// Audio commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdAudioToggleStereo() {
	g_sim.SetDualPokeysEnabled(!g_sim.IsDualPokeysEnabled());
}

static void OnCmdAudioToggleMonitor() {
	g_sim.SetAudioMonitorEnabled(!g_sim.IsAudioMonitorEnabled());
}

static void OnCmdAudioToggleScope() {
	g_sim.SetAudioScopeEnabled(!g_sim.IsAudioScopeEnabled());
}

static void OnCmdAudioToggleMute() {
	IATAudioOutput *out = g_sim.GetAudioOutput();
	if (out)
		out->SetMute(!out->GetMute());
}

static void OnCmdAudioToggleNonlinearMixing() {
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetNonlinearMixingEnabled(!pokey.IsNonlinearMixingEnabled());
}

static void OnCmdAudioToggleSpeakerFilter() {
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetSpeakerFilterEnabled(!pokey.IsSpeakerFilterEnabled());
}

static void OnCmdAudioToggleSerialNoise() {
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetSerialNoiseEnabled(!pokey.IsSerialNoiseEnabled());
}

static void OnCmdAudioToggleSlightSid() {
	g_sim.GetDeviceManager()->ToggleDevice("slightsid");
}

static void OnCmdAudioToggleCovox() {
	g_sim.GetDeviceManager()->ToggleDevice("covox");
}

///////////////////////////////////////////////////////////////////////////
// Debug commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdDebugToggleBreakAtExeRun() {
	IATDebugger *dbg = ATGetDebugger();
	dbg->SetBreakOnEXERunAddrEnabled(!dbg->IsBreakOnEXERunAddrEnabled());
}

static void OnCmdDebugToggleAutoReloadRoms() {
	g_sim.SetROMAutoReloadEnabled(!g_sim.IsROMAutoReloadEnabled());
}

static void OnCmdDebugToggleAutoLoadKernelSymbols() {
	g_sim.SetAutoLoadKernelSymbolsEnabled(!g_sim.IsAutoLoadKernelSymbolsEnabled());
}

static void OnCmdDebugToggleAutoLoadSystemSymbols() {
	IATDebugger *dbg = ATGetDebugger();
	dbg->SetAutoLoadSystemSymbols(!dbg->IsAutoLoadSystemSymbolsEnabled());
}

static void OnCmdDebugRun() {
	ATGetDebugger()->Run(kATDebugSrcMode_Same);
}

static void OnCmdDebugBreak() {
	ATGetDebugger()->Break();
}

static void OnCmdDebugRunStop() {
	if (g_sim.IsRunning() || ATGetDebugger()->AreCommandsQueued())
		ATGetDebugger()->Break();
	else
		ATGetDebugger()->Run(kATDebugSrcMode_Same);
}

static void OnCmdDebugToggleDebugger() {
	if (ATIsDebugConsoleActive())
		ATCloseConsole();
	else
		ATOpenConsole();
}

///////////////////////////////////////////////////////////////////////////
// Disk commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdDiskToggleSIOPatch() {
	g_sim.SetDiskSIOPatchEnabled(!g_sim.IsDiskSIOPatchEnabled());
}

static void OnCmdDiskToggleSIOOverrideDetection() {
	g_sim.SetDiskSIOOverrideDetectEnabled(!g_sim.IsDiskSIOOverrideDetectEnabled());
}

static void OnCmdDiskToggleSectorCounter() {
	g_sim.SetDiskSectorCounterEnabled(!g_sim.IsDiskSectorCounterEnabled());
}

static void OnCmdDiskDetachAll() {
	for (int i = 0; i < 8; ++i) {
		ATDiskInterface& di = g_sim.GetDiskInterface(i);
		if (di.IsDiskLoaded())
			di.UnloadDisk();
	}
}

template<int N>
static void OnCmdDiskDetach() {
	ATDiskInterface& di = g_sim.GetDiskInterface(N);
	if (di.IsDiskLoaded())
		di.UnloadDisk();
}

static void OnCmdDiskRotatePrev() {
	// Find highest active drive
	int activeDrives = 0;
	for (int i = 14; i >= 0; --i) {
		if (g_sim.GetDiskDrive(i).IsEnabled() || g_sim.GetDiskInterface(i).GetClientCount() > 1) {
			activeDrives = i + 1;
			break;
		}
	}
	if (activeDrives > 0)
		g_sim.RotateDrives(activeDrives, -1);
}

static void OnCmdDiskRotateNext() {
	int activeDrives = 0;
	for (int i = 14; i >= 0; --i) {
		if (g_sim.GetDiskDrive(i).IsEnabled() || g_sim.GetDiskInterface(i).GetClientCount() > 1) {
			activeDrives = i + 1;
			break;
		}
	}
	if (activeDrives > 0)
		g_sim.RotateDrives(activeDrives, +1);
}

///////////////////////////////////////////////////////////////////////////
// Cassette commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdCassetteToggleSIOPatch() {
	g_sim.SetCassetteSIOPatchEnabled(!g_sim.IsCassetteSIOPatchEnabled());
}

static void OnCmdCassetteToggleAutoBoot() {
	g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled());
}

static void OnCmdCassetteToggleAutoBasicBoot() {
	g_sim.SetCassetteAutoBasicBootEnabled(!g_sim.IsCassetteAutoBasicBootEnabled());
}

static void OnCmdCassetteToggleAutoRewind() {
	g_sim.SetCassetteAutoRewindEnabled(!g_sim.IsCassetteAutoRewindEnabled());
}

static void OnCmdCassetteToggleRandomizeStartPosition() {
	g_sim.SetCassetteRandomizedStartEnabled(!g_sim.IsCassetteRandomizedStartEnabled());
}

static void OnCmdCassetteUnload() {
	g_sim.GetCassette().Unload();
}

///////////////////////////////////////////////////////////////////////////
// Video commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdVideoToggleCTIA() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetCTIAMode(!gtia.IsCTIAMode());
}

static void OnCmdVideoToggleFrameBlending() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetBlendModeEnabled(!gtia.IsBlendModeEnabled());
}

static void OnCmdVideoToggleInterlace() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetInterlaceEnabled(!gtia.IsInterlaceEnabled());
}

static void OnCmdVideoToggleScanlines() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetScanlinesEnabled(!gtia.AreScanlinesEnabled());
}

static void OnCmdVideoToggleXEP80() {
	g_sim.GetDeviceManager()->ToggleDevice("xep80");
}

///////////////////////////////////////////////////////////////////////////
// Cheat commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdCheatTogglePMCollisions() {
	g_sim.GetGTIA().SetPMCollisionsEnabled(!g_sim.GetGTIA().ArePMCollisionsEnabled());
}

static void OnCmdCheatTogglePFCollisions() {
	g_sim.GetGTIA().SetPFCollisionsEnabled(!g_sim.GetGTIA().ArePFCollisionsEnabled());
}

///////////////////////////////////////////////////////////////////////////
// Input commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdInputToggleAutoCapture() {
	ATUISetMouseAutoCapture(!ATUIGetMouseAutoCapture());
}

///////////////////////////////////////////////////////////////////////////
// View commands
///////////////////////////////////////////////////////////////////////////

static void OnCmdViewToggleFPS() {
	ATUISetShowFPS(!ATUIGetShowFPS());
}

///////////////////////////////////////////////////////////////////////////
// Command table and registration
///////////////////////////////////////////////////////////////////////////

static const ATUICommand kLinuxCommands[] = {
	// System
	{ "System.TogglePause", OnCmdSystemTogglePause },
	{ "System.WarmReset", OnCmdSystemWarmReset },
	{ "System.ColdReset", OnCmdSystemColdReset },
	{ "System.ColdResetComputerOnly", OnCmdSystemColdResetComputerOnly },
	{ "System.TogglePauseWhenInactive", OnCmdSystemTogglePauseWhenInactive },
	{ "System.ToggleWarpSpeed", OnCmdSystemToggleWarpSpeed },
	{ "System.ToggleFPPatch", OnCmdSystemToggleFPPatch },
	{ "System.ToggleKeyboardPresent", OnCmdSystemToggleKeyboardPresent },
	{ "System.ToggleForcedSelfTest", OnCmdSystemToggleForcedSelfTest },
	{ "System.ToggleBASIC", OnCmdSystemToggleBASIC },
	{ "System.ToggleFastBoot", OnCmdSystemToggleFastBoot },
	{ "System.ToggleRTime8", OnCmdSystemToggleRTime8 },
	{ "System.SpeedMatchHardware", OnCmdSystemSpeedMatchHardware },
	{ "System.SpeedMatchBroadcast", OnCmdSystemSpeedMatchBroadcast },
	{ "System.SpeedInteger", OnCmdSystemSpeedInteger },
	{ "System.ToggleVSyncAdaptiveSpeed", OnCmdSystemToggleVSyncAdaptive },
	{ "System.Rewind", OnCmdRewind },

	// Cartridge
	{ "Cart.Detach", OnCmdCartDetach },
	{ "Cart.DetachSecond", OnCmdCartDetachSecond },

	// Audio
	{ "Audio.ToggleStereo", OnCmdAudioToggleStereo },
	{ "Audio.ToggleMonitor", OnCmdAudioToggleMonitor },
	{ "Audio.ToggleScope", OnCmdAudioToggleScope },
	{ "Audio.ToggleMute", OnCmdAudioToggleMute },
	{ "Audio.ToggleNonlinearMixing", OnCmdAudioToggleNonlinearMixing },
	{ "Audio.ToggleSpeakerFilter", OnCmdAudioToggleSpeakerFilter },
	{ "Audio.ToggleSerialNoise", OnCmdAudioToggleSerialNoise },
	{ "Audio.ToggleSlightSid", OnCmdAudioToggleSlightSid },
	{ "Audio.ToggleCovox", OnCmdAudioToggleCovox },

	// Debug
	{ "Debug.ToggleBreakAtExeRun", OnCmdDebugToggleBreakAtExeRun },
	{ "Debug.ToggleAutoReloadRoms", OnCmdDebugToggleAutoReloadRoms },
	{ "Debug.ToggleAutoLoadKernelSymbols", OnCmdDebugToggleAutoLoadKernelSymbols },
	{ "Debug.ToggleAutoLoadSystemSymbols", OnCmdDebugToggleAutoLoadSystemSymbols },
	{ "Debug.Run", OnCmdDebugRun },
	{ "Debug.Break", OnCmdDebugBreak },
	{ "Debug.RunStop", OnCmdDebugRunStop },
	{ "Debug.ToggleDebugger", OnCmdDebugToggleDebugger },

	// Disk
	{ "Disk.ToggleSIOPatch", OnCmdDiskToggleSIOPatch },
	{ "Disk.ToggleSIOOverrideDetection", OnCmdDiskToggleSIOOverrideDetection },
	{ "Disk.ToggleSectorCounter", OnCmdDiskToggleSectorCounter },
	{ "Disk.DetachAll", OnCmdDiskDetachAll },
	{ "Disk.Detach1", OnCmdDiskDetach<0> },
	{ "Disk.Detach2", OnCmdDiskDetach<1> },
	{ "Disk.Detach3", OnCmdDiskDetach<2> },
	{ "Disk.Detach4", OnCmdDiskDetach<3> },
	{ "Disk.Detach5", OnCmdDiskDetach<4> },
	{ "Disk.Detach6", OnCmdDiskDetach<5> },
	{ "Disk.Detach7", OnCmdDiskDetach<6> },
	{ "Disk.Detach8", OnCmdDiskDetach<7> },
	{ "Disk.RotatePrev", OnCmdDiskRotatePrev },
	{ "Disk.RotateNext", OnCmdDiskRotateNext },

	// Cassette
	{ "Cassette.ToggleSIOPatch", OnCmdCassetteToggleSIOPatch },
	{ "Cassette.ToggleAutoBoot", OnCmdCassetteToggleAutoBoot },
	{ "Cassette.ToggleAutoBasicBoot", OnCmdCassetteToggleAutoBasicBoot },
	{ "Cassette.ToggleAutoRewind", OnCmdCassetteToggleAutoRewind },
	{ "Cassette.ToggleRandomizeStartPosition", OnCmdCassetteToggleRandomizeStartPosition },
	{ "Cassette.Unload", OnCmdCassetteUnload },

	// Video
	{ "Video.ToggleCTIA", OnCmdVideoToggleCTIA },
	{ "Video.ToggleFrameBlending", OnCmdVideoToggleFrameBlending },
	{ "Video.ToggleInterlace", OnCmdVideoToggleInterlace },
	{ "Video.ToggleScanlines", OnCmdVideoToggleScanlines },
	{ "Video.ToggleXEP80", OnCmdVideoToggleXEP80 },

	// Cheat
	{ "Cheat.ToggleDisablePMCollisions", OnCmdCheatTogglePMCollisions },
	{ "Cheat.ToggleDisablePFCollisions", OnCmdCheatTogglePFCollisions },

	// Input
	{ "Input.ToggleAutoCapture", OnCmdInputToggleAutoCapture },

	// View
	{ "View.ToggleFPS", OnCmdViewToggleFPS },

};

void ATLinuxInitCommands() {
	g_ATUICommandMgr.RegisterCommands(kLinuxCommands, vdcountof(kLinuxCommands));
}
