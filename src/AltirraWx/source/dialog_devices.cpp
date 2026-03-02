//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include "dialogs_wx.h"

#include <algorithm>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/choicdlg.h>
#include <wx/textctrl.h>

#include <vd2/system/text.h>

#include "simulator.h"
#include "devicemanager.h"
#include <at/atcore/device.h>
#include <at/atcore/propertyset.h>

extern ATSimulator g_sim;
void ATImGuiShowToast(const char *message);

namespace {

// ===== Device Configuration Descriptor System =====
// Mirrors the ImGui implementation in emulator_imgui.cpp.

enum class DevCfgType {
	Checkbox, IntDropdown, StringEdit, PathSelect,
	IntInput, FloatInput, BitfieldCheckbox, CompoundIntDropdown
};

struct DevCfgChoice { int value; const char *name; };
struct DevCfgCompoundPair { uint32 val1; uint32 val2; };

struct DevCfgControl {
	DevCfgType type;
	const char *propKey;
	const char *label;
	const DevCfgChoice *choices;
	int choiceCount;
	bool defaultBool;
	const char *browseTitle;
	const DevCfgCompoundPair *pairs;
	float floatMin, floatMax, floatDefault;
};

struct DevCfgTagMapping {
	const char *tag;
	const char *title;
	const DevCfgControl *controls;
	int controlCount;
};

#define DEVCFG_ENTRY(tag, title, arr) { tag, title, arr, (int)(sizeof(arr)/sizeof(arr[0])) }

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
	{0, "$D100, 256 bytes"}, {1, "$D280, 128 bytes"}, {2, "$D500, 256 bytes"},
	{3, "$D600, 64 bytes"}, {4, "$D600, 256 bytes"}, {5, "$D700, 256 bytes"},
};

static const DevCfgCompoundPair kCovoxRangeValues[] = {
	{0xD100, 0x100}, {0xD280, 0x80}, {0xD500, 0x100},
	{0xD600, 0x40}, {0xD600, 0x100}, {0xD700, 0x100},
};

static const DevCfgChoice kCovoxChannelChoices[] = {
	{1, "Mono"}, {4, "Stereo"},
};

static const DevCfgChoice k850EmuLevelChoices[] = {
	{0, "None (R: handler only)"}, {1, "Minimal (stub)"}, {2, "Full (SIO + R: handler)"},
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
	{0x01, "SW1: Ignore printer fault"}, {0x02, "SW2: HD + high-speed SIO"},
	{0x04, "SW3: Enable printer port"}, {0x08, "SW4: Enable RS232 port"},
	{0x10, "SW5: Printer linefeeds"}, {0x20, "SW6: ProWriter mode"},
	{0x40, "SW7: MIO compat mode"},
};

static const DevCfgChoice kBBFloppySlotChoices[] = {
	{0, "Not Connected"}, {1, "D1:"}, {2, "D2:"}, {3, "D3:"}, {4, "D4:"},
	{5, "D5:"}, {6, "D6:"}, {7, "D7:"}, {8, "D8:"}, {9, "D9:"},
	{10, "D10:"}, {11, "D11:"}, {12, "D12:"}, {13, "D13:"}, {14, "D14:"},
};

static const DevCfgChoice kBBFloppyTypeChoices[] = {
	{0, "180K 5.25\" 40trk SS"}, {1, "360K 5.25\" 40trk DS"},
	{2, "1.2M 5.25\" 80trk HD"}, {3, "360K 3.5\" 80trk SS"},
	{4, "720K 3.5\" 80trk DS"}, {5, "1.4M 3.5\" 80trk HD"},
	{6, "1M 8\" 77trk HD"},
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
	{0, "1771+1791 (DD capable)"}, {1, "1771+1795 (DD, side cmp)"}, {2, "1771 only (SD)"},
};

static const DevCfgCompoundPair kPercomATFDCValues[] = {
	{0, 1}, {1, 1}, {0, 0},
};

static const DevCfgChoice kAMDCDriveTypeChoices[] = {
	{0, "None"}, {1, "3\"/5.25\" (40 track)"}, {2, "3\"/5.25\" (80 track)"},
};

static const DevCfgChoice k1020ColorChoices[] = {
	{0x000000, "Black"}, {0x181FF0, "Blue"}, {0x0B9C2F, "Green"}, {0xC91B12, "Red"},
};

// --- Per-device control arrays ---

static const DevCfgControl kCfgVirtHD[] = {
	{ DevCfgType::PathSelect, "path", "Directory Path", nullptr, 0, false, "Select Directory" },
};

static const DevCfgControl kCfgHardDisk[] = {
	{ DevCfgType::PathSelect, "path", "Image Path", nullptr, 0, false, "Select Disk Image" },
	{ DevCfgType::Checkbox, "write_enabled", "Write Enabled" },
	{ DevCfgType::Checkbox, "solid_state", "Solid State (SSD)" },
};

static const DevCfgControl kCfgKMKJZIDE[] = {
	{ DevCfgType::IntDropdown, "id", "Device ID", kIDChoices, 8 },
	{ DevCfgType::Checkbox, "enablesdx", "Enable SDX Switch" },
	{ DevCfgType::Checkbox, "writeprotect", "Write Protect" },
	{ DevCfgType::Checkbox, "nvramguard", "NVRAM Guard" },
};

