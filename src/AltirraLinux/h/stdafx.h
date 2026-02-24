//	Altirra - Atari 800/800XL emulator
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

// Linux-compatible precompiled header replacement for src/Altirra/h/stdafx.h

#pragma once

#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <math.h>

// MSVC compat for _stricmp/_strnicmp used directly in some source files
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif

// Linux tchar.h stub (no TCHAR on Linux)
#include <tchar.h>

#include <vd2/system/error.h>
#include <vd2/system/fraction.h>
#include <vd2/system/function.h>
#include <vd2/system/linearalloc.h>
#include <vd2/system/refcount.h>
#include <vd2/system/time.h>
#include <vd2/system/unknown.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vectors.h>
#include <vd2/Kasumi/pixmap.h>
#include <at/atcore/device.h>
#include <at/atcore/enumparse.h>
#include <atomic>
#include <deque>
#include <iterator>
#include <utility>
#include <optional>
