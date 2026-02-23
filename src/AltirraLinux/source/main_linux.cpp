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

#include <stdafx.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/thread.h>
#include <vd2/VDDisplay/display.h>

#include <at/atcore/device.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/ataudio/audiooutput.h>

#include "simulator.h"
#include "joystick.h"
#include "debugger.h"

#include <display_sdl2.h>
#include <input_sdl2.h>
#include <imgui_manager.h>
#include <debugger_imgui.h>

#include <SDL.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Forward declarations from debugger.cpp
void ATInitDebugger();
void ATShutdownDebugger();

// Global simulator instance — matches Windows main.cpp
ATSimulator g_sim;

// Display
static ATDisplaySDL2 *g_pDisplay = nullptr;

// Input
static ATInputSDL2 *g_pInput = nullptr;

// ImGui manager (also referenced by console_linux.cpp)
ATImGuiManager *g_pImGui = nullptr;

// Joystick manager factory (defined in joystick_sdl2.cpp)
IATJoystickManager *ATCreateJoystickManagerSDL2();

static bool g_running = true;

static void InitRegistry() {
	// Use in-memory registry for now.
	// Phase 8 will add JSON-backed persistent settings.
	VDRegistryProviderMemory *pMem = new VDRegistryProviderMemory;
	VDSetRegistryProvider(pMem);
}

static bool InitSDL(SDL_Window *&window, SDL_GLContext &glContext) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) < 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	window = SDL_CreateWindow(
		"Altirra (Linux)",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		912, 524,		// 2x NTSC resolution (456x262)
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
	);

	if (!window) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return false;
	}

	glContext = SDL_GL_CreateContext(window);
	if (!glContext) {
		fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		return false;
	}

	// Enable vsync
	SDL_GL_SetSwapInterval(1);

	return true;
}

static void HandleDebugShortcuts(const SDL_Event& event) {
	if (event.type != SDL_KEYDOWN)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;

	switch (event.key.keysym.scancode) {
		case SDL_SCANCODE_F5:
			if (dbg->IsRunning())
				dbg->Break();
			else
				dbg->Run(kATDebugSrcMode_Disasm);
			break;

		case SDL_SCANCODE_F10:
			if (!dbg->IsRunning())
				dbg->StepOver(kATDebugSrcMode_Disasm);
			break;

		case SDL_SCANCODE_F11:
			if (!dbg->IsRunning()) {
				if (shift)
					dbg->StepOut(kATDebugSrcMode_Disasm);
				else
					dbg->StepInto(kATDebugSrcMode_Disasm);
			}
			break;

		default:
			break;
	}
}

static void ProcessEvents(SDL_Window *window) {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		// Always let ImGui see the event (for input state tracking)
		if (g_pImGui)
			g_pImGui->ProcessEvent(event);

		// F12 toggles debugger overlay
		if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_F12) {
			if (g_pImGui)
				g_pImGui->ToggleVisible();
			continue;
		}

		// When overlay is visible, handle debug shortcuts
		if (g_pImGui && g_pImGui->IsVisible()) {
			HandleDebugShortcuts(event);

			// If ImGui wants the input, don't pass to emulation
			if (g_pImGui->WantCaptureMouse() || g_pImGui->WantCaptureKeyboard())
				continue;
		}

		// Let input handler try
		if (g_pInput && g_pInput->ProcessEvent(event))
			continue;

		switch (event.type) {
			case SDL_QUIT:
				g_running = false;
				break;

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_CLOSE)
					g_running = false;
				break;
		}
	}
}

static void RenderAndSwap(SDL_Window *window) {
	// Render emulation frame (upload texture + draw quad)
	g_pDisplay->RenderFrame();

	// Render ImGui overlay on top
	if (g_pImGui && g_pImGui->IsVisible()) {
		g_pImGui->NewFrame();
		ATImGuiDebuggerDraw();
		g_pImGui->Render();
	}

	// Single swap
	SDL_GL_SwapWindow(window);
}