static const DevCfgControl kCfgSIDE3[] = {
	{ DevCfgType::IntDropdown, "version", "Hardware Version", kSIDE3VersionChoices, 2 },
	{ DevCfgType::Checkbox, "led_enable", "Activity LED", nullptr, 0, true },
	{ DevCfgType::Checkbox, "recovery", "Recovery Mode" },
};

static const DevCfgControl kCfgMyIDE[] = {
	{ DevCfgType::IntDropdown, "cpldver", "CPLD Version", kMyIDECPLDChoices, 2 },
};

static const DevCfgControl kCfgModem[] = {
	{ DevCfgType::IntInput, "port", "Listen Port (0=disabled)" },
	{ DevCfgType::Checkbox, "outbound", "Allow Outbound", nullptr, 0, true },
	{ DevCfgType::Checkbox, "telnet", "Telnet Emulation", nullptr, 0, true },
	{ DevCfgType::Checkbox, "ipv6", "Listen IPv6", nullptr, 0, true },
	{ DevCfgType::Checkbox, "unthrottled", "Unthrottled" },
	{ DevCfgType::IntDropdown, "connect_rate", "Connect Rate", kConnectRateChoices, 10 },
	{ DevCfgType::Checkbox, "check_rate", "Require Matched DTE Rate" },
	{ DevCfgType::StringEdit, "dialaddr", "Dial Address" },
	{ DevCfgType::StringEdit, "dialsvc", "Dial Service" },
	{ DevCfgType::StringEdit, "termtype", "Terminal Type" },
};

static const DevCfgControl kCfgPrinter[] = {
	{ DevCfgType::Checkbox, "graphics", "Graphics Mode" },
	{ DevCfgType::Checkbox, "accurate_timing", "Accurate Timing" },
	{ DevCfgType::Checkbox, "sound", "Sound Emulation" },
};

static const DevCfgControl kCfgPCLink[] = {
	{ DevCfgType::PathSelect, "path", "Base Directory", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::Checkbox, "write", "Write Access" },
	{ DevCfgType::Checkbox, "set_timestamps", "Set Timestamps" },
};

static const DevCfgControl kCfgHostFS[] = {
	{ DevCfgType::PathSelect, "path1", "Drive 1 Path", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::PathSelect, "path2", "Drive 2 Path", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::PathSelect, "path3", "Drive 3 Path", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::PathSelect, "path4", "Drive 4 Path", nullptr, 0, false, "Select Directory" },
	{ DevCfgType::Checkbox, "readonly", "Read Only", nullptr, 0, true },
	{ DevCfgType::Checkbox, "longfilenames", "Long Filenames" },
	{ DevCfgType::Checkbox, "lowercase", "Lowercase Names", nullptr, 0, true },
};

static const DevCfgControl kCfgCustomDev[] = {
	{ DevCfgType::PathSelect, "path", "Config File Path", nullptr, 0, false, "Select Config File" },
	{ DevCfgType::Checkbox, "hotreload", "Hot Reload" },
	{ DevCfgType::Checkbox, "allowunsafe", "Allow Unsafe Ops" },
};

static const DevCfgControl kCfgVBXE[] = {
	{ DevCfgType::IntDropdown, "version", "Hardware Version", kVBXEVersionChoices, 3 },
	{ DevCfgType::Checkbox, "alt_page", "Alternate Page" },
	{ DevCfgType::Checkbox, "shared_mem", "Shared Memory" },
};

static const DevCfgControl kCfgXEP80[] = {
	{ DevCfgType::IntDropdown, "port", "Joystick Port", kXEP80PortChoices, 4 },
};

static const DevCfgControl kCfgVeronica[] = {
	{ DevCfgType::Checkbox, "version1", "V1 (Three RAM Chips)" },
};

static const DevCfgControl kCfgCorvus[] = {
	{ DevCfgType::Checkbox, "altports", "Ports 1+2 (XL/XE Compatible)" },
};

static const DevCfgControl kCfgComputerEyes[] = {
	{ DevCfgType::IntInput, "brightness", "Brightness (0-100)" },
};

static const DevCfgControl kCfgParFileWriter[] = {
	{ DevCfgType::PathSelect, "path", "Output Path", nullptr, 0, false, "Select Output File" },
	{ DevCfgType::Checkbox, "text_mode", "Text Mode (EOL Conversion)" },
};

static const DevCfgControl kCfgVideoStillImage[] = {
	{ DevCfgType::PathSelect, "path", "Image Path", nullptr, 0, false, "Select Image File" },
};

static const DevCfgControl kCfgDongle[] = {
	{ DevCfgType::IntDropdown, "port", "Joystick Port", kDonglePortChoices, 4 },
	{ DevCfgType::StringEdit, "mapping", "Mapping (16 Hex Digits)" },
};

static const DevCfgControl kCfgPrinterHLE[] = {
	{ DevCfgType::IntDropdown, "translation_mode", "Translation Mode", kPrinterTranslationChoices, 3 },
};

static const DevCfgControl kCfgDiskDriveFull[] = {
	{ DevCfgType::IntDropdown, "id", "Drive ID", kDriveIDChoices, 4 },
};

