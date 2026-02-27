//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - Enhanced text engine interface
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef f_AT_UIENHANCEDTEXT_H
#define f_AT_UIENHANCEDTEXT_H

#include <vd2/system/vdtypes.h>
#include <cstddef>

class ATSimulator;
class IATDeviceVideoOutput;

class IATUIEnhancedTextOutput {
public:
	virtual void InvalidateTextOutput() = 0;
};

class IATUIEnhancedTextEngine {
public:
	virtual ~IATUIEnhancedTextEngine() = default;

	virtual void Init(IATUIEnhancedTextOutput *output, ATSimulator *sim) = 0;
	virtual void Shutdown() = 0;

	virtual bool IsRawInputEnabled() const = 0;
	virtual IATDeviceVideoOutput *GetVideoOutput() = 0;

	virtual void OnSize(uint32 w, uint32 h) = 0;
	virtual void OnChar(int ch) = 0;
	virtual bool OnKeyDown(uint32 keyCode) = 0;
	virtual bool OnKeyUp(uint32 keyCode) = 0;

	virtual void Paste(const wchar_t *s, size_t len) = 0;

	virtual void Update(bool forceInvalidate) = 0;
};

IATUIEnhancedTextEngine *ATUICreateEnhancedTextEngine();

#endif	// f_AT_UIENHANCEDTEXT_H
