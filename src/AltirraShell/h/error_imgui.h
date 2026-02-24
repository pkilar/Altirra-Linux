//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - ImGui error dialog queue
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef AT_ERROR_IMGUI_H
#define AT_ERROR_IMGUI_H

#include <string>
#include <utility>
#include <vector>

// Thread-safe error queue. Errors are pushed from any thread via
// ATUIShowWarning/ATUIShowError (stubs_linux.cpp) and popped on the
// main thread by the ImGui emulator overlay.

// Retrieves all pending errors (clears the queue). Thread-safe.
std::vector<std::pair<std::string,std::string>> ATImGuiPopPendingErrors();

#endif