static const DevCfgControl kCfgDiskDriveHappy810[] = {
	{ DevCfgType::IntDropdown, "id", "Drive ID", kDriveIDChoices, 4 },
	{ DevCfgType::Checkbox, "autospeed", "Auto-Speed" },
	{ DevCfgType::FloatInput, "autospeedrate", "Auto-Speed Rate (RPM)", nullptr, 0, false, nullptr, nullptr, 200.0f, 400.0f, 266.0f },
};

static const DevCfgControl kCfgDiskDrive815[] = {
	{ DevCfgType::IntDropdown, "id", "Drive Pair", k815IDChoices, 4 },
	{ DevCfgType::Checkbox, "accurate_invert", "Accurate Invert" },
};

static const DevCfgControl kCfgSoundBoard[] = {
	{ DevCfgType::IntDropdown, "version", "Hardware Version", kSoundBoardVersionChoices, 3 },
	{ DevCfgType::IntDropdown, "base", "Base Address", kSoundBoardBaseChoices, 4 },
};

static const DevCfgControl kCfgCovox[] = {
	{ DevCfgType::CompoundIntDropdown, "base", "Address Range", kCovoxRangeChoices, 6, false, "size", kCovoxRangeValues },
	{ DevCfgType::IntDropdown, "channels", "Channels", kCovoxChannelChoices, 2 },
};

static const DevCfgControl kCfg850[] = {
	{ DevCfgType::Checkbox, "unthrottled", "Unthrottled" },
	{ DevCfgType::Checkbox, "baudex", "Extended Baud Rates" },
	{ DevCfgType::IntDropdown, "emulevel", "Emulation Level", k850EmuLevelChoices, 3 },
};

static const DevCfgControl kCfg850Full[] = {
	{ DevCfgType::IntDropdown, "serbaud1", "Port 1 Baud", k850BaudChoices, 15 },
	{ DevCfgType::IntDropdown, "serbaud2", "Port 2 Baud", k850BaudChoices, 15 },
	{ DevCfgType::IntDropdown, "serbaud3", "Port 3 Baud", k850BaudChoices, 15 },
	{ DevCfgType::IntDropdown, "serbaud4", "Port 4 Baud", k850BaudChoices, 15 },
};

static const DevCfgControl kCfg1400XL[] = {
	{ DevCfgType::IntInput, "port", "Listen Port (0=disabled)" },
	{ DevCfgType::Checkbox, "outbound", "Allow Outbound", nullptr, 0, true },
	{ DevCfgType::Checkbox, "telnet", "Telnet Emulation", nullptr, 0, true },
	{ DevCfgType::Checkbox, "telnetlf", "Telnet LF Mode", nullptr, 0, true },
	{ DevCfgType::Checkbox, "ipv6", "Listen IPv6", nullptr, 0, true },
	{ DevCfgType::Checkbox, "unthrottled", "Unthrottled" },
	{ DevCfgType::StringEdit, "dialaddr", "Dial Address" },
	{ DevCfgType::StringEdit, "dialsvc", "Dial Service" },
};

static const DevCfgControl kCfgNetSerial[] = {
	{ DevCfgType::StringEdit, "connect_addr", "Address" },
	{ DevCfgType::IntInput, "port", "TCP Port" },
	{ DevCfgType::IntInput, "baud_rate", "Baud Rate" },
	{ DevCfgType::Checkbox, "listen", "Listen Mode" },
};

static const DevCfgControl kCfgMultiplexer[] = {
	{ DevCfgType::IntDropdown, "device_id", "Device ID", kMultiplexerIDChoices, 9 },
	{ DevCfgType::StringEdit, "host_address", "Host Address" },
	{ DevCfgType::IntInput, "port", "TCP Port" },
	{ DevCfgType::Checkbox, "allow_external", "Allow External Connections" },
};

static const DevCfgControl kCfgPipeSerial[] = {
	{ DevCfgType::StringEdit, "pipe_name", "Pipe Name" },
	{ DevCfgType::IntInput, "baud_rate", "Baud Rate" },
};

static const DevCfgControl kCfgBlackBox[] = {
	{ DevCfgType::BitfieldCheckbox, "dipsw", "DIP Switches", kBBDipSwitchBits, 7 },
	{ DevCfgType::IntDropdown, "blksize", "Sector Size", kBlkSizeChoices, 2 },
	{ DevCfgType::IntDropdown, "ramsize", "RAM Size", kBBRamSizeChoices, 3 },
};

