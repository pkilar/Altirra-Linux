#ifndef AT_DEBUGGER_IMGUI_H
#define AT_DEBUGGER_IMGUI_H

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

#endif
