//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#pragma once

#include <wx/frame.h>

class wxAuiManager;

// Create and show the debugger frame (or bring existing one to front).
void ATWxDebuggerOpen(wxWindow *parent);

// Close the debugger frame if open.
void ATWxDebuggerClose();

// Returns true if the debugger frame exists and is visible.
bool ATWxDebuggerIsOpen();

// Append text to the debugger console pane.
void ATWxDebuggerAppendConsole(const char *s);

// Check if the debugger just hit a breakpoint (for auto-focus).
bool ATWxDebuggerDidBreak();

// Initialize/shutdown debugger hooks (called from wxApp OnInit/OnExit).
void ATWxDebuggerInit();
void ATWxDebuggerShutdown();