static const DevCfgControl kCfgBlackBoxFloppy[] = {
	{ DevCfgType::IntDropdown, "driveslot0", "Slot 1 Drive", kBBFloppySlotChoices, 15 },
	{ DevCfgType::IntDropdown, "drivetype0", "Slot 1 Type", kBBFloppyTypeChoices, 7 },
	{ DevCfgType::IntDropdown, "drivemapping0", "Slot 1 Mapping", kBBFloppyMappingChoices, 3 },
	{ DevCfgType::IntDropdown, "driveslot1", "Slot 2 Drive", kBBFloppySlotChoices, 15 },
	{ DevCfgType::IntDropdown, "drivetype1", "Slot 2 Type", kBBFloppyTypeChoices, 7 },
	{ DevCfgType::IntDropdown, "drivemapping1", "Slot 2 Mapping", kBBFloppyMappingChoices, 3 },
	{ DevCfgType::IntDropdown, "driveslot2", "Slot 3 Drive", kBBFloppySlotChoices, 15 },
	{ DevCfgType::IntDropdown, "drivetype2", "Slot 3 Type", kBBFloppyTypeChoices, 7 },
	{ DevCfgType::IntDropdown, "drivemapping2", "Slot 3 Mapping", kBBFloppyMappingChoices, 3 },
	{ DevCfgType::IntDropdown, "driveslot3", "Slot 4 Drive", kBBFloppySlotChoices, 15 },
	{ DevCfgType::IntDropdown, "drivetype3", "Slot 4 Type", kBBFloppyTypeChoices, 7 },
	{ DevCfgType::IntDropdown, "drivemapping3", "Slot 4 Mapping", kBBFloppyMappingChoices, 3 },
};

static const DevCfgControl kCfgDiskDriveATR8000[] = {
	{ DevCfgType::IntDropdown, "drivetype0", "Drive 1 Type", kATR8000DriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype1", "Drive 2 Type", kATR8000DriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype2", "Drive 3 Type", kATR8000DriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype3", "Drive 4 Type", kATR8000DriveTypeChoices, 3 },
	{ DevCfgType::StringEdit, "signal1", "Signal 1 (rts/dtr)" },
	{ DevCfgType::StringEdit, "signal2", "Signal 2 (cts/dsr/cd/srts)" },
};

static const DevCfgControl kCfgDiskDrivePercom[] = {
	{ DevCfgType::IntDropdown, "id", "Drive ID", kIDChoices, 8 },
	{ DevCfgType::IntDropdown, "drivetype0", "Drive 1 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype1", "Drive 2 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype2", "Drive 3 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype3", "Drive 4 Type", kPercomDriveTypeChoices, 3 },
};

static const DevCfgControl kCfgDiskDrivePercomAT[] = {
	{ DevCfgType::CompoundIntDropdown, "use1795", "FDC Type", kPercomATFDCChoices, 3, true, "ddcapable", kPercomATFDCValues },
	{ DevCfgType::IntDropdown, "drivetype0", "Drive 1 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype1", "Drive 2 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype2", "Drive 3 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype3", "Drive 4 Type", kPercomDriveTypeChoices, 3 },
};

static const DevCfgControl kCfgDiskDrivePercomATSPD[] = {
	{ DevCfgType::Checkbox, "use1795", "Use 1795 FDC" },
	{ DevCfgType::IntDropdown, "drivetype0", "Drive 1 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype1", "Drive 2 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype2", "Drive 3 Type", kPercomDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "drivetype3", "Drive 4 Type", kPercomDriveTypeChoices, 3 },
};

static const DevCfgControl kCfgDiskDriveAMDC[] = {
	{ DevCfgType::IntInput, "switches", "DIP Switches" },
	{ DevCfgType::Checkbox, "drive2", "Second External Drive" },
	{ DevCfgType::IntDropdown, "extdrive0", "Ext Drive 1 Type", kAMDCDriveTypeChoices, 3 },
	{ DevCfgType::IntDropdown, "extdrive1", "Ext Drive 2 Type", kAMDCDriveTypeChoices, 3 },
};

static const DevCfgControl kCfg1020[] = {
	{ DevCfgType::IntDropdown, "pencolor0", "Pen 1 Color", k1020ColorChoices, 4 },
	{ DevCfgType::IntDropdown, "pencolor1", "Pen 2 Color", k1020ColorChoices, 4 },
	{ DevCfgType::IntDropdown, "pencolor2", "Pen 3 Color", k1020ColorChoices, 4 },
	{ DevCfgType::IntDropdown, "pencolor3", "Pen 4 Color", k1020ColorChoices, 4 },
};

// --- Tag → config mapping table ---

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

// ===== Device Configuration Dialog =====
// Shown when configuring a device — uses the DevCfg descriptor system.

class ATDeviceConfigDialog : public wxDialog {
public:
	ATDeviceConfigDialog(wxWindow *parent, IATDevice *dev);

	bool WasApplied() const { return mApplied; }
	const ATPropertySet& GetProperties() const { return mProps; }

private:
	void BuildStructuredUI(const DevCfgTagMapping *mapping);
	void BuildGenericUI();
	void PopulateFromProps();
	void CollectToProps();
	void OnOK(wxCommandEvent& event);
	void OnBrowse(wxCommandEvent& event);

	ATPropertySet mProps;
	bool mApplied = false;
	const DevCfgTagMapping *mpMapping = nullptr;

	// Dynamic controls for structured UI
	struct ControlEntry {
		const DevCfgControl *desc;
		wxWindow *widget;
		int browseId;
	};
	vdvector<ControlEntry> mControls;
	int mNextBrowseId = 4000;
};

ATDeviceConfigDialog::ATDeviceConfigDialog(wxWindow *parent, IATDevice *dev)
	: wxDialog(parent, wxID_ANY, "Device Configuration", wxDefaultPosition,
		wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	dev->GetSettings(mProps);

	ATDeviceInfo devInfo;
	dev->GetDeviceInfo(devInfo);

	const char *tag = devInfo.mpDef ? devInfo.mpDef->mpTag : "";
	mpMapping = FindDevCfgMapping(tag);

	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	if (mpMapping) {
		SetTitle(wxString::Format("Configure %s", mpMapping->title));
		BuildStructuredUI(mpMapping);
	} else {
		SetTitle("Device Configuration");
		BuildGenericUI();
	}

	topSizer->Add(GetSizer(), 1, wxEXPAND);

	// OK/Cancel
	wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->Add(new wxButton(this, wxID_OK, "OK"), 0, wxRIGHT, 5);
	btnSizer->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0);

	wxSizer *outerSizer = new wxBoxSizer(wxVERTICAL);
	if (GetSizer()) {
		// Re-parent sizer contents
		outerSizer->Add(GetSizer(), 1, wxEXPAND);
	}
	outerSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxALL, 5);

	SetSizer(nullptr);
	SetSizerAndFit(outerSizer);

	PopulateFromProps();

	Bind(wxEVT_BUTTON, &ATDeviceConfigDialog::OnOK, this, wxID_OK);
}

