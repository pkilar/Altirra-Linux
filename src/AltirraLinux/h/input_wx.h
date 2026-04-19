//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef AT_INPUT_WX_H
#define AT_INPUT_WX_H

#include <vd2/system/vdtypes.h>

class ATInputManager;

class ATInputWx {
public:
	ATInputWx();
	~ATInputWx();

	void Init(ATInputManager *inputMan);
	void Shutdown();

	// Translate wxWidgets key code to Altirra input code
	uint32 TranslateWxKey(int wxKeyCode);

	// Process keyboard events — call from wxKeyEvent handlers
	void OnKeyDown(int wxKeyCode);
	void OnKeyUp(int wxKeyCode);

	// Process mouse events
	void OnMouseMove(int dx, int dy);
	void OnMouseButtonDown(int button);
	void OnMouseButtonUp(int button);
	void OnMouseWheel(int delta);

private:
	uint32 TranslateMouseButton(int button);

	ATInputManager *mpInputManager = nullptr;
	int mKeyboardUnit = -1;
	int mMouseUnit = -1;
};

#endif // AT_INPUT_WX_H
