//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include <input_wx.h>
#include <inputmanager.h>
#include <inputdefs.h>
#include <wx/defs.h>
#include <cstring>

ATInputWx::ATInputWx() {
}

ATInputWx::~ATInputWx() {
	Shutdown();
}

void ATInputWx::Init(ATInputManager *inputMan) {
	mpInputManager = inputMan;

	ATInputUnitIdentifier keyboardId;
	memset(&keyboardId, 0, sizeof(keyboardId));
	memcpy(keyboardId.buf, "wx_keyboard\0\0\0", 14);
	mKeyboardUnit = mpInputManager->RegisterInputUnit(keyboardId, L"wxWidgets Keyboard", nullptr);

	ATInputUnitIdentifier mouseId;
	memset(&mouseId, 0, sizeof(mouseId));
	memcpy(mouseId.buf, "wx_mouse\0\0\0\0\0\0", 14);
	mMouseUnit = mpInputManager->RegisterInputUnit(mouseId, L"wxWidgets Mouse", nullptr);
}

void ATInputWx::Shutdown() {
	if (mpInputManager) {
		if (mKeyboardUnit >= 0) {
			mpInputManager->UnregisterInputUnit(mKeyboardUnit);
			mKeyboardUnit = -1;
		}
		if (mMouseUnit >= 0) {
			mpInputManager->UnregisterInputUnit(mMouseUnit);
			mMouseUnit = -1;
		}
		mpInputManager = nullptr;
	}
}

uint32 ATInputWx::TranslateWxKey(int k) {
	switch (k) {
		// Letters
		case 'A': return kATInputCode_KeyA;
		case 'B': return kATInputCode_KeyB;
		case 'C': return kATInputCode_KeyC;
		case 'D': return kATInputCode_KeyD;
		case 'E': return kATInputCode_KeyE;
		case 'F': return kATInputCode_KeyF;
		case 'G': return kATInputCode_KeyG;
		case 'H': return kATInputCode_KeyH;
		case 'I': return kATInputCode_KeyI;
		case 'J': return kATInputCode_KeyJ;
		case 'K': return kATInputCode_KeyK;
		case 'L': return kATInputCode_KeyL;
		case 'M': return kATInputCode_KeyM;
		case 'N': return kATInputCode_KeyN;
		case 'O': return kATInputCode_KeyO;
		case 'P': return kATInputCode_KeyP;
		case 'Q': return kATInputCode_Keyq;
		case 'R': return kATInputCode_KeyR;
		case 'S': return kATInputCode_KeyS;
		case 'T': return kATInputCode_KeyT;
		case 'U': return kATInputCode_KeyU;
		case 'V': return kATInputCode_KeyV;
		case 'W': return kATInputCode_KeyW;
		case 'X': return kATInputCode_KeyX;
		case 'Y': return kATInputCode_KeyY;
		case 'Z': return kATInputCode_KeyZ;

		// Digits
		case '0': return kATInputCode_Key0;
		case '1': return kATInputCode_Key1;
		case '2': return kATInputCode_Key2;
		case '3': return kATInputCode_Key3;
		case '4': return kATInputCode_Key4;
		case '5': return kATInputCode_Key5;
		case '6': return kATInputCode_Key6;
		case '7': return kATInputCode_Key7;
		case '8': return kATInputCode_Key8;
		case '9': return kATInputCode_Key9;

		// F-keys
		case WXK_F1:  return kATInputCode_KeyF1;
		case WXK_F2:  return kATInputCode_KeyF2;
		case WXK_F3:  return kATInputCode_KeyF3;
		case WXK_F4:  return kATInputCode_KeyF4;
		case WXK_F5:  return kATInputCode_KeyF5;
		case WXK_F6:  return kATInputCode_KeyF6;
		case WXK_F7:  return kATInputCode_KeyF7;
		case WXK_F8:  return kATInputCode_KeyF8;
		case WXK_F9:  return kATInputCode_KeyF9;
		case WXK_F10: return kATInputCode_KeyF10;
		case WXK_F11: return kATInputCode_KeyF11;
		case WXK_F12: return kATInputCode_KeyF12;

		// Navigation
		case WXK_LEFT:     return kATInputCode_KeyLeft;
		case WXK_UP:       return kATInputCode_KeyUp;
		case WXK_RIGHT:    return kATInputCode_KeyRight;
		case WXK_DOWN:     return kATInputCode_KeyDown;
		case WXK_HOME:     return kATInputCode_KeyHome;
		case WXK_END:      return kATInputCode_KeyEnd;
		case WXK_PAGEUP:   return kATInputCode_KeyPrior;
		case WXK_PAGEDOWN: return kATInputCode_KeyNext;
		case WXK_INSERT:   return kATInputCode_KeyInsert;
		case WXK_DELETE:   return kATInputCode_KeyDelete;

		// Special keys
		case WXK_RETURN:    return kATInputCode_KeyReturn;
		case WXK_ESCAPE:    return kATInputCode_KeyEscape;
		case WXK_BACK:      return kATInputCode_KeyBack;
		case WXK_TAB:       return kATInputCode_KeyTab;
		case WXK_SPACE:     return kATInputCode_KeySpace;

		// Modifiers
		case WXK_SHIFT:   return kATInputCode_KeyLShift;
		case WXK_CONTROL: return kATInputCode_KeyLControl;

		// Caps Lock / Pause
		case WXK_CAPITAL:  return 0x14;  // VK_CAPITAL
		case WXK_PAUSE:    return 0x13;  // VK_PAUSE

		// Punctuation / OEM keys
		case ';':  return kATInputCode_KeyOem1;       // ;:
		case '=':  return kATInputCode_KeyOemPlus;    // =+
		case ',':  return kATInputCode_KeyOemComma;   // ,<
		case '-':  return kATInputCode_KeyOemMinus;   // -_
		case '.':  return kATInputCode_KeyOemPeriod;  // .>
		case '/':  return kATInputCode_KeyOem2;       // /?
		case '`':  return kATInputCode_KeyOem3;       // `~
		case '[':  return kATInputCode_KeyOem4;       // [{
		case '\\': return kATInputCode_KeyOem5;       // \.
		case ']':  return kATInputCode_KeyOem6;       // ]}
		case '\'': return kATInputCode_KeyOem7;       // '"

		// Numpad
		case WXK_NUMPAD0:     return kATInputCode_KeyNumpad0;
		case WXK_NUMPAD1:     return kATInputCode_KeyNumpad1;
		case WXK_NUMPAD2:     return kATInputCode_KeyNumpad2;
		case WXK_NUMPAD3:     return kATInputCode_KeyNumpad3;
		case WXK_NUMPAD4:     return kATInputCode_KeyNumpad4;
		case WXK_NUMPAD5:     return kATInputCode_KeyNumpad5;
		case WXK_NUMPAD6:     return kATInputCode_KeyNumpad6;
		case WXK_NUMPAD7:     return kATInputCode_KeyNumpad7;
		case WXK_NUMPAD8:     return kATInputCode_KeyNumpad8;
		case WXK_NUMPAD9:     return kATInputCode_KeyNumpad9;
		case WXK_NUMPAD_MULTIPLY: return kATInputCode_KeyMultiply;
		case WXK_NUMPAD_ADD:      return kATInputCode_KeyAdd;
		case WXK_NUMPAD_SUBTRACT: return kATInputCode_KeySubtract;
		case WXK_NUMPAD_DECIMAL:  return kATInputCode_KeyDecimal;
		case WXK_NUMPAD_DIVIDE:   return kATInputCode_KeyDivide;
		case WXK_NUMPAD_ENTER:    return kATInputCode_KeyNumpadEnter;

		default:
			return kATInputCode_None;
	}
}

