//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - Firmware type display name tables
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef f_AT_SHELL_FIRMWARE_NAMES_H
#define f_AT_SHELL_FIRMWARE_NAMES_H

#include "firmwaremanager.h"

inline const char *ATGetFirmwareTypeDisplayName(ATFirmwareType type) {
	switch(type) {
		case kATFirmwareType_Kernel800_OSA:				return "400/800 OS-A";
		case kATFirmwareType_Kernel800_OSB:				return "400/800 OS-B";
		case kATFirmwareType_Kernel1200XL:				return "1200XL OS";
		case kATFirmwareType_KernelXL:					return "XL/XE OS";
		case kATFirmwareType_KernelXEGS:				return "XEGS OS";
		case kATFirmwareType_Game:						return "XEGS Game";
		case kATFirmwareType_1400XLHandler:				return "1400XL Handler";
		case kATFirmwareType_1450XLDiskHandler:			return "1450XLD Disk Handler";
		case kATFirmwareType_1450XLDiskController:		return "1450XLD Disk Controller";
		case kATFirmwareType_1450XLTONGDiskController:	return "\"TONG\" Disk Controller";
		case kATFirmwareType_Kernel5200:				return "5200 OS";
		case kATFirmwareType_Basic:						return "BASIC";
		case kATFirmwareType_820:						return "820";
		case kATFirmwareType_1025:						return "1025";
		case kATFirmwareType_1029:						return "1029";
		case kATFirmwareType_810:						return "810";
		case kATFirmwareType_Happy810:					return "Happy 810";
		case kATFirmwareType_810Archiver:				return "810 Archiver";
		case kATFirmwareType_810Turbo:					return "810 Turbo";
		case kATFirmwareType_815:						return "815";
		case kATFirmwareType_1050:						return "1050";
		case kATFirmwareType_1050Duplicator:			return "1050 Duplicator";
		case kATFirmwareType_USDoubler:					return "US Doubler";
		case kATFirmwareType_Speedy1050:				return "Speedy 1050";
		case kATFirmwareType_Happy1050:					return "Happy 1050";
		case kATFirmwareType_SuperArchiver:				return "Super Archiver";
		case kATFirmwareType_TOMS1050:					return "TOMS 1050";
		case kATFirmwareType_Tygrys1050:				return "Tygrys 1050";
		case kATFirmwareType_IndusGT:					return "Indus GT";
		case kATFirmwareType_1050Turbo:					return "1050 Turbo";
		case kATFirmwareType_1050TurboII:				return "1050 Turbo II";
		case kATFirmwareType_ISPlate:					return "I.S. Plate";
		case kATFirmwareType_XF551:						return "XF551";
		case kATFirmwareType_ATR8000:					return "ATR8000";
		case kATFirmwareType_Percom:					return "PERCOM RFD";
		case kATFirmwareType_PercomAT:					return "PERCOM AT-88";
		case kATFirmwareType_PercomATSPD:				return "PERCOM AT88-SPD";
		case kATFirmwareType_AMDC:						return "Amdek AMDC";
		case kATFirmwareType_SpeedyXF:					return "Speedy XF";
		case kATFirmwareType_U1MB:						return "Ultimate1MB";
		case kATFirmwareType_MyIDE2:					return "MyIDE-II";
		case kATFirmwareType_SIDE:						return "SIDE";
		case kATFirmwareType_SIDE2:						return "SIDE 2";
		case kATFirmwareType_SIDE3:						return "SIDE 3";
		case kATFirmwareType_KMKJZIDE:					return "KMK/JZ IDE";
		case kATFirmwareType_KMKJZIDE2:					return "KMK/JZ IDE 2 Main";
		case kATFirmwareType_KMKJZIDE2_SDX:				return "KMK/JZ IDE 2 SDX";
		case kATFirmwareType_BlackBox:					return "Black Box";
		case kATFirmwareType_BlackBoxFloppy:			return "BB Floppy Board";
		case kATFirmwareType_MIO:						return "MIO";
		case kATFirmwareType_835:						return "835";
		case kATFirmwareType_850:						return "850";
		case kATFirmwareType_1030Firmware:				return "1030 Download";
		case kATFirmwareType_1030InternalROM:			return "1030 Internal";
		case kATFirmwareType_1030ExternalROM:			return "1030 External";
		case kATFirmwareType_RapidusFlash:				return "Rapidus Flash";
		case kATFirmwareType_RapidusCorePBI:			return "Rapidus Core";
		case kATFirmwareType_WarpOS:					return "APE Warp+";
		case kATFirmwareType_1090Firmware:				return "1090 Firmware";
		case kATFirmwareType_1090Charset:				return "1090 Charset";
		case kATFirmwareType_Bit3Firmware:				return "Bit3 Firmware";
		case kATFirmwareType_Bit3Charset:				return "Bit3 Charset";
		default:										return "Unknown";
	}
}

inline const char *ATGetFirmwareTypeCategoryName(ATFirmwareType type) {
	switch(type) {
		case kATFirmwareType_Kernel800_OSA:
		case kATFirmwareType_Kernel800_OSB:
		case kATFirmwareType_Kernel1200XL:
		case kATFirmwareType_KernelXL:
		case kATFirmwareType_KernelXEGS:
		case kATFirmwareType_Game:
		case kATFirmwareType_1400XLHandler:
		case kATFirmwareType_1450XLDiskHandler:
		case kATFirmwareType_1450XLDiskController:
		case kATFirmwareType_1450XLTONGDiskController:
		case kATFirmwareType_Kernel5200:
		case kATFirmwareType_Basic:
			return "Computer";

		case kATFirmwareType_820:
		case kATFirmwareType_1025:
		case kATFirmwareType_1029:
			return "Printers";

		case kATFirmwareType_810:
		case kATFirmwareType_Happy810:
		case kATFirmwareType_810Archiver:
		case kATFirmwareType_810Turbo:
		case kATFirmwareType_815:
		case kATFirmwareType_1050:
		case kATFirmwareType_1050Duplicator:
		case kATFirmwareType_USDoubler:
		case kATFirmwareType_Speedy1050:
		case kATFirmwareType_Happy1050:
		case kATFirmwareType_SuperArchiver:
		case kATFirmwareType_TOMS1050:
		case kATFirmwareType_Tygrys1050:
		case kATFirmwareType_IndusGT:
		case kATFirmwareType_1050Turbo:
		case kATFirmwareType_1050TurboII:
		case kATFirmwareType_ISPlate:
		case kATFirmwareType_XF551:
		case kATFirmwareType_ATR8000:
		case kATFirmwareType_Percom:
		case kATFirmwareType_PercomAT:
		case kATFirmwareType_PercomATSPD:
		case kATFirmwareType_AMDC:
		case kATFirmwareType_SpeedyXF:
			return "Disk Drives";

		default:
			return "Hardware";
	}
}

#endif	// f_AT_SHELL_FIRMWARE_NAMES_H