void ATDeviceConfigDialog::BuildStructuredUI(const DevCfgTagMapping *mapping) {
	wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
	grid->AddGrowableCol(1, 1);

	for (int i = 0; i < mapping->controlCount; ++i) {
		const DevCfgControl& ctrl = mapping->controls[i];
		ControlEntry entry;
		entry.desc = &ctrl;
		entry.browseId = -1;

		switch (ctrl.type) {
		case DevCfgType::Checkbox: {
			grid->Add(new wxStaticText(this, wxID_ANY, ""), 0);
			wxCheckBox *cb = new wxCheckBox(this, wxID_ANY, ctrl.label);
			entry.widget = cb;
			grid->Add(cb, 0);
			break;
		}
		case DevCfgType::IntDropdown:
		case DevCfgType::CompoundIntDropdown: {
			grid->Add(new wxStaticText(this, wxID_ANY, wxString(ctrl.label) + ":"),
				0, wxALIGN_CENTER_VERTICAL);
			wxChoice *ch = new wxChoice(this, wxID_ANY);
			for (int j = 0; j < ctrl.choiceCount; ++j)
				ch->Append(ctrl.choices[j].name);
			entry.widget = ch;
			grid->Add(ch, 1, wxEXPAND);
			break;
		}
		case DevCfgType::StringEdit: {
			grid->Add(new wxStaticText(this, wxID_ANY, wxString(ctrl.label) + ":"),
				0, wxALIGN_CENTER_VERTICAL);
			wxTextCtrl *tc = new wxTextCtrl(this, wxID_ANY);
			entry.widget = tc;
			grid->Add(tc, 1, wxEXPAND);
			break;
		}
		case DevCfgType::PathSelect: {
			grid->Add(new wxStaticText(this, wxID_ANY, wxString(ctrl.label) + ":"),
				0, wxALIGN_CENTER_VERTICAL);
			wxBoxSizer *pathRow = new wxBoxSizer(wxHORIZONTAL);
			wxTextCtrl *tc = new wxTextCtrl(this, wxID_ANY);
			entry.widget = tc;
			pathRow->Add(tc, 1, wxEXPAND | wxRIGHT, 3);
			int browseId = mNextBrowseId++;
			entry.browseId = browseId;
			wxButton *btn = new wxButton(this, browseId, "...", wxDefaultPosition, wxSize(30, -1));
			pathRow->Add(btn, 0);
			grid->Add(pathRow, 1, wxEXPAND);
			Bind(wxEVT_BUTTON, &ATDeviceConfigDialog::OnBrowse, this, browseId);
			break;
		}
		case DevCfgType::IntInput: {
			grid->Add(new wxStaticText(this, wxID_ANY, wxString(ctrl.label) + ":"),
				0, wxALIGN_CENTER_VERTICAL);
			wxSpinCtrl *sc = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition,
				wxDefaultSize, wxSP_ARROW_KEYS, 0, 999999, 0);
			entry.widget = sc;
			grid->Add(sc, 1, wxEXPAND);
			break;
		}
		case DevCfgType::FloatInput: {
			grid->Add(new wxStaticText(this, wxID_ANY, wxString(ctrl.label) + ":"),
				0, wxALIGN_CENTER_VERTICAL);
			wxSpinCtrlDouble *sd = new wxSpinCtrlDouble(this, wxID_ANY, "",
				wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
				ctrl.floatMin, ctrl.floatMax, ctrl.floatDefault, 0.1);
			entry.widget = sd;
			grid->Add(sd, 1, wxEXPAND);
			break;
		}
		case DevCfgType::BitfieldCheckbox: {
			grid->Add(new wxStaticText(this, wxID_ANY, wxString(ctrl.label) + ":"),
				0, wxALIGN_TOP);
			wxBoxSizer *bitSizer = new wxBoxSizer(wxVERTICAL);
			// Store all checkboxes in a panel
			wxPanel *panel = new wxPanel(this);
			wxBoxSizer *panelSizer = new wxBoxSizer(wxVERTICAL);
			for (int j = 0; j < ctrl.choiceCount; ++j) {
				wxCheckBox *cb = new wxCheckBox(panel, wxID_ANY, ctrl.choices[j].name);
				panelSizer->Add(cb, 0, wxBOTTOM, 2);
			}
			panel->SetSizer(panelSizer);
			entry.widget = panel;
			grid->Add(panel, 1, wxEXPAND);
			break;
		}
		}

		mControls.push_back(std::move(entry));
	}

	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(grid, 1, wxEXPAND | wxALL, 10);
	SetSizer(sizer);
}

