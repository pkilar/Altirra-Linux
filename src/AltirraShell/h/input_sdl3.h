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

#ifndef AT_INPUT_SDL3_H
#define AT_INPUT_SDL3_H

#include <vd2/system/vdtypes.h>
#include <SDL3/SDL.h>
#include <vector>

class ATInputManager;

// Translates SDL3 events into ATInputManager calls
class ATInputSDL3 {
public:
	ATInputSDL3();
	~ATInputSDL3();

	void Init(ATInputManager *inputMan);
	void Shutdown();

	// Process a single SDL event. Returns true if the event was consumed.
	bool ProcessEvent(const SDL_Event& event);

	// Translate SDL scancode to ATInputCode (VK equivalent). Public so
	// keyboard mapping in main_linux.cpp can reuse it.
	static uint32 TranslateSDLScancode(SDL_Scancode sc);

private:
	void OnGamepadAdded(SDL_JoystickID instanceID);
	void OnGamepadRemoved(SDL_JoystickID instanceID);

	ATInputManager *mpInputManager = nullptr;
	int mKeyboardUnit = -1;
	int mMouseUnit = -1;

	struct ControllerEntry {
		SDL_Gamepad *mpController;
		SDL_JoystickID mInstanceID;
		int mInputUnit;
	};

	std::vector<ControllerEntry> mControllers;
};

#endif // AT_INPUT_SDL3_H
