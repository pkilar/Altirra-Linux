// wxWidgets shim — provides the same API surface as the ImGui version
// so console_linux.cpp and other shared code compiles without ImGui.

#ifndef AT_DEBUGGER_IMGUI_H
#define AT_DEBUGGER_IMGUI_H

#include <vd2/system/vdtypes.h>

inline void ATImGuiDebuggerInit() {}
inline void ATImGuiDebuggerDraw() {}
inline void ATImGuiDebuggerDrawWindows() {}
inline void ATImGuiDebuggerShutdown() {}
inline void ATImGuiDebuggerAppendConsole(const char *) {}

// Window visibility — static bools returned by reference
inline bool& ATImGuiDebuggerShowRegisters()     { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowDisassembly()   { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowMemory()        { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowConsole()       { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowBreakpoints()   { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowWatch()         { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowCallStack()     { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowHistory()       { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowSourceCode()    { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowPrinterOutput() { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowProfiler()      { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowTrace()         { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowDebugDisplay()  { static bool v = false; return v; }
inline bool& ATImGuiDebuggerShowPerformance()   { static bool v = false; return v; }

inline void ATImGuiDebuggerNavigateSource(uint32) {}
inline void ATImGuiDebuggerNavigateSourceLine(const wchar_t *, int) {}
inline bool ATImGuiDebuggerDidBreak() { return false; }
inline bool ATImGuiDebuggerIsVisible() { return false; }
inline uint16 ATImGuiDebuggerGetDisasmAddr() { return 0; }

#endif