void ATDeviceConfigDialog::BuildGenericUI() {
	wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
	grid->AddGrowableCol(1, 1);

	// Show a message that this device uses generic property editor
	grid->Add(new wxStaticText(this, wxID_ANY, "This device has no structured configuration."),
		0, wxALL, 5);
	grid->Add(new wxStaticText(this, wxID_ANY, ""), 0);

	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(grid, 1, wxEXPAND | wxALL, 10);
	SetSizer(sizer);
}

void ATDeviceConfigDialog::PopulateFromProps() {
	if (!mpMapping)
		return;

	for (auto& entry : mControls) {
		const DevCfgControl& ctrl = *entry.desc;

		switch (ctrl.type) {
		case DevCfgType::Checkbox: {
			wxCheckBox *cb = static_cast<wxCheckBox*>(entry.widget);
			cb->SetValue(mProps.GetBool(ctrl.propKey, ctrl.defaultBool));
			break;
		}
		case DevCfgType::IntDropdown: {
			wxChoice *ch = static_cast<wxChoice*>(entry.widget);
			uint32 val = mProps.GetUint32(ctrl.propKey, 0);
			int sel = 0;
			for (int j = 0; j < ctrl.choiceCount; ++j) {
				if ((uint32)ctrl.choices[j].value == val) {
					sel = j;
					break;
				}
			}
			ch->SetSelection(sel);
			break;
		}
		case DevCfgType::CompoundIntDropdown: {
			wxChoice *ch = static_cast<wxChoice*>(entry.widget);
			if (ctrl.defaultBool) {
				// Bool-based compound: match on first prop
				bool v1 = mProps.GetBool(ctrl.propKey, false);
				bool v2 = mProps.GetBool(ctrl.browseTitle, false);
				int sel = 0;
				for (int j = 0; j < ctrl.choiceCount && ctrl.pairs; ++j) {
					if ((bool)ctrl.pairs[j].val1 == v1 && (bool)ctrl.pairs[j].val2 == v2) {
						sel = j;
						break;
					}
				}
				ch->SetSelection(sel);
			} else {
				uint32 v1 = mProps.GetUint32(ctrl.propKey, 0);
				uint32 v2 = ctrl.browseTitle ? mProps.GetUint32(ctrl.browseTitle, 0) : 0;
				int sel = 0;
				for (int j = 0; j < ctrl.choiceCount && ctrl.pairs; ++j) {
					if (ctrl.pairs[j].val1 == v1 && ctrl.pairs[j].val2 == v2) {
						sel = j;
						break;
					}
				}
				ch->SetSelection(sel);
			}
			break;
		}
		case DevCfgType::StringEdit: {
			wxTextCtrl *tc = static_cast<wxTextCtrl*>(entry.widget);
			const wchar_t *val = mProps.GetString(ctrl.propKey, L"");
			tc->SetValue(VDTextWToU8(VDStringW(val)).c_str());
			break;
		}
		case DevCfgType::PathSelect: {
			wxTextCtrl *tc = static_cast<wxTextCtrl*>(entry.widget);
			const wchar_t *val = mProps.GetString(ctrl.propKey, L"");
			tc->SetValue(VDTextWToU8(VDStringW(val)).c_str());
			break;
		}
		case DevCfgType::IntInput: {
			wxSpinCtrl *sc = static_cast<wxSpinCtrl*>(entry.widget);
			sc->SetValue((int)mProps.GetUint32(ctrl.propKey, 0));
			break;
		}
		case DevCfgType::FloatInput: {
			wxSpinCtrlDouble *sd = static_cast<wxSpinCtrlDouble*>(entry.widget);
			sd->SetValue((double)mProps.GetFloat(ctrl.propKey, ctrl.floatDefault));
			break;
		}
		case DevCfgType::BitfieldCheckbox: {
			wxPanel *panel = static_cast<wxPanel*>(entry.widget);
			uint32 bits = mProps.GetUint32(ctrl.propKey, 0);
			auto children = panel->GetChildren();
			int j = 0;
			for (auto it = children.begin(); it != children.end() && j < ctrl.choiceCount; ++it, ++j) {
				wxCheckBox *cb = dynamic_cast<wxCheckBox*>(*it);
				if (cb)
					cb->SetValue((bits & (uint32)ctrl.choices[j].value) != 0);
			}
			break;
		}
		}
	}
}

