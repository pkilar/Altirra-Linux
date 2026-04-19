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

#include <joystick.h>
#include <inputmanager.h>
#include <SDL3/SDL.h>
#include <cstring>

class ATJoystickManagerSDL3 final : public IATJoystickManager {
public:
	ATJoystickManagerSDL3();
	~ATJoystickManagerSDL3() override;

	bool Init(void *hwnd, ATInputManager *inputMan) override;
	void Shutdown() override;

	ATJoystickTransforms GetTransforms() const override;
	void SetTransforms(const ATJoystickTransforms& transforms) override;

	void SetCaptureMode(bool capture) override;
	void SetOnActivity(const vdfunction<void()>& fn) override;
	void RescanForDevices() override;

	PollResult Poll() override;
	bool PollForCapture(int& unit, uint32& inputCode, uint32& inputCode2) override;
	const ATJoystickState *PollForCapture(uint32& n) override;
	uint32 GetJoystickPortStates() const override;

private:
	ATInputManager *mpInputManager = nullptr;
	ATJoystickTransforms mTransforms {};
	bool mbCaptureMode = false;
	vdfunction<void()> mOnActivity;
};

ATJoystickManagerSDL3::ATJoystickManagerSDL3() {
	mTransforms.mStickAnalogDeadZone = 6554;    // ~10%
	mTransforms.mStickDigitalDeadZone = 19661;  // ~30%
	mTransforms.mStickAnalogPower = 1.0f;
	mTransforms.mTriggerAnalogDeadZone = 3277;  // ~5%
	mTransforms.mTriggerDigitalDeadZone = 16384; // ~25%
	mTransforms.mTriggerAnalogPower = 1.0f;
}

ATJoystickManagerSDL3::~ATJoystickManagerSDL3() {
	Shutdown();
}

bool ATJoystickManagerSDL3::Init(void *hwnd, ATInputManager *inputMan) {
	mpInputManager = inputMan;

	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
		return false;

	return true;
}

void ATJoystickManagerSDL3::Shutdown() {
	mpInputManager = nullptr;
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

ATJoystickTransforms ATJoystickManagerSDL3::GetTransforms() const {
	return mTransforms;
}

void ATJoystickManagerSDL3::SetTransforms(const ATJoystickTransforms& transforms) {
	mTransforms = transforms;
}

void ATJoystickManagerSDL3::SetCaptureMode(bool capture) {
	mbCaptureMode = capture;
}

void ATJoystickManagerSDL3::SetOnActivity(const vdfunction<void()>& fn) {
	mOnActivity = fn;
}

void ATJoystickManagerSDL3::RescanForDevices() {
	// SDL3 handles device enumeration via events (SDL_EVENT_GAMEPAD_ADDED/REMOVED)
	// which are processed through the ATInputSDL3 event loop
}

IATJoystickManager::PollResult ATJoystickManagerSDL3::Poll() {
	// Actual polling is done via SDL event loop in ATInputSDL3::ProcessEvent()
	int count = 0;
	SDL_JoystickID *gamepads = SDL_GetGamepads(&count);
	SDL_free(gamepads);
	if (count <= 0)
		return kPollResult_NoControllers;

	return kPollResult_OK;
}

bool ATJoystickManagerSDL3::PollForCapture(int& unit, uint32& inputCode, uint32& inputCode2) {
	// Capture mode for input binding — will be implemented in Phase 6
	return false;
}

const ATJoystickState *ATJoystickManagerSDL3::PollForCapture(uint32& n) {
	n = 0;
	return nullptr;
}

uint32 ATJoystickManagerSDL3::GetJoystickPortStates() const {
	return 0;
}

IATJoystickManager *ATCreateJoystickManagerSDL3() {
	return new ATJoystickManagerSDL3;
}
