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

#ifndef AT_INPUT_SDL2_H
#define AT_INPUT_SDL2_H

#include <vd2/system/vdtypes.h>
#include <SDL.h>
#include <vector>

class ATInputManager;

// Translates SDL2 events into ATInputManager calls
class ATInputSDL2 {
public:
	ATInputSDL2();
	~ATInputSDL2();

	void Init(ATInputManager *inputMan);
	void Shutdown();

	// Process a single SDL event. Returns true if the event was consumed.
	bool ProcessEvent(const SDL_Event& event);

private:
	static uint32 TranslateSDLScancode(SDL_Scancode sc);
	void OnControllerAdded(int joystickIndex);
	void OnControllerRemoved(SDL_JoystickID instanceID);

	ATInputManager *mpInputManager = nullptr;
	int mKeyboardUnit = -1;
	int mMouseUnit = -1;

	struct ControllerEntry {
		SDL_GameController *mpController;
		SDL_JoystickID mInstanceID;
		int mInputUnit;
	};

	std::vector<ControllerEntry> mControllers;
};

#endif // AT_INPUT_SDL2_H