void ATDeviceConfigDialog::CollectToProps() {
	if (!mpMapping)
		return;

	for (auto& entry : mControls) {
		const DevCfgControl& ctrl = *entry.desc;

		switch (ctrl.type) {
		case DevCfgType::Checkbox: {
			wxCheckBox *cb = static_cast<wxCheckBox*>(entry.widget);
			mProps.SetBool(ctrl.propKey, cb->GetValue());
			break;
		}
		case DevCfgType::IntDropdown: {
			wxChoice *ch = static_cast<wxChoice*>(entry.widget);
			int sel = ch->GetSelection();
			if (sel >= 0 && sel < ctrl.choiceCount)
				mProps.SetUint32(ctrl.propKey, (uint32)ctrl.choices[sel].value);
			break;
		}
		case DevCfgType::CompoundIntDropdown: {
			wxChoice *ch = static_cast<wxChoice*>(entry.widget);
			int sel = ch->GetSelection();
			if (sel >= 0 && sel < ctrl.choiceCount && ctrl.pairs) {
				if (ctrl.defaultBool) {
					mProps.SetBool(ctrl.propKey, (bool)ctrl.pairs[sel].val1);
					if (ctrl.browseTitle)
						mProps.SetBool(ctrl.browseTitle, (bool)ctrl.pairs[sel].val2);
				} else {
					mProps.SetUint32(ctrl.propKey, ctrl.pairs[sel].val1);
					if (ctrl.browseTitle)
						mProps.SetUint32(ctrl.browseTitle, ctrl.pairs[sel].val2);
				}
			}
			break;
		}
		case DevCfgType::StringEdit: {
			wxTextCtrl *tc = static_cast<wxTextCtrl*>(entry.widget);
			VDStringW val = VDTextU8ToW(VDStringA(tc->GetValue().utf8_str()));
			mProps.SetString(ctrl.propKey, val.c_str());
			break;
		}
		case DevCfgType::PathSelect: {
			wxTextCtrl *tc = static_cast<wxTextCtrl*>(entry.widget);
			VDStringW val = VDTextU8ToW(VDStringA(tc->GetValue().utf8_str()));
			mProps.SetString(ctrl.propKey, val.c_str());
			break;
		}
		case DevCfgType::IntInput: {
			wxSpinCtrl *sc = static_cast<wxSpinCtrl*>(entry.widget);
			mProps.SetUint32(ctrl.propKey, (uint32)sc->GetValue());
			break;
		}
		case DevCfgType::FloatInput: {
			wxSpinCtrlDouble *sd = static_cast<wxSpinCtrlDouble*>(entry.widget);
			mProps.SetFloat(ctrl.propKey, (float)sd->GetValue());
			break;
		}
		case DevCfgType::BitfieldCheckbox: {
			wxPanel *panel = static_cast<wxPanel*>(entry.widget);
			uint32 bits = 0;
			auto children = panel->GetChildren();
			int j = 0;
			for (auto it = children.begin(); it != children.end() && j < ctrl.choiceCount; ++it, ++j) {
				wxCheckBox *cb = dynamic_cast<wxCheckBox*>(*it);
				if (cb && cb->GetValue())
					bits |= (uint32)ctrl.choices[j].value;
			}
			mProps.SetUint32(ctrl.propKey, bits);
			break;
		}
		}
	}
}

void ATDeviceConfigDialog::OnOK(wxCommandEvent&) {
	CollectToProps();
	mApplied = true;
	EndModal(wxID_OK);
}

void ATDeviceConfigDialog::OnBrowse(wxCommandEvent& event) {
	int browseId = event.GetId();
	for (auto& entry : mControls) {
		if (entry.browseId == browseId && entry.desc->type == DevCfgType::PathSelect) {
			wxTextCtrl *tc = static_cast<wxTextCtrl*>(entry.widget);
			const char *title = entry.desc->browseTitle ? entry.desc->browseTitle : "Browse";
			wxFileDialog dlg(this, title, "", "",
				"All files (*)|*", wxFD_OPEN);
			if (dlg.ShowModal() == wxID_OK)
				tc->SetValue(dlg.GetPath());
			break;
		}
	}
}

// ===== Device Manager Dialog =====

class ATDeviceManagerDialog : public wxDialog {
public:
	ATDeviceManagerDialog(wxWindow *parent);

private:
	void PopulateDeviceList();
	void OnAddDevice(wxCommandEvent& event);
	void OnConfigureDevice(wxCommandEvent& event);
	void OnRemoveDevice(wxCommandEvent& event);
	void OnClose(wxCommandEvent& event);

	wxListCtrl *mpDeviceList = nullptr;

	struct DeviceEntry {
		IATDevice *dev;
		VDStringA tag;
		VDStringA name;
		VDStringA blurb;
	};
	vdvector<DeviceEntry> mDeviceEntries;

	enum { ID_ADD = 3300, ID_CONFIGURE, ID_REMOVE };
};