void ATInputWx::OnKeyDown(int wxKeyCode) {
	if (!mpInputManager)
		return;
	uint32 code = TranslateWxKey(wxKeyCode);
	if (code != kATInputCode_None)
		mpInputManager->OnButtonDown(mKeyboardUnit, code);
}

void ATInputWx::OnKeyUp(int wxKeyCode) {
	if (!mpInputManager)
		return;
	uint32 code = TranslateWxKey(wxKeyCode);
	if (code != kATInputCode_None)
		mpInputManager->OnButtonUp(mKeyboardUnit, code);
}

void ATInputWx::OnMouseMove(int dx, int dy) {
	if (!mpInputManager)
		return;
	mpInputManager->OnMouseMove(mMouseUnit, dx, dy);
}

uint32 ATInputWx::TranslateMouseButton(int button) {
	switch (button) {
		case 1: return kATInputCode_MouseLMB;   // wxMOUSE_BTN_LEFT
		case 2: return kATInputCode_MouseMMB;   // wxMOUSE_BTN_MIDDLE
		case 3: return kATInputCode_MouseRMB;   // wxMOUSE_BTN_RIGHT
		default: return kATInputCode_None;
	}
}

void ATInputWx::OnMouseButtonDown(int button) {
	if (!mpInputManager)
		return;
	uint32 code = TranslateMouseButton(button);
	if (code != kATInputCode_None)
		mpInputManager->OnButtonDown(mMouseUnit, code);
}

void ATInputWx::OnMouseButtonUp(int button) {
	if (!mpInputManager)
		return;
	uint32 code = TranslateMouseButton(button);
	if (code != kATInputCode_None)
		mpInputManager->OnButtonUp(mMouseUnit, code);
}

void ATInputWx::OnMouseWheel(int delta) {
	if (!mpInputManager)
		return;
	mpInputManager->OnMouseWheel(mMouseUnit, delta);
}
