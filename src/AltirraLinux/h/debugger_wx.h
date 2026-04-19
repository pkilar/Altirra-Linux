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

// Navigate the debugger source panel to show the source for a given address.
bool ATWxDebuggerNavigateSource(uint32 addr);
bool ATWxDebuggerShowPane(const char *paneName, wxWindow *parent = nullptr);
bool ATWxDebuggerHidePane(const char *paneName);
bool ATWxDebuggerIsPaneVisible(const char *paneName);
bool ATWxDebuggerActivatePane(uint32 paneId, wxWindow *parent = nullptr);
uint32 ATWxDebuggerGetActivePaneId();
bool ATWxDebuggerClosePane(uint32 paneId);
bool ATWxDebuggerSaveLayout(const char *name = nullptr);
bool ATWxDebuggerRestoreLayout(const char *name = nullptr, wxWindow *parent = nullptr);
void ATWxDebuggerLoadDefaultLayout(wxWindow *parent = nullptr);
bool ATWxDebuggerHandleActivePaneCommand(uint32 commandId);

// Window/pane management helpers used by Linux command shims.
bool ATWxDebuggerCloseActivePane();
bool ATWxDebuggerToggleFloatActivePane();
bool ATWxDebuggerCyclePane(bool forward);

// Initialize/shutdown debugger hooks (called from wxApp OnInit/OnExit).
void ATWxDebuggerInit();
void ATWxDebuggerShutdown();
