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
#include <SDL.h>
#include <cstring>

class ATJoystickManagerSDL2 final : public IATJoystickManager {
public:
	ATJoystickManagerSDL2();
	~ATJoystickManagerSDL2() override;

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

ATJoystickManagerSDL2::ATJoystickManagerSDL2() {
	mTransforms.mStickAnalogDeadZone = 6554;    // ~10%
	mTransforms.mStickDigitalDeadZone = 19661;  // ~30%
	mTransforms.mStickAnalogPower = 1.0f;
	mTransforms.mTriggerAnalogDeadZone = 3277;  // ~5%
	mTransforms.mTriggerDigitalDeadZone = 16384; // ~25%
	mTransforms.mTriggerAnalogPower = 1.0f;
}

ATJoystickManagerSDL2::~ATJoystickManagerSDL2() {
	Shutdown();
}

bool ATJoystickManagerSDL2::Init(void *hwnd, ATInputManager *inputMan) {
	mpInputManager = inputMan;

	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0)
		return false;

	return true;
}

void ATJoystickManagerSDL2::Shutdown() {
	mpInputManager = nullptr;
	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

ATJoystickTransforms ATJoystickManagerSDL2::GetTransforms() const {
	return mTransforms;
}

void ATJoystickManagerSDL2::SetTransforms(const ATJoystickTransforms& transforms) {
	mTransforms = transforms;
}

void ATJoystickManagerSDL2::SetCaptureMode(bool capture) {
	mbCaptureMode = capture;
}

void ATJoystickManagerSDL2::SetOnActivity(const vdfunction<void()>& fn) {
	mOnActivity = fn;
}

void ATJoystickManagerSDL2::RescanForDevices() {
	// SDL2 handles device enumeration via events (SDL_CONTROLLERDEVICEADDED/REMOVED)
	// which are processed through the ATInputSDL2 event loop
}

IATJoystickManager::PollResult ATJoystickManagerSDL2::Poll() {
	// Actual polling is done via SDL event loop in ATInputSDL2::ProcessEvent()
	int numJoysticks = SDL_NumJoysticks();
	if (numJoysticks <= 0)
		return kPollResult_NoControllers;

	return kPollResult_OK;
}

bool ATJoystickManagerSDL2::PollForCapture(int& unit, uint32& inputCode, uint32& inputCode2) {
	// Capture mode for input binding — will be implemented in Phase 6
	return false;
}

const ATJoystickState *ATJoystickManagerSDL2::PollForCapture(uint32& n) {
	n = 0;
	return nullptr;
}

uint32 ATJoystickManagerSDL2::GetJoystickPortStates() const {
	return 0;
}

IATJoystickManager *ATCreateJoystickManagerSDL2() {
	return new ATJoystickManagerSDL2;
}
