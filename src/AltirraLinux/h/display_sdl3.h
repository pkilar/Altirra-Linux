// wxWidgets shim — maps ATDisplaySDL3 to ATDisplayWx so stubs_linux.cpp
// and other shared code seamlessly use the wxGLCanvas-based display.

#ifndef AT_DISPLAY_SDL3_H
#define AT_DISPLAY_SDL3_H

#include <display_wx.h>

// stubs_linux.cpp references ATDisplaySDL3* — alias it to the real display
using ATDisplaySDL3 = ATDisplayWx;

#endif // AT_DISPLAY_SDL3_H
