//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - Dear ImGui emulator configuration UI
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/refcount.h>
#include <vd2/system/registry.h>
#include <vd2/system/file.h>
#include <vd2/VDDisplay/display.h>
#include <at/atdebugger/target.h>
#include <at/atcore/media.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/serializable.h>
#include <at/atio/image.h>
#include <at/atio/diskimage.h>
#include <at/atio/cartridgeimage.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>

#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>

#include <imgui.h>
#include <emulator_imgui.h>
#include <debugger_imgui.h>
#include <display_sdl2.h>
#include <filedialog_linux.h>
#include <error_imgui.h>
#include <cartridge_names.h>
#include <firmware_names.h>

// Forward declarations needed by simulator.h transitives
class ATIRQController;

#include "simulator.h"
#include "constants.h"
#include "cassette.h"
#include "disk.h"
#include "diskinterface.h"
#include <at/atio/diskfs.h>
#include "cartridge.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "gtia.h"
#include "uikeyboard.h"
#include "devicemanager.h"
#include "cpu.h"
#include "debugger.h"
#include "inputmanager.h"
#include "oshelper.h"
#include "versioninfo.h"
#include "cheatengine.h"
#include <vd2/system/fraction.h>
#include "audiowriter.h"
#include "sapwriter.h"
#include "vgmwriter.h"
#include "videowriter.h"
#include "inputmap.h"
#include "joystick.h"
#include "autosavemanager.h"
#include "settings.h"
#include "audiomonitor.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <set>
#include <vector>

// Forward declarations for functions defined in cartdetect.cpp (compiled in Linux build)
uint32 ATCartridgeAutodetectMode(const void *data, uint32 size, vdfastvector<int>& cartModes);

// Firmware search path accessor (defined in main_linux.cpp)
void ATGetFirmwareSearchPaths(vdvector<VDStringW>& paths);

// Firmware switching (defined in stubs_linux.cpp)
bool ATUISwitchKernel(VDGUIHandle, uint64 kernelId);

// Speed timing update (defined in stubs_linux.cpp)
void ATUIUpdateSpeedTiming();

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

// Display backend accessor (defined in main_linux.cpp)
extern ATDisplaySDL2 *ATGetLinuxDisplay();

// Quick save state (in-memory)
static vdrefptr<IATSerializable> s_pQuickState;

// Recording state
static vdautoptr<ATAudioWriter> s_pAudioWriter;
static vdautoptr<IATSAPWriter> s_pSapWriter;
static vdrefptr<IATVgmWriter> s_pVgmWriter;
static vdautoptr<IATVideoWriter> s_pVideoWriter;

// Window visibility
static bool s_showSystemConfig = false;
static bool s_showAbout = false;
static bool s_showCartridgeBrowser = false;
static bool s_showFirmwareManager = false;
static bool s_showAudioOptions = false;
static bool s_showVideoConfig = false;
static bool s_showKeyboardConfig = false;
static bool s_showDeviceManager = false;
static bool s_showCassetteControl = false;
static bool s_showBootOptions = false;
static bool s_showCPUOptions = false;
static bool s_showInputSetup = false;
static bool s_showShortcuts = false;
static bool s_showCheater = false;
static bool s_showProfileManager = false;

// Cheat engine state
static bool s_cheaterInitialized = false;
static int s_cheaterMode = 0;
static char s_cheaterValueBuf[16] = "";
static int s_cheaterBit16 = 0;
static std::vector<uint32> s_cheaterResults;
static uint32 s_cheaterResultCount = 0;
static int s_cheatEditIdx = -1;      // which cheat is being edited (-1 = none)
static int s_cheatEditField = -1;    // 0 = address, 1 = value
static char s_cheatEditBuf[16] = "";
static bool s_cheatEditFocus = false; // request keyboard focus on next frame

// Input capture state for binding editor
static bool s_inputCaptureActive = false;
static uint32 s_capturedInputCode = 0;
static bool s_inputCaptureGotResult = false;
static int s_captureTargetMappingIdx = -1;  // -1 = adding new, >=0 = rebinding existing
static bool s_quitRequested = false;
static bool s_quitConfirmed = false;
// s_showStatusBar removed — now backed by ATUIGetShowStatusBar()/ATUISetShowStatusBar()

// Device manager state
static IATDevice *s_devSelectedDevice = nullptr;
static bool s_devShowConfig = false;
static bool s_devShowAddPopup = false;
static int s_devAddSelectedIdx = -1;
static ATPropertySet s_devEditProps;
static std::string s_devEditTag;

// ============= Device config descriptor system =============

enum class DevCfgType { Checkbox, IntDropdown, StringEdit, PathSelect, IntInput, FloatInput, BitfieldCheckbox, CompoundIntDropdown };

struct DevCfgChoice { int value; const char *name; };
struct DevCfgCompoundPair { uint32 val1; uint32 val2; };

struct DevCfgControl {
	DevCfgType type;
	const char *propKey;       // property key in ATPropertySet; for CompoundIntDropdown: first key
	const char *label;         // display label
	const DevCfgChoice *choices; // for dropdown/bitfield choices
	int choiceCount;
	bool defaultBool;          // for checkbox; for CompoundIntDropdown: true=store as bools
	const char *browseTitle;   // for path select; for CompoundIntDropdown: second property key
	// Extended fields (zero-initialized for existing controls via aggregate init)
	const DevCfgCompoundPair *pairs;       // CompoundIntDropdown: value pair lookup
	float floatMin, floatMax, floatDefault; // FloatInput: range and default
};

struct DevCfgDescriptor {
	const char *title;
	const DevCfgControl *controls;
	int controlCount;
};

// --- Dropdown choices ---

static const DevCfgChoice kIDChoices[] = {
	{0, "ID 0"}, {1, "ID 1"}, {2, "ID 2"}, {3, "ID 3"},
	{4, "ID 4"}, {5, "ID 5"}, {6, "ID 6"}, {7, "ID 7"},
};

static const DevCfgChoice kSIDE3VersionChoices[] = {
	{10, "V1.0"}, {14, "V1.4"},
};

static const DevCfgChoice kMyIDECPLDChoices[] = {
	{1, "V1"}, {2, "V2Ex"},
};

// --- Per-device control arrays ---

static const DevCfgControl kCfgVirtHD[] = {
	{ DevCfgType::PathSelect, "path", "Directory Path", nullptr, 0, false, "Select Directory" },
};

