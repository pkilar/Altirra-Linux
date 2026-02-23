//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - Cartridge mode name tables
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef f_AT_SHELL_CARTRIDGE_NAMES_H
#define f_AT_SHELL_CARTRIDGE_NAMES_H

#include <at/atio/cartridgetypes.h>

inline const char *ATGetCartridgeModeName(int mode) {
	switch(mode) {
		case kATCartridgeMode_8K:					return "8K";
		case kATCartridgeMode_16K:					return "16K";
		case kATCartridgeMode_OSS_034M:				return "OSS '034M'";
		case kATCartridgeMode_5200_32K:				return "5200 32K";
		case kATCartridgeMode_DB_32K:				return "DB 32K";
		case kATCartridgeMode_5200_16K_TwoChip:		return "5200 16K (two chip)";
		case kATCartridgeMode_BountyBob5200:		return "Bounty Bob (5200)";
		case kATCartridgeMode_Williams_64K:			return "Williams 64K";
		case kATCartridgeMode_Express_64K:			return "Express 64K";
		case kATCartridgeMode_Diamond_64K:			return "Diamond 64K";
		case kATCartridgeMode_SpartaDosX_64K:		return "SpartaDOS X 64K";
		case kATCartridgeMode_XEGS_32K:				return "32K XEGS";
		case kATCartridgeMode_XEGS_64K:				return "64K XEGS";
		case kATCartridgeMode_XEGS_128K:			return "128K XEGS";
		case kATCartridgeMode_OSS_M091:				return "OSS 'M091'";
		case kATCartridgeMode_5200_16K_OneChip:		return "5200 16K (one chip)";
		case kATCartridgeMode_Atrax_128K:			return "Atrax 128K (decoded order)";
		case kATCartridgeMode_BountyBob800:			return "Bounty Bob (800)";
		case kATCartridgeMode_5200_8K:				return "5200 8K";
		case kATCartridgeMode_5200_4K:				return "5200 4K";
		case kATCartridgeMode_RightSlot_8K:			return "Right slot 8K";
		case kATCartridgeMode_Williams_32K:			return "Williams 32K";
		case kATCartridgeMode_XEGS_256K:			return "256K XEGS";
		case kATCartridgeMode_XEGS_512K:			return "512K XEGS";
		case kATCartridgeMode_XEGS_1M:				return "1M XEGS";
		case kATCartridgeMode_MegaCart_16K:			return "16K MegaCart";
		case kATCartridgeMode_MegaCart_32K:			return "32K MegaCart";
		case kATCartridgeMode_MegaCart_64K:			return "64K MegaCart";
		case kATCartridgeMode_MegaCart_128K:		return "128K MegaCart";
		case kATCartridgeMode_MegaCart_256K:		return "256K MegaCart";
		case kATCartridgeMode_MegaCart_512K:		return "512K MegaCart";
		case kATCartridgeMode_MegaCart_1M:			return "1M MegaCart";
		case kATCartridgeMode_MegaCart_2M:			return "2M MegaCart";
		case kATCartridgeMode_Switchable_XEGS_32K:	return "32K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_64K:	return "64K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_128K:	return "128K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_256K:	return "256K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_512K:	return "512K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_1M:	return "1M Switchable XEGS";
		case kATCartridgeMode_Phoenix_8K:			return "Phoenix 8K";
		case kATCartridgeMode_Blizzard_16K:			return "Blizzard 16K";
		case kATCartridgeMode_MaxFlash_128K:		return "MaxFlash 128K / 1Mbit";
		case kATCartridgeMode_MaxFlash_1024K:		return "MaxFlash 1M / 8Mbit - older (bank 127)";
		case kATCartridgeMode_SpartaDosX_128K:		return "SpartaDOS X 128K";
		case kATCartridgeMode_OSS_8K:				return "OSS 8K";
		case kATCartridgeMode_OSS_043M:				return "OSS '043M'";
		case kATCartridgeMode_Blizzard_4K:			return "Blizzard 4K";
		case kATCartridgeMode_AST_32K:				return "AST 32K";
		case kATCartridgeMode_Atrax_SDX_64K:		return "Atrax SDX 64K";
		case kATCartridgeMode_Atrax_SDX_128K:		return "Atrax SDX 128K";
		case kATCartridgeMode_Turbosoft_64K:		return "Turbosoft 64K";
		case kATCartridgeMode_Turbosoft_128K:		return "Turbosoft 128K";
		case kATCartridgeMode_MaxFlash_128K_MyIDE:	return "MaxFlash 128K + MyIDE";
		case kATCartridgeMode_Corina_1M_EEPROM:		return "Corina 1M + 8K EEPROM";
		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:	return "Corina 512K + 512K SRAM + 8K EEPROM";
		case kATCartridgeMode_TelelinkII:			return "8K Telelink II";
		case kATCartridgeMode_SIC_128K:				return "SIC! 128K";
		case kATCartridgeMode_SIC_256K:				return "SIC! 256K";
		case kATCartridgeMode_SIC_512K:				return "SIC! 512K";
		case kATCartridgeMode_MaxFlash_1024K_Bank0:	return "MaxFlash 1M / 8Mbit - newer (bank 0)";
		case kATCartridgeMode_MegaCart_1M_2:		return "Megacart 1M (2)";
		case kATCartridgeMode_5200_64K_32KBanks:	return "5200 64K Super Cart (32K banks)";
		case kATCartridgeMode_5200_128K_32KBanks:	return "5200 128K Super Cart (32K banks)";
		case kATCartridgeMode_5200_256K_32KBanks:	return "5200 256K Super Cart (32K banks)";
		case kATCartridgeMode_5200_512K_32KBanks:	return "5200 512K Super Cart (32K banks)";
		case kATCartridgeMode_MicroCalc:			return "MicroCalc 32K";
		case kATCartridgeMode_2K:					return "2K";
		case kATCartridgeMode_4K:					return "4K";
		case kATCartridgeMode_RightSlot_4K:			return "Right slot 4K";
		case kATCartridgeMode_Blizzard_32K:			return "Blizzard 32K";
		case kATCartridgeMode_MegaCart_512K_3:		return "MegaCart 512K (3)";
		case kATCartridgeMode_MegaMax_2M:			return "MegaMax 2M";
		case kATCartridgeMode_TheCart_128M:			return "The!Cart 128M";
		case kATCartridgeMode_MegaCart_4M_3:		return "MegaCart 4M (3)";
		case kATCartridgeMode_TheCart_32M:			return "The!Cart 32M";
		case kATCartridgeMode_TheCart_64M:			return "The!Cart 64M";
		case kATCartridgeMode_BountyBob5200Alt:		return "Bounty Bob (5200) - Alternate layout";
		case kATCartridgeMode_XEGS_64K_Alt:			return "XEGS 64K (alternate)";
		case kATCartridgeMode_Atrax_128K_Raw:		return "Atrax 128K (raw order)";
		case kATCartridgeMode_aDawliah_32K:			return "aDawliah 32K";
		case kATCartridgeMode_aDawliah_64K:			return "aDawliah 64K";
		case kATCartridgeMode_JRC6_64K:				return "JRC 64K";
		case kATCartridgeMode_JRC_RAMBOX:			return "JRC RAMBOX";
		case kATCartridgeMode_XEMulticart_8K:		return "XE Multicart (8K)";
		case kATCartridgeMode_XEMulticart_16K:		return "XE Multicart (16K)";
		case kATCartridgeMode_XEMulticart_32K:		return "XE Multicart (32K)";
		case kATCartridgeMode_XEMulticart_64K:		return "XE Multicart (64K)";
		case kATCartridgeMode_XEMulticart_128K:		return "XE Multicart (128K)";
		case kATCartridgeMode_XEMulticart_256K:		return "XE Multicart (256K)";
		case kATCartridgeMode_XEMulticart_512K:		return "XE Multicart (512K)";
		case kATCartridgeMode_XEMulticart_1M:		return "XE Multicart (1MB)";
		case kATCartridgeMode_SICPlus:				return "SIC+";
		case kATCartridgeMode_Williams_16K:			return "Williams 16K";
		case kATCartridgeMode_MDDOS:				return "MDDOS";
		case kATCartridgeMode_COS32K:				return "COS 32K";
		case kATCartridgeMode_Pronto:				return "Pronto";
		case kATCartridgeMode_JAtariCart_8K:		return "J(atari)Cart 8K";
		case kATCartridgeMode_JAtariCart_16K:		return "J(atari)Cart 16K";
		case kATCartridgeMode_JAtariCart_32K:		return "J(atari)Cart 32K";
		case kATCartridgeMode_JAtariCart_64K:		return "J(atari)Cart 64K";
		case kATCartridgeMode_JAtariCart_128K:		return "J(atari)Cart 128K";
		case kATCartridgeMode_JAtariCart_256K:		return "J(atari)Cart 256K";
		case kATCartridgeMode_JAtariCart_512K:		return "J(atari)Cart 512K";
		case kATCartridgeMode_JAtariCart_1024K:		return "J(atari)Cart 1MB";
		case kATCartridgeMode_DCart:				return "DCart";
		default:									return "";
	}
}