int main(int argc, char *argv[]) {
	fprintf(stderr, "Altirra Linux - starting up\n");

	// Detect CPU features (SSE2, AVX, etc.)
	CPUCheckForExtensions();

	// Init registry (in-memory for now)
	InitRegistry();

	// Init SDL2
	SDL_Window *window = nullptr;
	SDL_GLContext glContext = nullptr;

	if (!InitSDL(window, glContext)) {
		fprintf(stderr, "Failed to initialize SDL2/OpenGL\n");
		return 1;
	}

	fprintf(stderr, "SDL2/OpenGL initialized\n");

	// Init display backend
	g_pDisplay = new ATDisplaySDL2;
	if (!g_pDisplay->Init(window, glContext)) {
		fprintf(stderr, "Failed to initialize display backend\n");
		return 1;
	}

	fprintf(stderr, "Display backend initialized\n");

	// Init ImGui
	g_pImGui = new ATImGuiManager;
	if (!g_pImGui->Init(window, glContext)) {
		fprintf(stderr, "Warning: ImGui init failed (continuing without debugger UI)\n");
		delete g_pImGui;
		g_pImGui = nullptr;
	} else {
		fprintf(stderr, "ImGui initialized (F12 to toggle debugger)\n");
	}

	// Init simulator
	g_sim.Init();
	g_sim.SetRandomSeed(rand() ^ (rand() << 15));

	fprintf(stderr, "Simulator initialized\n");

	// Init debugger (must be after sim init)
	ATInitDebugger();
	ATImGuiDebuggerInit();
	fprintf(stderr, "Debugger initialized\n");

	// Connect display to GTIA
	g_sim.GetGTIA().SetVideoOutput(g_pDisplay);

	// Init audio
	IATAudioOutput *audioOutput = g_sim.GetAudioOutput();
	audioOutput->InitNativeAudio();

	fprintf(stderr, "Audio initialized\n");

	// Init input
	g_pInput = new ATInputSDL2;
	g_pInput->Init(g_sim.GetInputManager());

	// Init joystick manager
	IATJoystickManager *jm = ATCreateJoystickManagerSDL2();
	if (jm->Init(nullptr, g_sim.GetInputManager())) {
		g_sim.SetJoystickManager(jm);
		fprintf(stderr, "Joystick manager initialized\n");
	} else {
		delete jm;
		jm = nullptr;
		fprintf(stderr, "Joystick manager init failed (continuing without joystick support)\n");
	}

	// Load ROMs (will use HLE kernel if no external ROMs found)
	try {
		g_sim.LoadROMs();
		fprintf(stderr, "ROMs loaded\n");
	} catch (...) {
		fprintf(stderr, "Warning: ROM loading failed, will use HLE kernel\n");
	}

	// Cold reset and start emulation
	g_sim.ColdReset();
	g_sim.Resume();
	fprintf(stderr, "Emulation started\n");

	// Main loop
	while (g_running) {
		ProcessEvents(window);

		// Tick debugger (processes queued commands)
		IATDebugger *dbg = ATGetDebugger();
		if (dbg)
			dbg->Tick();

		// Advance emulation
		ATSimulator::AdvanceResult result = g_sim.Advance(false);

		if (result == ATSimulator::kAdvanceResult_WaitingForFrame) {
			RenderAndSwap(window);
		} else if (result == ATSimulator::kAdvanceResult_Stopped) {
			// Emulation stopped — still render but at reduced rate
			// Auto-show debugger on stop
			if (g_pImGui && !g_pImGui->IsVisible())
				g_pImGui->SetVisible(true);

			RenderAndSwap(window);
			SDL_Delay(16);
		}
		// kAdvanceResult_Running means more work to do — loop immediately
	}

	fprintf(stderr, "Shutting down...\n");

	// Disconnect display from GTIA before destroying it
	g_sim.GetGTIA().SetVideoOutput(nullptr);

	// Shutdown joystick
	if (g_sim.GetJoystickManager()) {
		IATJoystickManager *jm2 = g_sim.GetJoystickManager();
		g_sim.SetJoystickManager(nullptr);
		jm2->Shutdown();
		delete jm2;
	}

	// Shutdown input
	if (g_pInput) {
		g_pInput->Shutdown();
		delete g_pInput;
		g_pInput = nullptr;
	}

	// Shutdown debugger
	ATImGuiDebuggerShutdown();
	ATShutdownDebugger();

	// Shutdown simulator
	g_sim.Shutdown();

	// Shutdown ImGui
	if (g_pImGui) {
		g_pImGui->Shutdown();
		delete g_pImGui;
		g_pImGui = nullptr;
	}

	// Shutdown display
	if (g_pDisplay) {
		g_pDisplay->Shutdown();
		delete g_pDisplay;
		g_pDisplay = nullptr;
	}

	// Shutdown SDL
	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();

	fprintf(stderr, "Shutdown complete\n");
	return 0;
}