ATDeviceManagerDialog::ATDeviceManagerDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, "Device Manager", wxDefaultPosition,
		wxSize(600, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

	// Toolbar
	wxBoxSizer *toolRow = new wxBoxSizer(wxHORIZONTAL);
	toolRow->Add(new wxButton(this, ID_ADD, "Add..."), 0, wxRIGHT, 3);
	toolRow->Add(new wxButton(this, ID_CONFIGURE, "Configure..."), 0, wxRIGHT, 3);
	toolRow->Add(new wxButton(this, ID_REMOVE, "Remove"), 0);
	topSizer->Add(toolRow, 0, wxALL, 5);

	// Device list
	mpDeviceList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition,
		wxSize(-1, 300), wxLC_REPORT | wxLC_SINGLE_SEL);
	mpDeviceList->AppendColumn("Device", wxLIST_FORMAT_LEFT, 200);
	mpDeviceList->AppendColumn("Type", wxLIST_FORMAT_LEFT, 150);
	mpDeviceList->AppendColumn("Settings", wxLIST_FORMAT_LEFT, 200);
	topSizer->Add(mpDeviceList, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

	// Close button
	topSizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 5);

	SetSizer(topSizer);

	PopulateDeviceList();

	Bind(wxEVT_BUTTON, &ATDeviceManagerDialog::OnAddDevice, this, ID_ADD);
	Bind(wxEVT_BUTTON, &ATDeviceManagerDialog::OnConfigureDevice, this, ID_CONFIGURE);
	Bind(wxEVT_BUTTON, &ATDeviceManagerDialog::OnRemoveDevice, this, ID_REMOVE);
	Bind(wxEVT_BUTTON, &ATDeviceManagerDialog::OnClose, this, wxID_CLOSE);
}

void ATDeviceManagerDialog::PopulateDeviceList() {
	mpDeviceList->DeleteAllItems();
	mDeviceEntries.clear();

	ATDeviceManager *devMgr = g_sim.GetDeviceManager();
	uint32 count = devMgr->GetDeviceCount();

	for (uint32 i = 0; i < count; ++i) {
		IATDevice *dev = devMgr->GetDeviceByIndex(i);
		if (!dev)
			continue;

		ATDeviceInfo devInfo;
		dev->GetDeviceInfo(devInfo);

		if (!devInfo.mpDef)
			continue;

		// Skip internal/hidden devices
		if (devInfo.mpDef->mFlags & (kATDeviceDefFlag_Internal | kATDeviceDefFlag_Hidden))
			continue;

		DeviceEntry entry;
		entry.dev = dev;
		entry.tag = devInfo.mpDef->mpTag;
		entry.name = VDTextWToU8(VDStringW(devInfo.mpDef->mpName));

		VDStringW blurbW;
		dev->GetSettingsBlurb(blurbW);
		entry.blurb = VDTextWToU8(blurbW);

		long idx = mpDeviceList->InsertItem(mpDeviceList->GetItemCount(), entry.name.c_str());
		mpDeviceList->SetItem(idx, 1, entry.tag.c_str());
		mpDeviceList->SetItem(idx, 2, entry.blurb.c_str());

		mDeviceEntries.push_back(std::move(entry));
	}
}

void ATDeviceManagerDialog::OnAddDevice(wxCommandEvent&) {
	ATDeviceManager *devMgr = g_sim.GetDeviceManager();
	const auto& defs = devMgr->GetDeviceDefinitions();

	// Build sorted list of available devices
	struct DevDef {
		const ATDeviceDefinition *def;
		VDStringA name;
	};
	vdvector<DevDef> available;

	for (const ATDeviceDefinition *def : defs) {
		if (!def || (def->mFlags & (kATDeviceDefFlag_Internal | kATDeviceDefFlag_Hidden)))
			continue;
		DevDef dd;
		dd.def = def;
		dd.name = VDTextWToU8(VDStringW(def->mpName));
		available.push_back(std::move(dd));
	}

	std::sort(available.begin(), available.end(),
		[](const DevDef& a, const DevDef& b) { return a.name < b.name; });

	// Show selection dialog
	wxArrayString choices;
	for (const auto& dd : available)
		choices.Add(dd.name.c_str());

	wxSingleChoiceDialog dlg(this, "Select device to add:", "Add Device", choices);
	if (dlg.ShowModal() != wxID_OK)
		return;

	int sel = dlg.GetSelection();
	if (sel < 0 || sel >= (int)available.size())
		return;

	ATPropertySet emptyProps;
	devMgr->AddDevice(available[sel].def, emptyProps);
	PopulateDeviceList();
}

void ATDeviceManagerDialog::OnConfigureDevice(wxCommandEvent&) {
	long sel = mpDeviceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= (long)mDeviceEntries.size())
		return;

	IATDevice *dev = mDeviceEntries[sel].dev;

	ATDeviceConfigDialog cfgDlg(this, dev);
	if (cfgDlg.ShowModal() == wxID_OK && cfgDlg.WasApplied()) {
		ATPropertySet newProps = cfgDlg.GetProperties();
		g_sim.GetDeviceManager()->ReconfigureDevice(*dev, newProps);
		PopulateDeviceList();
	}
}

void ATDeviceManagerDialog::OnRemoveDevice(wxCommandEvent&) {
	long sel = mpDeviceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= (long)mDeviceEntries.size())
		return;

	IATDevice *dev = mDeviceEntries[sel].dev;
	g_sim.GetDeviceManager()->RemoveDevice(dev);
	PopulateDeviceList();
}

void ATDeviceManagerDialog::OnClose(wxCommandEvent&) {
	EndModal(wxID_CLOSE);
}

} // anonymous namespace

void ATShowDeviceManagerDialog(wxWindow *parent) {
	ATDeviceManagerDialog dlg(parent);
	dlg.ShowModal();
}
