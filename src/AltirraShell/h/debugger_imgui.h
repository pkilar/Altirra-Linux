#ifndef AT_DEBUGGER_IMGUI_H
#define AT_DEBUGGER_IMGUI_H

#include <vd2/system/vdtypes.h>

void ATImGuiDebuggerInit();
void ATImGuiDebuggerDraw();          // Full draw (menu bar + windows)
void ATImGuiDebuggerDrawWindows();   // Windows only (no menu bar)
void ATImGuiDebuggerShutdown();
void ATImGuiDebuggerAppendConsole(const char *s);

// Window visibility accessors (for emulator_imgui.cpp menu integration)
bool& ATImGuiDebuggerShowRegisters();
bool& ATImGuiDebuggerShowDisassembly();
bool& ATImGuiDebuggerShowMemory();
bool& ATImGuiDebuggerShowConsole();
bool& ATImGuiDebuggerShowBreakpoints();
bool& ATImGuiDebuggerShowWatch();
bool& ATImGuiDebuggerShowCallStack();
bool& ATImGuiDebuggerShowHistory();
bool& ATImGuiDebuggerShowSourceCode();
bool& ATImGuiDebuggerShowPrinterOutput();
bool& ATImGuiDebuggerShowProfiler();
bool& ATImGuiDebuggerShowTrace();
bool& ATImGuiDebuggerShowDebugDisplay();
bool& ATImGuiDebuggerShowPerformance();

// Navigate source code window to a specific address (opens window if needed)
void ATImGuiDebuggerNavigateSource(uint32 addr);

// Returns true (once) when the debugger transitions from running to stopped.
// Resets the flag on read so it fires only once per break event.
bool ATImGuiDebuggerDidBreak();

#endif