static const DevCfgControl kCfgHardDisk[] = {
	{ DevCfgType::PathSelect, "path", "Image Path", nullptr, 0, false, "Select Disk Image" },
	{ DevCfgType::Checkbox, "write_enabled", "Write Enabled", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "solid_state", "Solid State (SSD)", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgKMKJZIDE[] = {
	{ DevCfgType::IntDropdown, "id", "Device ID", kIDChoices, 8, false, nullptr },
	{ DevCfgType::Checkbox, "enablesdx", "Enable SDX Switch", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "writeprotect", "Write Protect", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "nvramguard", "NVRAM Guard", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgSIDE3[] = {
	{ DevCfgType::IntDropdown, "version", "Hardware Version", kSIDE3VersionChoices, 2, false, nullptr },
	{ DevCfgType::Checkbox, "led_enable", "Activity LED", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "recovery", "Recovery Mode", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgMyIDE[] = {
	{ DevCfgType::IntDropdown, "cpldver", "CPLD Version", kMyIDECPLDChoices, 2, false, nullptr },
};

static const DevCfgChoice kConnectRateChoices[] = {
	{300, "300"}, {1200, "1200"}, {2400, "2400"}, {4800, "4800"},
	{9600, "9600"}, {19200, "19200"}, {38400, "38400"}, {57600, "57600"},
	{115200, "115200"}, {230400, "230400"},
};

static const DevCfgChoice kVBXEVersionChoices[] = {
	{120, "FX 1.20"}, {124, "FX 1.24"}, {126, "FX 1.26"},
};

static const DevCfgChoice kXEP80PortChoices[] = {
	{1, "Port 1"}, {2, "Port 2"}, {3, "Port 3 (400/800)"}, {4, "Port 4 (400/800)"},
};

static const DevCfgChoice kDonglePortChoices[] = {
	{0, "Port 1"}, {1, "Port 2"}, {2, "Port 3 (400/800)"}, {3, "Port 4 (400/800)"},
};

static const DevCfgChoice kPrinterTranslationChoices[] = {
	{0, "Translate EOL"}, {1, "Raw"}, {2, "ATASCII to UTF-8"},
};

static const DevCfgChoice kDriveIDChoices[] = {
	{0, "Drive 1 (D1:)"}, {1, "Drive 2 (D2:)"}, {2, "Drive 3 (D3:)"}, {3, "Drive 4 (D4:)"},
};

static const DevCfgChoice k815IDChoices[] = {
	{0, "Drives 1-2 (D1:-D2:)"}, {2, "Drives 3-4 (D3:-D4:)"},
	{4, "Drives 5-6 (D5:-D6:)"}, {6, "Drives 7-8 (D7:-D8:)"},
};

static const DevCfgChoice kSoundBoardVersionChoices[] = {
	{110, "1.1 (VBXE-based)"}, {120, "1.2 (with multiplier)"}, {200, "2.0 Preview"},
};

static const DevCfgChoice kSoundBoardBaseChoices[] = {
	{0xD280, "$D280"}, {0xD2C0, "$D2C0"}, {0xD600, "$D600"}, {0xD700, "$D700"},
};

static const DevCfgChoice kCovoxRangeChoices[] = {
	{0, "$D100, 256 bytes"},
	{1, "$D280, 128 bytes"},
	{2, "$D500, 256 bytes"},
	{3, "$D600, 64 bytes"},
	{4, "$D600, 256 bytes"},
	{5, "$D700, 256 bytes"},
};

static const DevCfgCompoundPair kCovoxRangeValues[] = {
	{0xD100, 0x100}, {0xD280, 0x80}, {0xD500, 0x100},
	{0xD600, 0x40}, {0xD600, 0x100}, {0xD700, 0x100},
};

static const DevCfgChoice kCovoxChannelChoices[] = {
	{1, "Mono"}, {4, "Stereo"},
};

static const DevCfgChoice k850EmuLevelChoices[] = {
	{0, "None (emulated R: handler only)"},
	{1, "Minimal (stub loader only)"},
	{2, "Full (SIO protocol + 6502 R: handler)"},
};

static const DevCfgChoice k850BaudChoices[] = {
	{0, "Auto"}, {1, "300"}, {2, "45.5"}, {3, "50"}, {4, "56.875"},
	{5, "75"}, {6, "110"}, {7, "134.5"}, {8, "150"},
	{10, "600"}, {11, "1200"}, {12, "1800"}, {13, "2400"},
	{14, "4800"}, {15, "9600"},
};

static const DevCfgChoice kMultiplexerIDChoices[] = {
	{-1, "Host"}, {0, "Client (ID 1)"}, {1, "Client (ID 2)"}, {2, "Client (ID 3)"},
	{3, "Client (ID 4)"}, {4, "Client (ID 5)"}, {5, "Client (ID 6)"},
	{6, "Client (ID 7)"}, {7, "Client (ID 8)"},
};

static const DevCfgChoice kBlkSizeChoices[] = {
	{256, "256 bytes"}, {512, "512 bytes"},
};

static const DevCfgChoice kBBRamSizeChoices[] = {
	{8, "8K"}, {32, "32K"}, {64, "64K"},
};

static const DevCfgChoice kBBDipSwitchBits[] = {
	{0x01, "SW1: Ignore printer fault"},
	{0x02, "SW2: Enable HD + high-speed SIO"},
	{0x04, "SW3: Enable printer port"},
	{0x08, "SW4: Enable RS232 port"},
	{0x10, "SW5: Enable printer linefeeds"},
	{0x20, "SW6: ProWriter printer mode"},
	{0x40, "SW7: MIO compatibility mode"},
};

static const DevCfgChoice kBBFloppySlotChoices[] = {
	{0, "Not Connected"}, {1, "D1:"}, {2, "D2:"}, {3, "D3:"}, {4, "D4:"},
	{5, "D5:"}, {6, "D6:"}, {7, "D7:"}, {8, "D8:"}, {9, "D9:"},
	{10, "D10:"}, {11, "D11:"}, {12, "D12:"}, {13, "D13:"}, {14, "D14:"},
};

static const DevCfgChoice kBBFloppyTypeChoices[] = {
	{0, "180K 5.25\" 40-track SS"},
	{1, "360K 5.25\" 40-track DS"},
	{2, "1.2M 5.25\" 80-track DS HD"},
	{3, "360K 3.5\" 80-track SS"},
	{4, "720K 3.5\" 80-track DS"},
	{5, "1.4M 3.5\" 80-track DS HD"},
	{6, "1M 8\" 77-track DS HD"},
};

static const DevCfgChoice kBBFloppyMappingChoices[] = {
	{0, "XF551"}, {1, "ATR8000"}, {2, "PERCOM"},
};

static const DevCfgChoice kATR8000DriveTypeChoices[] = {
	{0, "None"}, {1, "5.25\""}, {2, "8\""},
};

static const DevCfgChoice kPercomDriveTypeChoices[] = {
	{0, "None"}, {1, "5.25\" (40 track)"}, {2, "5.25\" (80 track)"},
};

static const DevCfgChoice kPercomATFDCChoices[] = {
	{0, "1771+1791 (DD capable)"},
	{1, "1771+1795 (DD, side compare)"},
	{2, "1771 only (SD only)"},
};

static const DevCfgCompoundPair kPercomATFDCValues[] = {
	{0, 1},  // use1795=false, ddcapable=true
	{1, 1},  // use1795=true, ddcapable=true
	{0, 0},  // use1795=false, ddcapable=false
};

static const DevCfgChoice kAMDCDriveTypeChoices[] = {
	{0, "None"}, {1, "3\"/5.25\" (40 track)"}, {2, "3\"/5.25\" (80 track)"},
};

static const DevCfgChoice k1020ColorChoices[] = {
	{0x000000, "Black"}, {0x181FF0, "Blue"}, {0x0B9C2F, "Green"}, {0xC91B12, "Red"},
};

static const DevCfgControl kCfgModem[] = {
	{ DevCfgType::IntInput, "port", "Listen Port (0=disabled)", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "outbound", "Allow Outbound", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "telnet", "Telnet Emulation", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "ipv6", "Listen IPv6", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "unthrottled", "Unthrottled", nullptr, 0, false, nullptr },
	{ DevCfgType::IntDropdown, "connect_rate", "Connect Rate", kConnectRateChoices, 10, false, nullptr },
	{ DevCfgType::Checkbox, "check_rate", "Require Matched DTE Rate", nullptr, 0, false, nullptr },
	{ DevCfgType::StringEdit, "dialaddr", "Dial Address", nullptr, 0, false, nullptr },
	{ DevCfgType::StringEdit, "dialsvc", "Dial Service", nullptr, 0, false, nullptr },
	{ DevCfgType::StringEdit, "termtype", "Terminal Type", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgPrinter[] = {
	{ DevCfgType::Checkbox, "graphics", "Graphics Mode", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "accurate_timing", "Accurate Timing", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "sound", "Sound Emulation", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgPCLink[] = {
	{ DevCfgType::PathSelect, "path", "Base Directory", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::Checkbox, "write", "Write Access", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "set_timestamps", "Set Timestamps", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgHostFS[] = {
	{ DevCfgType::PathSelect, "path1", "Drive 1 Path", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::PathSelect, "path2", "Drive 2 Path", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::PathSelect, "path3", "Drive 3 Path", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::PathSelect, "path4", "Drive 4 Path", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::Checkbox, "readonly", "Read Only", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "longfilenames", "Long Filenames", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "lowercase", "Lowercase Names", nullptr, 0, true, nullptr },
};

static const DevCfgControl kCfgCustomDev[] = {
	{ DevCfgType::PathSelect, "path", "Config File Path", nullptr, 0, false, "Select Config File" },
	{ DevCfgType::Checkbox, "hotreload", "Hot Reload", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "allowunsafe", "Allow Unsafe Ops", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgVBXE[] = {
	{ DevCfgType::IntDropdown, "version", "Hardware Version", kVBXEVersionChoices, 3, false, nullptr },
	{ DevCfgType::Checkbox, "alt_page", "Alternate Page", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "shared_mem", "Shared Memory", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgXEP80[] = {
	{ DevCfgType::IntDropdown, "port", "Joystick Port", kXEP80PortChoices, 4, false, nullptr },
};

static const DevCfgControl kCfgVeronica[] = {
	{ DevCfgType::Checkbox, "version1", "V1 (Three RAM Chips)", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgCorvus[] = {
	{ DevCfgType::Checkbox, "altports", "Use Ports 1+2 (XL/XE Compatible)", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgComputerEyes[] = {
	{ DevCfgType::IntInput, "brightness", "Brightness (0-100)", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgParFileWriter[] = {
	{ DevCfgType::PathSelect, "path", "Output Path", nullptr, 0, false, "Select Output File" },
	{ DevCfgType::Checkbox, "text_mode", "Text Mode (EOL Conversion)", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgVideoStillImage[] = {
	{ DevCfgType::PathSelect, "path", "Image Path", nullptr, 0, false, "Select Image File" },
};

static const DevCfgControl kCfgDongle[] = {
	{ DevCfgType::IntDropdown, "port", "Joystick Port", kDonglePortChoices, 4, false, nullptr },
	{ DevCfgType::StringEdit, "mapping", "Mapping (16 Hex Digits)", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgPrinterHLE[] = {
	{ DevCfgType::IntDropdown, "translation_mode", "Translation Mode", kPrinterTranslationChoices, 3, false, nullptr },
};

static const DevCfgControl kCfgDiskDriveFull[] = {
	{ DevCfgType::IntDropdown, "id", "Drive ID", kDriveIDChoices, 4, false, nullptr },
};

static const DevCfgControl kCfgDiskDriveHappy810[] = {
	{ DevCfgType::IntDropdown, "id", "Drive ID", kDriveIDChoices, 4, false, nullptr },
	{ DevCfgType::Checkbox, "autospeed", "Auto-Speed", nullptr, 0, false, nullptr },
	{ DevCfgType::FloatInput, "autospeedrate", "Auto-Speed Rate (RPM)", nullptr, 0, false, nullptr, nullptr, 200.0f, 400.0f, 266.0f },
};

static const DevCfgControl kCfgDiskDrive815[] = {
	{ DevCfgType::IntDropdown, "id", "Drive Pair", k815IDChoices, 4, false, nullptr },
	{ DevCfgType::Checkbox, "accurate_invert", "Accurate Invert", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgSoundBoard[] = {
	{ DevCfgType::IntDropdown, "version", "Hardware Version", kSoundBoardVersionChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "base", "Base Address", kSoundBoardBaseChoices, 4, false, nullptr },
};

static const DevCfgControl kCfgCovox[] = {
	{ DevCfgType::CompoundIntDropdown, "base", "Address Range", kCovoxRangeChoices, 6, false, "size", kCovoxRangeValues },
	{ DevCfgType::IntDropdown, "channels", "Channels", kCovoxChannelChoices, 2, false, nullptr },
};

static const DevCfgControl kCfg850[] = {
	{ DevCfgType::Checkbox, "unthrottled", "Unthrottled", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "baudex", "Extended Baud Rates", nullptr, 0, false, nullptr },
	{ DevCfgType::IntDropdown, "emulevel", "Emulation Level", k850EmuLevelChoices, 3, false, nullptr },
};

static const DevCfgControl kCfg850Full[] = {
	{ DevCfgType::IntDropdown, "serbaud1", "Port 1 Baud Rate", k850BaudChoices, 15, false, nullptr },
	{ DevCfgType::IntDropdown, "serbaud2", "Port 2 Baud Rate", k850BaudChoices, 15, false, nullptr },
	{ DevCfgType::IntDropdown, "serbaud3", "Port 3 Baud Rate", k850BaudChoices, 15, false, nullptr },
	{ DevCfgType::IntDropdown, "serbaud4", "Port 4 Baud Rate", k850BaudChoices, 15, false, nullptr },
};

static const DevCfgControl kCfg1400XL[] = {
	{ DevCfgType::IntInput, "port", "Listen Port (0=disabled)", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "outbound", "Allow Outbound", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "telnet", "Telnet Emulation", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "telnetlf", "Telnet LF Mode", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "ipv6", "Listen IPv6", nullptr, 0, true, nullptr },
	{ DevCfgType::Checkbox, "unthrottled", "Unthrottled", nullptr, 0, false, nullptr },
	{ DevCfgType::StringEdit, "dialaddr", "Dial Address", nullptr, 0, false, nullptr },
	{ DevCfgType::StringEdit, "dialsvc", "Dial Service", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgNetSerial[] = {
	{ DevCfgType::StringEdit, "connect_addr", "Address", nullptr, 0, false, nullptr },
	{ DevCfgType::IntInput, "port", "TCP Port", nullptr, 0, false, nullptr },
	{ DevCfgType::IntInput, "baud_rate", "Baud Rate", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "listen", "Listen Mode", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgMultiplexer[] = {
	{ DevCfgType::IntDropdown, "device_id", "Device ID", kMultiplexerIDChoices, 9, false, nullptr },
	{ DevCfgType::StringEdit, "host_address", "Host Address", nullptr, 0, false, nullptr },
	{ DevCfgType::IntInput, "port", "TCP Port", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "allow_external", "Allow External Connections", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgPipeSerial[] = {
	{ DevCfgType::StringEdit, "pipe_name", "Pipe Name", nullptr, 0, false, nullptr },
	{ DevCfgType::IntInput, "baud_rate", "Baud Rate", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgBlackBox[] = {
	{ DevCfgType::BitfieldCheckbox, "dipsw", "DIP Switches", kBBDipSwitchBits, 7, false, nullptr },
	{ DevCfgType::IntDropdown, "blksize", "Sector Size", kBlkSizeChoices, 2, false, nullptr },
	{ DevCfgType::IntDropdown, "ramsize", "RAM Size", kBBRamSizeChoices, 3, false, nullptr },
};

static const DevCfgControl kCfgBlackBoxFloppy[] = {
	{ DevCfgType::IntDropdown, "driveslot0", "Slot 1 Drive", kBBFloppySlotChoices, 15, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype0", "Slot 1 Type", kBBFloppyTypeChoices, 7, false, nullptr },
	{ DevCfgType::IntDropdown, "drivemapping0", "Slot 1 Mapping", kBBFloppyMappingChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "driveslot1", "Slot 2 Drive", kBBFloppySlotChoices, 15, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype1", "Slot 2 Type", kBBFloppyTypeChoices, 7, false, nullptr },
	{ DevCfgType::IntDropdown, "drivemapping1", "Slot 2 Mapping", kBBFloppyMappingChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "driveslot2", "Slot 3 Drive", kBBFloppySlotChoices, 15, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype2", "Slot 3 Type", kBBFloppyTypeChoices, 7, false, nullptr },
	{ DevCfgType::IntDropdown, "drivemapping2", "Slot 3 Mapping", kBBFloppyMappingChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "driveslot3", "Slot 4 Drive", kBBFloppySlotChoices, 15, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype3", "Slot 4 Type", kBBFloppyTypeChoices, 7, false, nullptr },
	{ DevCfgType::IntDropdown, "drivemapping3", "Slot 4 Mapping", kBBFloppyMappingChoices, 3, false, nullptr },
};

static const DevCfgControl kCfgDiskDriveATR8000[] = {
	{ DevCfgType::IntDropdown, "drivetype0", "Drive 1 Type", kATR8000DriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype1", "Drive 2 Type", kATR8000DriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype2", "Drive 3 Type", kATR8000DriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype3", "Drive 4 Type", kATR8000DriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::StringEdit, "signal1", "Signal 1 (rts/dtr)", nullptr, 0, false, nullptr },
	{ DevCfgType::StringEdit, "signal2", "Signal 2 (cts/dsr/cd/srts)", nullptr, 0, false, nullptr },
};

static const DevCfgControl kCfgDiskDrivePercom[] = {
	{ DevCfgType::IntDropdown, "id", "Drive ID", kIDChoices, 8, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype0", "Drive 1 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype1", "Drive 2 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype2", "Drive 3 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype3", "Drive 4 Type", kPercomDriveTypeChoices, 3, false, nullptr },
};

static const DevCfgControl kCfgDiskDrivePercomAT[] = {
	{ DevCfgType::CompoundIntDropdown, "use1795", "FDC Type", kPercomATFDCChoices, 3, true, "ddcapable", kPercomATFDCValues },
	{ DevCfgType::IntDropdown, "drivetype0", "Drive 1 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype1", "Drive 2 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype2", "Drive 3 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype3", "Drive 4 Type", kPercomDriveTypeChoices, 3, false, nullptr },
};

static const DevCfgControl kCfgDiskDrivePercomATSPD[] = {
	{ DevCfgType::Checkbox, "use1795", "Use 1795 FDC (Side Compare Always On)", nullptr, 0, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype0", "Drive 1 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype1", "Drive 2 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype2", "Drive 3 Type", kPercomDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "drivetype3", "Drive 4 Type", kPercomDriveTypeChoices, 3, false, nullptr },
};

static const DevCfgControl kCfgDiskDriveAMDC[] = {
	{ DevCfgType::IntInput, "switches", "DIP Switches", nullptr, 0, false, nullptr },
	{ DevCfgType::Checkbox, "drive2", "Second External Drive", nullptr, 0, false, nullptr },
	{ DevCfgType::IntDropdown, "extdrive0", "External Drive 1 Type", kAMDCDriveTypeChoices, 3, false, nullptr },
	{ DevCfgType::IntDropdown, "extdrive1", "External Drive 2 Type", kAMDCDriveTypeChoices, 3, false, nullptr },
};

static const DevCfgControl kCfg1020[] = {
	{ DevCfgType::IntDropdown, "pencolor0", "Pen 1 Color", k1020ColorChoices, 4, false, nullptr },
	{ DevCfgType::IntDropdown, "pencolor1", "Pen 2 Color", k1020ColorChoices, 4, false, nullptr },
	{ DevCfgType::IntDropdown, "pencolor2", "Pen 3 Color", k1020ColorChoices, 4, false, nullptr },
	{ DevCfgType::IntDropdown, "pencolor3", "Pen 4 Color", k1020ColorChoices, 4, false, nullptr },
};

// --- Tag → descriptor lookup ---

struct DevCfgTagMapping {
	const char *tag;
	const char *title;
	const DevCfgControl *controls;
	int controlCount;
};

#define DEVCFG_ENTRY(tag, title, arr) { tag, title, arr, (int)(sizeof(arr)/sizeof(arr[0])) }

static const DevCfgTagMapping kDevCfgMappings[] = {
	DEVCFG_ENTRY("hdvirtfat16", "Virtual FAT16 Hard Disk", kCfgVirtHD),
	DEVCFG_ENTRY("hdvirtfat32", "Virtual FAT32 Hard Disk", kCfgVirtHD),
	DEVCFG_ENTRY("hdvirtsdfs", "Virtual SDFS Hard Disk", kCfgVirtHD),
	DEVCFG_ENTRY("harddisk", "Hard Disk Image", kCfgHardDisk),
	DEVCFG_ENTRY("kmkjzide", "KMK/JZ IDE", kCfgKMKJZIDE),
	DEVCFG_ENTRY("kmkjzide2", "KMK/JZ IDE II", kCfgKMKJZIDE),
	DEVCFG_ENTRY("side3", "SIDE 3", kCfgSIDE3),
	DEVCFG_ENTRY("myide-d1xx", "MyIDE Internal", kCfgMyIDE),
	DEVCFG_ENTRY("myide-d5xx", "MyIDE Cartridge", kCfgMyIDE),
	DEVCFG_ENTRY("myide-d2xx", "MyIDE-II", kCfgMyIDE),
	DEVCFG_ENTRY("myide2", "MyIDE-II", kCfgMyIDE),
	DEVCFG_ENTRY("modem", "Modem", kCfgModem),
	DEVCFG_ENTRY("835", "835 Modem", kCfgModem),
	DEVCFG_ENTRY("835full", "835 Modem (Full)", kCfgModem),
	DEVCFG_ENTRY("1030", "1030 Modem", kCfgModem),
	DEVCFG_ENTRY("1030full", "1030 Modem (Full)", kCfgModem),
	DEVCFG_ENTRY("sx212", "SX212 Modem", kCfgModem),
	DEVCFG_ENTRY("pocketmodem", "Pocket Modem", kCfgModem),
	DEVCFG_ENTRY("820", "820 Printer", kCfgPrinter),
	DEVCFG_ENTRY("1025", "1025 Printer", kCfgPrinter),
	DEVCFG_ENTRY("1029", "1029 Printer", kCfgPrinter),
	DEVCFG_ENTRY("pclink", "PCLink", kCfgPCLink),
	DEVCFG_ENTRY("hostfs", "Host FS Bridge", kCfgHostFS),
	DEVCFG_ENTRY("customdev", "Custom Device", kCfgCustomDev),
	DEVCFG_ENTRY("custom", "Custom Device", kCfgCustomDev),
	DEVCFG_ENTRY("vbxe", "VBXE", kCfgVBXE),
	DEVCFG_ENTRY("xep80", "XEP-80", kCfgXEP80),
	DEVCFG_ENTRY("veronica", "Veronica", kCfgVeronica),
	DEVCFG_ENTRY("corvus", "Corvus Disk", kCfgCorvus),
	DEVCFG_ENTRY("computereyes", "Computer Eyes", kCfgComputerEyes),
	DEVCFG_ENTRY("parfilewriter", "Parallel File Writer", kCfgParFileWriter),
	DEVCFG_ENTRY("videostillimage", "Video Still Image", kCfgVideoStillImage),
	DEVCFG_ENTRY("dongle", "Dongle", kCfgDongle),
	DEVCFG_ENTRY("printer", "Printer (P:)", kCfgPrinterHLE),
	DEVCFG_ENTRY("diskdrive810", "810 Disk Drive", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrive810archiver", "810 Archiver", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrive1050", "1050 Disk Drive", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdriveusdoubler", "US Doubler", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivespeedy1050", "Speedy 1050", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivehappy1050", "Happy 1050", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivesuperarchiver", "Super Archiver", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivesuperarchiverbw", "Super Archiver (B&W)", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivetoms1050", "Toms 1050", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrive1050duplicator", "1050 Duplicator", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivetygrys1050", "Tygrys 1050", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrive1050turbo", "1050 Turbo", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrive1050turboii", "1050 Turbo II", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdriveisplate", "IS-Plate", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdriveindusgt", "Indus GT", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivexf551", "XF551", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrive810turbo", "810 Turbo", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivespeedyxf", "Speedy XF", kCfgDiskDriveFull),
	DEVCFG_ENTRY("diskdrivehappy810", "Happy 810", kCfgDiskDriveHappy810),
	DEVCFG_ENTRY("diskdrive815", "815 Dual Disk Drive", kCfgDiskDrive815),
	DEVCFG_ENTRY("soundboard", "SoundBoard", kCfgSoundBoard),
	DEVCFG_ENTRY("covox", "Covox", kCfgCovox),
	DEVCFG_ENTRY("850", "850 Interface", kCfg850),
	DEVCFG_ENTRY("850full", "850 Interface (Full)", kCfg850Full),
	DEVCFG_ENTRY("1400xl", "1400XL Modem", kCfg1400XL),
	DEVCFG_ENTRY("netserial", "Network Serial", kCfgNetSerial),
	DEVCFG_ENTRY("multiplexer", "Multiplexer", kCfgMultiplexer),
	DEVCFG_ENTRY("pipeserial", "Pipe Serial", kCfgPipeSerial),
	DEVCFG_ENTRY("blackbox", "Black Box", kCfgBlackBox),
	DEVCFG_ENTRY("blackboxfloppy", "Black Box Floppy", kCfgBlackBoxFloppy),
	DEVCFG_ENTRY("diskdriveatr8000", "ATR8000", kCfgDiskDriveATR8000),
	DEVCFG_ENTRY("diskdrivepercom", "Percom RFD", kCfgDiskDrivePercom),
	DEVCFG_ENTRY("diskdrivepercomat", "Percom AT", kCfgDiskDrivePercomAT),
	DEVCFG_ENTRY("diskdrivepercomatspd", "Percom AT-SPD", kCfgDiskDrivePercomATSPD),
	DEVCFG_ENTRY("diskdriveamdc", "AM&DC", kCfgDiskDriveAMDC),
	DEVCFG_ENTRY("1020", "1020 Color Printer", kCfg1020),
};

#undef DEVCFG_ENTRY

static const DevCfgTagMapping *FindDevCfgMapping(const char *tag) {
	for (const auto& m : kDevCfgMappings) {
		if (strcmp(m.tag, tag) == 0)
			return &m;
	}
	return nullptr;
}

// Structured device config editor — returns true if values were changed
static bool DrawStructuredDeviceConfig(ATPropertySet& props, const DevCfgTagMapping& mapping) {
	bool changed = false;

	for (int i = 0; i < mapping.controlCount; i++) {
		const DevCfgControl& ctrl = mapping.controls[i];
		ImGui::PushID(ctrl.propKey);

		switch (ctrl.type) {
			case DevCfgType::Checkbox: {
				bool val = props.GetBool(ctrl.propKey, ctrl.defaultBool);
				if (ImGui::Checkbox(ctrl.label, &val)) {
					props.SetBool(ctrl.propKey, val);
					changed = true;
				}
				break;
			}

			case DevCfgType::IntDropdown: {
				int val = (int)props.GetUint32(ctrl.propKey, 0);

				// Find current selection index
				int selIdx = 0;
				for (int j = 0; j < ctrl.choiceCount; j++) {
					if (ctrl.choices[j].value == val) {
						selIdx = j;
						break;
					}
				}

				ImGui::SetNextItemWidth(150);
				if (ImGui::BeginCombo(ctrl.label, ctrl.choices[selIdx].name)) {
					for (int j = 0; j < ctrl.choiceCount; j++) {
						bool selected = (j == selIdx);
						if (ImGui::Selectable(ctrl.choices[j].name, selected)) {
							props.SetUint32(ctrl.propKey, (uint32)ctrl.choices[j].value);
							changed = true;
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				break;
			}

			case DevCfgType::StringEdit: {
				VDStringA u8val;
				const wchar_t *wval = props.GetString(ctrl.propKey);
				if (wval)
					u8val = VDTextWToU8(VDStringW(wval));

				char buf[512];
				strncpy(buf, u8val.c_str(), sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = 0;

				ImGui::SetNextItemWidth(300);
				if (ImGui::InputText(ctrl.label, buf, sizeof(buf))) {
					VDStringW wstr = VDTextU8ToW(VDStringA(buf));
					props.SetString(ctrl.propKey, wstr.c_str());
					changed = true;
				}
				break;
			}

			case DevCfgType::IntInput: {
				int val = (int)props.GetUint32(ctrl.propKey, 0);
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt(ctrl.label, &val)) {
					if (val < 0) val = 0;
					props.SetUint32(ctrl.propKey, (uint32)val);
					changed = true;
				}
				break;
			}

			case DevCfgType::FloatInput: {
				float val = props.GetFloat(ctrl.propKey, ctrl.floatDefault);
				ImGui::SetNextItemWidth(150);
				if (ImGui::SliderFloat(ctrl.label, &val, ctrl.floatMin, ctrl.floatMax, "%.1f")) {
					props.SetFloat(ctrl.propKey, val);
					changed = true;
				}
				break;
			}

			case DevCfgType::BitfieldCheckbox: {
				uint32 val = props.GetUint32(ctrl.propKey, 0);
				ImGui::Text("%s", ctrl.label);
				ImGui::Indent();
				for (int j = 0; j < ctrl.choiceCount; j++) {
					uint32 bit = (uint32)ctrl.choices[j].value;
					bool bitSet = (val & bit) != 0;
					if (ImGui::Checkbox(ctrl.choices[j].name, &bitSet)) {
						if (bitSet)
							val |= bit;
						else
							val &= ~bit;
						props.SetUint32(ctrl.propKey, val);
						changed = true;
					}
				}
				ImGui::Unindent();
				break;
			}

			case DevCfgType::CompoundIntDropdown: {
				const char *propKey2 = ctrl.browseTitle; // second property key
				uint32 val1, val2;
				if (ctrl.defaultBool) {
					val1 = props.GetBool(ctrl.propKey, false) ? 1 : 0;
					val2 = props.GetBool(propKey2, false) ? 1 : 0;
				} else {
					val1 = props.GetUint32(ctrl.propKey, 0);
					val2 = props.GetUint32(propKey2, 0);
				}

				// Find current selection by matching both values
				int selIdx = 0;
				for (int j = 0; j < ctrl.choiceCount; j++) {
					if (ctrl.pairs[j].val1 == val1 && ctrl.pairs[j].val2 == val2) {
						selIdx = j;
						break;
					}
				}

				ImGui::SetNextItemWidth(250);
				if (ImGui::BeginCombo(ctrl.label, ctrl.choices[selIdx].name)) {
					for (int j = 0; j < ctrl.choiceCount; j++) {
						bool selected = (j == selIdx);
						if (ImGui::Selectable(ctrl.choices[j].name, selected)) {
							if (ctrl.defaultBool) {
								props.SetBool(ctrl.propKey, ctrl.pairs[j].val1 != 0);
								props.SetBool(propKey2, ctrl.pairs[j].val2 != 0);
							} else {
								props.SetUint32(ctrl.propKey, ctrl.pairs[j].val1);
								props.SetUint32(propKey2, ctrl.pairs[j].val2);
							}
							changed = true;
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				break;
			}

			case DevCfgType::PathSelect: {
				VDStringA u8val;
				const wchar_t *wval = props.GetString(ctrl.propKey);
				if (wval)
					u8val = VDTextWToU8(VDStringW(wval));

				char buf[512];
				strncpy(buf, u8val.c_str(), sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = 0;

				ImGui::SetNextItemWidth(250);
				if (ImGui::InputText(ctrl.label, buf, sizeof(buf))) {
					VDStringW wstr = VDTextU8ToW(VDStringA(buf));
					props.SetString(ctrl.propKey, wstr.c_str());
					changed = true;
				}

				ImGui::SameLine();
				char browseId[32];
				snprintf(browseId, sizeof(browseId), "Browse##%s", ctrl.propKey);
				if (ImGui::Button(browseId)) {
					VDStringW path = ATLinuxOpenFileDialog(
						ctrl.browseTitle ? ctrl.browseTitle : "Select Path",
						"All Files|*");
					if (!path.empty()) {
						props.SetString(ctrl.propKey, path.c_str());
						changed = true;
					}
				}
				break;
			}
		}

		ImGui::PopID();
	}

	return changed;
}

// Cartridge browser state
static bool s_cartMapperActive = false;
static vdfastvector<uint8> s_cartLoadBuffer;
static VDStringW s_cartLoadPath;
static vdfastvector<int> s_cartDetectedModes;
static vdfastvector<int> s_cartDisplayModes;
static int s_cartSelectedMapper = -1;
static bool s_cartShowAll = false;
static uint32 s_cartRecommendedIdx = 0;

// Firmware manager state
static vdvector<ATFirmwareInfo> s_fwList;
static bool s_fwListDirty = true;
static bool s_fwDefaultsChanged = false;
static uint64 s_fwSelectedId = 0;
static std::string s_fwScanResult;
static bool s_fwShowScanResult = false;

// New disk dialog state
static bool s_showNewDisk = false;
static int s_newDiskSlot = 0;
static int s_newDiskFormat = 1;  // 0=Custom, 1=SD 720, 2=MD 1040, 3=DD 720, 4=DSDD 1440
static int s_newDiskFS = 0;      // 0=None, 1=DOS 2, 2=MyDOS

// Disk explorer state
static bool s_showDiskExplorer = false;
static int s_diskExplorerSlot = -1;  // -1 = standalone (not tied to a drive)
static vdautoptr<IATDiskFS> s_diskFS;
static vdrefptr<IATDiskImage> s_diskStandaloneImage;  // holds image for standalone explorer
static std::string s_diskExplorerTitle;  // custom title for standalone mode
static ATDiskFSKey s_diskCurDir = ATDiskFSKey::None;
static std::vector<ATDiskFSEntryInfo> s_diskEntries;
static int s_diskSelectedIdx = -1;  // last clicked item (for single-item ops like rename)
static std::set<int> s_diskSelection;  // multi-select set
static int s_diskShiftAnchor = -1;  // anchor for shift-click range selection
static std::vector<ATDiskFSKey> s_diskDirStack;
static bool s_diskReadOnly = false;
static bool s_diskTextMode = false;  // Atari EOL (0x9B) ↔ Unix LF (0x0A) conversion
static std::string s_diskFSInfoStr;
static char s_diskRenameBuffer[64] = {};
static bool s_diskRenameActive = false;

// Toast notification system
struct Toast {
	std::string message;
	double expireTime;
};
static std::vector<Toast> s_toasts;

static void ShowToast(const char *msg) {
	double now = (double)SDL_GetTicks() / 1000.0;
	s_toasts.push_back({msg, now + 2.5});
}

static void DrawToasts() {
	if (s_toasts.empty())
		return;

	double now = (double)SDL_GetTicks() / 1000.0;

	// Remove expired
	while (!s_toasts.empty() && s_toasts.front().expireTime <= now)
		s_toasts.erase(s_toasts.begin());

	if (s_toasts.empty())
		return;

	ImVec2 vp = ImGui::GetMainViewport()->Size;
	float y = vp.y - 60.0f;

	for (int i = (int)s_toasts.size() - 1; i >= 0; --i) {
		float remaining = (float)(s_toasts[i].expireTime - now);
		float alpha = remaining < 0.5f ? remaining * 2.0f : 1.0f;

		ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.75f * alpha);

		char winId[32];
		snprintf(winId, sizeof(winId), "##toast%d", i);
		ImGui::Begin(winId, nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);

		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", s_toasts[i].message.c_str());
		ImGui::End();

		y -= 30.0f;
	}
}

static void DrawSourceMessage() {
	ATDisplaySDL2 *disp = ATGetLinuxDisplay();
	if (!disp)
		return;

	const char *msg = disp->GetSourceMessage();
	if (!msg)
		return;

	ImVec2 vp = ImGui::GetMainViewport()->Size;
	ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowBgAlpha(0.75f);
	ImGui::Begin("##srcmsg", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
	ImGui::Text("%s", msg);
	ImGui::End();
}

// FPS counter state
static double s_lastFpsTime = 0;
static int s_fpsFrameCount = 0;
static float s_currentFps = 0;

// Error popup state
static bool s_showErrorPopup = false;
static std::string s_errorTitle;
static std::string s_errorMessage;

// Pending file dialog result tracking
static enum class PendingDialog {
	kNone,
	kOpenImage,
	kMountDisk,
	kSaveState,
	kLoadState,
	kLoadTape,
	kSaveTape,
	kBootImage
} s_pendingDialog = PendingDialog::kNone;
static int s_pendingMountDiskSlot = 0;

static const char *kDiskFilters =
	"Disk Images|*.atr;*.xfd;*.dcm;*.pro;*.atx"
	"|Cartridge Images|*.bin;*.car"
	"|Tape Images|*.cas;*.wav"
	"|All Files|*";

static const char *kDiskOnlyFilters =
	"Disk Images|*.atr;*.xfd;*.dcm;*.pro;*.atx"
	"|All Files|*";

static const char *kSaveStateFilters =
	"Save States|*.atstate2;*.altstate"
	"|All Files|*";

static const char *kCartFilters =
	"Cartridge Images|*.car;*.bin;*.rom"
	"|All Files|*";

static const char *kTapeFilters =
	"Tape Images|*.cas;*.wav"
	"|All Files|*";

static const char *kFirmwareFilters =
	"ROM Images|*.rom;*.bin;*.epr;*.epm"
	"|All Files|*";

static const char *kWavFilters =
	"WAV Audio|*.wav"
	"|All Files|*";

static const char *kSapFilters =
	"SAP Type-R|*.sap"
	"|All Files|*";

static const char *kVgmFilters =
	"VGM Files|*.vgm"
	"|All Files|*";

static const char *kAviFilters =
	"AVI Video|*.avi"
	"|All Files|*";

// ============= Video recording config dialog state =============

static bool s_showVideoRecordDialog = false;
static int s_videoCodecIndex = 0;       // 0=ZMBV, 1=Raw, 2=RLE [, 3=H.264+AAC if FFmpeg]
static int s_videoScalingIndex = 0;     // 0=None, 1=480p Narrow, 2=480p Wide, 3=720p Narrow, 4=720p Wide
static int s_videoResamplingIndex = 0;  // 0=Nearest, 1=Sharp Bilinear, 2=Bilinear
static bool s_videoHalfRate = false;
static bool s_videoEncodeAll = false;
static int s_videoBitRateKbps = 2000;   // 500-8000 kbps for H.264

static void StartVideoRecording(const wchar_t *path) {
	try {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();

		ATCreateVideoWriter(~s_pVideoWriter);

		int w, h;
		bool rgb32;
		gtia.GetRawFrameFormat(w, h, rgb32);

		uint32 palette[256];
		if (!rgb32)
			gtia.GetPalette(palette);

		const bool hz50 = g_sim.IsVideo50Hz();
		VDFraction frameRate = hz50 ? VDFraction(1773447, 114*312) : VDFraction(3579545, 2*114*262);
		double samplingRate = hz50 ? 1773447.0 / 28.0 : 3579545.0 / 56.0;
		double par = gtia.GetPixelAspectRatio();

#ifdef AT_HAVE_FFMPEG
		static const ATVideoEncoding kCodecs[] = { kATVideoEncoding_ZMBV, kATVideoEncoding_Raw, kATVideoEncoding_RLE, kATVideoEncoding_H264_AAC };
#else
		static const ATVideoEncoding kCodecs[] = { kATVideoEncoding_ZMBV, kATVideoEncoding_Raw, kATVideoEncoding_RLE };
#endif
		ATVideoEncoding encoding = kCodecs[s_videoCodecIndex];

		static const ATVideoRecordingScalingMode kScaling[] = {
			ATVideoRecordingScalingMode::None,
			ATVideoRecordingScalingMode::Scale480Narrow,
			ATVideoRecordingScalingMode::Scale480Wide,
			ATVideoRecordingScalingMode::Scale720Narrow,
			ATVideoRecordingScalingMode::Scale720Wide
		};
		ATVideoRecordingScalingMode scalingMode = kScaling[s_videoScalingIndex];

		static const ATVideoRecordingResamplingMode kResampling[] = {
			ATVideoRecordingResamplingMode::Nearest,
			ATVideoRecordingResamplingMode::SharpBilinear,
			ATVideoRecordingResamplingMode::Bilinear
		};
		ATVideoRecordingResamplingMode resamplingMode = kResampling[s_videoResamplingIndex];

		double timestampRate = hz50 ? 1773447.0 : 1789772.5;

		uint32 videoBitRate = 0, audioBitRate = 0;
		if (encoding == kATVideoEncoding_H264_AAC) {
			videoBitRate = s_videoBitRateKbps * 1000;
			audioBitRate = 128000;
		}

		s_pVideoWriter->Init(path, encoding, videoBitRate, audioBitRate,
			w, h, frameRate, par, resamplingMode, scalingMode,
			rgb32 ? nullptr : palette,
			samplingRate, g_sim.IsDualPokeysEnabled(),
			timestampRate, s_videoHalfRate, s_videoEncodeAll,
			g_sim.GetUIRenderer());

		g_sim.GetAudioOutput()->SetAudioTap(s_pVideoWriter->AsAudioTap());
		gtia.AddVideoTap(s_pVideoWriter->AsVideoTap());
	} catch (const std::exception& e) {
		s_pVideoWriter.reset();
		char msg[512];
		snprintf(msg, sizeof(msg), "Video recording failed: %s", e.what());
		ShowToast(msg);
	} catch (...) {
		s_pVideoWriter.reset();
		ShowToast("Failed to start video recording");
	}
}

// ============= Recording helpers =============

static bool IsRecordingActive() {
	return s_pAudioWriter || s_pSapWriter || s_pVgmWriter || s_pVideoWriter;
}

static void StopAllRecording() {
	if (s_pVideoWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);
		g_sim.GetGTIA().RemoveVideoTap(s_pVideoWriter->AsVideoTap());
		try { s_pVideoWriter->Shutdown(); } catch (...) {}
		s_pVideoWriter.reset();
	}
	if (s_pAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);
		try { s_pAudioWriter->Finalize(); } catch (...) {}
		s_pAudioWriter.reset();
	}
	if (s_pSapWriter) {
		try { s_pSapWriter->Shutdown(); } catch (...) {}
		s_pSapWriter.reset();
	}
	if (s_pVgmWriter) {
		try { s_pVgmWriter->Shutdown(); } catch (...) {}
		s_pVgmWriter.clear();
	}
}

// ============= Hardware mode names =============

static const char *kHardwareModeNames[] = {
	"800",
	"800XL",
	"5200",
	"XEGS",
	"1200XL",
	"130XE",
	"1400XL"
};
static_assert(sizeof(kHardwareModeNames) / sizeof(kHardwareModeNames[0]) == kATHardwareModeCount);

// Memory modes sorted by size (matching Windows UI order) with descriptive labels.
// The enum order is non-sequential (48K,52K,64K,128K,...,16K,8K,...), so we use an
// indirection table to display them in sorted order.
struct MemoryModeEntry {
	ATMemoryMode mode;
	const char *label;
};

static const MemoryModeEntry kSortedMemoryModes[] = {
	{ kATMemoryMode_8K,			"8K" },
	{ kATMemoryMode_16K,		"16K" },
	{ kATMemoryMode_24K,		"24K" },
	{ kATMemoryMode_32K,		"32K" },
	{ kATMemoryMode_40K,		"40K" },
	{ kATMemoryMode_48K,		"48K (800)" },
	{ kATMemoryMode_52K,		"52K" },
	{ kATMemoryMode_64K,		"64K (800XL/1200XL)" },
	{ kATMemoryMode_128K,		"128K (130XE)" },
	{ kATMemoryMode_256K,		"256K (Rambo)" },
	{ kATMemoryMode_320K,		"320K (Rambo)" },
	{ kATMemoryMode_320K_Compy,	"320K (Compy)" },
	{ kATMemoryMode_576K,		"576K (Rambo)" },
	{ kATMemoryMode_576K_Compy,	"576K (Compy)" },
	{ kATMemoryMode_1088K,		"1088K" },
};
static_assert(sizeof(kSortedMemoryModes) / sizeof(kSortedMemoryModes[0]) == kATMemoryModeCount);

// Helper to find the sorted index for a given ATMemoryMode
static int MemoryModeToSortedIndex(ATMemoryMode mode) {
	for (int i = 0; i < (int)kATMemoryModeCount; ++i) {
		if (kSortedMemoryModes[i].mode == mode)
			return i;
	}
	return 0;
}

// ImGui combo callback for sorted memory modes
static bool MemoryModeComboGetter(void *, int idx, const char **out) {
	if (idx < 0 || idx >= (int)kATMemoryModeCount)
		return false;
	*out = kSortedMemoryModes[idx].label;
	return true;
}

static const char *kVideoStandardNames[] = {
	"NTSC",
	"PAL",
	"SECAM",
	"PAL-60",
	"NTSC-50"
};
static_assert(sizeof(kVideoStandardNames) / sizeof(kVideoStandardNames[0]) == kATVideoStandardCount);

static const char *kDisplayFilterNames[] = {
	"Point",
	"Bilinear",
	"Bicubic",
	"Any Suitable",
	"Sharp Bilinear"
};

static const char *kArtifactModeNames[] = {
	"None", "NTSC", "PAL", "NTSC Hi", "PAL Hi", "Auto", "Auto Hi"
};
static_assert(sizeof(kArtifactModeNames)/sizeof(kArtifactModeNames[0]) == (int)ATArtifactMode::Count);

static const char *kMonitorModeNames[] = {
	"Color", "Peritel", "Green Mono", "Amber Mono", "Bluish White Mono", "White Mono"
};
static_assert(sizeof(kMonitorModeNames)/sizeof(kMonitorModeNames[0]) == (int)ATMonitorMode::Count);

static const char *kOverscanModeNames[] = {
	"Normal (168cc)", "Extended (192cc)", "Full (228cc)", "OS Screen (160cc)", "Widescreen (176cc)"
};

static const char *kVertOverscanModeNames[] = {
	"Default", "OS Screen (192)", "Normal (224)", "Extended (240)", "Full"
};

static const char *kStretchModeNames[] = {
	"Unconstrained", "Preserve Aspect Ratio", "Square Pixels", "Integral", "Integral + Aspect"
};
static_assert(sizeof(kStretchModeNames)/sizeof(kStretchModeNames[0]) == kATDisplayStretchModeCount);

static const char *kColorMatchingNames[] = {
	"None", "sRGB", "Adobe RGB", "Gamma 2.2", "Gamma 2.4"
};

static const char *kLumaRampNames[] = {
	"Linear", "XL"
};

static const char *kCassetteTurboModeNames[] = {
	"None (FSK only)",
	"Command Control",
	"Proceed Sense",
	"Interrupt Sense",
	"KSO Turbo 2000",
	"Turbo D",
	"Data Control",
	"Always"
};
static_assert(sizeof(kCassetteTurboModeNames)/sizeof(kCassetteTurboModeNames[0]) == kATCassetteTurboMode_Always + 1);

static const char *kCPUModeNames[] = {
	"6502",
	"65C02",
	"65C816"
};
static_assert(sizeof(kCPUModeNames)/sizeof(kCPUModeNames[0]) == kATCPUModeCount);

static const char *ATGetControllerTypeName(ATInputControllerType type) {
	switch (type) {
		case kATInputControllerType_Joystick:		return "Joystick";
		case kATInputControllerType_Paddle:			return "Paddle";
		case kATInputControllerType_STMouse:		return "ST Mouse";
		case kATInputControllerType_Console:		return "Console";
		case kATInputControllerType_5200Controller:	return "5200 Controller";
		case kATInputControllerType_InputState:		return "Input State";
		case kATInputControllerType_LightGun:		return "Light Gun (XG-1)";
		case kATInputControllerType_Tablet:			return "Tablet";
		case kATInputControllerType_KoalaPad:		return "KoalaPad";
		case kATInputControllerType_AmigaMouse:		return "Amiga Mouse";
		case kATInputControllerType_Keypad:			return "Keypad";
		case kATInputControllerType_Trackball_CX80:	return "Trackball CX-80";
		case kATInputControllerType_5200Trackball:	return "5200 Trackball";
		case kATInputControllerType_Driving:		return "Driving Controller";
		case kATInputControllerType_Keyboard:		return "Keyboard";
		case kATInputControllerType_LightPen:		return "Light Pen";
		case kATInputControllerType_PowerPad:		return "Power Pad";
		case kATInputControllerType_LightPenStack:	return "Light Pen Stack";
		default:									return "Unknown";
	}
}

static const char *ATGetInputCodeName(uint32 code) {
	static char buf[32];
	uint32 id = code & kATInputCode_IdMask;

	if (id >= kATInputCode_JoyClass) {
		if (id >= kATInputCode_JoyButton0) {
			snprintf(buf, sizeof(buf), "Joy Btn%d", id - kATInputCode_JoyButton0);
			return buf;
		}
		switch (id) {
			case kATInputCode_JoyStick1Left:	return "Joy Left";
			case kATInputCode_JoyStick1Right:	return "Joy Right";
			case kATInputCode_JoyStick1Up:		return "Joy Up";
			case kATInputCode_JoyStick1Down:	return "Joy Down";
			case kATInputCode_JoyHoriz1:		return "Joy Axis X";
			case kATInputCode_JoyVert1:			return "Joy Axis Y";
			case kATInputCode_JoyPOVLeft:		return "Joy POV Left";
			case kATInputCode_JoyPOVRight:		return "Joy POV Right";
			case kATInputCode_JoyPOVUp:			return "Joy POV Up";
			case kATInputCode_JoyPOVDown:		return "Joy POV Down";
			default: snprintf(buf, sizeof(buf), "Joy 0x%X", id); return buf;
		}
	} else if (id >= kATInputCode_MouseClass) {
		switch (id) {
			case kATInputCode_MouseHoriz:		return "Mouse X";
			case kATInputCode_MouseVert:		return "Mouse Y";
			case kATInputCode_MouseLMB:			return "Mouse LMB";
			case kATInputCode_MouseRMB:			return "Mouse RMB";
			case kATInputCode_MouseMMB:			return "Mouse MMB";
			case kATInputCode_MouseLeft:		return "Mouse Left";
			case kATInputCode_MouseRight:		return "Mouse Right";
			case kATInputCode_MouseUp:			return "Mouse Up";
			case kATInputCode_MouseDown:		return "Mouse Down";
			default: snprintf(buf, sizeof(buf), "Mouse 0x%X", id); return buf;
		}
	} else {
		// Key codes
		if (id >= kATInputCode_KeyA && id <= kATInputCode_KeyZ) {
			snprintf(buf, sizeof(buf), "Key %c", (char)id);
			return buf;
		}
		if (id >= kATInputCode_Key0 && id <= kATInputCode_Key9) {
			snprintf(buf, sizeof(buf), "Key %c", (char)id);
			return buf;
		}
		switch (id) {
			case kATInputCode_KeyUp:		return "Up";
			case kATInputCode_KeyDown:		return "Down";
			case kATInputCode_KeyLeft:		return "Left";
			case kATInputCode_KeyRight:		return "Right";
			case kATInputCode_KeySpace:		return "Space";
			case kATInputCode_KeyReturn:	return "Enter";
			case kATInputCode_KeyEscape:	return "Escape";
			case kATInputCode_KeyTab:		return "Tab";
			case kATInputCode_KeyBack:		return "Backspace";
			case kATInputCode_KeyInsert:	return "Insert";
			case kATInputCode_KeyDelete:	return "Delete";
			case kATInputCode_KeyHome:		return "Home";
			case kATInputCode_KeyEnd:		return "End";
			case kATInputCode_KeyPrior:		return "Page Up";
			case kATInputCode_KeyNext:		return "Page Down";
			case kATInputCode_KeyLShift:	return "L.Shift";
			case kATInputCode_KeyRShift:	return "R.Shift";
			case kATInputCode_KeyLControl:	return "L.Ctrl";
			case kATInputCode_KeyRControl:	return "R.Ctrl";
			case kATInputCode_KeyNumpadEnter: return "Numpad Enter";
			default:
				if (id >= kATInputCode_KeyNumpad0 && id <= kATInputCode_KeyNumpad9) {
					snprintf(buf, sizeof(buf), "Numpad %d", id - kATInputCode_KeyNumpad0);
					return buf;
				}
				if (id >= kATInputCode_KeyF1 && id <= kATInputCode_KeyF12) {
					snprintf(buf, sizeof(buf), "F%d", id - kATInputCode_KeyF1 + 1);
					return buf;
				}
				snprintf(buf, sizeof(buf), "Key 0x%X", id);
				return buf;
		}
	}
}

static const char *ATGetInputTriggerName(uint32 code) {
	static char buf[32];
	uint32 trigger = code & kATInputTrigger_Mask;
	switch (trigger) {
		case kATInputTrigger_Button0:		return "Button";
		case kATInputTrigger_Up:			return "Up";
		case kATInputTrigger_Down:			return "Down";
		case kATInputTrigger_Left:			return "Left";
		case kATInputTrigger_Right:			return "Right";
		case kATInputTrigger_ScrollUp:		return "Scroll Up";
		case kATInputTrigger_ScrollDown:	return "Scroll Down";
		case kATInputTrigger_Start:			return "Start";
		case kATInputTrigger_Select:		return "Select";
		case kATInputTrigger_Option:		return "Option";
		case kATInputTrigger_Turbo:			return "Turbo";
		case kATInputTrigger_ColdReset:		return "Cold Reset";
		case kATInputTrigger_WarmReset:		return "Warm Reset";
		case kATInputTrigger_Rewind:		return "Rewind";
		case kATInputTrigger_RewindMenu:	return "Rewind Menu";
		case kATInputTrigger_KeySpace:		return "Key Space";
		case kATInputTrigger_5200_Start:	return "5200 Start";
		case kATInputTrigger_5200_Pause:	return "5200 Pause";
		case kATInputTrigger_5200_Reset:	return "5200 Reset";
		case kATInputTrigger_Axis0:			return "Axis";
		case kATInputTrigger_Flag0:			return "Flag";
		default:
			if (trigger >= kATInputTrigger_5200_0 && trigger <= kATInputTrigger_5200_Pound) {
				const char *k5200Keys[] = {"0","1","2","3","4","5","6","7","8","9","*","#"};
				snprintf(buf, sizeof(buf), "5200 [%s]", k5200Keys[trigger - kATInputTrigger_5200_0]);
			} else if (trigger >= kATInputTrigger_UILeft && trigger <= kATInputTrigger_UIRightShift) {
				const char *kUINames[] = {"UI Left","UI Right","UI Up","UI Down","UI Accept","UI Reject","UI Menu","UI Option","UI Switch L","UI Switch R","UI L.Shift","UI R.Shift"};
				uint32 idx = trigger - kATInputTrigger_UILeft;
				if (idx < 12)
					return kUINames[idx];
				snprintf(buf, sizeof(buf), "UI 0x%X", trigger);
			} else {
				snprintf(buf, sizeof(buf), "Trigger 0x%X", trigger);
			}
			return buf;
	}
}

// ============= Helper: load file via dialog =============

static void MRUAdd(const wchar_t *path);  // forward declaration

static void TryLoadImage(const VDStringW& path) {
	if (path.empty())
		return;

	try {
		g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
		MRUAdd(path.c_str());
		VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(path.c_str())));
		char msg[256];
		snprintf(msg, sizeof(msg), "Loaded: %s", fname.c_str());
		ShowToast(msg);
	} catch (const std::exception& e) {
		char msg[512];
		snprintf(msg, sizeof(msg), "Load failed: %s", e.what());
		ShowToast(msg);
	} catch (...) {
		ShowToast("Failed to load image");
	}
}

static void TryMountDisk(int index, const VDStringW& path) {
	if (path.empty())
		return;

	try {
		ATDiskInterface& di = g_sim.GetDiskInterface(index);
		di.LoadDisk(path.c_str());

		// Enable the built-in disk drive emulator if no external drive
		// device is attached (matches ATSimulator::Load behavior)
		if (di.GetClientCount() < 2)
			g_sim.GetDiskDrive(index).SetEnabled(true);

		VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(path.c_str())));
		char msg[128];
		snprintf(msg, sizeof(msg), "D%d: %s", index + 1, fname.c_str());
		ShowToast(msg);
	} catch (const std::exception& e) {
		char msg[256];
		snprintf(msg, sizeof(msg), "D%d mount failed: %s", index + 1, e.what());
		ShowToast(msg);
	} catch (...) {
		char msg[64];
		snprintf(msg, sizeof(msg), "Failed to mount D%d", index + 1);
		ShowToast(msg);
	}
}

// ============= Paste text to Atari via POKEY =============
// Character-to-scancode table (ported from uikeyboard.cpp)

static struct CharToScanCodeInit {
	uint8 table[256];
	constexpr CharToScanCodeInit() : table{} {
		for (auto& v : table)
			v = 0xFF;

		// Lower-case letters
		table['a'] = 0x3F; table['b'] = 0x15; table['c'] = 0x12;
		table['d'] = 0x3A; table['e'] = 0x2A; table['f'] = 0x38;
		table['g'] = 0x3D; table['h'] = 0x39; table['i'] = 0x0D;
		table['j'] = 0x01; table['k'] = 0x05; table['l'] = 0x00;
		table['m'] = 0x25; table['n'] = 0x23; table['o'] = 0x08;
		table['p'] = 0x0A; table['q'] = 0x2F; table['r'] = 0x28;
		table['s'] = 0x3E; table['t'] = 0x2D; table['u'] = 0x0B;
		table['v'] = 0x10; table['w'] = 0x2E; table['x'] = 0x16;
		table['y'] = 0x2B; table['z'] = 0x17;

		// Upper-case letters (shift + scancode)
		table['A'] = 0x7F; table['B'] = 0x55; table['C'] = 0x52;
		table['D'] = 0x7A; table['E'] = 0x6A; table['F'] = 0x78;
		table['G'] = 0x7D; table['H'] = 0x79; table['I'] = 0x4D;
		table['J'] = 0x41; table['K'] = 0x45; table['L'] = 0x40;
		table['M'] = 0x65; table['N'] = 0x63; table['O'] = 0x48;
		table['P'] = 0x4A; table['Q'] = 0x6F; table['R'] = 0x68;
		table['S'] = 0x7E; table['T'] = 0x6D; table['U'] = 0x4B;
		table['V'] = 0x50; table['W'] = 0x6E; table['X'] = 0x56;
		table['Y'] = 0x6B; table['Z'] = 0x57;

		// Digits
		table['0'] = 0x32; table['1'] = 0x1F; table['2'] = 0x1E;
		table['3'] = 0x1A; table['4'] = 0x18; table['5'] = 0x1D;
		table['6'] = 0x1B; table['7'] = 0x33; table['8'] = 0x35;
		table['9'] = 0x30;

		// Punctuation and symbols
		table[' '] = 0x21;
		table['!'] = 0x5F; table['"'] = 0x5E; table['#'] = 0x5A;
		table['$'] = 0x58; table['%'] = 0x5D; table['&'] = 0x5B;
		table['\''] = 0x73; table['('] = 0x70; table[')'] = 0x72;
		table['*'] = 0x07; table['+'] = 0x06; table[','] = 0x20;
		table['-'] = 0x0E; table['.'] = 0x22; table['/'] = 0x26;
		table[':'] = 0x42; table[';'] = 0x02; table['<'] = 0x36;
		table['='] = 0x0F; table['>'] = 0x37; table['?'] = 0x66;
		table['@'] = 0x75; table['['] = 0x60; table['\\'] = 0x46;
		table[']'] = 0x62; table['^'] = 0x47; table['_'] = 0x4E;
		table['`'] = 0x27; table['|'] = 0x4F; table['~'] = 0x67;
	}
} s_charToScanCode;

static void PasteTextToEmulator() {
	char *text = SDL_GetClipboardText();
	if (!text || !*text) {
		SDL_free(text);
		return;
	}

	// Convert UTF-8 clipboard to wide chars
	VDStringW ws = VDTextU8ToW(VDStringSpanA(text));
	SDL_free(text);

	auto& pokey = g_sim.GetPokey();

	for (size_t i = 0; i < ws.size(); ++i) {
		wchar_t c = ws[i];

		// Skip null and zero-width characters
		if (!c || (c >= 0x200B && c <= 0x200F) || c == 0xFEFF)
			continue;

		// Normalize smart quotes and dashes
		switch (c) {
			case L'\u2010': case L'\u2011': case L'\u2012':
			case L'\u2013': case L'\u2014': case L'\u2015':
				c = L'-'; break;
			case L'\u2018': case L'\u2019':
				c = L'\''; break;
			case L'\u201C': case L'\u201D':
				c = L'"'; break;
			case L'\u2026':
				pokey.PushKey(s_charToScanCode.table['.'], false, true, false, true);
				pokey.PushKey(s_charToScanCode.table['.'], false, true, false, true);
				c = L'.';
				break;
		}

		// Handle newlines
		if (c == L'\r' || c == L'\n') {
			if (c == L'\r' && i + 1 < ws.size() && ws[i + 1] == L'\n')
				++i;
			pokey.PushKey(0x0C, false, true, false, true);
			continue;
		}

		// Handle tab
		if (c == L'\t') {
			pokey.PushKey(0x2C, false, true, false, true);
			continue;
		}

		// Map character to scancode
		if (c < 0x100) {
			uint8 sc = s_charToScanCode.table[(uint8)c];
			if (sc != 0xFF)
				pokey.PushKey(sc, false, true, false, true);
		}
	}

	ShowToast("Text pasted");
}

// ============= MRU (Recent Files) helpers =============
// Uses same registry format as Windows ATAddMRUListItem/ATGetMRUListItem

static void MRUAdd(const wchar_t *path) {
	VDRegistryAppKey key("MRU List", true);

	VDStringW order;
	key.getString("Order", order);

	// Check if already present — if so, promote it
	VDStringW existing;
	for (size_t i = 0; i < order.size(); ++i) {
		char keyname[2] = { (char)order[i], 0 };
		key.getString(keyname, existing);
		if (existing.comparei(path) == 0) {
			// Promote to front
			wchar_t c = order[i];
			order.erase(i, 1);
			order.insert(order.begin(), c);
			key.setString("Order", order.c_str());
			return;
		}
	}

	// Add new entry
	int slot = 0;
	if (order.size() >= 10) {
		wchar_t c = order.back();
		if (c >= L'A' && c < L'A' + 10)
			slot = c - L'A';
		order.resize(9);
	} else {
		slot = (int)order.size();
	}

	order.insert(order.begin(), L'A' + slot);
	char keyname[2] = { (char)('A' + slot), 0 };
	key.setString(keyname, path);
	key.setString("Order", order.c_str());
}

static VDStringW MRUGet(uint32 index) {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);

	VDStringW s;
	if (index < order.size()) {
		char keyname[2] = { (char)order[index], 0 };
		key.getString(keyname, s);
	}
	return s;
}

static uint32 MRUCount() {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	return (uint32)order.size();
}

static void MRUClear() {
	VDRegistryAppKey key("MRU List", true);
	key.removeValue("Order");
}

// ============= Main Menu Bar =============

static void DrawMenuBar() {
	IATDebugger *dbg = ATGetDebugger();

	if (!ImGui::BeginMainMenuBar())
		return;

	// --- System menu ---
	if (ImGui::BeginMenu("System")) {
		if (ImGui::BeginMenu("Hardware Mode")) {
			ATHardwareMode curHW = g_sim.GetHardwareMode();
			for (uint32 i = 0; i < kATHardwareModeCount; ++i) {
				if (ImGui::MenuItem(kHardwareModeNames[i], nullptr, curHW == (ATHardwareMode)i)) {
					ATUISwitchHardwareMode(nullptr, (ATHardwareMode)i, true);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Kernel")) {
			ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();
			uint64 curKernel = g_sim.GetKernelId();
			ATHardwareMode hwmode = g_sim.GetHardwareMode();

			// Autoselect option (id 0 = let simulator pick)
			if (ImGui::MenuItem("[Autoselect]", nullptr, curKernel == 0)) {
				ATUISwitchKernel(nullptr, 0);
			}

			// Built-in HLE kernels
			if (hwmode != kATHardwareMode_5200) {
				if (ImGui::MenuItem("Internal OS-B", nullptr, curKernel == kATFirmwareId_Kernel_LLE)) {
					ATUISwitchKernel(nullptr, kATFirmwareId_Kernel_LLE);
				}
				if (ImGui::MenuItem("Internal XL OS", nullptr, curKernel == kATFirmwareId_Kernel_LLEXL)) {
					ATUISwitchKernel(nullptr, kATFirmwareId_Kernel_LLEXL);
				}
			} else {
				if (ImGui::MenuItem("Internal 5200 OS", nullptr, curKernel == kATFirmwareId_5200_LLE)) {
					ATUISwitchKernel(nullptr, kATFirmwareId_5200_LLE);
				}
			}

			// User-added firmware ROMs that match current hardware mode
			if (fwMgr) {
				vdvector<ATFirmwareInfo> fwList;
				fwMgr->GetFirmwareList(fwList);

				// Filter to matching firmware and sort alphabetically by name
				vdvector<const ATFirmwareInfo *> matchList;
				for (const ATFirmwareInfo& fw : fwList) {
					if (!fw.mbVisible)
						continue;

					bool match = false;
					switch (fw.mType) {
						case kATFirmwareType_Kernel800_OSA:
						case kATFirmwareType_Kernel800_OSB:
							match = (hwmode != kATHardwareMode_5200);
							break;
						case kATFirmwareType_KernelXL:
						case kATFirmwareType_KernelXEGS:
						case kATFirmwareType_Kernel1200XL:
							match = kATHardwareModeTraits[hwmode].mbRunsXLOS;
							break;
						case kATFirmwareType_Kernel5200:
							match = (hwmode == kATHardwareMode_5200);
							break;
						default:
							break;
					}

					if (match)
						matchList.push_back(&fw);
				}

				std::sort(matchList.begin(), matchList.end(),
					[](const ATFirmwareInfo *a, const ATFirmwareInfo *b) {
						return a->mName.comparei(b->mName) < 0;
					});

				if (!matchList.empty())
					ImGui::Separator();

				for (const ATFirmwareInfo *fw : matchList) {
					VDStringA u8name = VDTextWToU8(fw->mName);
					if (ImGui::MenuItem(u8name.c_str(), nullptr, fw->mId == curKernel)) {
						ATUISwitchKernel(nullptr, fw->mId);
					}
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Memory")) {
			ATMemoryMode curMem = g_sim.GetMemoryMode();
			for (const auto& entry : kSortedMemoryModes) {
				if (ImGui::MenuItem(entry.label, nullptr, curMem == entry.mode)) {
					ATUISwitchMemoryMode(nullptr, entry.mode);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Video")) {
			ATVideoStandard curVS = g_sim.GetVideoStandard();
			for (uint32 i = 0; i < kATVideoStandardCount; ++i) {
				if (ImGui::MenuItem(kVideoStandardNames[i], nullptr, curVS == (ATVideoStandard)i)) {
					// Block video standard change in 5200 mode (must be NTSC)
					if (g_sim.GetHardwareMode() == kATHardwareMode_5200 && (ATVideoStandard)i != kATVideoStandard_NTSC)
						continue;
					g_sim.SetVideoStandard((ATVideoStandard)i);
					ATUIUpdateSpeedTiming();
					g_sim.ColdReset();
				}
			}
			ImGui::EndMenu();
		}

		bool basicEnabled = g_sim.IsBASICEnabled();
		if (ImGui::MenuItem("BASIC", nullptr, &basicEnabled)) {
			g_sim.SetBASICEnabled(basicEnabled);
			g_sim.ColdReset();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("System Settings...")) {
			s_showSystemConfig = true;
		}
		if (ImGui::MenuItem("Cartridge...")) {
			s_showCartridgeBrowser = true;
		}
		if (ImGui::MenuItem("Firmware Manager...")) {
			s_showFirmwareManager = true;
			s_fwListDirty = true;
		}
		if (ImGui::MenuItem("Audio Options...")) {
			s_showAudioOptions = true;
		}
		if (ImGui::MenuItem("Video Settings...")) {
			s_showVideoConfig = true;
		}
		if (ImGui::MenuItem("Keyboard...")) {
			s_showKeyboardConfig = true;
		}
		if (ImGui::MenuItem("Input Setup...")) {
			s_showInputSetup = true;
		}
		if (ImGui::MenuItem("Cycle Quick Maps", "Shift+F1")) {
			ATInputManager *inputMgr = g_sim.GetInputManager();
			if (inputMgr) {
				ATInputMap *pMap = inputMgr->CycleQuickMaps();
				if (pMap) {
					VDStringA name = VDTextWToU8(VDStringW(pMap->GetName()));
					char msg[128];
					snprintf(msg, sizeof(msg), "Quick map: %s", name.c_str());
					ShowToast(msg);
				} else {
					ShowToast("Quick maps disabled");
				}
			}
		}
		if (ImGui::MenuItem("Devices...")) {
			s_showDeviceManager = true;
		}
		if (ImGui::MenuItem("Boot & Acceleration...")) {
			s_showBootOptions = true;
		}
		if (ImGui::MenuItem("CPU & Memory...")) {
			s_showCPUOptions = true;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Cold Reset", "Shift+F6")) {
			g_sim.ColdReset();
			g_sim.Resume();
		}
		if (ImGui::MenuItem("Warm Reset", "F6")) {
			g_sim.WarmReset();
			g_sim.Resume();
		}

		if (ImGui::BeginMenu("Console Switches")) {
			ATGTIAEmulator& gtia = g_sim.GetGTIA();
			uint8 consw = gtia.ReadConsoleSwitchInputs();

			// Console switch inputs are active-low: bit=0 means pressed
			bool optionDown = !(consw & 0x04);
			bool selectDown = !(consw & 0x02);
			bool startDown  = !(consw & 0x01);

			if (ImGui::MenuItem("Option", nullptr, optionDown))
				gtia.SetConsoleSwitch(0x04, !optionDown);
			if (ImGui::MenuItem("Select", nullptr, selectDown))
				gtia.SetConsoleSwitch(0x02, !selectDown);
			if (ImGui::MenuItem("Start", nullptr, startDown))
				gtia.SetConsoleSwitch(0x01, !startDown);

			ImGui::Separator();
			if (ImGui::MenuItem("Release All")) {
				gtia.SetConsoleSwitch(0x07, false);
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		{
			IATAutoSaveManager& asMgr = g_sim.GetAutoSaveManager();
			bool rewindEnabled = asMgr.GetRewindEnabled();
			if (ImGui::MenuItem("Enable Rewind", nullptr, &rewindEnabled))
				asMgr.SetRewindEnabled(rewindEnabled);
			if (ImGui::MenuItem("Rewind", nullptr, false, rewindEnabled))
				asMgr.Rewind();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Save Settings", "Ctrl+S")) {
			extern void ATLinuxSaveSettings();
			ATLinuxSaveSettings();
			ShowToast("Settings saved");
		}

		if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
			ATImGuiRequestQuit();
		}

		ImGui::EndMenu();
	}

	// --- Profiles menu ---
	if (ImGui::BeginMenu("Profiles")) {
		vdfastvector<uint32> profileIds;
		ATSettingsProfileEnum(profileIds);
		uint32 currentId = ATSettingsGetCurrentProfileId();

		for (uint32 id : profileIds) {
			if (!ATSettingsProfileGetVisible(id))
				continue;
			VDStringW name = ATSettingsProfileGetName(id);
			VDStringA u8name = VDTextWToU8(name);
			if (ImGui::MenuItem(u8name.c_str(), nullptr, id == currentId))
				ATSettingsSwitchProfile(id);
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Profile Manager..."))
			s_showProfileManager = true;

		ImGui::EndMenu();
	}

	// --- File menu ---
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("Open Image...", "Ctrl+O")) {
			VDStringW path = ATLinuxOpenFileDialog("Open Image", kDiskFilters);
			if (!path.empty()) {
				TryLoadImage(path);
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kOpenImage;
			}
		}

		if (ImGui::MenuItem("Boot Image...", "Ctrl+Shift+O")) {
			VDStringW path = ATLinuxOpenFileDialog("Boot Image", kDiskFilters);
			if (!path.empty()) {
				try {
					g_sim.UnloadAll();
					g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
					g_sim.ColdReset();
					g_sim.Resume();
					MRUAdd(path.c_str());
					VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(path.c_str())));
					char msg[256];
					snprintf(msg, sizeof(msg), "Booted: %s", fname.c_str());
					ShowToast(msg);
				} catch (...) {
					ShowToast("Failed to boot image");
				}
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kBootImage;
			}
		}

		// Recent files submenu
		uint32 mruCount = MRUCount();
		if (ImGui::BeginMenu("Recent Files", mruCount > 0)) {
			for (uint32 i = 0; i < mruCount && i < 10; ++i) {
				VDStringW wpath = MRUGet(i);
				if (wpath.empty())
					continue;

				VDStringA u8path = VDTextWToU8(VDStringW(VDFileSplitPath(wpath.c_str())));
				char label[256];
				snprintf(label, sizeof(label), "%u. %s", i + 1, u8path.c_str());

				if (ImGui::MenuItem(label)) {
					TryLoadImage(wpath);
				}

				// Tooltip with full path
				if (ImGui::IsItemHovered()) {
					VDStringA fullU8 = VDTextWToU8(wpath);
					ImGui::SetTooltip("%s", fullU8.c_str());
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Clear Recent Files")) {
				MRUClear();
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Quick Save State", "F7")) {
			try {
				s_pQuickState.clear();
				vdrefptr<IATSerializable> snapInfo;
				g_sim.CreateSnapshot(~s_pQuickState, ~snapInfo);
				ShowToast("State saved");
			} catch (...) {
				ShowToast("Save state failed");
			}
		}
		if (ImGui::MenuItem("Quick Load State", "F8", false, s_pQuickState != nullptr)) {
			try {
				ATStateLoadContext ctx {};
				g_sim.ApplySnapshot(*s_pQuickState, &ctx);
				ShowToast("State loaded");
			} catch (...) {
				ShowToast("Load state failed");
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Save State As...")) {
			VDStringW path = ATLinuxSaveFileDialog("Save State", kSaveStateFilters);
			if (!path.empty()) {
				try {
					g_sim.SaveState(path.c_str());
					ShowToast("State saved to file");
				} catch (...) {
					ShowToast("Save state failed");
				}
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kSaveState;
			}
		}
		if (ImGui::MenuItem("Load State...")) {
			VDStringW path = ATLinuxOpenFileDialog("Load State", kSaveStateFilters);
			if (!path.empty()) {
				try {
					g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
				} catch (const std::exception& e) {
					char msg[512];
					snprintf(msg, sizeof(msg), "Load state failed: %s", e.what());
					ShowToast(msg);
				} catch (...) {
					ShowToast("Load state failed");
				}
			} else if (ATLinuxFileDialogIsFallbackOpen()) {
				s_pendingDialog = PendingDialog::kLoadState;
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Cassette..."))
			s_showCassetteControl = true;

		ImGui::Separator();

		if (ImGui::BeginMenu("Disk Drives")) {
			for (int i = 0; i < 15; ++i) {
				char label[32];
				ATDiskInterface& di = g_sim.GetDiskInterface(i);

				// Always show D1-D8; only show D9-D15 if a disk is loaded
				if (i >= 8 && !di.IsDiskLoaded())
					continue;

				if (ImGui::BeginMenu(di.IsDiskLoaded()
					? (snprintf(label, sizeof(label), "D%d: %ls", i + 1,
						VDFileSplitPath(di.GetPath())), label)
					: (snprintf(label, sizeof(label), "D%d: [empty]", i + 1), label)))
				{
					snprintf(label, sizeof(label), "Mount D%d...", i + 1);
					if (ImGui::MenuItem(label)) {
						VDStringW path = ATLinuxOpenFileDialog("Mount Disk", kDiskOnlyFilters);
						if (!path.empty()) {
							TryMountDisk(i, path);
						} else if (ATLinuxFileDialogIsFallbackOpen()) {
							s_pendingDialog = PendingDialog::kMountDisk;
							s_pendingMountDiskSlot = i;
						}
					}

					snprintf(label, sizeof(label), "New Disk in D%d...", i + 1);
					if (ImGui::MenuItem(label)) {
						s_showNewDisk = true;
						s_newDiskSlot = i;
					}

					if (di.IsDiskLoaded()) {
						if (di.IsDirty()) {
							snprintf(label, sizeof(label), "Save D%d", i + 1);
							if (ImGui::MenuItem(label)) {
								try {
									di.SaveDisk();
									char msg[64];
									snprintf(msg, sizeof(msg), "D%d saved", i + 1);
									ShowToast(msg);
								} catch (...) {
									ShowToast("Disk save failed");
								}
							}
						}

						snprintf(label, sizeof(label), "Save D%d As...", i + 1);
						if (ImGui::MenuItem(label)) {
							VDStringW path = ATLinuxSaveFileDialog("Save Disk As",
								"ATR Disk Images|*.atr|XFD Disk Images|*.xfd|All Files|*");
							if (!path.empty()) {
								try {
									ATDiskImageFormat fmt = kATDiskImageFormat_ATR;
									const wchar_t *ext = VDFileSplitExt(path.c_str());
									if (ext && !vdwcsicmp(ext, L".xfd"))
										fmt = kATDiskImageFormat_XFD;
									di.SaveDiskAs(path.c_str(), fmt);
									ShowToast("Disk saved");
								} catch (...) {
									ShowToast("Disk save failed");
								}
							}
						}

						ImGui::Separator();

						ATMediaWriteMode wm = di.GetWriteMode();
						bool readOnly = (wm == kATMediaWriteMode_RO);
						if (ImGui::MenuItem("Read Only", nullptr, readOnly)) {
							di.SetWriteMode(readOnly ? kATMediaWriteMode_VRW : kATMediaWriteMode_RO);
						}

						if (ImGui::MenuItem("Explore...")) {
							s_showDiskExplorer = true;
							s_diskExplorerSlot = i;
							// Will open FS on first draw
							if (s_diskFS) {
								try { s_diskFS->Flush(); } catch (...) {}
								s_diskFS.reset();
							}
							s_diskEntries.clear();
							s_diskDirStack.clear();
							s_diskCurDir = ATDiskFSKey::None;
							s_diskSelectedIdx = -1;
							s_diskSelection.clear();
							s_diskShiftAnchor = -1;
							s_diskReadOnly = readOnly;
							s_diskFSInfoStr.clear();
							try {
								IATDiskImage *img = di.GetDiskImage();
								if (img) {
									s_diskFS = ATDiskMountImage(img, readOnly);

									ATDiskFSValidationReport report;
									s_diskFS->Validate(report);
									if (report.IsSerious()) {
										s_diskReadOnly = true;
										s_diskFS->SetReadOnly(true);
										ShowToast("Filesystem errors detected - mounted read-only");
									}

									ATDiskFSInfo fsInfo;
									s_diskFS->GetInfo(fsInfo);
									char infoStr[128];
									snprintf(infoStr, sizeof(infoStr), "%s  |  %u free blocks (%u bytes/block)",
										fsInfo.mFSType.c_str(), fsInfo.mFreeBlocks, fsInfo.mBlockSize);
									s_diskFSInfoStr = infoStr;
								}
							} catch (const std::exception& e) {
								char msg[256];
								snprintf(msg, sizeof(msg), "Mount FS failed: %s", e.what());
								ShowToast(msg);
								s_showDiskExplorer = false;
							}
						}

						ImGui::Separator();

						snprintf(label, sizeof(label), "Unmount D%d", i + 1);
						if (ImGui::MenuItem(label)) {
							di.UnloadDisk();
						}
					}

					ImGui::EndMenu();
				}
			}

			ImGui::Separator();

			// Find highest active drive for rotation
			int activeDrives = 0;
			for (int i = 14; i >= 0; --i) {
				if (g_sim.GetDiskInterface(i).IsDiskLoaded()) {
					activeDrives = i + 1;
					break;
				}
			}

			if (ImGui::MenuItem("Rotate Down", nullptr, false, activeDrives >= 2)) {
				g_sim.RotateDrives(activeDrives, +1);
				const wchar_t *label = g_sim.GetDiskInterface(0).GetMountedImageLabel().c_str();
				VDStringA u8 = VDTextWToU8(VDStringW(label));
				char msg[128];
				snprintf(msg, sizeof(msg), "D1: %s", u8.c_str());
				ShowToast(msg);
			}
			if (ImGui::MenuItem("Rotate Up", nullptr, false, activeDrives >= 2)) {
				g_sim.RotateDrives(activeDrives, -1);
				const wchar_t *label = g_sim.GetDiskInterface(0).GetMountedImageLabel().c_str();
				VDStringA u8 = VDTextWToU8(VDStringW(label));
				char msg[128];
				snprintf(msg, sizeof(msg), "D1: %s", u8.c_str());
				ShowToast(msg);
			}

			ImGui::Separator();

			bool hasDirty = false;
			for (int i = 0; i < 15 && !hasDirty; ++i) {
				ATDiskInterface& di = g_sim.GetDiskInterface(i);
				if (di.IsDiskLoaded() && di.IsDirty())
					hasDirty = true;
			}

			if (ImGui::MenuItem("Save All Modified", nullptr, false, hasDirty)) {
				int saved = 0;
				for (int i = 0; i < 15; ++i) {
					ATDiskInterface& di = g_sim.GetDiskInterface(i);
					if (di.IsDiskLoaded() && di.IsDirty()) {
						try { di.SaveDisk(); ++saved; } catch (...) {}
					}
				}
				char msg[64];
				snprintf(msg, sizeof(msg), "Saved %d disk(s)", saved);
				ShowToast(msg);
			}

			if (ImGui::MenuItem("Unmount All", nullptr, false, activeDrives >= 1)) {
				for (int i = 0; i < 15; ++i)
					g_sim.GetDiskInterface(i).UnloadDisk();
				ShowToast("All disks unmounted");
			}

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Disk Explorer...")) {
			VDStringW diskPath = ATLinuxOpenFileDialog("Open Disk Image",
				"Disk Images|*.atr;*.xfd;*.atx;*.dcm;*.pro|All Files|*");
			if (!diskPath.empty()) {
				try {
					if (s_diskFS) {
						try { s_diskFS->Flush(); } catch (...) {}
						s_diskFS.reset();
					}
					s_diskStandaloneImage.clear();

					ATLoadDiskImage(diskPath.c_str(), ~s_diskStandaloneImage);

					s_diskFS = ATDiskMountImage(s_diskStandaloneImage, false);

					ATDiskFSValidationReport report;
					s_diskFS->Validate(report);
					s_diskReadOnly = false;
					if (report.IsSerious()) {
						s_diskReadOnly = true;
						s_diskFS->SetReadOnly(true);
						ShowToast("Filesystem errors detected - mounted read-only");
					}

					ATDiskFSInfo fsInfo;
					s_diskFS->GetInfo(fsInfo);
					char infoStr[128];
					snprintf(infoStr, sizeof(infoStr), "%s  |  %u free blocks (%u bytes/block)",
						fsInfo.mFSType.c_str(), fsInfo.mFreeBlocks, fsInfo.mBlockSize);
					s_diskFSInfoStr = infoStr;

					const wchar_t *fname = VDFileSplitPath(diskPath.c_str());
					VDStringA titleU8 = VDTextWToU8(VDStringW(fname));
					s_diskExplorerTitle.assign(titleU8.c_str(), titleU8.size());

					s_diskExplorerSlot = -1;
					s_diskEntries.clear();
					s_diskDirStack.clear();
					s_diskCurDir = ATDiskFSKey::None;
					s_diskSelectedIdx = -1;
					s_diskSelection.clear();
					s_diskShiftAnchor = -1;
					s_showDiskExplorer = true;
				} catch (const std::exception& e) {
					s_diskFS.reset();
					s_diskStandaloneImage.clear();
					char msg[256];
					snprintf(msg, sizeof(msg), "Open disk failed: %s", e.what());
					ShowToast(msg);
				}
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Save Screenshot...", "F9")) {
			VDPixmapBuffer pxbuf;
			VDPixmap px;
			if (g_sim.GetGTIA().GetLastFrameBuffer(pxbuf, px)) {
				VDStringW path = ATLinuxSaveFileDialog("Save Screenshot",
					"PNG Images|*.png|All Files|*");
				if (!path.empty()) {
					try {
						// Ensure .png extension
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".png";
						ATSaveFrame(px, path.c_str());
					} catch (const std::exception& e) {
						char msg[512];
						snprintf(msg, sizeof(msg), "Screenshot failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						ShowToast("Failed to save screenshot");
					}
				}
			}
		}

		if (ImGui::BeginMenu("Record")) {
			bool recording = IsRecordingActive();
			bool isPAL = g_sim.GetVideoStandard() == kATVideoStandard_PAL;
			bool isStereo = g_sim.IsDualPokeysEnabled();

			if (ImGui::MenuItem("Record Video (AVI)...", nullptr, false, !recording)) {
				s_showVideoRecordDialog = true;
			}

			if (s_pVideoWriter) {
				if (s_pVideoWriter->IsPaused()) {
					if (ImGui::MenuItem("Resume Recording"))
						s_pVideoWriter->Resume();
				} else {
					if (ImGui::MenuItem("Pause Recording"))
						s_pVideoWriter->Pause();
				}
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Record Audio (WAV)...", nullptr, false, !recording)) {
				VDStringW path = ATLinuxSaveFileDialog("Record Audio", kWavFilters);
				if (!path.empty()) {
					try {
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".wav";
						s_pAudioWriter = new ATAudioWriter(path.c_str(), false, isStereo, isPAL, nullptr);
						g_sim.GetAudioOutput()->SetAudioTap(s_pAudioWriter);
					} catch (const std::exception& e) {
						s_pAudioWriter.reset();
						char msg[512];
						snprintf(msg, sizeof(msg), "Audio recording failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						s_pAudioWriter.reset();
						ShowToast("Failed to start audio recording");
					}
				}
			}

			if (ImGui::MenuItem("Record Raw Audio (PCM)...", nullptr, false, !recording)) {
				VDStringW path = ATLinuxSaveFileDialog("Record Raw Audio",
					"Raw PCM|*.pcm|All Files|*");
				if (!path.empty()) {
					try {
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".pcm";
						s_pAudioWriter = new ATAudioWriter(path.c_str(), true, isStereo, isPAL, nullptr);
						g_sim.GetAudioOutput()->SetAudioTap(s_pAudioWriter);
					} catch (const std::exception& e) {
						s_pAudioWriter.reset();
						char msg[512];
						snprintf(msg, sizeof(msg), "Raw audio recording failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						s_pAudioWriter.reset();
						ShowToast("Failed to start raw audio recording");
					}
				}
			}

			if (ImGui::MenuItem("Record SAP Type-R...", nullptr, false, !recording)) {
				VDStringW path = ATLinuxSaveFileDialog("Record SAP", kSapFilters);
				if (!path.empty()) {
					try {
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".sap";
						s_pSapWriter = ATCreateSAPWriter();
						s_pSapWriter->Init(
							g_sim.GetEventManager(),
							&g_sim.GetPokey(),
							nullptr,
							path.c_str(),
							isPAL);
					} catch (const std::exception& e) {
						s_pSapWriter.reset();
						char msg[512];
						snprintf(msg, sizeof(msg), "SAP recording failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						s_pSapWriter.reset();
						ShowToast("Failed to start SAP recording");
					}
				}
			}

			if (ImGui::MenuItem("Record VGM...", nullptr, false, !recording)) {
				VDStringW path = ATLinuxSaveFileDialog("Record VGM", kVgmFilters);
				if (!path.empty()) {
					try {
						if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
							path += L".vgm";
						s_pVgmWriter = ATCreateVgmWriter();
						s_pVgmWriter->Init(path.c_str(), g_sim);
					} catch (const std::exception& e) {
						s_pVgmWriter.clear();
						char msg[512];
						snprintf(msg, sizeof(msg), "VGM recording failed: %s", e.what());
						ShowToast(msg);
					} catch (...) {
						s_pVgmWriter.clear();
						ShowToast("Failed to start VGM recording");
					}
				}
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Stop Recording", nullptr, false, recording)) {
				StopAllRecording();
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Exit")) {
			// Signal main loop to quit
			SDL_Event quit;
			quit.type = SDL_QUIT;
			SDL_PushEvent(&quit);
		}

		ImGui::EndMenu();
	}

	// --- Edit menu ---
	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Paste Text", "Ctrl+V"))
			PasteTextToEmulator();

		ImGui::EndMenu();
	}

	// --- View menu ---
	if (ImGui::BeginMenu("View")) {
		bool showFPS = ATUIGetShowFPS();
		if (ImGui::MenuItem("Show FPS", nullptr, &showFPS))
			ATUISetShowFPS(showFPS);

		if (ImGui::BeginMenu("Display Filter")) {
			ATDisplayFilterMode curFilter = ATUIGetDisplayFilterMode();
			for (int i = 0; i < 5; ++i) {
				if (ImGui::MenuItem(kDisplayFilterNames[i], nullptr, curFilter == (ATDisplayFilterMode)i)) {
					ATUISetDisplayFilterMode((ATDisplayFilterMode)i);

					// Apply to GL backend
					ATDisplaySDL2 *disp = ATGetLinuxDisplay();
					if (disp) {
						IVDVideoDisplay::FilterMode fm =
							(i == kATDisplayFilterMode_Point)
								? IVDVideoDisplay::kFilterPoint
								: IVDVideoDisplay::kFilterBilinear;
						disp->SetFilterMode(fm);
					}
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Stretch Mode")) {
			ATDisplayStretchMode curStretch = ATUIGetDisplayStretchMode();
			for (int i = 0; i < (int)kATDisplayStretchModeCount; ++i) {
				if (ImGui::MenuItem(kStretchModeNames[i], nullptr, curStretch == (ATDisplayStretchMode)i))
					ATUISetDisplayStretchMode((ATDisplayStretchMode)i);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window Size")) {
			// Base resolution depends on video standard
			bool isPAL = g_sim.GetVideoStandard() == kATVideoStandard_PAL
				|| g_sim.GetVideoStandard() == kATVideoStandard_SECAM;
			int baseW = 456;
			int baseH = isPAL ? 312 : 262;

			for (int scale = 1; scale <= 4; ++scale) {
				int w = baseW * scale;
				int h = baseH * scale;
				char label[48];
				snprintf(label, sizeof(label), "%dx (%dx%d)", scale, w, h);
				if (ImGui::MenuItem(label))
					ATSetWindowSize(w, h);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Enhanced Text")) {
			ATUIEnhancedTextMode curMode = ATUIGetEnhancedTextMode();
			if (ImGui::MenuItem("None", nullptr, curMode == kATUIEnhancedTextMode_None))
				ATUISetEnhancedTextMode(kATUIEnhancedTextMode_None);
			if (ImGui::MenuItem("Hardware", nullptr, curMode == kATUIEnhancedTextMode_Hardware))
				ATUISetEnhancedTextMode(kATUIEnhancedTextMode_Hardware);
			if (ImGui::MenuItem("Software (CIO)", nullptr, curMode == kATUIEnhancedTextMode_Software))
				ATUISetEnhancedTextMode(kATUIEnhancedTextMode_Software);
			ImGui::EndMenu();
		}

		ImGui::Separator();

		{
			bool monEnabled = g_sim.IsAudioMonitorEnabled();
			if (ImGui::MenuItem("Audio Monitor", nullptr, &monEnabled))
				g_sim.SetAudioMonitorEnabled(monEnabled);
		}
		{
			bool scopeEnabled = g_sim.IsAudioScopeEnabled();
			if (ImGui::MenuItem("Audio Scope", nullptr, &scopeEnabled))
				g_sim.SetAudioScopeEnabled(scopeEnabled);
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Video Settings..."))
			s_showVideoConfig = true;

		bool fullscreen = ATUIGetFullscreen();
		if (ImGui::MenuItem("Fullscreen", "F11", &fullscreen))
			ATSetFullscreen(fullscreen);

		{
			bool statusBar = ATUIGetShowStatusBar();
			if (ImGui::MenuItem("Status Bar", nullptr, &statusBar))
				ATUISetShowStatusBar(statusBar);
		}

		{
			bool autoHide = ATUIGetPointerAutoHide();
			if (ImGui::MenuItem("Auto-Hide Cursor", nullptr, &autoHide))
				ATUISetPointerAutoHide(autoHide);
		}

		{
			bool confine = ATUIGetConstrainMouseFullScreen();
			if (ImGui::MenuItem("Confine Mouse in Fullscreen", nullptr, &confine))
				ATUISetConstrainMouseFullScreen(confine);
		}

		ImGui::EndMenu();
	}

	// --- Speed menu ---
	if (ImGui::BeginMenu("Speed")) {
		bool paused = g_sim.IsPaused();
		if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Pause")) {
			if (paused)
				g_sim.Resume();
			else
				g_sim.Pause();
		}

		bool turbo = ATUIGetTurbo();
		if (ImGui::MenuItem("Turbo", nullptr, &turbo))
			ATUISetTurbo(turbo);

		{
			bool slowmo = ATUIGetSlowMotion();
			if (ImGui::MenuItem("Slow Motion", nullptr, &slowmo))
				ATUISetSlowMotion(slowmo);
		}

		ImGui::Separator();

		float speed = ATUIGetSpeedModifier();

		if (ImGui::MenuItem("50%",  nullptr, speed == 0.5f))  ATUISetSpeedModifier(0.5f);
		if (ImGui::MenuItem("100%", nullptr, speed == 1.0f))  ATUISetSpeedModifier(1.0f);
		if (ImGui::MenuItem("200%", nullptr, speed == 2.0f))  ATUISetSpeedModifier(2.0f);
		if (ImGui::MenuItem("400%", nullptr, speed == 4.0f))  ATUISetSpeedModifier(4.0f);

		ImGui::Separator();

		int speedPct = (int)(speed * 100.0f + 0.5f);
		ImGui::SetNextItemWidth(120);
		if (ImGui::SliderInt("##speed", &speedPct, 10, 800, "%d%%")) {
			ATUISetSpeedModifier((float)speedPct / 100.0f);
		}

		ImGui::Separator();

		{
			IATAudioOutput *audioOut = g_sim.GetAudioOutput();
			if (audioOut) {
				bool mute = audioOut->GetMute();
				if (ImGui::MenuItem("Mute Audio", "F4", &mute)) {
					audioOut->SetMute(mute);
				}
			}
		}

		ImGui::Separator();

		{
			bool pauseInactive = ATUIGetPauseWhenInactive();
			if (ImGui::MenuItem("Pause When Inactive", nullptr, &pauseInactive))
				ATUISetPauseWhenInactive(pauseInactive);
		}

		ImGui::EndMenu();
	}

	// --- Debug menu ---
	if (ImGui::BeginMenu("Debug")) {
		if (dbg) {
			bool running = dbg->IsRunning();

			if (running) {
				if (ImGui::MenuItem("Break", "F5"))
					dbg->Break();
			} else {
				if (ImGui::MenuItem("Run", "F5"))
					dbg->Run(kATDebugSrcMode_Disasm);
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Step Into", "F11", false, !running))
				dbg->StepInto(kATDebugSrcMode_Disasm);
			if (ImGui::MenuItem("Step Over", "F10", false, !running))
				dbg->StepOver(kATDebugSrcMode_Disasm);
			if (ImGui::MenuItem("Step Out", "Shift+F11", false, !running))
				dbg->StepOut(kATDebugSrcMode_Disasm);
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Registers", nullptr, &ATImGuiDebuggerShowRegisters());
			ImGui::MenuItem("Disassembly", nullptr, &ATImGuiDebuggerShowDisassembly());
			ImGui::MenuItem("Memory", nullptr, &ATImGuiDebuggerShowMemory());
			ImGui::MenuItem("Console", nullptr, &ATImGuiDebuggerShowConsole());
			ImGui::MenuItem("Breakpoints", nullptr, &ATImGuiDebuggerShowBreakpoints());
			ImGui::MenuItem("Watch", nullptr, &ATImGuiDebuggerShowWatch());
			ImGui::MenuItem("Call Stack", nullptr, &ATImGuiDebuggerShowCallStack());
			ImGui::MenuItem("History", nullptr, &ATImGuiDebuggerShowHistory());
			ImGui::Separator();
			ImGui::MenuItem("Source Code", nullptr, &ATImGuiDebuggerShowSourceCode());
			ImGui::MenuItem("Printer Output", nullptr, &ATImGuiDebuggerShowPrinterOutput());
			ImGui::MenuItem("Profiler", nullptr, &ATImGuiDebuggerShowProfiler());
			ImGui::Separator();
			ImGui::MenuItem("Trace Viewer", nullptr, &ATImGuiDebuggerShowTrace());
			ImGui::MenuItem("Debug Display", nullptr, &ATImGuiDebuggerShowDebugDisplay());
			ImGui::MenuItem("Performance", nullptr, &ATImGuiDebuggerShowPerformance());
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Load Symbols...", nullptr, false, dbg != nullptr)) {
			VDStringW path = ATLinuxOpenFileDialog("Load Symbols",
				"Symbol Files|*.lbl;*.lst;*.lab;*.sym;*.mads"
				"|All Files|*");
			if (!path.empty() && dbg) {
				dbg->LoadSymbols(path.c_str(), true);
				ShowToast("Symbols loaded");
			}
		}

		if (ImGui::MenuItem("Unload All Symbols", nullptr, false, dbg != nullptr)) {
			if (dbg)
				dbg->QueueCommand(".unloadsym", false);
		}

		ImGui::EndMenu();
	}

	// --- Tools menu ---
	if (ImGui::BeginMenu("Tools")) {
		if (ImGui::MenuItem("Cheat Engine...", nullptr, s_showCheater))
			s_showCheater = !s_showCheater;
		ImGui::EndMenu();
	}

	// --- Help menu ---
	if (ImGui::BeginMenu("Help")) {
		if (ImGui::MenuItem("Keyboard Shortcuts..."))
			s_showShortcuts = true;

		ImGui::Separator();

		if (ImGui::MenuItem("Open Config Directory")) {
			vdvector<VDStringW> fwPaths;
			ATGetFirmwareSearchPaths(fwPaths);
			if (!fwPaths.empty()) {
				// First firmware path parent is the config dir
				VDStringW configDir = VDFileSplitPathLeft(fwPaths[0]);
				VDStringW dummy = VDMakePath(configDir.c_str(), L".");
				ATShowFileInSystemExplorer(dummy.c_str());
			}
		}
		if (ImGui::MenuItem("Open Firmware Directory")) {
			vdvector<VDStringW> fwPaths;
			ATGetFirmwareSearchPaths(fwPaths);
			if (!fwPaths.empty()) {
				VDStringW dummy = VDMakePath(fwPaths[0].c_str(), L".");
				ATShowFileInSystemExplorer(dummy.c_str());
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("About Altirra..."))
			s_showAbout = true;

		ImGui::EndMenu();
	}

	// --- Right-aligned status ---
	{
		ATHardwareMode hw = g_sim.GetHardwareMode();
		ATVideoStandard vs = g_sim.GetVideoStandard();

		const char *hwName = (hw < kATHardwareModeCount) ? kHardwareModeNames[hw] : "?";
		const char *vsName = (vs < kATVideoStandardCount) ? kVideoStandardNames[vs] : "?";

		bool running = dbg && dbg->IsRunning();
		const char *status = running ? "RUNNING" : "STOPPED";
		ImVec4 statusColor = running
			? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
			: ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

		char rightText[64];
		snprintf(rightText, sizeof(rightText), "%s %s   %s", hwName, vsName, status);

		float textW = ImGui::CalcTextSize(rightText).x;
		float barW = ImGui::GetWindowWidth();

		if (barW - textW > 200.0f) {
			ImGui::SameLine(barW - textW - 20.0f);

			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s %s", hwName, vsName);
			ImGui::SameLine(0, 12);
			ImGui::TextColored(statusColor, "%s", status);
		}
	}

	ImGui::EndMainMenuBar();
}

// ============= System Configuration Window =============

// Pending state for System Settings dialog — edited by combos,
// applied only when "Apply & Cold Reset" is pressed.
static int s_pendingHwMode = -1;
static int s_pendingMemMode = -1;
static int s_pendingVideoStd = -1;
static bool s_pendingBasic = false;

static void DrawSystemConfig() {
	if (!s_showSystemConfig)
		return;

	ImGui::SetNextWindowSize(ImVec2(350, 280), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("System Settings", &s_showSystemConfig)) {
		ImGui::End();
		return;
	}

	// Sync pending state from simulator on first open
	if (s_pendingHwMode < 0) {
		s_pendingHwMode = (int)g_sim.GetHardwareMode();
		s_pendingMemMode = MemoryModeToSortedIndex(g_sim.GetMemoryMode());
		s_pendingVideoStd = (int)g_sim.GetVideoStandard();
		s_pendingBasic = g_sim.IsBASICEnabled();
	}

	// Hardware mode
	ImGui::Text("Hardware:");
	ImGui::SameLine(100);
	ImGui::SetNextItemWidth(180);
	ImGui::Combo("##hw", &s_pendingHwMode, kHardwareModeNames, kATHardwareModeCount);

	// Memory mode
	ImGui::Text("Memory:");
	ImGui::SameLine(100);
	ImGui::SetNextItemWidth(180);
	ImGui::Combo("##mem", &s_pendingMemMode, MemoryModeComboGetter, nullptr, kATMemoryModeCount);

	// Video standard
	ImGui::Text("Video:");
	ImGui::SameLine(100);
	ImGui::SetNextItemWidth(180);
	ImGui::Combo("##vid", &s_pendingVideoStd, kVideoStandardNames, kATVideoStandardCount);

	// BASIC
	ImGui::Text("BASIC:");
	ImGui::SameLine(100);
	ImGui::Checkbox("##basic", &s_pendingBasic);

	ImGui::Separator();

	// Display options (applied immediately — not emulation state)
	{
		bool statusBar = ATUIGetShowStatusBar();
		if (ImGui::Checkbox("Show Status Bar", &statusBar))
			ATUISetShowStatusBar(statusBar);
	}

	ImGui::Separator();

	if (ImGui::Button("Apply & Cold Reset", ImVec2(180, 0))) {
		auto newHW = (ATHardwareMode)s_pendingHwMode;
		auto newMem = kSortedMemoryModes[s_pendingMemMode].mode;
		auto newVid = (ATVideoStandard)s_pendingVideoStd;

		// Switch hardware mode (sets stock defaults for memory/kernel)
		ATUISwitchHardwareMode(nullptr, newHW, false);

		// Apply user's explicit overrides from the dialog
		ATUISwitchMemoryMode(nullptr, newMem);
		g_sim.SetVideoStandard(newVid);
		g_sim.SetBASICEnabled(s_pendingBasic);

		ATUIUpdateSpeedTiming();
		g_sim.LoadROMs();
		g_sim.ColdReset();
		g_sim.Resume();

		// Reset pending state so next open re-syncs
		s_pendingHwMode = -1;
	}

	// Reset pending state when dialog is closed
	if (!s_showSystemConfig)
		s_pendingHwMode = -1;

	ImGui::End();
}

// ============= Status Bar =============

static void DrawStatusBar() {
	ATDisplaySDL2 *disp = ATGetLinuxDisplay();

	if (!ATUIGetShowStatusBar()) {
		if (disp)
			disp->SetBottomMargin(0);
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	float barHeight = ImGui::GetFrameHeight() + 4.0f;

	// Tell display backend to reserve space so emulator output doesn't
	// render behind the status bar. Convert logical pixels to physical.
	if (disp) {
		float scale = io.DisplayFramebufferScale.y;
		disp->SetBottomMargin((int)(barHeight * scale + 0.5f));
	}

	ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - barHeight));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, barHeight));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoFocusOnAppearing;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 0.95f));

	if (ImGui::Begin("##statusbar", nullptr, flags)) {
		ATHardwareMode hw = g_sim.GetHardwareMode();
		ATVideoStandard vs = g_sim.GetVideoStandard();

		const char *hwName = (hw < kATHardwareModeCount) ? kHardwareModeNames[hw] : "?";
		const char *vsName = (vs < kATVideoStandardCount) ? kVideoStandardNames[vs] : "?";

		ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s %s", hwName, vsName);

		// 1200XL LED indicators (only shown when active)
		{
			uint8 leds = ATImGuiGetIndicatorState().mLedStatus;
			if (leds) {
				ImGui::SameLine(0, 16);
				if (leds & 1)
					ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "L1");
				if (leds & 2) {
					ImGui::SameLine(0, 4);
					ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "L2");
				}
			}
		}

		// Disk status with activity indicators
		// Colors per drive (dim/bright pairs, matching Windows palette)
		static const ImVec4 kDiskDim[8] = {
			{0.57f, 0.63f, 0.00f, 1.0f}, {0.83f, 0.44f, 0.25f, 1.0f},
			{0.83f, 0.33f, 0.81f, 1.0f}, {0.57f, 0.40f, 1.00f, 1.0f},
			{0.28f, 0.59f, 0.93f, 1.0f}, {0.21f, 0.73f, 0.38f, 1.0f},
			{0.42f, 0.70f, 0.00f, 1.0f}, {0.73f, 0.53f, 0.05f, 1.0f},
		};
		static const ImVec4 kDiskBright[8] = {
			{1.0f, 1.0f, 0.40f, 1.0f}, {1.0f, 0.91f, 0.72f, 1.0f},
			{1.0f, 0.80f, 1.00f, 1.0f}, {1.0f, 0.87f, 1.00f, 1.0f},
			{0.75f, 1.0f, 1.00f, 1.0f}, {0.67f, 1.0f, 0.85f, 1.0f},
			{0.89f, 1.0f, 0.44f, 1.0f}, {1.0f, 0.99f, 0.52f, 1.0f},
		};
		static const ImVec4 kDiskIdle = {0.5f, 0.5f, 0.5f, 1.0f};
		static const ImVec4 kDiskDirty = {1.0f, 0.8f, 0.4f, 1.0f};
		static const ImVec4 kDiskError = {1.0f, 0.2f, 0.2f, 1.0f};

		ATImGuiIndicatorState& ind = ATImGuiGetIndicatorState();
		uint32 statusFlags = ind.mStatusFlags | ind.mStatusHoldFlags;
		bool errorBlink = (SDL_GetTicks() % 1000) >= 500;
		uint32 diskErrorVis = errorBlink ? ind.mDiskErrorFlags : 0;

		// Tick hold counters (once per frame)
		for (uint32 f = ind.mStatusHoldFlags; f; f &= f - 1) {
			int idx = __builtin_ctz(f);
			if (idx < 17 && ind.mStatusHoldCounters[idx]) {
				if (!--ind.mStatusHoldCounters[idx])
					ind.mStatusHoldFlags &= ~(1u << idx);
			}
		}

		// Always show D1-D4; show D5-D15 only when loaded or active
		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			uint32 flag = 1u << i;
			bool motorOn = (ind.mDiskMotorFlags & flag) != 0;
			bool sioActive = (statusFlags & flag) != 0;
			bool hasError = (diskErrorVis & flag) != 0;
			bool ledOn = (ind.mDiskLEDFlags & flag) != 0;
			bool isActive = sioActive || hasError;

			if (i >= 4 && !di.IsDiskLoaded() && !motorOn && !sioActive && !ledOn)
				continue;

			ImGui::SameLine(0, 16);

			if (di.IsDiskLoaded()) {
				const wchar_t *filename = VDFileSplitPath(di.GetPath());
				VDStringA u8 = VDTextWToU8(VDStringW(filename));

				const ImVec4 *color;
				if (hasError)
					color = &kDiskError;
				else if (isActive)
					color = &kDiskBright[i & 7];
				else if (motorOn)
					color = &kDiskDim[i & 7];
				else if (di.IsDirty())
					color = &kDiskDirty;
				else if (ledOn)
					color = &kDiskDim[i & 7];
				else
					color = nullptr;

				// Compute track/sector from disk geometry (always shown)
				uint32 sector = ind.mStatusCounter[i];
				uint32 track = 0;
				uint32 secInTrack = 0;
				IATDiskImage *img = di.GetDiskImage();
				if (img && sector > 0) {
					ATDiskGeometryInfo geo = img->GetGeometry();
					uint32 spt = geo.mSectorsPerTrack ? geo.mSectorsPerTrack : 18;
					track = (sector - 1) / spt;
					secInTrack = (sector - 1) % spt + 1;
				}

				const char *dirty = di.IsDirty() ? "*" : "";
				if (color)
					ImGui::TextColored(*color, "D%d: %s%s [T%02u S%02u]", i + 1, u8.c_str(), dirty, track, secInTrack);
				else
					ImGui::Text("D%d: %s [T%02u S%02u]", i + 1, u8.c_str(), track, secInTrack);
			} else if (motorOn || sioActive) {
				const ImVec4& color = isActive ? kDiskBright[i & 7] : kDiskDim[i & 7];
				ImGui::TextColored(color, "D%d:", i + 1);
			} else {
				ImGui::TextColored(kDiskIdle, "D%d: -", i + 1);
			}
		}

		// H: device indicator
		if (ind.mHReadCounter || ind.mHWriteCounter) {
			ImGui::SameLine(0, 16);
			bool bright = ind.mHReadCounter > 24 || ind.mHWriteCounter > 24;
			ImVec4 hColor = bright ? ImVec4(0.2f, 1.0f, 0.6f, 1.0f) : ImVec4(0.1f, 0.6f, 0.35f, 1.0f);
			const char *suffix = (ind.mHReadCounter && ind.mHWriteCounter) ? "H:RW"
				: ind.mHWriteCounter ? "H:W" : "H:R";
			ImGui::TextColored(hColor, "%s", suffix);
			if (ind.mHReadCounter) --ind.mHReadCounter;
			if (ind.mHWriteCounter) --ind.mHWriteCounter;
		}

		// PCLink indicator
		if (ind.mPCLinkReadCounter || ind.mPCLinkWriteCounter) {
			ImGui::SameLine(0, 16);
			bool bright = ind.mPCLinkReadCounter > 24 || ind.mPCLinkWriteCounter > 24;
			ImVec4 pColor = bright ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f) : ImVec4(0.2f, 0.5f, 0.7f, 1.0f);
			const char *suffix = (ind.mPCLinkReadCounter && ind.mPCLinkWriteCounter) ? "PCL:RW"
				: ind.mPCLinkWriteCounter ? "PCL:W" : "PCL:R";
			ImGui::TextColored(pColor, "%s", suffix);
			if (ind.mPCLinkReadCounter) --ind.mPCLinkReadCounter;
			if (ind.mPCLinkWriteCounter) --ind.mPCLinkWriteCounter;
		}

		// IDE/hard disk indicator
		if (ind.mbHardDiskRead || ind.mbHardDiskWrite) {
			ImGui::SameLine(0, 16);
			ImVec4 hdColor = ind.mbHardDiskWrite ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f) : ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
			ImGui::TextColored(hdColor, "HD:%c%u", ind.mbHardDiskWrite ? 'W' : 'R', ind.mHardDiskLBA);
			if (ind.mHardDiskCounter) {
				if (!--ind.mHardDiskCounter) {
					ind.mbHardDiskRead = false;
					ind.mbHardDiskWrite = false;
				}
			}
		}

		// Flash write indicator
		if (ind.mFlashWriteCounter) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.1f, 1.0f), "FL");
			--ind.mFlashWriteCounter;
		}

		// Cartridge indicator (flashes white on bank switch activity)
		if (g_sim.IsCartridgeAttached(0)) {
			ImGui::SameLine(0, 16);
			if (ind.mCartridgeActivityCounter) {
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "CART");
				--ind.mCartridgeActivityCounter;
			} else {
				ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f), "CART");
			}
		}

		// Modem connection indicator
		if (ind.mModemConnection[0]) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "MDM:%s", ind.mModemConnection);
		}

		// Tape indicator with position
		{
			ATCassetteEmulator& cas = g_sim.GetCassette();
			if (cas.IsLoaded()) {
				ImGui::SameLine(0, 16);
				float pos = cas.GetPosition();
				int posMin = (int)(pos / 60.0f);
				int posSec = (int)pos % 60;
				if (cas.IsPlayEnabled() && !cas.IsPaused())
					ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "TAPE> %d:%02d", posMin, posSec);
				else if (cas.IsRecordEnabled() && !cas.IsPaused())
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "TAPE* %d:%02d", posMin, posSec);
				else
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "TAPE %d:%02d", posMin, posSec);
			}
		}

		// Pause indicator
		if (g_sim.IsPaused()) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "PAUSED");
		}

		// Turbo/speed indicator
		if (ATUIGetTurbo()) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[TURBO]");
		} else {
			float speed = ATUIGetSpeedModifier();
			int pct = (int)(speed * 100.0f + 0.5f);
			if (pct != 100) {
				ImGui::SameLine(0, 16);
				ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "%d%%", pct);
			}
		}

		// Recording indicator
		if (IsRecordingActive()) {
			ImGui::SameLine(0, 16);
			const char *recType = s_pVideoWriter ? "REC:AVI"
				: s_pAudioWriter
					? (s_pAudioWriter->IsRecordingRaw() ? "REC:PCM" : "REC:WAV")
				: s_pSapWriter ? "REC:SAP"
				: "REC:VGM";
			ImVec4 recColor = (s_pVideoWriter && s_pVideoWriter->IsPaused())
				? ImVec4(1.0f, 0.6f, 0.2f, 1.0f)
				: ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
			ImGui::TextColored(recColor, "%s", recType);

			// Show elapsed time and file size for video recording
			auto& ind = ATImGuiGetIndicatorState();
			if (s_pVideoWriter && ind.mRecordingTime >= 0) {
				int secs = (int)ind.mRecordingTime;
				int mins = secs / 60;
				secs %= 60;
				ImGui::SameLine(0, 4);
				if (ind.mRecordingSize >= 1048576)
					ImGui::TextColored(recColor, "%d:%02d %.1fMB", mins, secs, ind.mRecordingSize / 1048576.0);
				else
					ImGui::TextColored(recColor, "%d:%02d %lldKB", mins, secs, (long long)(ind.mRecordingSize / 1024));
				if (ind.mbRecordingPaused) {
					ImGui::SameLine(0, 4);
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "PAUSED");
				}
			}
		}

		// Mute indicator
		{
			IATAudioOutput *audioOut = g_sim.GetAudioOutput();
			if (audioOut && audioOut->GetMute()) {
				ImGui::SameLine(0, 16);
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "MUTE");
			}
		}

		// FPS counter
		++s_fpsFrameCount;
		double now = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
		if (s_lastFpsTime == 0)
			s_lastFpsTime = now;
		double elapsed = now - s_lastFpsTime;
		if (elapsed >= 0.5) {
			s_currentFps = (float)(s_fpsFrameCount / elapsed);
			s_fpsFrameCount = 0;
			s_lastFpsTime = now;
		}

		if (ATUIGetShowFPS()) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%.1f fps", s_currentFps);
		}

		// Held console buttons (Start/Select/Option held during boot)
		{
			uint8 held = ind.mHeldButtonMask;
			if (held) {
				ImVec4 heldColor(1.0f, 1.0f, 0.3f, 1.0f);
				if (held & 1) { ImGui::SameLine(0, 16); ImGui::TextColored(heldColor, "Start"); }
				if (held & 2) { ImGui::SameLine(0, 4);  ImGui::TextColored(heldColor, "Select"); }
				if (held & 4) { ImGui::SameLine(0, 4);  ImGui::TextColored(heldColor, "Option"); }
			}
		}

		// Pending hold mode (keys to hold on next reset)
		if (ind.mbPendingHoldMode || ind.mPendingHeldButtons || ind.mPendingHeldKey >= 0) {
			ImGui::SameLine(0, 16);
			ImVec4 holdColor(0.6f, 1.0f, 0.6f, 1.0f);
			char holdBuf[128];
			int pos = 0;
			if (ind.mbPendingHoldMode)
				pos += snprintf(holdBuf + pos, sizeof(holdBuf) - pos, "Hold: ");
			if (ind.mPendingHeldButtons & 1)
				pos += snprintf(holdBuf + pos, sizeof(holdBuf) - pos, "Start+");
			if (ind.mPendingHeldButtons & 2)
				pos += snprintf(holdBuf + pos, sizeof(holdBuf) - pos, "Select+");
			if (ind.mPendingHeldButtons & 4)
				pos += snprintf(holdBuf + pos, sizeof(holdBuf) - pos, "Option+");
			if (ind.mPendingHeldKey >= 0) {
				const wchar_t *label = ATUIGetNameForKeyCode((uint8)ind.mPendingHeldKey);
				if (label) {
					VDStringA u8 = VDTextWToU8(VDStringW(label));
					pos += snprintf(holdBuf + pos, sizeof(holdBuf) - pos, "%s", u8.c_str());
				} else {
					pos += snprintf(holdBuf + pos, sizeof(holdBuf) - pos, "[$%02X]", ind.mPendingHeldKey);
				}
			}
			// Trim trailing '+'
			if (pos > 0 && holdBuf[pos - 1] == '+')
				holdBuf[pos - 1] = 0;
			ImGui::TextColored(holdColor, "%s", holdBuf);
		}

		// Tracing indicator
		if (ind.mTracingSize >= 0) {
			ImGui::SameLine(0, 16);
			ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Tracing %.1fM",
				(double)ind.mTracingSize / 1048576.0);
		}

		// Status messages (highest priority non-empty; auto-expire Status after 1500ms)
		{
			const char *msg = nullptr;
			for (int i = 2; i >= 0; --i) {
				if (ind.mStatusMessages[i][0]) {
					if (i == 0) {
						uint32 age = SDL_GetTicks() - ind.mStatusMessageTimestamp;
						if (age > 1500) {
							ind.mStatusMessages[0][0] = 0;
							continue;
						}
					}
					msg = ind.mStatusMessages[i];
					break;
				}
			}
			if (msg) {
				ImGui::SameLine(0, 16);
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.8f, 1.0f), "%s", msg);
			}
		}

		// Watched values
		{
			bool anyWatch = false;
			for (int i = 0; i < 8; ++i) {
				if (ind.mWatchSlots[i].active) {
					anyWatch = true;
					break;
				}
			}
			if (anyWatch) {
				for (int i = 0; i < 8; ++i) {
					auto& slot = ind.mWatchSlots[i];
					if (!slot.active)
						continue;
					ImGui::SameLine(0, i == 0 ? 16 : 8);
					switch (slot.format) {
						case 1: // Dec
							ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%d", (int)slot.value);
							break;
						case 2: // Hex8
							ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%02X", slot.value);
							break;
						case 3: // Hex16
							ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%04X", slot.value);
							break;
						case 4: // Hex32
							ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%08X", slot.value);
							break;
						default:
							break;
					}
				}
			}
		}
	}
	ImGui::End();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

// ============= Audio Monitor =============

static void DrawAudioMonitorPokey(int pokey, float columnWidth, float totalHeight) {
	auto& ind = ATImGuiGetIndicatorState();
	const double cyclesPerSecond = ind.mCyclesPerSecond > 0.0 ? ind.mCyclesPerSecond : 1789773.0;

	ATPokeyAudioLog *log = nullptr;
	ATPokeyRegisterState *rstate = nullptr;
	const uint8 chanMask = ind.mpAudioMonitors[pokey]->Update(&log, &rstate);

	const uint8 audctl = rstate->mReg[8];
	const uint8 skctl = rstate->mReg[15];
	const int borrowOffset12 = (skctl & 8) ? 6 : 4;
	const int slowRate = (audctl & 0x01) ? 114 : 28;

	int divisors[4];
	divisors[0] = (audctl & 0x40)
		? (int)rstate->mReg[0] + borrowOffset12
		: ((int)rstate->mReg[0] + 1) * slowRate;
	divisors[1] = (audctl & 0x10)
		? ((audctl & 0x40)
			? rstate->mReg[0] + ((int)rstate->mReg[2] << 8) + borrowOffset12 + 3
			: (rstate->mReg[0] + ((int)rstate->mReg[2] << 8) + 1) * slowRate)
		: ((int)rstate->mReg[2] + 1) * slowRate;
	divisors[2] = (audctl & 0x20)
		? (int)rstate->mReg[4] + borrowOffset12
		: ((int)rstate->mReg[4] + 1) * slowRate;
	divisors[3] = (audctl & 0x08)
		? ((audctl & 0x20)
			? rstate->mReg[4] + ((int)rstate->mReg[6] << 8) + 7
			: (rstate->mReg[4] + ((int)rstate->mReg[6] << 8) + 1) * slowRate)
		: ((int)rstate->mReg[6] + 1) * slowRate;

	const float chanHeight = totalHeight / 4.0f;

	for (int ch = 0; ch < 4; ++ch) {
		ImGui::PushID(pokey * 4 + ch);

		const bool active = (chanMask & (1 << ch)) != 0;
		const ImU32 textCol = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(128, 128, 128, 255);
		const ImU32 waveCol = active ? IM_COL32(0, 200, 0, 255) : IM_COL32(0, 80, 0, 255);

		// Frequency
		float freq = (float)((cyclesPerSecond * 0.5) / divisors[ch]);
		ImGui::PushStyleColor(ImGuiCol_Text, textCol);
		ImGui::Text("Ch%d %7.1f Hz", ch + 1, freq);

		// Mode indicators
		ImGui::SameLine(0.0f, 8.0f);

		// Clock rate
		if ((ch == 1 && (audctl & 0x10)) || (ch == 3 && (audctl & 0x08)))
			ImGui::Text("16");
		else if ((ch == 0 && (audctl & 0x40)) || (ch == 2 && (audctl & 0x20)))
			ImGui::Text("1.79");
		else
			ImGui::Text("%s", (audctl & 1) ? "15K" : "64K");

		ImGui::SameLine(0.0f, 4.0f);

		// High-pass
		if ((ch == 0 && (audctl & 4)) || (ch == 1 && (audctl & 2)))
			ImGui::Text("H");
		else
			ImGui::Text(" ");

		ImGui::SameLine(0.0f, 4.0f);

		// Mode: volume-only, poly, two-tone, clock divider
		const uint8 ctl = rstate->mReg[ch * 2 + 1];
		if (ctl & 0x10) {
			ImGui::Text("V");
		} else {
			char mode[5] = {};
			mode[0] = (ctl & 0x80) ? 'L' : '5';
			mode[1] = (ch < 2) ? ((skctl & 0x08) ? '2' : ' ') : ((skctl & 0x10) ? 'A' : ' ');
			if (ctl & 0x20)
				mode[2] = 'T';
			else if (ctl & 0x40)
				mode[2] = '4';
			else if (audctl & 0x80)
				mode[2] = '9';
			else {
				mode[2] = '1';
				mode[3] = '7';
			}
			ImGui::Text("%s", mode);
		}

		ImGui::PopStyleColor();

		// Volume bar + waveform
		{
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			float waveHeight = chanHeight - ImGui::GetTextLineHeightWithSpacing() - 4.0f;
			if (waveHeight < 8.0f)
				waveHeight = 8.0f;

			ImDrawList *dl = ImGui::GetWindowDrawList();

			// Volume bar
			int vol = ctl & 15;
			float volHeight = waveHeight * vol / 15.0f;
			dl->AddRectFilled(
				ImVec2(cursor.x, cursor.y + waveHeight - volHeight),
				ImVec2(cursor.x + 3.0f, cursor.y + waveHeight),
				IM_COL32(255, 255, 0, 200));

			// Waveform
			const uint32 n = log->mLastFrameSampleCount;
			if (n > 1) {
				float waveX = cursor.x + 6.0f;
				float waveW = columnWidth - 6.0f;
				float hstepf = waveW / (float)n;
				float waveformScale = -(waveHeight - 2.0f) / (float)log->mFullScaleValue;
				float baseY = cursor.y + waveHeight - 1.0f;

				ImVector<ImVec2> pts;
				pts.resize((int)n);
				float px = waveX;
				for (uint32 pos = 0; pos < n; ++pos) {
					float py = log->mpStates[pos].mChannelOutputs[ch] * waveformScale + baseY;
					pts[pos] = ImVec2(px, py);
					px += hstepf;
				}
				dl->AddPolyline(pts.Data, (int)n, waveCol, ImDrawFlags_None, 1.0f);
			}

			ImGui::Dummy(ImVec2(columnWidth, waveHeight));
		}

		ImGui::PopID();
	}
}

static void DrawAudioMonitor() {
	auto& ind = ATImGuiGetIndicatorState();

	if (!ind.mbAudioDisplayEnabled[0] && !ind.mbAudioDisplayEnabled[1])
		return;

	const bool dualPokey = ind.mbAudioDisplayEnabled[0] && ind.mpAudioMonitors[0]
		&& ind.mbAudioDisplayEnabled[1] && ind.mpAudioMonitors[1];

	ImGui::SetNextWindowSize(ImVec2(dualPokey ? 720.0f : 480.0f, 240.0f), ImGuiCond_FirstUseEver);
	bool open = true;
	if (!ImGui::Begin("Audio Monitor", &open)) {
		ImGui::End();
		if (!open)
			g_sim.SetAudioMonitorEnabled(false);
		return;
	}

	if (!open) {
		ImGui::End();
		g_sim.SetAudioMonitorEnabled(false);
		return;
	}

	const ImVec2 contentAvail = ImGui::GetContentRegionAvail();

	if (dualPokey) {
		// Side-by-side layout: POKEY 1 (left) | separator | POKEY 2 (right)
		float colWidth = (contentAvail.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

		if (ImGui::BeginChild("##pokey1", ImVec2(colWidth, contentAvail.y), ImGuiChildFlags_None)) {
			ImGui::TextDisabled("POKEY 1");
			float bodyHeight = contentAvail.y - ImGui::GetTextLineHeightWithSpacing();
			DrawAudioMonitorPokey(0, colWidth, bodyHeight);
		}
		ImGui::EndChild();

		ImGui::SameLine();

		if (ImGui::BeginChild("##pokey2", ImVec2(colWidth, contentAvail.y), ImGuiChildFlags_None)) {
			ImGui::TextDisabled("POKEY 2 (Stereo)");
			float bodyHeight = contentAvail.y - ImGui::GetTextLineHeightWithSpacing();
			DrawAudioMonitorPokey(1, colWidth, bodyHeight);
		}
		ImGui::EndChild();
	} else {
		// Single POKEY — use full width
		int which = (ind.mbAudioDisplayEnabled[0] && ind.mpAudioMonitors[0]) ? 0 : 1;
		DrawAudioMonitorPokey(which, contentAvail.x, contentAvail.y);
	}

	ImGui::End();
}

// ============= Audio Scope =============

static constexpr float kScopeUsPerDiv[] = {
	100.0f, 200.0f, 500.0f,
	1000.0f, 2000.0f, 5000.0f,
	10000.0f, 20000.0f, 50000.0f,
	100000.0f, 200000.0f,
};
static constexpr int kScopeRateCount = (int)(sizeof(kScopeUsPerDiv) / sizeof(kScopeUsPerDiv[0]));

static int s_scopeRateIndex = 3;
static std::vector<float> s_scopeWaveforms[2];
static uint32 s_scopeSampleScale = 1;
static uint32 s_scopeSamplesRequested = 0;
static float s_scopeLastWidth = 0;

static void ScopeUpdateSampleCounts() {
	auto& ind = ATImGuiGetIndicatorState();

	float usPerDiv = kScopeUsPerDiv[s_scopeRateIndex];
	float usPerView = usPerDiv * 10.0f;
	float secsPerView = usPerView / 1000000.0f;
	float samplesPerSec = 63920.8f;
	float samplesPerViewF = samplesPerSec * secsPerView;

	uint32 n = (uint32)ceilf(samplesPerViewF);
	s_scopeSamplesRequested = n;

	for (int i = 0; i < 2; ++i) {
		if (ind.mpAudioMonitors[i])
			ind.mpAudioMonitors[i]->SetMixedSampleCount(n);
	}
}

static void DrawAudioScope() {
	auto& ind = ATImGuiGetIndicatorState();

	if (!ind.mbAudioScopeEnabled)
		return;

	ImGui::SetNextWindowSize(ImVec2(512, 180), ImGuiCond_FirstUseEver);
	bool open = true;
	if (!ImGui::Begin("Audio Scope", &open)) {
		ImGui::End();
		if (!open)
			g_sim.SetAudioScopeEnabled(false);
		return;
	}

	if (!open) {
		ImGui::End();
		g_sim.SetAudioScopeEnabled(false);
		return;
	}

	// Time-base controls
	if (ImGui::Button("<")) {
		if (s_scopeRateIndex > 0) {
			--s_scopeRateIndex;
			ScopeUpdateSampleCounts();
		}
	}
	ImGui::SameLine();

	float usPerDiv = kScopeUsPerDiv[s_scopeRateIndex];
	if (usPerDiv < 1000.0f)
		ImGui::Text("%.1f us/div", usPerDiv);
	else
		ImGui::Text("%.0f ms/div", usPerDiv / 1000.0f);

	ImGui::SameLine();
	if (ImGui::Button(">")) {
		if (s_scopeRateIndex < kScopeRateCount - 1) {
			++s_scopeRateIndex;
			ScopeUpdateSampleCounts();
		}
	}

	// Scope area
	ImVec2 avail = ImGui::GetContentRegionAvail();
	if (avail.x < 16.0f || avail.y < 16.0f) {
		ImGui::End();
		return;
	}

	// Update sample counts on window resize
	if (s_scopeLastWidth != avail.x) {
		s_scopeLastWidth = avail.x;
		ScopeUpdateSampleCounts();
	}

	ImVec2 origin = ImGui::GetCursorScreenPos();
	float w = avail.x;
	float h = avail.y;
	ImDrawList *dl = ImGui::GetWindowDrawList();

	// Background
	dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h), IM_COL32(0, 0, 0, 200));

	// Grid: 10 vertical divisions + center horizontal line
	const ImU32 gridCol = IM_COL32(128, 128, 128, 128);
	for (int i = 1; i < 10; ++i) {
		float x = origin.x + w * i / 10.0f;
		dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + h), gridCol);
	}
	float ymid = origin.y + h * 0.5f;
	dl->AddLine(ImVec2(origin.x, ymid), ImVec2(origin.x + w, ymid), gridCol);

	// Update and draw waveforms
	float usPerView = usPerDiv * 10.0f;
	float secsPerView = usPerView / 1000000.0f;
	float samplesPerSec = 63920.8f;
	float samplesPerViewF = samplesPerSec * secsPerView;

	// Collect audio data
	ATPokeyAudioLog *logs[2] = {};
	bool logsReady = true;
	for (int i = 0; i < 2; ++i) {
		ATAudioMonitor *mon = ind.mpAudioMonitors[i];
		if (!mon)
			continue;
		ATPokeyRegisterState *rstate;
		mon->Update(&logs[i], &rstate);
		if (logs[i]->mNumMixedSamples < logs[i]->mMaxMixedSamples)
			logsReady = false;
	}

	if (logsReady) {
		sint32 n = (sint32)s_scopeSamplesRequested;
		s_scopeSampleScale = 1;

		while (s_scopeSampleScale < 64 && n > (sint32)w * 2) {
			s_scopeSampleScale += s_scopeSampleScale;
			n >>= 1;
		}

		for (int i = 0; i < 2; ++i) {
			ATPokeyAudioLog *log = logs[i];
			if (!log)
				continue;

			auto& wf = s_scopeWaveforms[i];
			wf.resize(n);

			const float *src = log->mpMixedSamples;
			float *dst = wf.data();
			uint32 step = s_scopeSampleScale;

			float scale = 1.0f / (float)step;
			for (sint32 j = 0; j < n; ++j) {
				float v = 0;
				for (uint32 k = 0; k < step; ++k)
					v += src[k];
				dst[j] = v * scale;
				src += step;
			}

			log->mNumMixedSamples = 0;
		}
	}

	// Draw stored waveforms
	for (int i = 0; i < 2; ++i) {
		const auto& wf = s_scopeWaveforms[i];
		if (wf.empty())
			continue;

		size_t n = wf.size();
		ImU32 color = i ? IM_COL32(0, 180, 0, 255) : IM_COL32(255, 0, 0, 255);

		float yscale = -(h * 0.5f);
		float yoffset = ymid;
		float xscale = w / samplesPerViewF * (float)s_scopeSampleScale;

		ImVector<ImVec2> pts;
		pts.resize((int)n);
		for (size_t j = 0; j < n; ++j) {
			pts[(int)j] = ImVec2(
				origin.x + (float)j * xscale + xscale * 0.5f,
				wf[j] * yscale + yoffset);
		}
		dl->AddPolyline(pts.Data, (int)n, color, ImDrawFlags_None, 1.0f);
	}

	ImGui::Dummy(avail);

	ImGui::End();
}

// ============= About Window =============

static void DrawAbout() {
	if (!s_showAbout)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("About Altirra", &s_showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Altirra (Linux port) " AT_VERSION);
	ImGui::Separator();
	ImGui::TextWrapped(
		"Atari 800/800XL/5200 emulator by Avery Lee.\n"
		"Linux port by Paul Kilar using SDL2, OpenGL, and Dear ImGui.\n"
		"\n"
		"This program is free software under the GNU General "
		"Public License v2 or later."
	);

	ImGui::Separator();
	if (ImGui::Button("Close", ImVec2(120, 0)))
		s_showAbout = false;

	ImGui::End();
}

// ============= Keyboard Shortcuts Window =============

static void DrawShortcuts() {
	if (!s_showShortcuts)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Keyboard Shortcuts", &s_showShortcuts)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
		ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 140);
		ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		auto row = [](const char *key, const char *action) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(key);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(action);
		};

		row("F1 (hold)", "Turbo / Warp");
		row("Shift+F1", "Cycle Quick Maps");
		row("F4", "Toggle Mute");
		row("F5", "Break / Run (debugger)");
		row("F6", "Warm Reset");
		row("Shift+F6", "Cold Reset");
		row("F7", "Quick Save State");
		row("F8", "Quick Load State");
		row("F9", "Save Screenshot");
		row("F10", "Step Over (debugger)");
		row("F11", "Fullscreen / Step Into");
		row("Shift+F11", "Step Out (debugger)");
		row("F12", "Toggle Overlay");
		row("Alt+Return", "Toggle Fullscreen");
		row("Pause", "Pause / Resume");
		row("Ctrl+O", "Open Image");
		row("Ctrl+Shift+O", "Boot Image");
		row("Ctrl+V", "Paste Text");
		row("Ctrl+S", "Save Settings");
		row("Ctrl+Q", "Quit");

		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::TextDisabled("F5/F10/F11(step) only active with overlay open");

	ImGui::End();
}

// ============= File dialog fallback polling =============

static void PollFileDialogFallback() {
	if (s_pendingDialog == PendingDialog::kNone)
		return;

	VDStringW result;
	if (ATLinuxFileDialogDrawFallback(result)) {
		switch (s_pendingDialog) {
			case PendingDialog::kOpenImage:
				TryLoadImage(result);
				break;
			case PendingDialog::kMountDisk:
				TryMountDisk(s_pendingMountDiskSlot, result);
				break;
			case PendingDialog::kSaveState:
				if (!result.empty()) {
					try { g_sim.SaveState(result.c_str()); ShowToast("State saved"); }
					catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Save state failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Save state failed"); }
				}
				break;
			case PendingDialog::kLoadState:
				if (!result.empty()) {
					try { g_sim.Load(result.c_str(), kATMediaWriteMode_RO, nullptr); ShowToast("State loaded"); }
					catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Load state failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Load state failed"); }
				}
				break;
			case PendingDialog::kLoadTape:
				if (!result.empty()) {
					try { g_sim.GetCassette().Load(result.c_str()); ShowToast("Tape loaded"); }
					catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Load tape failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Load tape failed"); }
				}
				break;
			case PendingDialog::kSaveTape:
				if (!result.empty()) {
					try {
						g_sim.GetCassette().SetImagePersistent(result.c_str());
						g_sim.GetCassette().SetImageClean();
						ShowToast("Tape saved");
					} catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Save tape failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Save tape failed"); }
				}
				break;
			case PendingDialog::kBootImage:
				if (!result.empty()) {
					try {
						g_sim.UnloadAll();
						g_sim.Load(result.c_str(), kATMediaWriteMode_RO, nullptr);
						g_sim.ColdReset();
						g_sim.Resume();
						MRUAdd(result.c_str());
						ShowToast("Booted image");
					} catch (const std::exception& e) { char m[512]; snprintf(m, sizeof(m), "Boot failed: %s", e.what()); ShowToast(m); }
					catch (...) { ShowToast("Failed to boot image"); }
				}
				break;
			default:
				break;
		}
		s_pendingDialog = PendingDialog::kNone;
	}

	if (!ATLinuxFileDialogIsFallbackOpen())
		s_pendingDialog = PendingDialog::kNone;
}

// ============= Error Popup =============

static void CheckPendingErrors() {
	// Pop any errors from the thread-safe queue
	auto errors = ATImGuiPopPendingErrors();
	if (!errors.empty() && !s_showErrorPopup) {
		// Show the first error; rest will be shown on subsequent frames
		s_errorTitle = std::move(errors[0].first);
		s_errorMessage = std::move(errors[0].second);
		s_showErrorPopup = true;
		ImGui::OpenPopup("##error_popup");
	}
}

static void DrawErrorPopup() {
	if (!s_showErrorPopup)
		return;

	// Ensure popup is opened
	if (!ImGui::IsPopupOpen("##error_popup"))
		ImGui::OpenPopup("##error_popup");

	ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("##error_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", s_errorTitle.c_str());
		ImGui::Separator();
		ImGui::TextWrapped("%s", s_errorMessage.c_str());
		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			s_showErrorPopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

// ============= Cartridge Browser Window =============

static void CartBuildDisplayModes() {
	s_cartDisplayModes = s_cartDetectedModes;

	if (s_cartShowAll) {
		vdfastvector<bool> present(kATCartridgeModeCount, false);

		present[kATCartridgeMode_None] = true;
		present[kATCartridgeMode_SuperCharger3D] = true;

		for (int m : s_cartDisplayModes) {
			if (m >= 0 && m < (int)kATCartridgeModeCount)
				present[m] = true;
		}

		for (int i = 0; i <= (int)kATCartridgeMapper_Max; ++i) {
			ATCartridgeMode mode = ATGetCartridgeModeForMapper(i);
			if (mode != kATCartridgeMode_None && !present[mode]) {
				present[mode] = true;
				s_cartDisplayModes.push_back(mode);
			}
		}

		for (int i = 1; i < (int)kATCartridgeModeCount; ++i) {
			if (!present[i])
				s_cartDisplayModes.push_back(i);
		}
	}
}

static void DrawCartridgeBrowser() {
	if (!s_showCartridgeBrowser)
		return;

	ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Cartridge Browser", &s_showCartridgeBrowser)) {
		ImGui::End();
		return;
	}

	// Show current cartridge slots
	for (uint32 slot = 0; slot < 2; ++slot) {
		char slotLabel[64];

		if (g_sim.IsCartridgeAttached(slot)) {
			ATCartridgeEmulator *cart = g_sim.GetCartridge(slot);
			const wchar_t *path = cart ? cart->GetPath() : nullptr;
			const char *modeName = cart ? ATGetCartridgeModeName(cart->GetMode()) : "?";

			if (path && *path) {
				VDStringA u8name = VDTextWToU8(VDStringW(VDFileSplitPath(path)));
				snprintf(slotLabel, sizeof(slotLabel), "Slot %u: %s - %s", slot + 1, modeName, u8name.c_str());
			} else {
				snprintf(slotLabel, sizeof(slotLabel), "Slot %u: %s", slot + 1, modeName);
			}

			ImGui::Text("%s", slotLabel);
			ImGui::SameLine();

			char btnId[32];
			snprintf(btnId, sizeof(btnId), "Unload##slot%u", slot);
			if (ImGui::SmallButton(btnId)) {
				g_sim.UnloadCartridge(slot);
			}
		} else {
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Slot %u: [empty]", slot + 1);
		}
	}

	ImGui::Separator();

	// Load buttons
	if (ImGui::Button("Load Cartridge...")) {
		VDStringW path = ATLinuxOpenFileDialog("Load Cartridge", kCartFilters);
		if (!path.empty()) {
			try {
				ATCartLoadContext loadCtx {};
				loadCtx.mbReturnOnUnknownMapper = true;

				if (!g_sim.LoadCartridge(0, path.c_str(), &loadCtx)) {
					if (loadCtx.mLoadStatus == kATCartLoadStatus_UnknownMapper && loadCtx.mpCaptureBuffer) {
						// Raw file with unknown mapper — activate mapper selection
						s_cartLoadBuffer = *loadCtx.mpCaptureBuffer;
						s_cartLoadPath = path;
						s_cartRecommendedIdx = ATCartridgeAutodetectMode(
							s_cartLoadBuffer.data(), (uint32)s_cartLoadBuffer.size(), s_cartDetectedModes);
						s_cartShowAll = false;
						CartBuildDisplayModes();
						s_cartSelectedMapper = s_cartDisplayModes.empty() ? -1 : 0;
						s_cartMapperActive = true;
					}
				}
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Load cartridge failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to load cartridge");
			}
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("Load Raw Binary...")) {
		VDStringW path = ATLinuxOpenFileDialog("Load Raw Binary", kCartFilters);
		if (!path.empty()) {
			try {
				VDFile f(path.c_str());
				sint64 fileSize = f.size();
				if (fileSize > 0 && fileSize <= 128 * 1024 * 1024) {
					s_cartLoadBuffer.resize((size_t)fileSize);
					f.read(s_cartLoadBuffer.data(), (long)fileSize);
					f.close();

					s_cartLoadPath = path;
					s_cartRecommendedIdx = ATCartridgeAutodetectMode(
						s_cartLoadBuffer.data(), (uint32)s_cartLoadBuffer.size(), s_cartDetectedModes);
					s_cartShowAll = false;
					CartBuildDisplayModes();
					s_cartSelectedMapper = s_cartDisplayModes.empty() ? -1 : 0;
					s_cartMapperActive = true;
				}
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Read binary failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to read binary file");
			}
		}
	}

	// Mapper selection panel
	if (s_cartMapperActive) {
		ImGui::Separator();
		ImGui::Text("Mapper Selection");

		if (ImGui::Checkbox("Show All Mappers", &s_cartShowAll)) {
			CartBuildDisplayModes();
			s_cartSelectedMapper = s_cartDisplayModes.empty() ? -1 : 0;
		}

		ImGui::BeginChild("##mapperlist", ImVec2(0, 200), ImGuiChildFlags_Borders);

		for (int i = 0; i < (int)s_cartDisplayModes.size(); ++i) {
			int mode = s_cartDisplayModes[i];
			int mapper = ATGetCartridgeMapperForMode((ATCartridgeMode)mode, 0);
			const char *name = ATGetCartridgeModeName(mode);
			const char *desc = ATGetCartridgeModeDesc(mode);

			char label[256];
			if (mapper > 0) {
				if ((uint32)i < s_cartRecommendedIdx && s_cartRecommendedIdx == 1)
					snprintf(label, sizeof(label), "#%d  %s (recommended) - %s", mapper, name, desc);
				else if ((uint32)i < s_cartRecommendedIdx)
					snprintf(label, sizeof(label), "#%d  *%s - %s", mapper, name, desc);
				else
					snprintf(label, sizeof(label), "#%d  %s - %s", mapper, name, desc);
			} else {
				if ((uint32)i < s_cartRecommendedIdx && s_cartRecommendedIdx == 1)
					snprintf(label, sizeof(label), "%s (recommended) - %s", name, desc);
				else if ((uint32)i < s_cartRecommendedIdx)
					snprintf(label, sizeof(label), "*%s - %s", name, desc);
				else
					snprintf(label, sizeof(label), "%s - %s", name, desc);
			}

			if (ImGui::Selectable(label, s_cartSelectedMapper == i))
				s_cartSelectedMapper = i;
		}

		ImGui::EndChild();

		if (ImGui::Button("OK", ImVec2(80, 0)) && s_cartSelectedMapper >= 0
			&& s_cartSelectedMapper < (int)s_cartDisplayModes.size())
		{
			try {
				ATCartLoadContext loadCtx {};
				loadCtx.mCartMapper = s_cartDisplayModes[s_cartSelectedMapper];
				g_sim.LoadCartridge(0, s_cartLoadPath.c_str(), &loadCtx);
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Load cartridge failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to load cartridge with selected mapper");
			}

			s_cartMapperActive = false;
			s_cartLoadBuffer.clear();
			s_cartDetectedModes.clear();
			s_cartDisplayModes.clear();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			s_cartMapperActive = false;
			s_cartLoadBuffer.clear();
			s_cartDetectedModes.clear();
			s_cartDisplayModes.clear();
		}
	}

	ImGui::End();
}

// ============= Profile Manager Window =============

static void DrawProfileManager() {
	if (!s_showProfileManager)
		return;

	ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Profile Manager", &s_showProfileManager)) {
		ImGui::End();
		return;
	}

	vdfastvector<uint32> profileIds;
	ATSettingsProfileEnum(profileIds);
	uint32 currentId = ATSettingsGetCurrentProfileId();
	static uint32 s_selectedProfileId = 0;
	static char s_nameBuf[128] = "";
	static bool s_renamePrefill = false;

	// Profile list
	if (ImGui::BeginChild("ProfileList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2), ImGuiChildFlags_Borders)) {
		for (uint32 id : profileIds) {
			if (!ATSettingsProfileGetVisible(id))
				continue;
			VDStringW name = ATSettingsProfileGetName(id);
			VDStringA u8name = VDTextWToU8(name);
			if (id == currentId)
				u8name += " (active)";

			if (ImGui::Selectable(u8name.c_str(), s_selectedProfileId == id))
				s_selectedProfileId = id;
		}
	}
	ImGui::EndChild();

	// Determine if selection is valid and whether it's a built-in default profile
	bool hasSelection = ATSettingsIsValidProfile(s_selectedProfileId);
	bool isDefault = false;
	if (hasSelection) {
		for (int i = 0; i < kATDefaultProfileCount; ++i) {
			if (ATGetDefaultProfileId((ATDefaultProfile)i) == s_selectedProfileId) {
				isDefault = true;
				break;
			}
		}
	}

	// Action buttons
	if (ImGui::Button("Switch To") && hasSelection && s_selectedProfileId != currentId)
		ATSettingsSwitchProfile(s_selectedProfileId);

	ImGui::SameLine();
	if (ImGui::Button("New...")) {
		s_nameBuf[0] = 0;
		ImGui::OpenPopup("NewProfile");
	}

	ImGui::SameLine();
	if (ImGui::Button("Rename...") && hasSelection && !isDefault) {
		VDStringW curName = ATSettingsProfileGetName(s_selectedProfileId);
		VDStringA u8cur = VDTextWToU8(curName);
		vdstrlcpy(s_nameBuf, u8cur.c_str(), sizeof(s_nameBuf));
		s_renamePrefill = true;
		ImGui::OpenPopup("RenameProfile");
	}

	ImGui::SameLine();
	if (ImGui::Button("Delete") && hasSelection && !isDefault)
		ImGui::OpenPopup("ConfirmDelete");

	// New profile popup
	if (ImGui::BeginPopup("NewProfile")) {
		ImGui::Text("Profile name:");
		ImGui::InputText("##newname", s_nameBuf, sizeof(s_nameBuf));
		if (ImGui::Button("Create") && s_nameBuf[0]) {
			uint32 newId = ATSettingsGenerateProfileId();
			ATSettingsProfileSetName(newId, VDTextU8ToW(VDStringA(s_nameBuf)).c_str());
			ATSettingsProfileSetVisible(newId, true);
			ATSettingsProfileSetCategoryMask(newId, kATSettingsCategory_AllCategories);
			ATSettingsSwitchProfile(newId);
			s_selectedProfileId = newId;
			s_nameBuf[0] = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Rename popup
	if (ImGui::BeginPopup("RenameProfile")) {
		ImGui::Text("New name:");
		if (s_renamePrefill) {
			ImGui::SetKeyboardFocusHere();
			s_renamePrefill = false;
		}
		ImGui::InputText("##rename", s_nameBuf, sizeof(s_nameBuf));
		if (ImGui::Button("OK") && s_nameBuf[0]) {
			ATSettingsProfileSetName(s_selectedProfileId,
				VDTextU8ToW(VDStringA(s_nameBuf)).c_str());
			s_nameBuf[0] = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Delete confirmation popup
	if (ImGui::BeginPopup("ConfirmDelete")) {
		VDStringW dname = ATSettingsProfileGetName(s_selectedProfileId);
		VDStringA u8dname = VDTextWToU8(dname);
		ImGui::Text("Delete profile \"%s\"?", u8dname.c_str());
		if (ImGui::Button("Delete")) {
			ATSettingsProfileDelete(s_selectedProfileId);
			s_selectedProfileId = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::End();
}

// ============= Firmware Manager Window =============

static void FirmwareRefreshList() {
	ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();
	if (fwMgr) {
		s_fwList.clear();
		fwMgr->GetFirmwareList(s_fwList);
	}
	s_fwListDirty = false;
}

static void DrawFirmwareManager() {
	if (!s_showFirmwareManager) {
		// Window just closed — reload ROMs if defaults were changed
		if (s_fwDefaultsChanged) {
			s_fwDefaultsChanged = false;
			if (g_sim.LoadROMs()) {
				g_sim.ColdReset();
				g_sim.Resume();
			}
		}
		return;
	}

	if (s_fwListDirty)
		FirmwareRefreshList();

	ImGui::SetNextWindowSize(ImVec2(650, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Firmware Manager", &s_showFirmwareManager)) {
		ImGui::End();
		return;
	}

	ATFirmwareManager *fwMgr = g_sim.GetFirmwareManager();

	// Toolbar
	if (ImGui::Button("Scan Directories")) {
		int addedCount = 0;
		vdvector<VDStringW> searchPaths;
		ATGetFirmwareSearchPaths(searchPaths);

		for (const VDStringW& dirPath : searchPaths) {
			VDStringA u8dir = VDTextWToU8(dirPath);

			VDDirectoryIterator it(VDMakePath(dirPath.c_str(), L"*").c_str());
			while (it.Next()) {
				if (it.IsDirectory())
					continue;

				sint64 fileSize = it.GetSize();
				if (!ATFirmwareAutodetectCheckSize(fileSize))
					continue;

				try {
					VDStringW filePath = VDMakePath(dirPath.c_str(), it.GetName());
					VDFile f(filePath.c_str());
					vdfastvector<uint8> buf((size_t)fileSize);
					f.read(buf.data(), (long)fileSize);
					f.close();

					ATFirmwareInfo info {};
					ATSpecificFirmwareType specificType {};
					sint32 knownIdx = -1;

					ATFirmwareDetection detection = ATFirmwareAutodetect(
						buf.data(), (uint32)buf.size(), info, specificType, knownIdx);

					if (detection != ATFirmwareDetection::None) {
						// Check if already registered
						bool alreadyExists = false;
						for (const ATFirmwareInfo& existing : s_fwList) {
							if (existing.mPath == filePath) {
								alreadyExists = true;
								break;
							}
						}

						if (!alreadyExists) {
							info.mId = kATFirmwareId_Custom + (uint64)rand();
							info.mPath = filePath;
							info.mbVisible = true;
							info.mbAutoselect = true;
							if (info.mName.empty())
								info.mName = VDStringW(it.GetName());

							if (fwMgr)
								fwMgr->AddFirmware(info);
							++addedCount;
						}
					}
				} catch (...) {
					// Skip files that can't be read
				}
			}
		}

		char msg[128];
		snprintf(msg, sizeof(msg), "Scan complete: %d new firmware image(s) found.", addedCount);
		s_fwScanResult = msg;
		s_fwShowScanResult = true;
		s_fwListDirty = true;
	}

	ImGui::SameLine();

	if (ImGui::Button("Add File...")) {
		VDStringW path = ATLinuxOpenFileDialog("Add Firmware", kFirmwareFilters);
		if (!path.empty()) {
			try {
				VDFile f(path.c_str());
				sint64 fileSize = f.size();
				vdfastvector<uint8> buf((size_t)fileSize);
				f.read(buf.data(), (long)fileSize);
				f.close();

				ATFirmwareInfo info {};
				ATSpecificFirmwareType specificType {};
				sint32 knownIdx = -1;

				ATFirmwareDetection detection = ATFirmwareAutodetect(
					buf.data(), (uint32)buf.size(), info, specificType, knownIdx);

				if (detection == ATFirmwareDetection::None) {
					info.mType = kATFirmwareType_Unknown;
				}

				info.mId = kATFirmwareId_Custom + (uint64)rand();
				info.mPath = path;
				info.mbVisible = true;
				info.mbAutoselect = true;
				if (info.mName.empty())
					info.mName = VDStringW(VDFileSplitPath(path.c_str()));

				if (fwMgr)
					fwMgr->AddFirmware(info);
				s_fwListDirty = true;
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Add firmware failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to add firmware file");
			}
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("Remove") && s_fwSelectedId != 0) {
		if (fwMgr)
			fwMgr->RemoveFirmware(s_fwSelectedId);
		s_fwSelectedId = 0;
		s_fwListDirty = true;
	}

	if (s_fwShowScanResult) {
		ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", s_fwScanResult.c_str());
	}

	// Re-fetch if dirty after toolbar actions
	if (s_fwListDirty)
		FirmwareRefreshList();

	// Firmware list table
	ImGui::Separator();

	if (ImGui::BeginTable("##fwtable", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp
			| ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate,
		ImVec2(0, 200)))
	{
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 2.0f);
		ImGui::TableSetupColumn("Type", 0, 1.0f);
		ImGui::TableSetupColumn("Path", 0, 2.0f);
		ImGui::TableHeadersRow();

		// Build sorted index of visible firmware
		vdvector<const ATFirmwareInfo *> sortedFw;
		for (const ATFirmwareInfo& fw : s_fwList) {
			if (fw.mbVisible)
				sortedFw.push_back(&fw);
		}

		if (ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs()) {
			if (sortSpecs->SpecsCount > 0) {
				const auto& spec = sortSpecs->Specs[0];
				bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
				std::sort(sortedFw.begin(), sortedFw.end(),
					[&spec, ascending](const ATFirmwareInfo *a, const ATFirmwareInfo *b) {
						int cmp = 0;
						switch (spec.ColumnIndex) {
							case 0: cmp = a->mName.comparei(b->mName); break;
							case 1: cmp = strcmp(ATGetFirmwareTypeDisplayName(a->mType),
								ATGetFirmwareTypeDisplayName(b->mType)); break;
							case 2: cmp = a->mPath.comparei(b->mPath); break;
						}
						return ascending ? cmp < 0 : cmp > 0;
					});
			}
		}

		for (const ATFirmwareInfo *fw : sortedFw) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			VDStringA u8name = VDTextWToU8(fw->mName);
			bool selected = (s_fwSelectedId == fw->mId);

			if (ImGui::Selectable(u8name.c_str(), selected,
				ImGuiSelectableFlags_SpanAllColumns))
			{
				s_fwSelectedId = fw->mId;
			}

			ImGui::TableNextColumn();
			ImGui::Text("%s", ATGetFirmwareTypeDisplayName(fw->mType));

			ImGui::TableNextColumn();
			VDStringA u8path = VDTextWToU8(fw->mPath);
			ImGui::Text("%s", u8path.c_str());
		}

		ImGui::EndTable();
	}

	// Defaults section
	ImGui::Separator();
	ImGui::Text("Default Firmware:");

	struct DefaultEntry {
		const char *label;
		ATFirmwareType type;
	};

	static const DefaultEntry kDefaults[] = {
		{ "400/800 Kernel", kATFirmwareType_Kernel800_OSB },
		{ "XL/XE Kernel",   kATFirmwareType_KernelXL },
		{ "BASIC",          kATFirmwareType_Basic },
		{ "5200 OS",        kATFirmwareType_Kernel5200 },
		{ "XEGS OS",        kATFirmwareType_KernelXEGS },
	};

	for (const DefaultEntry& def : kDefaults) {
		uint64 currentDefault = fwMgr ? fwMgr->GetDefaultFirmware(def.type) : 0;

		ImGui::Text("%s:", def.label);
		ImGui::SameLine(140);
		ImGui::SetNextItemWidth(300);

		// Build combo items: "Built-in" + matching firmware
		char comboId[64];
		snprintf(comboId, sizeof(comboId), "##fwdef_%u", (unsigned)def.type);

		// Find current display name
		const char *currentName = "Built-in HLE";
		for (const ATFirmwareInfo& fw : s_fwList) {
			if (fw.mId == currentDefault) {
				// Use a static buffer for the preview
				static VDStringA s_previewBuf;
				s_previewBuf = VDTextWToU8(fw.mName);
				currentName = s_previewBuf.c_str();
				break;
			}
		}

		if (ImGui::BeginCombo(comboId, currentName)) {
			// Built-in option
			if (ImGui::Selectable("Built-in HLE", currentDefault == 0)) {
				if (fwMgr) {
					fwMgr->SetDefaultFirmware(def.type, 0);
					s_fwDefaultsChanged = true;
				}
			}

			// Matching firmware entries, sorted alphabetically
			vdvector<const ATFirmwareInfo *> matchingFw;
			for (const ATFirmwareInfo& fw : s_fwList) {
				if (fw.mType == def.type && fw.mbVisible)
					matchingFw.push_back(&fw);
			}
			std::sort(matchingFw.begin(), matchingFw.end(),
				[](const ATFirmwareInfo *a, const ATFirmwareInfo *b) {
					return a->mName.comparei(b->mName) < 0;
				});

			for (const ATFirmwareInfo *fw : matchingFw) {
				VDStringA u8name = VDTextWToU8(fw->mName);
				bool isSelected = (fw->mId == currentDefault);

				if (ImGui::Selectable(u8name.c_str(), isSelected)) {
					if (fwMgr) {
						fwMgr->SetDefaultFirmware(def.type, fw->mId);
						s_fwDefaultsChanged = true;
					}
				}
			}

			ImGui::EndCombo();
		}
	}

	ImGui::End();
}

// ============= Audio Options Window =============

static void DrawAudioOptions() {
	if (!s_showAudioOptions)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Audio Options", &s_showAudioOptions)) {
		ImGui::End();
		return;
	}

	IATAudioOutput *audioOut = g_sim.GetAudioOutput();

	// Volume controls
	if (ImGui::CollapsingHeader("Volume", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (audioOut) {
			float masterVol = audioOut->GetVolume();
			ImGui::Text("Master Volume:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderFloat("##mastervol", &masterVol, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
				audioOut->SetVolume(masterVol);
			}

			bool mute = audioOut->GetMute();
			ImGui::SameLine();
			if (ImGui::Checkbox("Mute", &mute))
				audioOut->SetMute(mute);

			float driveVol = audioOut->GetMixLevel(kATAudioMix_Drive);
			ImGui::Text("Drive Volume:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderFloat("##drivevol", &driveVol, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
				audioOut->SetMixLevel(kATAudioMix_Drive, driveVol);
			}

			float covoxVol = audioOut->GetMixLevel(kATAudioMix_Covox);
			ImGui::Text("Covox Volume:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderFloat("##covoxvol", &covoxVol, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
				audioOut->SetMixLevel(kATAudioMix_Covox, covoxVol);
			}
		} else {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Audio output not available");
		}
	}

	// Latency
	if (ImGui::CollapsingHeader("Buffering")) {
		if (audioOut) {
			int latency = audioOut->GetLatency();
			ImGui::Text("Latency:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderInt("##latency", &latency, 10, 500, "%d ms")) {
				audioOut->SetLatency(latency);
			}

			int extraBuf = audioOut->GetExtraBuffer();
			ImGui::Text("Extra Buffer:");
			ImGui::SameLine(140);
			ImGui::SetNextItemWidth(200);
			if (ImGui::SliderInt("##extrabuf", &extraBuf, 20, 500, "%d ms")) {
				audioOut->SetExtraBuffer(extraBuf);
			}
		}
	}

	// Drive sounds
	if (ImGui::CollapsingHeader("Drive Sounds", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool driveSounds = g_sim.GetDiskInterface(0).AreDriveSoundsEnabled();
		if (ImGui::Checkbox("Enable drive sounds", &driveSounds)) {
			for (int i = 0; i < 15; ++i)
				g_sim.GetDiskInterface(i).SetDriveSoundsEnabled(driveSounds);
		}
	}

	// POKEY options
	if (ImGui::CollapsingHeader("POKEY Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		ATPokeyEmulator& pokey = g_sim.GetPokey();

		bool dualPokey = g_sim.IsDualPokeysEnabled();
		if (ImGui::Checkbox("Dual POKEY (stereo)", &dualPokey))
			g_sim.SetDualPokeysEnabled(dualPokey);

		bool stereoMono = pokey.IsStereoAsMonoEnabled();
		if (ImGui::Checkbox("Downmix stereo to mono", &stereoMono))
			pokey.SetStereoAsMonoEnabled(stereoMono);

		bool nonlinear = pokey.IsNonlinearMixingEnabled();
		if (ImGui::Checkbox("Non-linear mixing", &nonlinear))
			pokey.SetNonlinearMixingEnabled(nonlinear);

		bool speakerFilter = pokey.IsSpeakerFilterEnabled();
		if (ImGui::Checkbox("Speaker filter", &speakerFilter))
			pokey.SetSpeakerFilterEnabled(speakerFilter);

		bool serialNoise = pokey.IsSerialNoiseEnabled();
		if (ImGui::Checkbox("Serial noise", &serialNoise))
			pokey.SetSerialNoiseEnabled(serialNoise);
	}

	// Audio status
	if (ImGui::CollapsingHeader("Audio Status")) {
		if (audioOut) {
			ATUIAudioStatus status = audioOut->GetAudioStatus();

			ImGui::Text("Sampling Rate:  %.1f Hz", status.mSamplingRate);
			ImGui::Text("Incoming Rate:  %.1f Hz", status.mIncomingRate);
			ImGui::Text("Expected Rate:  %.1f Hz", status.mExpectedRate);
			ImGui::Text("Buffer Range:   %d - %d (target: %d - %d)",
				status.mMeasuredMin, status.mMeasuredMax,
				status.mTargetMin, status.mTargetMax);
			ImGui::Text("Underflows: %d  Overflows: %d  Drops: %d",
				status.mUnderflowCount, status.mOverflowCount, status.mDropCount);
			ImGui::Text("Stereo Mixing:  %s", status.mbStereoMixing ? "Yes" : "No");
		}
	}

	ImGui::End();
}

// ============= Disk Explorer =============

static void DiskExplorerRefresh() {
	s_diskEntries.clear();
	s_diskSelectedIdx = -1;
	s_diskSelection.clear();
	s_diskShiftAnchor = -1;
	if (!s_diskFS)
		return;

	ATDiskFSEntryInfo info;
	ATDiskFSFindHandle h = s_diskFS->FindFirst(s_diskCurDir, info);
	if (h != ATDiskFSFindHandle::Invalid) {
		do {
			s_diskEntries.push_back(info);
		} while (s_diskFS->FindNext(h, info));
		s_diskFS->FindEnd(h);
	}

	// Sort: directories first, then alphabetical
	std::sort(s_diskEntries.begin(), s_diskEntries.end(),
		[](const ATDiskFSEntryInfo& a, const ATDiskFSEntryInfo& b) {
			if (a.mbIsDirectory != b.mbIsDirectory)
				return a.mbIsDirectory > b.mbIsDirectory;
			return a.mFileName < b.mFileName;
		});
}

// Convert a host file path to an Atari-compatible 8.3 filename.
static VDStringA SanitizeAtari83Name(const wchar_t *hostPath) {
	const wchar_t *fname = VDFileSplitPath(hostPath);
	VDStringA u8name = VDTextWToU8(VDStringW(fname));

	const char *nameStr = u8name.c_str();
	const char *dotPtr = strrchr(nameStr, '.');
	VDStringA basePart, extPart;
	if (dotPtr) {
		basePart = VDStringA(nameStr, dotPtr);
		extPart = VDStringA(dotPtr + 1);
	} else {
		basePart = u8name;
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
	sanitize(basePart);
	sanitize(extPart);

	if (basePart.size() > 8)
		basePart.resize(8);
	if (extPart.size() > 3)
		extPart.resize(3);

	if (basePart.empty())
		basePart = "FILE";

	if (!extPart.empty())
		return basePart + "." + extPart;
	return basePart;
}

static void DiskExplorerClose() {
	if (s_diskFS) {
		try { s_diskFS->Flush(); } catch (...) {}
		s_diskFS.reset();
	}
	s_diskStandaloneImage.clear();
	s_diskExplorerTitle.clear();
	s_diskEntries.clear();
	s_diskDirStack.clear();
	s_diskSelectedIdx = -1;
	s_diskSelection.clear();
	s_diskShiftAnchor = -1;
	s_diskCurDir = ATDiskFSKey::None;
	s_showDiskExplorer = false;
}

bool ATImGuiIsDiskExplorerActive() {
	return s_showDiskExplorer && s_diskFS && !s_diskReadOnly;
}

void ATImGuiDiskExplorerImportFile(const wchar_t *hostPath) {
	if (!s_diskFS || s_diskReadOnly)
		return;

	try {
		VDFile f(hostPath, nsVDFile::kRead | nsVDFile::kOpenExisting);
		sint64 len = f.size();
		if (len > 0x1000000) {
			ShowToast("File too large (>16MB)");
			return;
		}
		vdfastvector<uint8> data((size_t)len);
		if (len > 0)
			f.read(data.data(), (long)len);
		f.close();

		VDStringA atariName = SanitizeAtari83Name(hostPath);

		if (s_diskTextMode) {
			for (auto& b : data) {
				if (b == 0x0A)
					b = 0x9B;
			}
		}

		s_diskFS->WriteFile(s_diskCurDir, atariName.c_str(), data.data(), (uint32)data.size());
		s_diskFS->Flush();
		DiskExplorerRefresh();

		char msg[256];
		snprintf(msg, sizeof(msg), "Imported: %s", atariName.c_str());
		ShowToast(msg);
	} catch (const std::exception& e) {
		char msg[256];
		snprintf(msg, sizeof(msg), "Import failed: %s", e.what());
		ShowToast(msg);
	}
}

void ATImGuiOpenDiskExplorer(IATDiskImage *image, const wchar_t *imageName, bool readOnly) {
	if (!image)
		return;

	DiskExplorerClose();

	try {
		s_diskFS = ATDiskMountImage(image, readOnly);

		ATDiskFSValidationReport report;
		s_diskFS->Validate(report);
		s_diskReadOnly = readOnly;
		if (report.IsSerious()) {
			s_diskReadOnly = true;
			s_diskFS->SetReadOnly(true);
			ShowToast("Filesystem errors detected - mounted read-only");
		}

		ATDiskFSInfo fsInfo;
		s_diskFS->GetInfo(fsInfo);
		char infoStr[128];
		snprintf(infoStr, sizeof(infoStr), "%s  |  %u free blocks (%u bytes/block)",
			fsInfo.mFSType.c_str(), fsInfo.mFreeBlocks, fsInfo.mBlockSize);
		s_diskFSInfoStr = infoStr;

		if (imageName) {
			VDStringA titleU8 = VDTextWToU8(VDStringW(imageName));
			s_diskExplorerTitle.assign(titleU8.c_str(), titleU8.size());
		}

		s_diskExplorerSlot = -1;
		s_diskEntries.clear();
		s_diskDirStack.clear();
		s_diskCurDir = ATDiskFSKey::None;
		s_diskSelectedIdx = -1;
		s_diskSelection.clear();
		s_diskShiftAnchor = -1;
		s_showDiskExplorer = true;
	} catch (const std::exception& e) {
		s_diskFS.reset();
		char msg[256];
		snprintf(msg, sizeof(msg), "Open disk failed: %s", e.what());
		ShowToast(msg);
	}
}

static void DrawDiskExplorer() {
	if (!s_showDiskExplorer)
		return;

	if (!s_diskFS) {
		s_showDiskExplorer = false;
		return;
	}

	// First open: refresh listing
	if (s_diskEntries.empty() && s_diskSelectedIdx == -1)
		DiskExplorerRefresh();

	char title[256];
	if (s_diskExplorerSlot >= 0)
		snprintf(title, sizeof(title), "Disk Explorer - D%d###diskexp", s_diskExplorerSlot + 1);
	else if (!s_diskExplorerTitle.empty())
		snprintf(title, sizeof(title), "Disk Explorer - %s###diskexp", s_diskExplorerTitle.c_str());
	else
		snprintf(title, sizeof(title), "Disk Explorer###diskexp");

	ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_FirstUseEver);
	bool open = true;
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		if (!open)
			DiskExplorerClose();
		return;
	}

	if (!open) {
		ImGui::End();
		DiskExplorerClose();
		return;
	}

	// FS info bar
	if (!s_diskFSInfoStr.empty()) {
		ImGui::TextDisabled("%s", s_diskFSInfoStr.c_str());
		if (s_diskReadOnly) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "[READ ONLY]");
		}
	}

	// Toolbar
	bool canUp = !s_diskDirStack.empty();
	if (ImGui::Button("Up") && canUp) {
		s_diskCurDir = s_diskDirStack.back();
		s_diskDirStack.pop_back();
		DiskExplorerRefresh();
	}

	ImGui::SameLine();
	bool hasSel = !s_diskSelection.empty();
	bool selIsFile = hasSel && s_diskSelectedIdx >= 0 && s_diskSelectedIdx < (int)s_diskEntries.size()
		&& !s_diskEntries[s_diskSelectedIdx].mbIsDirectory;

	// Count selected files (non-directories) for bulk ops
	int selFileCount = 0;
	for (int idx : s_diskSelection) {
		if (idx >= 0 && idx < (int)s_diskEntries.size() && !s_diskEntries[idx].mbIsDirectory)
			++selFileCount;
	}

	if (ImGui::Button("Extract") && selIsFile) {
		const ATDiskFSEntryInfo& entry = s_diskEntries[s_diskSelectedIdx];
		VDStringW savePath = ATLinuxSaveFileDialog("Extract File", "All Files|*");
		if (!savePath.empty()) {
			try {
				vdfastvector<uint8> data;
				s_diskFS->ReadFile(entry.mKey, data);
				if (s_diskTextMode) {
					for (auto& b : data) {
						if (b == 0x9B)
							b = 0x0A;
					}
				}
				VDFile f(savePath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways);
				if (!data.empty())
					f.write(data.data(), (long)data.size());
				f.close();
				ShowToast("File extracted");
			} catch (const std::exception& e) {
				char msg[256];
				snprintf(msg, sizeof(msg), "Extract failed: %s", e.what());
				ShowToast(msg);
			}
		}
	}

	// Bulk extract: extract all selected files to a directory
	if (selFileCount > 1) {
		ImGui::SameLine();
		char bulkLabel[32];
		snprintf(bulkLabel, sizeof(bulkLabel), "Extract %d", selFileCount);
		if (ImGui::Button(bulkLabel)) {
			VDStringW dirPath = ATLinuxOpenFileDialog("Select Directory for Extraction", "All Files|*");
			if (!dirPath.empty()) {
				// Use the selected path as a directory (strip filename if one was selected)
				VDStringW dir = dirPath;
				int extracted = 0, failed = 0;
				for (int idx : s_diskSelection) {
					if (idx < 0 || idx >= (int)s_diskEntries.size() || s_diskEntries[idx].mbIsDirectory)
						continue;
					const ATDiskFSEntryInfo& e = s_diskEntries[idx];
					try {
						vdfastvector<uint8> data;
						s_diskFS->ReadFile(e.mKey, data);
						if (s_diskTextMode) {
							for (auto& b : data) {
								if (b == 0x9B) b = 0x0A;
							}
						}
						VDStringW filePath = dir;
						filePath += L'/';
						filePath += VDTextU8ToW(VDStringA(e.mFileName.c_str()));
						VDFile f(filePath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways);
						if (!data.empty())
							f.write(data.data(), (long)data.size());
						f.close();
						++extracted;
					} catch (...) {
						++failed;
					}
				}
				char msg[128];
				if (failed)
					snprintf(msg, sizeof(msg), "Extracted %d file(s), %d failed", extracted, failed);
				else
					snprintf(msg, sizeof(msg), "Extracted %d file(s)", extracted);
				ShowToast(msg);
			}
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Import") && !s_diskReadOnly) {
		VDStringW importPath = ATLinuxOpenFileDialog("Import File", "All Files|*");
		if (!importPath.empty()) {
			try {
				VDFile f(importPath.c_str(), nsVDFile::kRead | nsVDFile::kOpenExisting);
				sint64 len = f.size();
				if (len > 0x1000000) {
					ShowToast("File too large (>16MB)");
				} else {
					vdfastvector<uint8> data((size_t)len);
					if (len > 0)
						f.read(data.data(), (long)len);
					f.close();

					VDStringA atariName = SanitizeAtari83Name(importPath.c_str());

					if (s_diskTextMode) {
						for (auto& b : data) {
							if (b == 0x0A)
								b = 0x9B;
						}
					}

					s_diskFS->WriteFile(s_diskCurDir, atariName.c_str(), data.data(), (uint32)data.size());
					s_diskFS->Flush();
					DiskExplorerRefresh();
					ShowToast("File imported");
				}
			} catch (const std::exception& e) {
				char msg[256];
				snprintf(msg, sizeof(msg), "Import failed: %s", e.what());
				ShowToast(msg);
			}
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Import Files...") && !s_diskReadOnly) {
		auto importPaths = ATLinuxOpenMultiFileDialog("Import Files", "All Files|*");
		if (!importPaths.empty()) {
			int imported = 0, failed = 0;
			for (const auto& ip : importPaths) {
				try {
					VDFile f(ip.c_str(), nsVDFile::kRead | nsVDFile::kOpenExisting);
					sint64 len = f.size();
					if (len > 0x1000000) {
						++failed;
						continue;
					}
					vdfastvector<uint8> data((size_t)len);
					if (len > 0)
						f.read(data.data(), (long)len);
					f.close();

					VDStringA atariName = SanitizeAtari83Name(ip.c_str());

					if (s_diskTextMode) {
						for (auto& b : data) {
							if (b == 0x0A)
								b = 0x9B;
						}
					}

					s_diskFS->WriteFile(s_diskCurDir, atariName.c_str(), data.data(), (uint32)data.size());
					++imported;
				} catch (...) {
					++failed;
				}
			}
			if (imported > 0) {
				s_diskFS->Flush();
				DiskExplorerRefresh();
			}
			char msg[128];
			if (failed)
				snprintf(msg, sizeof(msg), "Imported %d file(s), %d failed", imported, failed);
			else
				snprintf(msg, sizeof(msg), "Imported %d file(s)", imported);
			ShowToast(msg);
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Delete") && hasSel && !s_diskReadOnly) {
		ImGui::OpenPopup("Confirm Delete");
	}

	ImGui::SameLine();
	if (ImGui::Button("Rename") && selIsFile && !s_diskReadOnly) {
		const ATDiskFSEntryInfo& entry = s_diskEntries[s_diskSelectedIdx];
		strncpy(s_diskRenameBuffer, entry.mFileName.c_str(), sizeof(s_diskRenameBuffer) - 1);
		s_diskRenameBuffer[sizeof(s_diskRenameBuffer) - 1] = 0;
		s_diskRenameActive = true;
		ImGui::OpenPopup("Rename File");
	}

	ImGui::SameLine();
	if (ImGui::Button("New Dir") && !s_diskReadOnly) {
		ImGui::OpenPopup("New Directory");
	}

	ImGui::SameLine();
	ImGui::Checkbox("Text Mode", &s_diskTextMode);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Convert Atari EOL (0x9B) <-> Unix LF (0x0A)\non extract/import");

	// Delete confirmation popup
	if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (s_diskSelection.size() == 1) {
			int idx = *s_diskSelection.begin();
			if (idx >= 0 && idx < (int)s_diskEntries.size())
				ImGui::Text("Delete \"%s\"?", s_diskEntries[idx].mFileName.c_str());
		} else if (s_diskSelection.size() > 1) {
			ImGui::Text("Delete %d selected item(s)?", (int)s_diskSelection.size());
		}

		if (ImGui::Button("Delete", ImVec2(80, 0))) {
			int deleted = 0, failed = 0;
			// Delete in reverse order to preserve indices during iteration
			for (auto it = s_diskSelection.rbegin(); it != s_diskSelection.rend(); ++it) {
				int idx = *it;
				if (idx < 0 || idx >= (int)s_diskEntries.size())
					continue;
				try {
					s_diskFS->DeleteFile(s_diskEntries[idx].mKey);
					++deleted;
				} catch (...) {
					++failed;
				}
			}
			if (deleted > 0) {
				try { s_diskFS->Flush(); } catch (...) {}
				DiskExplorerRefresh();
			}
			char msg[128];
			if (failed)
				snprintf(msg, sizeof(msg), "Deleted %d, %d failed", deleted, failed);
			else if (deleted == 1)
				snprintf(msg, sizeof(msg), "File deleted");
			else
				snprintf(msg, sizeof(msg), "Deleted %d item(s)", deleted);
			ShowToast(msg);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0)))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	// Rename popup
	if (ImGui::BeginPopupModal("Rename File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::SetNextItemWidth(200);
		bool apply = ImGui::InputText("##rename", s_diskRenameBuffer, sizeof(s_diskRenameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);

		if (apply || ImGui::Button("OK", ImVec2(80, 0))) {
			if (hasSel && s_diskRenameBuffer[0]) {
				try {
					s_diskFS->RenameFile(s_diskEntries[s_diskSelectedIdx].mKey, s_diskRenameBuffer);
					s_diskFS->Flush();
					DiskExplorerRefresh();
					ShowToast("File renamed");
				} catch (const std::exception& e) {
					char msg[256];
					snprintf(msg, sizeof(msg), "Rename failed: %s", e.what());
					ShowToast(msg);
				}
			}
			s_diskRenameActive = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			s_diskRenameActive = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	// New directory popup
	static char s_newDirName[64] = {};
	if (ImGui::BeginPopupModal("New Directory", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::SetNextItemWidth(200);
		bool apply = ImGui::InputText("Name##newdir", s_newDirName, sizeof(s_newDirName),
			ImGuiInputTextFlags_EnterReturnsTrue);

		if (apply || ImGui::Button("Create", ImVec2(80, 0))) {
			if (s_newDirName[0]) {
				try {
					s_diskFS->CreateDir(s_diskCurDir, s_newDirName);
					s_diskFS->Flush();
					DiskExplorerRefresh();
					ShowToast("Directory created");
				} catch (const std::exception& e) {
					char msg[256];
					snprintf(msg, sizeof(msg), "Create dir failed: %s", e.what());
					ShowToast(msg);
				}
			}
			s_newDirName[0] = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			s_newDirName[0] = 0;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	ImGui::Separator();

	// Ctrl+A: select all
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
	{
		s_diskSelection.clear();
		for (int i = 0; i < (int)s_diskEntries.size(); ++i)
			s_diskSelection.insert(i);
	}

	// File listing table
	if (ImGui::BeginTable("##diskfiles", 4,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, -ImGui::GetFrameHeightWithSpacing())))
	{
		ImGui::TableSetupColumn("Name", 0, 3.0f);
		ImGui::TableSetupColumn("Sectors", 0, 1.0f);
		ImGui::TableSetupColumn("Bytes", 0, 1.0f);
		ImGui::TableSetupColumn("Date", 0, 2.0f);
		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)s_diskEntries.size(); ++i) {
			const ATDiskFSEntryInfo& entry = s_diskEntries[i];

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			char nameStr[64];
			if (entry.mbIsDirectory)
				snprintf(nameStr, sizeof(nameStr), "[%s]", entry.mFileName.c_str());
			else
				snprintf(nameStr, sizeof(nameStr), "%s", entry.mFileName.c_str());

			bool selected = s_diskSelection.count(i) > 0;
			if (ImGui::Selectable(nameStr, selected,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
			{
				ImGuiIO& io = ImGui::GetIO();

				if (io.KeyShift && s_diskShiftAnchor >= 0) {
					// Shift+click: range select from anchor
					s_diskSelection.clear();
					int lo = std::min(s_diskShiftAnchor, i);
					int hi = std::max(s_diskShiftAnchor, i);
					for (int j = lo; j <= hi; ++j)
						s_diskSelection.insert(j);
				} else if (io.KeyCtrl) {
					// Ctrl+click: toggle individual item
					if (selected)
						s_diskSelection.erase(i);
					else
						s_diskSelection.insert(i);
					s_diskShiftAnchor = i;
				} else {
					// Plain click: single select
					s_diskSelection.clear();
					s_diskSelection.insert(i);
					s_diskShiftAnchor = i;
				}
				s_diskSelectedIdx = i;

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && entry.mbIsDirectory) {
					s_diskDirStack.push_back(s_diskCurDir);
					s_diskCurDir = entry.mKey;
					DiskExplorerRefresh();
				}
			}

			// Right-click context menu
			if (ImGui::BeginPopupContextItem()) {
				s_diskSelectedIdx = i;
				if (entry.mbIsDirectory) {
					if (ImGui::MenuItem("Open")) {
						s_diskDirStack.push_back(s_diskCurDir);
						s_diskCurDir = entry.mKey;
						DiskExplorerRefresh();
					}
				}
				if (!entry.mbIsDirectory && ImGui::MenuItem("Extract")) {
					VDStringW savePath = ATLinuxSaveFileDialog("Extract File", "All Files|*");
					if (!savePath.empty()) {
						try {
							vdfastvector<uint8> data;
							s_diskFS->ReadFile(entry.mKey, data);
							if (s_diskTextMode) {
								for (auto& b : data) {
									if (b == 0x9B)
										b = 0x0A;
								}
							}
							VDFile f(savePath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways);
							if (!data.empty())
								f.write(data.data(), (long)data.size());
							f.close();
							ShowToast("File extracted");
						} catch (const std::exception& e) {
							char msg[256];
							snprintf(msg, sizeof(msg), "Extract failed: %s", e.what());
							ShowToast(msg);
						}
					}
				}
				if (!entry.mbIsDirectory && !s_diskReadOnly && ImGui::MenuItem("Rename")) {
					strncpy(s_diskRenameBuffer, entry.mFileName.c_str(), sizeof(s_diskRenameBuffer) - 1);
					s_diskRenameBuffer[sizeof(s_diskRenameBuffer) - 1] = 0;
					s_diskRenameActive = true;
					ImGui::OpenPopup("Rename File");
				}
				if (!s_diskReadOnly && ImGui::MenuItem("Delete")) {
					ImGui::OpenPopup("Confirm Delete");
				}
				ImGui::EndPopup();
			}

			ImGui::TableNextColumn();
			ImGui::Text("%u", entry.mSectors);

			ImGui::TableNextColumn();
			ImGui::Text("%u", entry.mBytes);

			ImGui::TableNextColumn();
			if (entry.mbDateValid) {
				ImGui::Text("%04u-%02u-%02u %02u:%02u",
					entry.mDate.mYear, entry.mDate.mMonth, entry.mDate.mDay,
					entry.mDate.mHour, entry.mDate.mMinute);
			} else {
				ImGui::TextDisabled("---");
			}
		}

		ImGui::EndTable();
	}

	// Status bar
	ImGui::Text("%d items", (int)s_diskEntries.size());

	ImGui::End();
}

// ============= Device Configuration Window =============

// Generic property set editor - renders ImGui controls for each property
static bool DrawPropertySetEditor(ATPropertySet& props) {
	bool changed = false;

	struct PropEntry {
		std::string name;
		ATPropertyType type;
		ATPropertyValue value;
	};

	// Collect properties into a sortable list
	std::vector<PropEntry> entries;
	props.EnumProperties([&](const char *name, const ATPropertyValue& val) {
		PropEntry e;
		e.name = name;
		e.type = val.mType;
		e.value = val;
		// For string type, deep copy
		if (val.mType == kATPropertyType_String16 && val.mValStr16) {
			// Store pointer directly; it's valid for the editor's lifetime
			e.value.mValStr16 = val.mValStr16;
		}
		entries.push_back(std::move(e));
	});

	// Sort alphabetically
	std::sort(entries.begin(), entries.end(), [](const PropEntry& a, const PropEntry& b) {
		return a.name < b.name;
	});

	for (const PropEntry& e : entries) {
		ImGui::PushID(e.name.c_str());

		switch (e.type) {
			case kATPropertyType_Bool: {
				bool val = e.value.mValBool;
				if (ImGui::Checkbox(e.name.c_str(), &val)) {
					props.SetBool(e.name.c_str(), val);
					changed = true;
				}
				break;
			}

			case kATPropertyType_Int32: {
				int val = (int)e.value.mValI32;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt(e.name.c_str(), &val)) {
					props.SetInt32(e.name.c_str(), (sint32)val);
					changed = true;
				}
				break;
			}

			case kATPropertyType_Uint32: {
				int val = (int)e.value.mValU32;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt(e.name.c_str(), &val)) {
					if (val >= 0) {
						props.SetUint32(e.name.c_str(), (uint32)val);
						changed = true;
					}
				}
				break;
			}

			case kATPropertyType_Float: {
				float val = e.value.mValF;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputFloat(e.name.c_str(), &val, 0, 0, "%.3f")) {
					props.SetFloat(e.name.c_str(), val);
					changed = true;
				}
				break;
			}

			case kATPropertyType_Double: {
				float val = (float)e.value.mValD;
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputFloat(e.name.c_str(), &val, 0, 0, "%.6f")) {
					props.SetDouble(e.name.c_str(), (double)val);
					changed = true;
				}
				break;
			}

			case kATPropertyType_String16: {
				VDStringA u8val;
				if (e.value.mValStr16)
					u8val = VDTextWToU8(VDStringW(e.value.mValStr16));

				char buf[512];
				strncpy(buf, u8val.c_str(), sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = 0;

				ImGui::SetNextItemWidth(300);
				if (ImGui::InputText(e.name.c_str(), buf, sizeof(buf))) {
					VDStringW wstr = VDTextU8ToW(VDStringA(buf));
					props.SetString(e.name.c_str(), wstr.c_str());
					changed = true;
				}
				break;
			}

			default:
				ImGui::Text("%s: <unsupported type>", e.name.c_str());
				break;
		}

		ImGui::PopID();
	}

	if (entries.empty()) {
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No configurable properties.");
	}

	return changed;
}

static void DrawDeviceManager() {
	if (!s_showDeviceManager)
		return;

	ATDeviceManager *devMgr = g_sim.GetDeviceManager();
	if (!devMgr) {
		s_showDeviceManager = false;
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Device Manager", &s_showDeviceManager)) {
		ImGui::End();
		return;
	}

	// Toolbar
	if (ImGui::Button("Add...")) {
		s_devShowAddPopup = true;
		s_devAddSelectedIdx = -1;
		ImGui::OpenPopup("Add Device");
	}

	ImGui::SameLine();

	if (ImGui::Button("Configure...") && s_devSelectedDevice) {
		ATDeviceInfo info;
		s_devSelectedDevice->GetDeviceInfo(info);
		s_devEditTag = info.mpDef ? info.mpDef->mpTag : "";
		s_devEditProps.Clear();
		s_devSelectedDevice->GetSettings(s_devEditProps);
		s_devShowConfig = true;
	}

	ImGui::SameLine();

	if (ImGui::Button("Remove") && s_devSelectedDevice) {
		devMgr->RemoveDevice(s_devSelectedDevice);
		s_devSelectedDevice = nullptr;
		s_devShowConfig = false;
	}

	// Add Device popup
	if (ImGui::BeginPopupModal("Add Device", &s_devShowAddPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
		const auto& defs = devMgr->GetDeviceDefinitions();

		ImGui::Text("Select device type:");

		// Build sorted index of visible devices
		vdfastvector<int> sortedIndices;
		for (int i = 0; i < (int)defs.size(); ++i) {
			const ATDeviceDefinition *def = defs[i];
			if (def->mFlags & (kATDeviceDefFlag_Internal | kATDeviceDefFlag_Hidden))
				continue;
			sortedIndices.push_back(i);
		}
		std::sort(sortedIndices.begin(), sortedIndices.end(), [&defs](int a, int b) {
			return wcscmp(defs[a]->mpName, defs[b]->mpName) < 0;
		});

		if (ImGui::BeginListBox("##devlist", ImVec2(400, 250))) {
			for (int idx : sortedIndices) {
				const ATDeviceDefinition *def = defs[idx];
				VDStringA u8name = VDTextWToU8(VDStringW(def->mpName));
				if (ImGui::Selectable(u8name.c_str(), s_devAddSelectedIdx == idx))
					s_devAddSelectedIdx = idx;
			}
			ImGui::EndListBox();
		}

		if (ImGui::Button("Add", ImVec2(80, 0)) && s_devAddSelectedIdx >= 0
			&& s_devAddSelectedIdx < (int)defs.size()) {
			const ATDeviceDefinition *def = defs[s_devAddSelectedIdx];
			ATPropertySet props;
			try {
				devMgr->AddDevice(def->mpTag, props);
			} catch (const MyError& e) {
				extern void ATUIShowError(const VDException& e);
				ATUIShowError(e);
			}
			s_devShowAddPopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			s_devShowAddPopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	// Device list
	ImGui::Separator();

	uint32 devCount = devMgr->GetDeviceCount();

	if (ImGui::BeginTable("##devtable", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, 200)))
	{
		ImGui::TableSetupColumn("Device", 0, 2.0f);
		ImGui::TableSetupColumn("Type", 0, 1.5f);
		ImGui::TableSetupColumn("Settings", 0, 2.0f);
		ImGui::TableHeadersRow();

		for (uint32 i = 0; i < devCount; ++i) {
			IATDevice *dev = devMgr->GetDeviceByIndex(i);
			if (!dev)
				continue;

			ATDeviceInfo info;
			dev->GetDeviceInfo(info);

			VDStringW blurb;
			dev->GetSettingsBlurb(blurb);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			const wchar_t *devName = info.mpDef ? info.mpDef->mpName : L"Unknown";
			VDStringA u8name = VDTextWToU8(VDStringW(devName));
			bool selected = (s_devSelectedDevice == dev);

			if (ImGui::Selectable(u8name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
				s_devSelectedDevice = dev;

			ImGui::TableNextColumn();
			ImGui::Text("%s", info.mpDef ? info.mpDef->mpTag : "?");

			ImGui::TableNextColumn();
			VDStringA u8blurb = VDTextWToU8(blurb);
			ImGui::Text("%s", u8blurb.c_str());
		}

		ImGui::EndTable();
	}

	// Device configuration editor (inline)
	if (s_devShowConfig && s_devSelectedDevice) {
		ImGui::Separator();

		ATDeviceInfo info;
		s_devSelectedDevice->GetDeviceInfo(info);
		const wchar_t *devName = info.mpDef ? info.mpDef->mpName : L"Device";
		VDStringA u8name = VDTextWToU8(VDStringW(devName));

		const DevCfgTagMapping *mapping = FindDevCfgMapping(s_devEditTag.c_str());

		if (mapping)
			ImGui::Text("Configure: %s", mapping->title);
		else
			ImGui::Text("Configure: %s", u8name.c_str());

		ImGui::BeginChild("##devcfg", ImVec2(0, 180), ImGuiChildFlags_Borders);
		if (mapping)
			DrawStructuredDeviceConfig(s_devEditProps, *mapping);
		else
			DrawPropertySetEditor(s_devEditProps);
		ImGui::EndChild();

		if (ImGui::Button("Apply")) {
			devMgr->ReconfigureDevice(*s_devSelectedDevice, s_devEditProps);
			s_devShowConfig = false;
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
			s_devShowConfig = false;
	}

	ImGui::End();
}

// ============= Keyboard Configuration Window =============

static void DrawKeyboardConfig() {
	if (!s_showKeyboardConfig)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Keyboard Settings", &s_showKeyboardConfig)) {
		ImGui::End();
		return;
	}

	// Layout mode
	if (ImGui::CollapsingHeader("Layout Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
		int layoutMode = (int)g_kbdOpts.mLayoutMode;
		bool changed = false;

		changed |= ImGui::RadioButton("Natural", &layoutMode, ATUIKeyboardOptions::kLM_Natural);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Direct (Raw)", &layoutMode, ATUIKeyboardOptions::kLM_Raw);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Custom", &layoutMode, ATUIKeyboardOptions::kLM_Custom);

		if (changed) {
			g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)layoutMode;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		}

		ImGui::TextWrapped(
			layoutMode == ATUIKeyboardOptions::kLM_Natural
				? "Host keyboard layout mapped to logical Atari keys."
			: layoutMode == ATUIKeyboardOptions::kLM_Raw
				? "Physical scancodes map directly to Atari keyboard matrix."
				: "User-customizable per-key mappings."
		);
	}

	// Keyboard mode
	if (ImGui::CollapsingHeader("Keyboard Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
		int keyMode = g_kbdOpts.mbFullRawKeys ? 2 : (g_kbdOpts.mbRawKeys ? 1 : 0);
		bool changed = false;

		changed |= ImGui::RadioButton("Cooked", &keyMode, 0);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Raw", &keyMode, 1);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Full Raw", &keyMode, 2);

		if (changed) {
			g_kbdOpts.mbRawKeys = (keyMode >= 1);
			g_kbdOpts.mbFullRawKeys = (keyMode >= 2);
		}
	}

	// Arrow key mode
	if (ImGui::CollapsingHeader("Arrow Key Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
		int arrowMode = (int)g_kbdOpts.mArrowKeyMode;
		bool changed = false;

		changed |= ImGui::RadioButton("Invert Ctrl", &arrowMode, ATUIKeyboardOptions::kAKM_InvertCtrl);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Auto Ctrl", &arrowMode, ATUIKeyboardOptions::kAKM_AutoCtrl);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Default Ctrl", &arrowMode, ATUIKeyboardOptions::kAKM_DefaultCtrl);

		if (changed) {
			g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)arrowMode;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		}
	}

	// Toggles
	if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox("1200XL function keys (F1-F4)", &g_kbdOpts.mbEnableFunctionKeys))
			ATUIInitVirtualKeyMap(g_kbdOpts);

		ImGui::Checkbox("Allow Shift on cold reset", &g_kbdOpts.mbAllowShiftOnColdReset);
		ImGui::Checkbox("Allow input map keyboard overlap", &g_kbdOpts.mbAllowInputMapOverlap);
		ImGui::Checkbox("Allow input map modifier overlap", &g_kbdOpts.mbAllowInputMapModifierOverlap);
	}

	ImGui::End();
}

// ============= CPU & Memory Options =============

static void DrawCPUOptions() {
	if (!s_showCPUOptions)
		return;

	ImGui::SetNextWindowSize(ImVec2(380, 340), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("CPU & Memory", &s_showCPUOptions)) {
		ImGui::End();
		return;
	}

	// CPU type
	if (ImGui::CollapsingHeader("CPU", ImGuiTreeNodeFlags_DefaultOpen)) {
		int cpuMode = (int)g_sim.GetCPUMode();
		ImGui::Text("CPU Type:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(180);
		if (ImGui::Combo("##cpumode", &cpuMode, kCPUModeNames, kATCPUModeCount))
			g_sim.SetCPUMode((ATCPUMode)cpuMode, 1);

		// Profiling/verification tools
		bool profiling = g_sim.IsProfilingEnabled();
		if (ImGui::Checkbox("CPU profiling", &profiling))
			g_sim.SetProfilingEnabled(profiling);

		bool verifier = g_sim.IsVerifierEnabled();
		if (ImGui::Checkbox("CPU verifier", &verifier))
			g_sim.SetVerifierEnabled(verifier);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Enable illegal instruction and address checks");

		bool heatmap = g_sim.IsHeatMapEnabled();
		if (ImGui::Checkbox("Heat map", &heatmap))
			g_sim.SetHeatMapEnabled(heatmap);
	}

	// Memory options
	if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
		// Memory mode (also in System Settings, duplicated for convenience)
		int memIdx = MemoryModeToSortedIndex(g_sim.GetMemoryMode());
		ImGui::Text("Memory:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(180);
		if (ImGui::Combo("##memmode", &memIdx, MemoryModeComboGetter, nullptr, kATMemoryModeCount))
			ATUISwitchMemoryMode(nullptr, kSortedMemoryModes[memIdx].mode);

		// Axlon memory
		uint8 axlonBits = g_sim.GetAxlonMemoryMode();
		int axlonKB = axlonBits ? (1 << axlonBits) / 1024 * 16 : 0;
		const char *axlonNames[] = { "Disabled", "64K", "128K", "256K", "512K", "1024K", "2048K", "4096K" };
		int axlonIdx = 0;
		if (axlonBits >= 2 && axlonBits <= 8)
			axlonIdx = axlonBits - 1;

		ImGui::Text("Axlon RAM:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(180);
		if (ImGui::Combo("##axlon", &axlonIdx, axlonNames, 8))
			g_sim.SetAxlonMemoryMode(axlonIdx == 0 ? 0 : (uint8)(axlonIdx + 1));

		bool axlonAlias = g_sim.GetAxlonAliasingEnabled();
		if (ImGui::Checkbox("Axlon aliasing", &axlonAlias))
			g_sim.SetAxlonAliasingEnabled(axlonAlias);

		// High memory banks
		sint32 highBanks = g_sim.GetHighMemoryBanks();
		int highIdx = highBanks < 0 ? 0 : (highBanks == 0 ? 1 : 2 + highBanks);
		ImGui::Text("High RAM:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(180);
		const char *highNames[] = { "Auto", "None", "64K", "256K", "1024K", "4096K", "16384K", "65536K" };
		if (ImGui::Combo("##highmem", &highIdx, highNames, 8)) {
			sint32 newBanks = highIdx == 0 ? -1 : (highIdx == 1 ? 0 : highIdx - 2);
			g_sim.SetHighMemoryBanks(newBanks);
		}

		bool mapRAM = g_sim.IsMapRAMEnabled();
		if (ImGui::Checkbox("MapRAM", &mapRAM))
			g_sim.SetMapRAMEnabled(mapRAM);

		bool shadowROM = g_sim.GetShadowROMEnabled();
		if (ImGui::Checkbox("Shadow ROM", &shadowROM))
			g_sim.SetShadowROMEnabled(shadowROM);
	}

	ImGui::End();
}

// ============= Boot & Acceleration Options =============

static void DrawBootOptions() {
	if (!s_showBootOptions)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 420), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Boot & Acceleration", &s_showBootOptions)) {
		ImGui::End();
		return;
	}

	// Boot options
	if (ImGui::CollapsingHeader("Boot Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool fastBoot = g_sim.IsFastBootEnabled();
		if (ImGui::Checkbox("Fast boot", &fastBoot))
			g_sim.SetFastBootEnabled(fastBoot);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Skip memory test and OS initialization delay");

		bool selfTest = g_sim.IsForcedSelfTest();
		if (ImGui::Checkbox("Force self-test on cold reset", &selfTest))
			g_sim.SetForcedSelfTest(selfTest);

		bool kbdPresent = g_sim.IsKeyboardPresent();
		if (ImGui::Checkbox("Keyboard present", &kbdPresent))
			g_sim.SetKeyboardPresent(kbdPresent);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Force keyboard present line (needed for some 5200 games)");
	}

	// SIO acceleration
	if (ImGui::CollapsingHeader("SIO Acceleration", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool sioPatch = g_sim.IsSIOPatchEnabled();
		if (ImGui::Checkbox("SIO patch (OS acceleration)", &sioPatch))
			g_sim.SetSIOPatchEnabled(sioPatch);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Accelerate SIO transfers through OS hooks");

		bool diskSIO = g_sim.IsDiskSIOPatchEnabled();
		if (ImGui::Checkbox("Disk SIO patch", &diskSIO))
			g_sim.SetDiskSIOPatchEnabled(diskSIO);

		bool diskBurst = g_sim.GetDiskBurstTransfersEnabled();
		if (ImGui::Checkbox("Disk burst transfers", &diskBurst))
			g_sim.SetDiskBurstTransfersEnabled(diskBurst);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Use burst mode for faster disk access");

		bool diskAccurate = g_sim.IsDiskAccurateTimingEnabled();
		if (ImGui::Checkbox("Accurate disk timing", &diskAccurate))
			g_sim.SetDiskAccurateTimingEnabled(diskAccurate);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Enable cycle-accurate disk drive timing");

		bool sioOverride = g_sim.IsDiskSIOOverrideDetectEnabled();
		if (ImGui::Checkbox("SIO override detection", &sioOverride))
			g_sim.SetDiskSIOOverrideDetectEnabled(sioOverride);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Detect when programs hook SIO and disable acceleration");
	}

	// Other patches
	if (ImGui::CollapsingHeader("Acceleration Patches")) {
		bool fpPatch = g_sim.IsFPPatchEnabled();
		if (ImGui::Checkbox("Floating-point acceleration", &fpPatch))
			g_sim.SetFPPatchEnabled(fpPatch);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Accelerate math pack routines");

		bool cioPBI = g_sim.IsCIOPBIPatchEnabled();
		if (ImGui::Checkbox("CIO PBI patch", &cioPBI))
			g_sim.SetCIOPBIPatchEnabled(cioPBI);

		bool sioPBI = g_sim.IsSIOPBIPatchEnabled();
		if (ImGui::Checkbox("SIO PBI patch", &sioPBI))
			g_sim.SetSIOPBIPatchEnabled(sioPBI);

		bool devSIO = g_sim.GetDeviceSIOPatchEnabled();
		if (ImGui::Checkbox("Device SIO patch", &devSIO))
			g_sim.SetDeviceSIOPatchEnabled(devSIO);

		bool devCIOBurst = g_sim.GetDeviceCIOBurstTransfersEnabled();
		if (ImGui::Checkbox("Device CIO burst transfers", &devCIOBurst))
			g_sim.SetDeviceCIOBurstTransfersEnabled(devCIOBurst);

		bool devSIOBurst = g_sim.GetDeviceSIOBurstTransfersEnabled();
		if (ImGui::Checkbox("Device SIO burst transfers", &devSIOBurst))
			g_sim.SetDeviceSIOBurstTransfersEnabled(devSIOBurst);

		bool sectorCounter = g_sim.IsDiskSectorCounterEnabled();
		if (ImGui::Checkbox("Disk sector counter", &sectorCounter))
			g_sim.SetDiskSectorCounterEnabled(sectorCounter);
	}

	ImGui::End();
}

// ============= Input Setup Window =============

static void DrawInputSetup() {
	if (!s_showInputSetup)
		return;

	ATInputManager *inputMgr = g_sim.GetInputManager();
	if (!inputMgr)
		return;

	ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Input Setup", &s_showInputSetup)) {
		ImGui::End();
		return;
	}

	// --- Detected controllers ---
	if (ImGui::CollapsingHeader("Detected Controllers", ImGuiTreeNodeFlags_DefaultOpen)) {
		int numJoysticks = SDL_NumJoysticks();
		if (numJoysticks > 0) {
			for (int i = 0; i < numJoysticks; ++i) {
				const char *name = SDL_JoystickNameForIndex(i);
				if (SDL_IsGameController(i)) {
					ImGui::BulletText("Gamepad %d: %s", i + 1, name ? name : "(unknown)");
				} else {
					ImGui::BulletText("Joystick %d: %s", i + 1, name ? name : "(unknown)");
				}
			}
		} else {
			ImGui::TextDisabled("No joysticks/gamepads detected");
		}

		if (ImGui::Button("Rescan")) {
			IATJoystickManager *jm = g_sim.GetJoystickManager();
			if (jm)
				jm->RescanForDevices();
		}
	}

	// --- Active input maps ---
	if (ImGui::CollapsingHeader("Input Maps", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("Input maps bind host inputs (keyboard, mouse, gamepad) to Atari controllers.");
		ImGui::Spacing();

		uint32 mapCount = inputMgr->GetInputMapCount();

		if (mapCount == 0) {
			ImGui::TextDisabled("No input maps configured. Click 'Reset to Defaults' to add standard maps.");
		}

		// Scrollable list of maps
		static int s_selectedMapIdx = -1;
		if (ImGui::BeginChild("##maplist", ImVec2(0, 280), ImGuiChildFlags_Borders)) {

			for (uint32 i = 0; i < mapCount; ++i) {
				ATInputMap *imap = nullptr;
				if (!inputMgr->GetInputMapByIndex(i, &imap))
					continue;

				ImGui::PushID((int)i);

				bool enabled = inputMgr->IsInputMapEnabled(imap);
				VDStringA u8name = VDTextWToU8(VDStringW(imap->GetName()));

				// Checkbox for enable/disable
				if (ImGui::Checkbox("##en", &enabled)) {
					inputMgr->ActivateInputMap(imap, enabled);
				}
				ImGui::SameLine();

				// Build inline summary of controller types and ports
				char portInfo[128] = "";
				{
					uint32 ctrlCount = imap->GetControllerCount();
					int pos = 0;
					for (uint32 c = 0; c < ctrlCount && pos < 100; ++c) {
						const ATInputMap::Controller& ctrl = imap->GetController(c);
						if (c > 0)
							pos += snprintf(portInfo + pos, sizeof(portInfo) - pos, ", ");
						pos += snprintf(portInfo + pos, sizeof(portInfo) - pos, "%s P%u",
							ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1);
					}
				}

				// Selectable row with port info
				bool selected = (s_selectedMapIdx == (int)i);
				char label[256];
				snprintf(label, sizeof(label), "%-30s  %s", u8name.c_str(), portInfo);
				if (ImGui::Selectable(label, selected)) {
					s_selectedMapIdx = (int)i;
				}

				// Tooltip with full map details on hover
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					uint32 mappingCount = imap->GetMappingCount();
					ImGui::Text("%u mapping(s)", mappingCount);
					if (imap->IsQuickMap())
						ImGui::Text("[Quick Map]");
					ImGui::EndTooltip();
				}

				ImGui::PopID();
			}

			// Buttons for selected map
			ImGui::Spacing();
			ImGui::Separator();

			{
				ATInputMap *imap = nullptr;
				if (s_selectedMapIdx >= 0 && (uint32)s_selectedMapIdx < mapCount)
					inputMgr->GetInputMapByIndex(s_selectedMapIdx, &imap);
				if (ImGui::Button("Remove") && imap) {
					inputMgr->RemoveInputMap(imap);
					s_selectedMapIdx = -1;
				}
			}
		}
		ImGui::EndChild();

		// --- Selected map binding viewer/editor ---
		ATInputMap *selMap = nullptr;
		if (s_selectedMapIdx >= 0 && (uint32)s_selectedMapIdx < mapCount) {
			inputMgr->GetInputMapByIndex(s_selectedMapIdx, &selMap);
		}

		if (selMap) {
			ImGui::Spacing();
			VDStringA mapTitle = VDTextWToU8(VDStringW(selMap->GetName()));
			ImGui::Text("Bindings: %s", mapTitle.c_str());

			// Controllers summary
			uint32 ctrlCount = selMap->GetControllerCount();
			if (ctrlCount > 0) {
				ImGui::SameLine();
				ImGui::TextDisabled("(");
				for (uint32 c = 0; c < ctrlCount; ++c) {
					const ATInputMap::Controller& ctrl = selMap->GetController(c);
					if (c > 0) {
						ImGui::SameLine(0, 0);
						ImGui::TextDisabled(", ");
					}
					ImGui::SameLine(0, 0);
					ImGui::TextDisabled("%s P%u", ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1);
				}
				ImGui::SameLine(0, 0);
				ImGui::TextDisabled(")");
			}

			uint32 mappingCount = selMap->GetMappingCount();
			bool mapModified = false;
			bool openCapturePopup = false;

			if (mappingCount == 0) {
				ImGui::TextDisabled("No bindings in this map.");
			} else if (ImGui::BeginTable("##bindings", 5,
					ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
					| ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp,
					ImVec2(0, 200))) {
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_DefaultSort, 0.28f);
				ImGui::TableSetupColumn("Controller", 0, 0.22f);
				ImGui::TableSetupColumn("Target", 0, 0.20f);
				ImGui::TableSetupColumn("Mode", 0, 0.15f);
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_NoSort, 0.15f);
				ImGui::TableHeadersRow();

				// Build sortable mapping list
				struct MappingEntry {
					uint32 idx;
					const char *inputName;
					const char *ctrlName;
					uint32 ctrlPort;
					const char *triggerName;
					const char *modeName;
				};

				static vdfastvector<MappingEntry> sortedMappings;
				sortedMappings.clear();
				sortedMappings.reserve(mappingCount);

				for (uint32 m = 0; m < mappingCount; ++m) {
					const ATInputMap::Mapping& mapping = selMap->GetMapping(m);
					MappingEntry entry;
					entry.idx = m;
					entry.inputName = ATGetInputCodeName(mapping.mInputCode);

					uint32 cid = mapping.mControllerId;
					if (cid < ctrlCount) {
						const ATInputMap::Controller& ctrl = selMap->GetController(cid);
						entry.ctrlName = ATGetControllerTypeName(ctrl.mType);
						entry.ctrlPort = ctrl.mIndex + 1;
					} else {
						entry.ctrlName = "?";
						entry.ctrlPort = 0;
					}

					entry.triggerName = ATGetInputTriggerName(mapping.mCode);

					uint32 mode = mapping.mCode & kATInputTriggerMode_Mask;
					switch (mode) {
						case kATInputTriggerMode_AutoFire:	entry.modeName = "Auto-fire"; break;
						case kATInputTriggerMode_Toggle:	entry.modeName = "Toggle"; break;
						case kATInputTriggerMode_ToggleAF:	entry.modeName = "Toggle AF"; break;
						case kATInputTriggerMode_Relative:	entry.modeName = "Relative"; break;
						case kATInputTriggerMode_Absolute:	entry.modeName = "Absolute"; break;
						case kATInputTriggerMode_Inverted:	entry.modeName = "Inverted"; break;
						default:							entry.modeName = ""; break;
					}

					sortedMappings.push_back(entry);
				}

				// Apply sort
				if (ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs()) {
					if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
						int col = sortSpecs->Specs[0].ColumnIndex;
						bool asc = (sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
						std::sort(sortedMappings.begin(), sortedMappings.end(),
							[col, asc](const MappingEntry& a, const MappingEntry& b) {
								int cmp = 0;
								switch (col) {
									case 0: cmp = strcmp(a.inputName, b.inputName); break;
									case 1: cmp = strcmp(a.ctrlName, b.ctrlName); break;
									case 2: cmp = strcmp(a.triggerName, b.triggerName); break;
									case 3: cmp = strcmp(a.modeName, b.modeName); break;
								}
								return asc ? (cmp < 0) : (cmp > 0);
							});
						sortSpecs->SpecsDirty = false;
					}
				}

				for (const auto& entry : sortedMappings) {
					ImGui::PushID((int)entry.idx);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry.inputName);
					ImGui::TableNextColumn();
					if (entry.ctrlPort > 0)
						ImGui::Text("%s P%u", entry.ctrlName, entry.ctrlPort);
					else
						ImGui::TextUnformatted(entry.ctrlName);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry.triggerName);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry.modeName);
					ImGui::TableNextColumn();
					if (ImGui::SmallButton("Rebind")) {
						s_captureTargetMappingIdx = (int)entry.idx;
						s_inputCaptureGotResult = false;
						s_inputCaptureActive = true;
						openCapturePopup = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Del")) {
						selMap->RemoveMapping(entry.idx);
						mapModified = true;
					}
					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			ImGui::Text("%u binding(s)", mappingCount);

			// --- Add new binding ---
			ImGui::Spacing();
			{
				static int s_newControllerIdx = 0;
				static int s_newTriggerIdx = 0;

				static const struct { uint32 code; const char *name; } kCommonTriggers[] = {
					{ kATInputTrigger_Up,		"Up" },
					{ kATInputTrigger_Down,		"Down" },
					{ kATInputTrigger_Left,		"Left" },
					{ kATInputTrigger_Right,	"Right" },
					{ kATInputTrigger_Button0,	"Button" },
					{ kATInputTrigger_Start,	"Start" },
					{ kATInputTrigger_Select,	"Select" },
					{ kATInputTrigger_Option,	"Option" },
					{ kATInputTrigger_Axis0,	"Axis" },
				};
				static const int kNumCommonTriggers = (int)(sizeof(kCommonTriggers) / sizeof(kCommonTriggers[0]));

				if (s_newTriggerIdx >= kNumCommonTriggers)
					s_newTriggerIdx = 0;

				// Controller selector
				if (ctrlCount > 0) {
					ImGui::SetNextItemWidth(200);
					if (ImGui::BeginCombo("##newctrl", [&]() -> const char* {
						if (s_newControllerIdx >= 0 && (uint32)s_newControllerIdx < ctrlCount) {
							const ATInputMap::Controller& ctrl = selMap->GetController(s_newControllerIdx);
							static char cbuf[64];
							snprintf(cbuf, sizeof(cbuf), "%s P%u", ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1);
							return cbuf;
						}
						return "Select controller";
					}())) {
						for (uint32 c = 0; c < ctrlCount; ++c) {
							const ATInputMap::Controller& ctrl = selMap->GetController(c);
							char label[64];
							snprintf(label, sizeof(label), "%s P%u", ATGetControllerTypeName(ctrl.mType), ctrl.mIndex + 1);
							if (ImGui::Selectable(label, (int)c == s_newControllerIdx))
								s_newControllerIdx = (int)c;
						}
						ImGui::EndCombo();
					}

					ImGui::SameLine();
					ImGui::SetNextItemWidth(120);
					if (ImGui::BeginCombo("##newtrig", kCommonTriggers[s_newTriggerIdx].name)) {
						for (int t = 0; t < kNumCommonTriggers; ++t) {
							if (ImGui::Selectable(kCommonTriggers[t].name, t == s_newTriggerIdx))
								s_newTriggerIdx = t;
						}
						ImGui::EndCombo();
					}

					ImGui::SameLine();
					if (ImGui::Button("Add & Capture")) {
						s_captureTargetMappingIdx = -1;
						s_inputCaptureGotResult = false;
						s_inputCaptureActive = true;
						openCapturePopup = true;
					}
				} else {
					ImGui::TextDisabled("Add a controller to the map first to add bindings.");
				}

				// --- Capture Input modal popup ---
				if (openCapturePopup)
					ImGui::OpenPopup("Capture Input");

				ImVec2 center = ImGui::GetMainViewport()->GetCenter();
				ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
				if (ImGui::BeginPopupModal("Capture Input", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::Text("Press a key, gamepad button, or move a stick...");
					ImGui::TextDisabled("Shift+Escape to cancel");
					ImGui::Spacing();

					if (s_inputCaptureGotResult) {
						if (s_capturedInputCode == kATInputCode_None) {
							// Cancelled
						} else if (s_captureTargetMappingIdx >= 0) {
							// Rebinding existing mapping
							selMap->SetMappingInputCode((uint32)s_captureTargetMappingIdx, s_capturedInputCode);
							mapModified = true;
						} else {
							// Adding new mapping
							uint32 controllerId = (s_newControllerIdx >= 0 && (uint32)s_newControllerIdx < ctrlCount)
								? (uint32)s_newControllerIdx : 0;
							selMap->AddMapping(s_capturedInputCode, controllerId,
								kCommonTriggers[s_newTriggerIdx].code);
							mapModified = true;
						}

						s_inputCaptureActive = false;
						s_inputCaptureGotResult = false;
						s_capturedInputCode = 0;
						ImGui::CloseCurrentPopup();
					}

					if (ImGui::Button("Cancel")) {
						s_inputCaptureActive = false;
						s_inputCaptureGotResult = false;
						s_capturedInputCode = 0;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}

			// Re-register map with input manager after any mutation
			if (mapModified) {
				bool wasEnabled = inputMgr->IsInputMapEnabled(selMap);
				if (wasEnabled) {
					inputMgr->ActivateInputMap(selMap, false);
					inputMgr->ActivateInputMap(selMap, true);
				}
			}
		}
	}

	// --- Add from presets ---
	if (ImGui::CollapsingHeader("Add Preset Map")) {
		uint32 presetCount = inputMgr->GetPresetInputMapCount();

		static int s_selectedPresetIdx = -1;

		if (ImGui::BeginChild("##presetlist", ImVec2(0, 200), ImGuiChildFlags_Borders)) {
			for (uint32 i = 0; i < presetCount; ++i) {
				vdrefptr<ATInputMap> preset;
				if (!inputMgr->GetPresetInputMapByIndex(i, ~preset))
					continue;

				VDStringA u8name = VDTextWToU8(VDStringW(preset->GetName()));
				bool selected = (s_selectedPresetIdx == (int)i);

				if (ImGui::Selectable(u8name.c_str(), selected)) {
					s_selectedPresetIdx = (int)i;
				}
			}
		}
		ImGui::EndChild();

		if (ImGui::Button("Add Selected Preset") && s_selectedPresetIdx >= 0) {
			vdrefptr<ATInputMap> preset;
			if (inputMgr->GetPresetInputMapByIndex(s_selectedPresetIdx, ~preset)) {
				inputMgr->AddInputMap(preset);
			}
		}
	}

	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button("Reset to Defaults")) {
		inputMgr->ResetToDefaults();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Remove all maps and add back the default set.\nIncludes arrow keys, numpad, mouse, and gamepad presets.");
	}

	ImGui::End();
}

// ============= Cassette Control Window =============

static void FormatTapeTime(char *buf, size_t bufLen, float seconds) {
	int mins = (int)(seconds / 60.0f);
	float secs = seconds - mins * 60.0f;
	snprintf(buf, bufLen, "%d:%05.2f", mins, secs);
}

static void DrawCassetteControl() {
	if (!s_showCassetteControl)
		return;

	ATCassetteEmulator& cas = g_sim.GetCassette();

	ImGui::SetNextWindowSize(ImVec2(460, 340), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Cassette Control", &s_showCassetteControl)) {
		ImGui::End();
		return;
	}

	// Load/New/Unload/Save toolbar
	if (ImGui::Button("Load Tape...")) {
		VDStringW path = ATLinuxOpenFileDialog("Load Tape", kTapeFilters);
		if (!path.empty()) {
			try {
				cas.Load(path.c_str());
				ShowToast("Tape loaded");
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Load tape failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to load tape image");
			}
		} else if (ATLinuxFileDialogIsFallbackOpen()) {
			s_pendingDialog = PendingDialog::kLoadTape;
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("New Tape")) {
		cas.LoadNew();
	}

	ImGui::SameLine();

	if (ImGui::Button("Unload") && cas.IsLoaded()) {
		cas.Unload();
	}

	ImGui::SameLine();

	if (ImGui::Button("Save As...") && cas.IsLoaded()) {
		VDStringW path = ATLinuxSaveFileDialog("Save Tape", kTapeFilters);
		if (!path.empty()) {
			try {
				cas.SetImagePersistent(path.c_str());
				cas.SetImageClean();
				ShowToast("Tape saved");
			} catch (const std::exception& e) {
				char msg[512];
				snprintf(msg, sizeof(msg), "Save tape failed: %s", e.what());
				ShowToast(msg);
			} catch (...) {
				ShowToast("Failed to save tape image");
			}
		} else if (ATLinuxFileDialogIsFallbackOpen()) {
			s_pendingDialog = PendingDialog::kSaveTape;
		}
	}

	// Current tape info
	ImGui::Separator();

	if (cas.IsLoaded()) {
		const wchar_t *tapePath = cas.GetPath();
		if (tapePath && *tapePath) {
			VDStringA u8path = VDTextWToU8(VDStringW(VDFileSplitPath(tapePath)));
			ImGui::Text("File: %s", u8path.c_str());
		} else {
			ImGui::Text("File: [new tape]");
		}

		if (cas.IsImageDirty())
			ImGui::SameLine(), ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(modified)");
	} else {
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No tape loaded");
	}

	// Position display and scrubber
	float pos = cas.GetPosition();
	float len = cas.GetLength();

	char posStr[32], lenStr[32];
	FormatTapeTime(posStr, sizeof(posStr), pos);
	FormatTapeTime(lenStr, sizeof(lenStr), len);

	ImGui::Text("Position: %s / %s", posStr, lenStr);

	if (cas.IsLoaded() && len > 0) {
		float scrubPos = pos;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##tapepos", &scrubPos, 0.0f, len, "")) {
			cas.SeekToTime(scrubPos);
		}
	}

	// Transport controls
	ImGui::Separator();

	bool loaded = cas.IsLoaded();
	bool playing = cas.IsPlayEnabled();
	bool recording = cas.IsRecordEnabled();
	bool paused = cas.IsPaused();
	bool stopped = cas.IsStopped();

	// Rewind
	if (ImGui::Button("Rewind") && loaded)
		cas.RewindToStart();

	ImGui::SameLine();

	// Play
	if (playing && !paused) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
		if (ImGui::Button("Play"))
			cas.SetPaused(true);
		ImGui::PopStyleColor();
	} else {
		if (ImGui::Button("Play") && loaded) {
			if (paused)
				cas.SetPaused(false);
			else
				cas.Play();
		}
	}

	ImGui::SameLine();

	// Record
	if (recording && !paused) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
		if (ImGui::Button("Record"))
			cas.SetPaused(true);
		ImGui::PopStyleColor();
	} else {
		if (ImGui::Button("Record") && loaded) {
			if (paused && recording)
				cas.SetPaused(false);
			else
				cas.Record();
		}
	}

	ImGui::SameLine();

	// Stop
	if (ImGui::Button("Stop") && loaded && !stopped)
		cas.Stop();

	ImGui::SameLine();

	// Skip backward/forward
	if (ImGui::Button("-10s") && loaded)
		cas.SkipBackward(10.0f);

	ImGui::SameLine();

	if (ImGui::Button("+10s") && loaded)
		cas.SkipForward(10.0f);

	// Status indicators
	{
		ImGui::SameLine(0, 16);

		if (playing && !paused)
			ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "PLAY");
		else if (recording && !paused)
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC");
		else if (paused)
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "PAUSED");
		else
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "STOP");

		// Motor indicator
		if (cas.IsMotorRunning()) {
			ImGui::SameLine(0, 12);
			ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "MOTOR");
		}
	}

	// Tape options
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Options")) {
		// Turbo mode
		int turboMode = (int)cas.GetTurboMode();
		ImGui::Text("Turbo Mode:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(200);
		if (ImGui::Combo("##turbomode", &turboMode, kCassetteTurboModeNames, kATCassetteTurboMode_Always + 1))
			cas.SetTurboMode((ATCassetteTurboMode)turboMode);

		bool autoRewind = cas.IsAutoRewindEnabled();
		if (ImGui::Checkbox("Auto-rewind on load", &autoRewind))
			cas.SetAutoRewindEnabled(autoRewind);

		bool loadAsAudio = cas.IsLoadDataAsAudioEnabled();
		if (ImGui::Checkbox("Load data as audio", &loadAsAudio))
			cas.SetLoadDataAsAudioEnable(loadAsAudio);
	}

	ImGui::End();
}

// ============= Video Configuration Window =============

static void DrawVideoConfig() {
	if (!s_showVideoConfig)
		return;

	ImGui::SetNextWindowSize(ImVec2(500, 550), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Video Settings", &s_showVideoConfig)) {
		ImGui::End();
		return;
	}

	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	// Display options
	if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
		// Display filter (already in View menu, but also here for convenience)
		int filterMode = (int)ATUIGetDisplayFilterMode();
		ImGui::Text("Filter:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##filter", &filterMode, kDisplayFilterNames, 5)) {
			ATUISetDisplayFilterMode((ATDisplayFilterMode)filterMode);

			ATDisplaySDL2 *disp = ATGetLinuxDisplay();
			if (disp) {
				IVDVideoDisplay::FilterMode fm =
					(filterMode == kATDisplayFilterMode_Point)
						? IVDVideoDisplay::kFilterPoint
						: IVDVideoDisplay::kFilterBilinear;
				disp->SetFilterMode(fm);
			}
		}

		// Stretch mode
		int stretchMode = (int)ATUIGetDisplayStretchMode();
		ImGui::Text("Stretch:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##stretch", &stretchMode, kStretchModeNames, kATDisplayStretchModeCount))
			ATUISetDisplayStretchMode((ATDisplayStretchMode)stretchMode);

		// Overscan
		int osMode = (int)gtia.GetOverscanMode();
		ImGui::Text("H Overscan:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##hoverscan", &osMode, kOverscanModeNames, ATGTIAEmulator::kOverscanCount))
			gtia.SetOverscanMode((ATGTIAEmulator::OverscanMode)osMode);

		int vosMode = (int)gtia.GetVerticalOverscanMode();
		ImGui::Text("V Overscan:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##voverscan", &vosMode, kVertOverscanModeNames, ATGTIAEmulator::kVerticalOverscanCount))
			gtia.SetVerticalOverscanMode((ATGTIAEmulator::VerticalOverscanMode)vosMode);
	}

	// Artifacting
	if (ImGui::CollapsingHeader("Artifacting", ImGuiTreeNodeFlags_DefaultOpen)) {
		int artMode = (int)gtia.GetArtifactingMode();
		ImGui::Text("Mode:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##artifact", &artMode, kArtifactModeNames, (int)ATArtifactMode::Count))
			gtia.SetArtifactingMode((ATArtifactMode)artMode);

		// Monitor mode
		int monMode = (int)gtia.GetMonitorMode();
		ImGui::Text("Monitor:");
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(220);
		if (ImGui::Combo("##monitor", &monMode, kMonitorModeNames, (int)ATMonitorMode::Count))
			gtia.SetMonitorMode((ATMonitorMode)monMode);

		// Scanlines / interlace
		bool scanlines = gtia.AreScanlinesEnabled();
		if (ImGui::Checkbox("Scanlines", &scanlines))
			gtia.SetScanlinesEnabled(scanlines);

		bool interlace = gtia.IsInterlaceEnabled();
		if (ImGui::Checkbox("Interlace", &interlace))
			gtia.SetInterlaceEnabled(interlace);

		if (interlace) {
			int deinterlace = (int)gtia.GetDeinterlaceMode();
			const char *deinterlaceNames[] = { "None", "Adaptive Bob" };
			ImGui::Text("Deinterlace:");
			ImGui::SameLine(120);
			ImGui::SetNextItemWidth(220);
			if (ImGui::Combo("##deinterlace", &deinterlace, deinterlaceNames, 2))
				gtia.SetDeinterlaceMode((ATVideoDeinterlaceMode)deinterlace);
		}
	}

	// Color adjustment
	if (ImGui::CollapsingHeader("Color Adjustment")) {
		ATColorSettings cs = gtia.GetColorSettings();
		bool isPAL = gtia.IsPALMode();
		ATColorParams& params = isPAL ? cs.mPALParams : cs.mNTSCParams;
		bool changed = false;

		ImGui::Text("Editing: %s parameters", isPAL ? "PAL" : "NTSC");

		bool usePALParams = cs.mbUsePALParams;
		if (ImGui::Checkbox("Use separate PAL parameters", &usePALParams)) {
			cs.mbUsePALParams = usePALParams;
			changed = true;
		}

		ImGui::Separator();

		// Presets
		uint32 presetCount = ATGetColorPresetCount();
		if (presetCount > 0 && ImGui::BeginCombo("##preset", "Load Preset...")) {
			for (uint32 i = 0; i < presetCount; ++i) {
				VDStringA name = VDTextWToU8(VDStringW(ATGetColorPresetNameByIndex(i)));
				if (ImGui::Selectable(name.c_str())) {
					params = ATGetColorPresetByIndex(i);
					changed = true;
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Hue Start", &params.mHueStart, -60.0f, 60.0f, "%.1f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Hue Range", &params.mHueRange, 160.0f, 360.0f, "%.1f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Brightness", &params.mBrightness, -0.5f, 0.5f, "%.3f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Contrast", &params.mContrast, 0.0f, 2.0f, "%.3f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Saturation", &params.mSaturation, 0.0f, 1.0f, "%.3f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Gamma", &params.mGammaCorrect, 0.5f, 3.0f, "%.2f");
		ImGui::SetNextItemWidth(200);
		changed |= ImGui::SliderFloat("Intensity", &params.mIntensityScale, 0.5f, 2.0f, "%.2f");

		// Color matching
		int colorMatch = (int)params.mColorMatchingMode;
		ImGui::SetNextItemWidth(200);
		if (ImGui::Combo("Color Matching", &colorMatch, kColorMatchingNames, 5)) {
			params.mColorMatchingMode = (ATColorMatchingMode)colorMatch;
			changed = true;
		}

		// Luma ramp
		int lumaRamp = (int)params.mLumaRampMode;
		ImGui::SetNextItemWidth(200);
		if (ImGui::Combo("Luma Ramp", &lumaRamp, kLumaRampNames, kATLumaRampModeCount)) {
			params.mLumaRampMode = (ATLumaRampMode)lumaRamp;
			changed = true;
		}

		if (ImGui::TreeNode("Artifact Tuning")) {
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Artifact Hue", &params.mArtifactHue, -180.0f, 180.0f, "%.1f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Artifact Sat", &params.mArtifactSat, 0.0f, 2.0f, "%.3f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Artifact Sharpness", &params.mArtifactSharpness, 0.0f, 1.0f, "%.3f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("RGB Correction")) {
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Red Shift", &params.mRedShift, -30.0f, 30.0f, "%.1f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Red Scale", &params.mRedScale, 0.5f, 2.0f, "%.3f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Green Shift", &params.mGrnShift, -30.0f, 30.0f, "%.1f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Green Scale", &params.mGrnScale, 0.5f, 2.0f, "%.3f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Blue Shift", &params.mBluShift, -30.0f, 30.0f, "%.1f");
			ImGui::SetNextItemWidth(200);
			changed |= ImGui::SliderFloat("Blue Scale", &params.mBluScale, 0.5f, 2.0f, "%.3f");
			ImGui::TreePop();
		}

		bool palQuirks = params.mbUsePALQuirks;
		if (ImGui::Checkbox("PAL quirks", &palQuirks)) {
			params.mbUsePALQuirks = palQuirks;
			changed = true;
		}

		if (changed)
			gtia.SetColorSettings(cs);

		if (ImGui::Button("Reset to Defaults")) {
			ATColorSettings defaults = gtia.GetDefaultColorSettings();
			gtia.SetColorSettings(defaults);
		}
	}

	ImGui::End();
}

// ============= New Disk Dialog =============

static const struct {
	const char *name;
	uint32 sectorCount;
	uint32 sectorSize;
} kNewDiskFormats[] = {
	{ "Single Density (90K - 720 sectors, 128 bytes)",   720, 128 },
	{ "Medium Density (130K - 1040 sectors, 128 bytes)", 1040, 128 },
	{ "Double Density (180K - 720 sectors, 256 bytes)",  720, 256 },
	{ "Double-Sided DD (360K - 1440 sectors, 256 bytes)", 1440, 256 },
};

static const char *kNewDiskFSNames[] = {
	"None (unformatted)",
	"DOS 2.0",
	"MyDOS",
};

// ============= Video Recording Config Dialog =============

static void DrawVideoRecordDialog() {
	if (!s_showVideoRecordDialog)
		return;

	ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Record Video", &s_showVideoRecordDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Codec:");
#ifdef AT_HAVE_FFMPEG
	static const char *kCodecNames[] = { "ZMBV (Lossless)", "Raw (Uncompressed)", "RLE (Palette only)", "H.264+AAC (MP4)" };
	const int codecCount = 4;
#else
	static const char *kCodecNames[] = { "ZMBV (Lossless)", "Raw (Uncompressed)", "RLE (Palette only)" };
	const int codecCount = 3;
#endif
	ImGui::Combo("##codec", &s_videoCodecIndex, kCodecNames, codecCount);

#ifdef AT_HAVE_FFMPEG
	if (s_videoCodecIndex == 3) {
		ImGui::Spacing();
		ImGui::Text("Video Bitrate:");
		ImGui::SliderInt("##vbitrate", &s_videoBitRateKbps, 500, 8000, "%d kbps");
	}
#endif

	ImGui::Spacing();
	ImGui::Text("Scaling:");
	static const char *kScalingNames[] = { "None (Native)", "480p Narrow (640x480)", "480p Wide (854x480)", "720p Narrow (960x720)", "720p Wide (1280x720)" };
	ImGui::Combo("##scaling", &s_videoScalingIndex, kScalingNames, 5);

	if (s_videoScalingIndex > 0) {
		ImGui::Spacing();
		ImGui::Text("Resampling:");
		static const char *kResamplingNames[] = { "Nearest", "Sharp Bilinear", "Bilinear" };
		ImGui::Combo("##resampling", &s_videoResamplingIndex, kResamplingNames, 3);
	}

	ImGui::Spacing();
	ImGui::Checkbox("Half frame rate", &s_videoHalfRate);
	ImGui::Checkbox("Encode all frames", &s_videoEncodeAll);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button("Record...", ImVec2(120, 0))) {
#ifdef AT_HAVE_FFMPEG
		static const ATVideoEncoding kDialogCodecs[] = { kATVideoEncoding_ZMBV, kATVideoEncoding_Raw, kATVideoEncoding_RLE, kATVideoEncoding_H264_AAC };
		bool isMP4 = (kDialogCodecs[s_videoCodecIndex] == kATVideoEncoding_H264_AAC);
#else
		bool isMP4 = false;
#endif
		const char *filter = isMP4
			? "MP4 Video|*.mp4|All Files|*"
			: "AVI Video|*.avi|All Files|*";
		VDStringW path = ATLinuxSaveFileDialog("Record Video", filter);
		if (!path.empty()) {
			if (VDFileSplitExt(path.c_str()) == path.c_str() + path.size())
				path += isMP4 ? L".mp4" : L".avi";
			StartVideoRecording(path.c_str());
			s_showVideoRecordDialog = false;
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 0))) {
		s_showVideoRecordDialog = false;
	}

	ImGui::End();
}

// ============= New Disk Dialog =============

static void DrawNewDisk() {
	if (!s_showNewDisk)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Create New Disk", &s_showNewDisk, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Create blank disk image in D%d:", s_newDiskSlot + 1);
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Disk Format:");
	for (int i = 0; i < (int)(sizeof(kNewDiskFormats)/sizeof(kNewDiskFormats[0])); ++i) {
		ImGui::RadioButton(kNewDiskFormats[i].name, &s_newDiskFormat, i);
	}

	ImGui::Spacing();
	ImGui::Text("Filesystem:");
	ImGui::Combo("##fs", &s_newDiskFS, kNewDiskFSNames, 3);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button("Create", ImVec2(100, 0))) {
		try {
			ATDiskInterface& di = g_sim.GetDiskInterface(s_newDiskSlot);
			di.UnloadDisk();

			uint32 sectorCount = kNewDiskFormats[s_newDiskFormat].sectorCount;
			uint32 sectorSize = kNewDiskFormats[s_newDiskFormat].sectorSize;
			di.CreateDisk(sectorCount, 3, sectorSize);
			di.SetWriteMode(kATMediaWriteMode_VRW);

			IATDiskImage *image = di.GetDiskImage();
			if (image && s_newDiskFS > 0) {
				vdautoptr<IATDiskFS> fs;
				if (s_newDiskFS == 1)
					fs = ATDiskFormatImageDOS2(image);
				else if (s_newDiskFS == 2)
					fs = ATDiskFormatImageMyDOS(image);

				if (fs)
					fs->Flush();
			}
			char msg[64];
			snprintf(msg, sizeof(msg), "New disk created in D%d", s_newDiskSlot + 1);
			ShowToast(msg);
		} catch (...) {
			ShowToast("Failed to create new disk");
		}
		s_showNewDisk = false;
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 0))) {
		s_showNewDisk = false;
	}

	ImGui::End();
}

// ============= Quit Confirmation =============

static bool HasDirtyDisks() {
	for (int i = 0; i < 15; ++i) {
		ATDiskInterface& di = g_sim.GetDiskInterface(i);
		if (di.IsDiskLoaded() && di.IsDirty())
			return true;
	}
	return false;
}

static void DrawQuitConfirmation() {
	if (!s_quitRequested)
		return;

	ImGui::OpenPopup("Quit Altirra?");
	s_quitRequested = false;

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Quit Altirra?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("The following disks have unsaved changes:");
		ImGui::Separator();

		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			if (di.IsDiskLoaded() && di.IsDirty()) {
				const wchar_t *filename = VDFileSplitPath(di.GetPath());
				VDStringA u8 = VDTextWToU8(VDStringW(filename));
				ImGui::BulletText("D%d: %s", i + 1, u8.c_str());
			}
		}

		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Save All & Quit", ImVec2(140, 0))) {
			for (int i = 0; i < 15; ++i) {
				ATDiskInterface& di = g_sim.GetDiskInterface(i);
				if (di.IsDiskLoaded() && di.IsDirty()) {
					try { di.SaveDisk(); } catch (...) {}
				}
			}
			s_quitConfirmed = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Quit Without Saving", ImVec2(160, 0))) {
			s_quitConfirmed = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

// ============= Cheat Engine =============

static void UpdateCheaterResults() {
	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) return;

	enum { kMaxResults = 1000 };
	s_cheaterResults.resize(kMaxResults);
	s_cheaterResultCount = ce->GetValidOffsets(s_cheaterResults.data(), kMaxResults);
	s_cheaterResults.resize(s_cheaterResultCount);
}

static void DrawCheater() {
	if (!s_showCheater)
		return;

	// Enable cheat engine on open
	if (!g_sim.GetCheatEngine())
		g_sim.SetCheatEngineEnabled(true);

	ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Cheat Engine", &s_showCheater)) {
		ImGui::End();
		return;
	}

	// Handle close
	if (!s_showCheater) {
		g_sim.SetCheatEngineEnabled(false);
		s_cheaterInitialized = false;
		s_cheaterResultCount = 0;
		s_cheaterResults.clear();
		ImGui::End();
		return;
	}

	ATCheatEngine *ce = g_sim.GetCheatEngine();
	if (!ce) {
		ImGui::TextDisabled("Cheat engine not available.");
		ImGui::End();
		return;
	}

	// Mode names matching ATCheatSnapshotMode order
	static const char *kModeNames[] = {
		"New Snapshot",
		"= Unchanged",
		"!= Changed",
		"< Less Than Previous",
		"<= Less or Equal",
		"> Greater Than Previous",
		">= Greater or Equal",
		"=X Equal to Value"
	};

	// Controls
	ImGui::SetNextItemWidth(200);
	ImGui::Combo("##mode", &s_cheaterMode, kModeNames, kATCheatSnapModeCount);

	ImGui::SameLine();
	ImGui::RadioButton("8-bit", &s_cheaterBit16, 0);
	ImGui::SameLine();
	ImGui::RadioButton("16-bit", &s_cheaterBit16, 1);

	bool needsValue = (s_cheaterMode == kATCheatSnapMode_EqualRef);
	if (needsValue) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		ImGui::InputText("Value", s_cheaterValueBuf, sizeof(s_cheaterValueBuf));
	}

	ImGui::SameLine();
	if (ImGui::Button("Search / Filter")) {
		bool bit16 = s_cheaterBit16 != 0;

		if (s_cheaterMode == 0) {
			// New snapshot
			ce->Snapshot(kATCheatSnapMode_Replace, 0, false);
			s_cheaterInitialized = true;
		} else {
			uint32 value = 0;
			if (needsValue) {
				const char *v = s_cheaterValueBuf;
				while (*v == ' ') ++v;
				if (*v == '$')
					sscanf(v + 1, "%x", &value);
				else
					sscanf(v, "%u", &value);
			}
			ce->Snapshot((ATCheatSnapshotMode)s_cheaterMode, value, bit16);
		}
		UpdateCheaterResults();
	}

	ImGui::SameLine();
	if (ImGui::Button("Clear All")) {
		ce->Clear();
		s_cheaterInitialized = false;
		s_cheaterResultCount = 0;
		s_cheaterResults.clear();
	}

	// Two panes: Results (left) | Active Cheats (right)
	float panelW = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;
	bool bit16 = s_cheaterBit16 != 0;

	// Results pane
	ImGui::BeginChild("##results", ImVec2(panelW, 0), ImGuiChildFlags_Border);
	ImGui::Text("Results: %u", s_cheaterResultCount);

	if (s_cheaterResultCount > 0 && ImGui::BeginTable("##restbl", 2,
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		// Limit display to avoid performance issues
		uint32 displayCount = std::min(s_cheaterResultCount, (uint32)500);
		for (uint32 i = 0; i < displayCount; ++i) {
			uint32 offset = s_cheaterResults[i];
			uint32 val = ce->GetOffsetCurrentValue(offset, bit16);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			char addrBuf[16];
			snprintf(addrBuf, sizeof(addrBuf), "$%04X", offset);

			bool selected = false;
			if (ImGui::Selectable(addrBuf, &selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				if (ImGui::IsMouseDoubleClicked(0)) {
					// Double-click: add as cheat
					ce->AddCheat(offset, bit16);
				}
			}

			ImGui::TableNextColumn();
			if (bit16)
				ImGui::Text("$%04X (%u)", val, val);
			else
				ImGui::Text("$%02X (%u)", val, val);
		}
		if (s_cheaterResultCount > 500)
			ImGui::Text("... and %u more", s_cheaterResultCount - 500);

		ImGui::EndTable();
	} else if (s_cheaterInitialized && s_cheaterResultCount == 0) {
		ImGui::TextDisabled("No results. Try a different filter.");
	} else if (!s_cheaterInitialized) {
		ImGui::TextDisabled("Click \"Search / Filter\" with\n\"New Snapshot\" to begin.");
	}

	ImGui::EndChild();

	ImGui::SameLine();

	// Active cheats pane — use a group so the table scrolls but
	// the Load/Save buttons stay pinned at the bottom.
	ImGui::BeginGroup();
	ImGui::Text("Active Cheats: %u", ce->GetCheatCount());

	// Reserve space for Load/Save buttons below the table.
	float buttonRowH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	float tableH = ImGui::GetContentRegionAvail().y - buttonRowH;
	if (tableH < 60)
		tableH = 60;

	ImGui::BeginChild("##cheats", ImVec2(0, tableH), ImGuiChildFlags_Border);

	if (ce->GetCheatCount() > 0 && ImGui::BeginTable("##cheattbl", 4,
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableHeadersRow();

		int deleteIdx = -1;
		for (uint32 i = 0; i < ce->GetCheatCount(); ++i) {
			const ATCheatEngine::Cheat& cheat = ce->GetCheatByIndex(i);

			ImGui::TableNextRow();

			// Address column — double-click to edit
			ImGui::TableNextColumn();
			if (s_cheatEditIdx == (int)i && s_cheatEditField == 0) {
				char inputId[16];
				snprintf(inputId, sizeof(inputId), "##ea%u", i);
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (s_cheatEditFocus) {
					ImGui::SetKeyboardFocusHere();
					s_cheatEditFocus = false;
				}
				if (ImGui::InputText(inputId, s_cheatEditBuf, sizeof(s_cheatEditBuf),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
					uint32 newAddr = 0;
					const char *p = s_cheatEditBuf;
					while (*p == ' ') ++p;
					if (*p == '$') sscanf(p + 1, "%x", &newAddr);
					else sscanf(p, "%x", &newAddr);
					ATCheatEngine::Cheat mod = cheat;
					mod.mAddress = newAddr;
					ce->UpdateCheat(i, mod);
					s_cheatEditIdx = -1;
				}
				if (ImGui::IsItemDeactivated() && s_cheatEditIdx == (int)i && s_cheatEditField == 0)
					s_cheatEditIdx = -1;
			} else {
				char addrLabel[24];
				snprintf(addrLabel, sizeof(addrLabel), "$%04X##a%u", cheat.mAddress, i);
				if (ImGui::Selectable(addrLabel, false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
					s_cheatEditIdx = (int)i;
					s_cheatEditField = 0;
					snprintf(s_cheatEditBuf, sizeof(s_cheatEditBuf), "$%04X", cheat.mAddress);
					s_cheatEditFocus = true;
				}
			}

			// Value column — double-click to edit
			ImGui::TableNextColumn();
			if (s_cheatEditIdx == (int)i && s_cheatEditField == 1) {
				char inputId[16];
				snprintf(inputId, sizeof(inputId), "##ev%u", i);
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (s_cheatEditFocus) {
					ImGui::SetKeyboardFocusHere();
					s_cheatEditFocus = false;
				}
				if (ImGui::InputText(inputId, s_cheatEditBuf, sizeof(s_cheatEditBuf),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
					uint32 newVal = 0;
					const char *p = s_cheatEditBuf;
					while (*p == ' ') ++p;
					if (*p == '$') sscanf(p + 1, "%x", &newVal);
					else sscanf(p, "%u", &newVal);
					ATCheatEngine::Cheat mod = cheat;
					mod.mValue = (uint16)newVal;
					ce->UpdateCheat(i, mod);
					s_cheatEditIdx = -1;
				}
				if (ImGui::IsItemDeactivated() && s_cheatEditIdx == (int)i && s_cheatEditField == 1)
					s_cheatEditIdx = -1;
			} else {
				char valLabel[24];
				if (cheat.mb16Bit)
					snprintf(valLabel, sizeof(valLabel), "$%04X##v%u", cheat.mValue, i);
				else
					snprintf(valLabel, sizeof(valLabel), "$%02X##v%u", cheat.mValue, i);
				if (ImGui::Selectable(valLabel, false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
					s_cheatEditIdx = (int)i;
					s_cheatEditField = 1;
					if (cheat.mb16Bit)
						snprintf(s_cheatEditBuf, sizeof(s_cheatEditBuf), "$%04X", cheat.mValue);
					else
						snprintf(s_cheatEditBuf, sizeof(s_cheatEditBuf), "$%02X", cheat.mValue);
					s_cheatEditFocus = true;
				}
			}

			ImGui::TableNextColumn();
			bool enabled = cheat.mbEnabled;
			char cbId[16];
			snprintf(cbId, sizeof(cbId), "##en%u", i);
			if (ImGui::Checkbox(cbId, &enabled)) {
				ATCheatEngine::Cheat mod = cheat;
				mod.mbEnabled = enabled;
				ce->UpdateCheat(i, mod);
			}

			ImGui::TableNextColumn();
			char btnId[16];
			snprintf(btnId, sizeof(btnId), "X##d%u", i);
			if (ImGui::SmallButton(btnId))
				deleteIdx = (int)i;
		}
		ImGui::EndTable();

		if (deleteIdx >= 0) {
			if (s_cheatEditIdx == deleteIdx)
				s_cheatEditIdx = -1;
			ce->RemoveCheatByIndex(deleteIdx);
		}
	}

	ImGui::EndChild();

	// Load/Save buttons — always visible below the table
	if (ImGui::Button("Load...")) {
		VDStringW path = ATLinuxOpenFileDialog("Load Cheats",
			"Cheat Files|*.atcheats|All Files|*");
		if (!path.empty()) {
			try {
				ce->Load(path.c_str());
				UpdateCheaterResults();
			} catch (const std::exception& e) {
				(void)e;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Save...")) {
		VDStringW path = ATLinuxSaveFileDialog("Save Cheats",
			"Cheat Files|*.atcheats|All Files|*");
		if (!path.empty()) {
			try {
				ce->Save(path.c_str());
			} catch (const std::exception& e) {
				(void)e;
			}
		}
	}

	ImGui::EndGroup();

	ImGui::End();
}

// ============= Public API =============

void ATImGuiEmulatorInit() {
	// No init needed yet
}

void ATImGuiEmulatorDraw() {
	DrawMenuBar();
	DrawSystemConfig();
	DrawCPUOptions();
	DrawBootOptions();
	DrawCassetteControl();
	DrawCartridgeBrowser();
	DrawProfileManager();
	DrawFirmwareManager();
	DrawAudioOptions();
	DrawVideoConfig();
	DrawKeyboardConfig();
	DrawInputSetup();
	DrawDeviceManager();
	DrawDiskExplorer();
	DrawStatusBar();
	DrawAudioMonitor();
	DrawAudioScope();
	DrawAbout();
	DrawShortcuts();
	DrawCheater();
	DrawVideoRecordDialog();
	DrawNewDisk();
	DrawQuitConfirmation();
	PollFileDialogFallback();
	CheckPendingErrors();
	DrawErrorPopup();

	DrawToasts();
	DrawSourceMessage();

	// Draw debugger windows (without toolbar — menu bar handles that now)
	ATImGuiDebuggerDrawWindows();
}

void ATImGuiEmulatorShutdown() {
	StopAllRecording();
	// Disable cheat engine if open
	if (s_showCheater)
		g_sim.SetCheatEngineEnabled(false);
	s_showCheater = false;
	s_cheaterInitialized = false;
	s_cheaterResults.clear();

	s_pQuickState.clear();
	s_cartLoadBuffer.clear();
	s_cartDetectedModes.clear();
	s_cartDisplayModes.clear();
	s_fwList.clear();
	s_devSelectedDevice = nullptr;
	s_devEditProps.Clear();
	if (s_diskFS) {
		try { s_diskFS->Flush(); } catch (...) {}
		delete s_diskFS;
		s_diskFS = nullptr;
	}
	s_diskEntries.clear();
}

void ATImGuiRequestQuit() {
	if (HasDirtyDisks()) {
		s_quitRequested = true;
		s_quitConfirmed = false;
	} else {
		s_quitConfirmed = true;
	}
}

bool ATImGuiIsQuitConfirmed() {
	if (s_quitConfirmed) {
		s_quitConfirmed = false;
		return true;
	}
	return false;
}

void ATImGuiShowToast(const char *message) {
	ShowToast(message);
}

bool ATImGuiIsCapturingInput() {
	return s_inputCaptureActive;
}

void ATImGuiOnCapturedInput(uint32 inputCode) {
	s_capturedInputCode = inputCode;
	s_inputCaptureGotResult = true;
}

void ATImGuiPasteText() {
	PasteTextToEmulator();
}

void ATImGuiOpenImage() {
	VDStringW path = ATLinuxOpenFileDialog("Open Image", kDiskFilters);
	if (!path.empty())
		TryLoadImage(path);
}

void ATImGuiBootImage() {
	VDStringW path = ATLinuxOpenFileDialog("Boot Image", kDiskFilters);
	if (!path.empty()) {
		try {
			g_sim.UnloadAll();
			g_sim.Load(path.c_str(), kATMediaWriteMode_RO, nullptr);
			g_sim.ColdReset();
			g_sim.Resume();
			MRUAdd(path.c_str());
			VDStringA fname = VDTextWToU8(VDStringW(VDFileSplitPath(path.c_str())));
			char msg[256];
			snprintf(msg, sizeof(msg), "Booted: %s", fname.c_str());
			ShowToast(msg);
		} catch (const std::exception& e) {
			char msg[512];
			snprintf(msg, sizeof(msg), "Boot failed: %s", e.what());
			ShowToast(msg);
		} catch (...) {
			ShowToast("Failed to boot image");
		}
	}
}

static double s_startTime = 0;

void ATImGuiDrawToastsOnly() {
	DrawToasts();
	DrawStatusBar();

	// Show overlay hint for first 5 seconds
	if (s_startTime == 0)
		s_startTime = (double)SDL_GetTicks() / 1000.0;

	double elapsed = (double)SDL_GetTicks() / 1000.0 - s_startTime;
	if (elapsed < 5.0) {
		float alpha = elapsed > 4.0f ? (float)(5.0 - elapsed) : 1.0f;
		ImVec2 vp = ImGui::GetMainViewport()->Size;
		ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, 30.0f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowBgAlpha(0.6f * alpha);
		ImGui::Begin("##hint", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "Press F12 for menu");
		ImGui::End();
	}
}
