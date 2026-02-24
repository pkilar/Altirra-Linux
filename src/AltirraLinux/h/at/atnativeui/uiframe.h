// Linux shim for at/atnativeui/uiframe.h
// Provides minimal stubs so debugger.cpp compiles without the Win32 UI layer.

#ifndef AT_UIFRAME_H
#define AT_UIFRAME_H

#include <vd2/system/vdtypes.h>

class ATFrameWindow {};

inline ATFrameWindow *ATUIGetMainWindow() { return nullptr; }

inline void ATActivateUIPane(uint32 id, bool giveFocus, bool visible = true, uint32 relid = 0, int reldock = 0) {}

#endif
