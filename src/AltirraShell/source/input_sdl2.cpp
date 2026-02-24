//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
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

#include <input_sdl2.h>
#include <inputmanager.h>
#include <inputdefs.h>
#include <cstring>

ATInputSDL2::ATInputSDL2() {
}

ATInputSDL2::~ATInputSDL2() {
	Shutdown();
}

void ATInputSDL2::Init(ATInputManager *inputMan) {
	mpInputManager = inputMan;

	// Register keyboard input unit
	ATInputUnitIdentifier keyboardId;
	memset(&keyboardId, 0, sizeof(keyboardId));
	memcpy(keyboardId.buf, "sdl2_keyboard", 13);
	mKeyboardUnit = mpInputManager->RegisterInputUnit(keyboardId, L"SDL2 Keyboard", nullptr);

	// Register mouse input unit
	ATInputUnitIdentifier mouseId;
	memset(&mouseId, 0, sizeof(mouseId));
	memcpy(mouseId.buf, "sdl2_mouse\0\0\0\0\0", 15);
	mMouseUnit = mpInputManager->RegisterInputUnit(mouseId, L"SDL2 Mouse", nullptr);
}

void ATInputSDL2::Shutdown() {
	// Close all open controllers
	for (auto& entry : mControllers) {
		if (entry.mpController)
			SDL_GameControllerClose(entry.mpController);
	}
	mControllers.clear();

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

uint32 ATInputSDL2::TranslateSDLScancode(SDL_Scancode sc) {
	switch (sc) {
		// Letters
		case SDL_SCANCODE_A: return kATInputCode_KeyA;
		case SDL_SCANCODE_B: return kATInputCode_KeyB;
		case SDL_SCANCODE_C: return kATInputCode_KeyC;
		case SDL_SCANCODE_D: return kATInputCode_KeyD;
		case SDL_SCANCODE_E: return kATInputCode_KeyE;
		case SDL_SCANCODE_F: return kATInputCode_KeyF;
		case SDL_SCANCODE_G: return kATInputCode_KeyG;
		case SDL_SCANCODE_H: return kATInputCode_KeyH;
		case SDL_SCANCODE_I: return kATInputCode_KeyI;
		case SDL_SCANCODE_J: return kATInputCode_KeyJ;
		case SDL_SCANCODE_K: return kATInputCode_KeyK;
		case SDL_SCANCODE_L: return kATInputCode_KeyL;
		case SDL_SCANCODE_M: return kATInputCode_KeyM;
		case SDL_SCANCODE_N: return kATInputCode_KeyN;
		case SDL_SCANCODE_O: return kATInputCode_KeyO;
		case SDL_SCANCODE_P: return kATInputCode_KeyP;
		case SDL_SCANCODE_Q: return kATInputCode_Keyq;
		case SDL_SCANCODE_R: return kATInputCode_KeyR;
		case SDL_SCANCODE_S: return kATInputCode_KeyS;
		case SDL_SCANCODE_T: return kATInputCode_KeyT;
		case SDL_SCANCODE_U: return kATInputCode_KeyU;
		case SDL_SCANCODE_V: return kATInputCode_KeyV;
		case SDL_SCANCODE_W: return kATInputCode_KeyW;
		case SDL_SCANCODE_X: return kATInputCode_KeyX;
		case SDL_SCANCODE_Y: return kATInputCode_KeyY;
		case SDL_SCANCODE_Z: return kATInputCode_KeyZ;

		// Digits
		case SDL_SCANCODE_0: return kATInputCode_Key0;
		case SDL_SCANCODE_1: return kATInputCode_Key1;
		case SDL_SCANCODE_2: return kATInputCode_Key2;
		case SDL_SCANCODE_3: return kATInputCode_Key3;
		case SDL_SCANCODE_4: return kATInputCode_Key4;
		case SDL_SCANCODE_5: return kATInputCode_Key5;
		case SDL_SCANCODE_6: return kATInputCode_Key6;
		case SDL_SCANCODE_7: return kATInputCode_Key7;
		case SDL_SCANCODE_8: return kATInputCode_Key8;
		case SDL_SCANCODE_9: return kATInputCode_Key9;

		// F-keys
		case SDL_SCANCODE_F1:  return kATInputCode_KeyF1;
		case SDL_SCANCODE_F2:  return kATInputCode_KeyF2;
		case SDL_SCANCODE_F3:  return kATInputCode_KeyF3;
		case SDL_SCANCODE_F4:  return kATInputCode_KeyF4;
		case SDL_SCANCODE_F5:  return kATInputCode_KeyF5;
		case SDL_SCANCODE_F6:  return kATInputCode_KeyF6;
		case SDL_SCANCODE_F7:  return kATInputCode_KeyF7;
		case SDL_SCANCODE_F8:  return kATInputCode_KeyF8;
		case SDL_SCANCODE_F9:  return kATInputCode_KeyF9;
		case SDL_SCANCODE_F10: return kATInputCode_KeyF10;
		case SDL_SCANCODE_F11: return kATInputCode_KeyF11;
		case SDL_SCANCODE_F12: return kATInputCode_KeyF12;

		// Navigation
		case SDL_SCANCODE_LEFT:     return kATInputCode_KeyLeft;
		case SDL_SCANCODE_UP:       return kATInputCode_KeyUp;
		case SDL_SCANCODE_RIGHT:    return kATInputCode_KeyRight;
		case SDL_SCANCODE_DOWN:     return kATInputCode_KeyDown;
		case SDL_SCANCODE_HOME:     return kATInputCode_KeyHome;
		case SDL_SCANCODE_END:      return kATInputCode_KeyEnd;
		case SDL_SCANCODE_PAGEUP:   return kATInputCode_KeyPrior;
		case SDL_SCANCODE_PAGEDOWN: return kATInputCode_KeyNext;
		case SDL_SCANCODE_INSERT:   return kATInputCode_KeyInsert;
		case SDL_SCANCODE_DELETE:   return kATInputCode_KeyDelete;

		// Special keys
		case SDL_SCANCODE_RETURN:    return kATInputCode_KeyReturn;
		case SDL_SCANCODE_ESCAPE:    return kATInputCode_KeyEscape;
		case SDL_SCANCODE_BACKSPACE: return kATInputCode_KeyBack;
		case SDL_SCANCODE_TAB:       return kATInputCode_KeyTab;
		case SDL_SCANCODE_SPACE:     return kATInputCode_KeySpace;

		// Modifiers
		case SDL_SCANCODE_LSHIFT: return kATInputCode_KeyLShift;
		case SDL_SCANCODE_RSHIFT: return kATInputCode_KeyRShift;
		case SDL_SCANCODE_LCTRL:  return kATInputCode_KeyLControl;
		case SDL_SCANCODE_RCTRL:  return kATInputCode_KeyRControl;

		// Caps Lock / Pause (needed for keyboard mapping)
		case SDL_SCANCODE_CAPSLOCK:  return 0x14;  // VK_CAPITAL
		case SDL_SCANCODE_PAUSE:     return 0x13;  // VK_PAUSE

		// Punctuation / OEM keys
		case SDL_SCANCODE_SEMICOLON:    return kATInputCode_KeyOem1;       // ;:
		case SDL_SCANCODE_EQUALS:       return kATInputCode_KeyOemPlus;    // =+
		case SDL_SCANCODE_COMMA:        return kATInputCode_KeyOemComma;   // ,<
		case SDL_SCANCODE_MINUS:        return kATInputCode_KeyOemMinus;   // -_
		case SDL_SCANCODE_PERIOD:       return kATInputCode_KeyOemPeriod;  // .>
		case SDL_SCANCODE_SLASH:        return kATInputCode_KeyOem2;       // /?
		case SDL_SCANCODE_GRAVE:        return kATInputCode_KeyOem3;       // `~
		case SDL_SCANCODE_LEFTBRACKET:  return kATInputCode_KeyOem4;       // [{
		case SDL_SCANCODE_BACKSLASH:    return kATInputCode_KeyOem5;       // \|
		case SDL_SCANCODE_RIGHTBRACKET: return kATInputCode_KeyOem6;       // ]}
		case SDL_SCANCODE_APOSTROPHE:   return kATInputCode_KeyOem7;       // '"

		// Numpad
		case SDL_SCANCODE_KP_0:       return kATInputCode_KeyNumpad0;
		case SDL_SCANCODE_KP_1:       return kATInputCode_KeyNumpad1;
		case SDL_SCANCODE_KP_2:       return kATInputCode_KeyNumpad2;
		case SDL_SCANCODE_KP_3:       return kATInputCode_KeyNumpad3;
		case SDL_SCANCODE_KP_4:       return kATInputCode_KeyNumpad4;
		case SDL_SCANCODE_KP_5:       return kATInputCode_KeyNumpad5;
		case SDL_SCANCODE_KP_6:       return kATInputCode_KeyNumpad6;
		case SDL_SCANCODE_KP_7:       return kATInputCode_KeyNumpad7;
		case SDL_SCANCODE_KP_8:       return kATInputCode_KeyNumpad8;
		case SDL_SCANCODE_KP_9:       return kATInputCode_KeyNumpad9;
		case SDL_SCANCODE_KP_MULTIPLY: return kATInputCode_KeyMultiply;
		case SDL_SCANCODE_KP_PLUS:     return kATInputCode_KeyAdd;
		case SDL_SCANCODE_KP_MINUS:    return kATInputCode_KeySubtract;
		case SDL_SCANCODE_KP_PERIOD:   return kATInputCode_KeyDecimal;
		case SDL_SCANCODE_KP_DIVIDE:   return kATInputCode_KeyDivide;
		case SDL_SCANCODE_KP_ENTER:    return kATInputCode_KeyNumpadEnter;

		default:
			return kATInputCode_None;
	}
}

bool ATInputSDL2::ProcessEvent(const SDL_Event& event) {
	if (!mpInputManager)
		return false;

	switch (event.type) {
		case SDL_KEYDOWN: {
			if (event.key.repeat)
				return false;
			uint32 code = TranslateSDLScancode(event.key.keysym.scancode);
			if (code != kATInputCode_None) {
				mpInputManager->OnButtonDown(mKeyboardUnit, code);
				return true;
			}
			return false;
		}

		case SDL_KEYUP: {
			uint32 code = TranslateSDLScancode(event.key.keysym.scancode);
			if (code != kATInputCode_None) {
				mpInputManager->OnButtonUp(mKeyboardUnit, code);
				return true;
			}
			return false;
		}

		case SDL_MOUSEMOTION: {
			mpInputManager->OnMouseMove(mMouseUnit, event.motion.xrel, event.motion.yrel);
			return true;
		}

		case SDL_MOUSEBUTTONDOWN: {
			uint32 code;
			switch (event.button.button) {
				case SDL_BUTTON_LEFT:   code = kATInputCode_MouseLMB; break;
				case SDL_BUTTON_MIDDLE: code = kATInputCode_MouseMMB; break;
				case SDL_BUTTON_RIGHT:  code = kATInputCode_MouseRMB; break;
				case SDL_BUTTON_X1:     code = kATInputCode_MouseX1B; break;
				case SDL_BUTTON_X2:     code = kATInputCode_MouseX2B; break;
				default: return false;
			}
			mpInputManager->OnButtonDown(mMouseUnit, code);
			return true;
		}

		case SDL_MOUSEBUTTONUP: {
			uint32 code;
			switch (event.button.button) {
				case SDL_BUTTON_LEFT:   code = kATInputCode_MouseLMB; break;
				case SDL_BUTTON_MIDDLE: code = kATInputCode_MouseMMB; break;
				case SDL_BUTTON_RIGHT:  code = kATInputCode_MouseRMB; break;
				case SDL_BUTTON_X1:     code = kATInputCode_MouseX1B; break;
				case SDL_BUTTON_X2:     code = kATInputCode_MouseX2B; break;
				default: return false;
			}
			mpInputManager->OnButtonUp(mMouseUnit, code);
			return true;
		}

		case SDL_MOUSEWHEEL: {
			if (event.wheel.y != 0) {
				float delta = (float)event.wheel.y;
				if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
					delta = -delta;
				mpInputManager->OnMouseWheel(mMouseUnit, delta);
			}
			if (event.wheel.x != 0) {
				float delta = (float)event.wheel.x;
				if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
					delta = -delta;
				mpInputManager->OnMouseHWheel(mMouseUnit, delta);
			}
			return true;
		}

		case SDL_CONTROLLERAXISMOTION: {
			// Find the controller entry
			for (const auto& entry : mControllers) {
				if (entry.mInstanceID == event.caxis.which) {
					// Map SDL axis to ATInputCode axis
					// SDL axis range: -32768..32767, ATInputCode expects 16:16 fixed point (-65536..65536)
					sint32 rawValue = event.caxis.value;
					sint32 scaledValue = rawValue * 2;  // Scale to approximately +-65536

					uint32 axisCode;
					switch (event.caxis.axis) {
						case SDL_CONTROLLER_AXIS_LEFTX:       axisCode = kATInputCode_JoyHoriz1; break;
						case SDL_CONTROLLER_AXIS_LEFTY:        axisCode = kATInputCode_JoyVert1; break;
						case SDL_CONTROLLER_AXIS_RIGHTX:       axisCode = kATInputCode_JoyHoriz3; break;
						case SDL_CONTROLLER_AXIS_RIGHTY:       axisCode = kATInputCode_JoyVert3; break;
						case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  axisCode = kATInputCode_JoyVert2; break;
						case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:  axisCode = kATInputCode_JoyVert4; break;
						default: return false;
					}

					mpInputManager->OnAxisInput(entry.mInputUnit, axisCode, scaledValue, scaledValue);
					return true;
				}
			}
			return false;
		}

		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP: {
			for (const auto& entry : mControllers) {
				if (entry.mInstanceID == event.cbutton.which) {
					uint32 code = kATInputCode_JoyButton0 + event.cbutton.button;
					if (event.type == SDL_CONTROLLERBUTTONDOWN)
						mpInputManager->OnButtonDown(entry.mInputUnit, code);
					else
						mpInputManager->OnButtonUp(entry.mInputUnit, code);
					return true;
				}
			}
			return false;
		}

		case SDL_CONTROLLERDEVICEADDED:
			OnControllerAdded(event.cdevice.which);
			return true;

		case SDL_CONTROLLERDEVICEREMOVED:
			OnControllerRemoved(event.cdevice.which);
			return true;

		default:
			return false;
	}
}

void ATInputSDL2::OnControllerAdded(int joystickIndex) {
	if (!SDL_IsGameController(joystickIndex))
		return;

	SDL_GameController *controller = SDL_GameControllerOpen(joystickIndex);
	if (!controller)
		return;

	SDL_JoystickID instanceID = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));

	// Check if already registered
	for (const auto& entry : mControllers) {
		if (entry.mInstanceID == instanceID)
			return;
	}

	// Register input unit for this controller
	ATInputUnitIdentifier joyId;
	memset(&joyId, 0, sizeof(joyId));
	snprintf(joyId.buf, sizeof(joyId.buf), "sdl2_joy%d", (int)instanceID);

	const char *name = SDL_GameControllerName(controller);
	wchar_t wname[64];
	if (name) {
		for (int i = 0; i < 63 && name[i]; ++i)
			wname[i] = (wchar_t)name[i];
		wname[63] = 0;
	} else {
		wcscpy(wname, L"SDL2 Controller");
	}

	int unit = -1;
	if (mpInputManager)
		unit = mpInputManager->RegisterInputUnit(joyId, wname, nullptr);

	ControllerEntry entry;
	entry.mpController = controller;
	entry.mInstanceID = instanceID;
	entry.mInputUnit = unit;
	mControllers.push_back(entry);
}

void ATInputSDL2::OnControllerRemoved(SDL_JoystickID instanceID) {
	for (auto it = mControllers.begin(); it != mControllers.end(); ++it) {
		if (it->mInstanceID == instanceID) {
			if (mpInputManager && it->mInputUnit >= 0)
				mpInputManager->UnregisterInputUnit(it->mInputUnit);
			if (it->mpController)
				SDL_GameControllerClose(it->mpController);
			mControllers.erase(it);
			return;
		}
	}
}