inline const char *ATGetCartridgeModeDesc(int mode) {
	switch(mode) {
		case kATCartridgeMode_8K:					return "8K fixed";
		case kATCartridgeMode_16K:					return "16K fixed";
		case kATCartridgeMode_OSS_034M:				return "4K banked by CCTL data + 4K fixed";
		case kATCartridgeMode_5200_32K:				return "32K fixed";
		case kATCartridgeMode_DB_32K:				return "8K banked by CCTL address + 8K fixed";
		case kATCartridgeMode_5200_16K_TwoChip:		return "16K fixed";
		case kATCartridgeMode_BountyBob800:
		case kATCartridgeMode_BountyBob5200:
		case kATCartridgeMode_BountyBob5200Alt:		return "4K+4K banked by $4/5FF6-9 + 8K fixed";

		case kATCartridgeMode_Williams_64K:			[[fallthrough]];
		case kATCartridgeMode_Williams_32K:			[[fallthrough]];
		case kATCartridgeMode_Williams_16K:			return "8K banked by CCTL address (switchable)";

		case kATCartridgeMode_Express_64K:			return "8K banked by CCTL $D57x (switchable)";
		case kATCartridgeMode_Diamond_64K:			return "8K banked by CCTL $D5Dx (switchable)";
		case kATCartridgeMode_Atrax_SDX_64K:
		case kATCartridgeMode_SpartaDosX_64K:		return "8K banked by CCTL $D5Ex (switchable)";

		case kATCartridgeMode_XEGS_32K:
		case kATCartridgeMode_XEGS_64K:
		case kATCartridgeMode_XEGS_64K_Alt:
		case kATCartridgeMode_XEGS_128K:
		case kATCartridgeMode_XEGS_256K:
		case kATCartridgeMode_XEGS_512K:
		case kATCartridgeMode_XEGS_1M:				return "8K banked by CCTL data + 8K fixed (switchable)";

		case kATCartridgeMode_OSS_M091:				return "4K banked by CCTL data + 4K fixed";
		case kATCartridgeMode_5200_16K_OneChip:		return "16K fixed";
		case kATCartridgeMode_Atrax_128K:
		case kATCartridgeMode_Atrax_128K_Raw:		return "8K banked by CCTL data (switchable)";
		case kATCartridgeMode_5200_8K:				return "8K fixed";
		case kATCartridgeMode_5200_4K:				return "4K fixed";
		case kATCartridgeMode_RightSlot_8K:			return "8K right slot fixed";

		case kATCartridgeMode_MegaCart_16K:
		case kATCartridgeMode_MegaCart_32K:
		case kATCartridgeMode_MegaCart_64K:
		case kATCartridgeMode_MegaCart_128K:
		case kATCartridgeMode_MegaCart_256K:
		case kATCartridgeMode_MegaCart_512K:
		case kATCartridgeMode_MegaCart_1M:
		case kATCartridgeMode_MegaCart_2M:			return "16K banked by CCTL data (switchable)";

		case kATCartridgeMode_Switchable_XEGS_32K:
		case kATCartridgeMode_Switchable_XEGS_64K:
		case kATCartridgeMode_Switchable_XEGS_128K:
		case kATCartridgeMode_Switchable_XEGS_256K:
		case kATCartridgeMode_Switchable_XEGS_512K:
		case kATCartridgeMode_Switchable_XEGS_1M:	return "8K banked by CCTL data + 8K fixed (switchable)";

		case kATCartridgeMode_Phoenix_8K:			return "8K fixed (one-time disable)";
		case kATCartridgeMode_Blizzard_4K:			return "8K fixed (one-time disable)";
		case kATCartridgeMode_Blizzard_16K:			return "16K fixed (one-time disable)";
		case kATCartridgeMode_Blizzard_32K:			return "8K banked (autoincrement + disable)";

		case kATCartridgeMode_MaxFlash_128K:		return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_MaxFlash_1024K:		return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_MaxFlash_1024K_Bank0:	return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_MaxFlash_128K_MyIDE:	return "8K banked + CCTL keyhole (switchable)";

		case kATCartridgeMode_Atrax_SDX_128K:
		case kATCartridgeMode_SpartaDosX_128K:		return "8K banked by CCTL $D5E0-D5FF address (switchable)";

		case kATCartridgeMode_OSS_8K:				return "4K banked by CCTL data + 4K fixed";
		case kATCartridgeMode_OSS_043M:				return "4K banked by CCTL data + 4K fixed";
		case kATCartridgeMode_AST_32K:				return "8K disableable + CCTL autoincrement by write";

		case kATCartridgeMode_Turbosoft_64K:
		case kATCartridgeMode_Turbosoft_128K:		return "8K banked by CCTL address (switchable)";

		case kATCartridgeMode_Corina_1M_EEPROM:
		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:	return "8K+8K banked (complex)";

		case kATCartridgeMode_TelelinkII:			return "8K fixed + EEPROM";
		case kATCartridgeMode_SIC_128K:
		case kATCartridgeMode_SIC_256K:
		case kATCartridgeMode_SIC_512K:				return "16K banked by CCTL $D500-D51F access (8K+8K switchable)";
		case kATCartridgeMode_MegaCart_1M_2:		return "8K banked by CCTL data (switchable)";
		case kATCartridgeMode_5200_64K_32KBanks:	return "32K banked by $BFD0-BFFF access";
		case kATCartridgeMode_5200_128K_32KBanks:	return "32K banked by $BFD0-BFFF access";
		case kATCartridgeMode_5200_256K_32KBanks:	return "32K banked by $BFC0-BFFF access";
		case kATCartridgeMode_5200_512K_32KBanks:	return "32K banked by $BFC0-BFFF access";
		case kATCartridgeMode_MicroCalc:			return "8K banked by CCTL access (autoincrement, switchable)";
		case kATCartridgeMode_2K:					return "2K fixed";
		case kATCartridgeMode_4K:					return "4K fixed";
		case kATCartridgeMode_RightSlot_4K:			return "4K fixed right slot";
		case kATCartridgeMode_MegaCart_512K_3:		return "16K banked by CCTL data (switchable)";
		case kATCartridgeMode_MegaMax_2M:			return "16K banked by CCTL address (switchable)";
		case kATCartridgeMode_MegaCart_4M_3:		return "16K banked by CCTL data (switchable)";

		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:			return "8K+8K banked (complex)";

		case kATCartridgeMode_aDawliah_32K:			return "8K banked by CCTL access (autoincrement)";
		case kATCartridgeMode_aDawliah_64K:			return "8K banked by CCTL access (autoincrement)";

		case kATCartridgeMode_JRC6_64K:				return "8K banked by CCTL $D500-D57F data (switchable)";
		case kATCartridgeMode_JRC_RAMBOX:			return "8K banked by CCTL $D500-D57F data (switchable) + RAM";

		case kATCartridgeMode_XEMulticart_8K:
		case kATCartridgeMode_XEMulticart_16K:
		case kATCartridgeMode_XEMulticart_32K:
		case kATCartridgeMode_XEMulticart_64K:
		case kATCartridgeMode_XEMulticart_128K:
		case kATCartridgeMode_XEMulticart_256K:
		case kATCartridgeMode_XEMulticart_512K:
		case kATCartridgeMode_XEMulticart_1M:		return "8K or 16K banked by CCTL write";

		case kATCartridgeMode_SICPlus:				return "16K banked by CCTL $D500-D51F access (8K+8K switchable)";

		case kATCartridgeMode_MDDOS:				return "4K banked by CCTL access (4K+4K switchable)";
		case kATCartridgeMode_COS32K:				return "16K banked by CCTL access";
		case kATCartridgeMode_Pronto:				return "16K fixed + EEPROM";

		case kATCartridgeMode_JAtariCart_8K:
		case kATCartridgeMode_JAtariCart_16K:
		case kATCartridgeMode_JAtariCart_32K:
		case kATCartridgeMode_JAtariCart_64K:
		case kATCartridgeMode_JAtariCart_128K:
		case kATCartridgeMode_JAtariCart_256K:
		case kATCartridgeMode_JAtariCart_512K:
		case kATCartridgeMode_JAtariCart_1024K:		return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_DCart:				return "8K banked by CCTL write (switchable) + keyhole";

		default:									return "";
	}
}

#endif	// f_AT_SHELL_CARTRIDGE_NAMES_H
