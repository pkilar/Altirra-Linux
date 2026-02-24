// Compatibility shim: redirect bare <intrin.h> to platform-appropriate headers.
// On MSVC, <intrin.h> is a system header providing compiler intrinsics.
// On GCC/Clang, we redirect to <x86intrin.h> or <arm_neon.h> as appropriate.

#ifndef f_COMPAT_INTRIN_H
#define f_COMPAT_INTRIN_H

#ifdef _MSC_VER
	#include_next <intrin.h>
#else
	#include <vd2/system/vdtypes.h>
	#if defined(VD_CPU_AMD64) || defined(VD_CPU_X86)
		#include <x86intrin.h>
	#elif defined(VD_CPU_ARM64)
		#include <arm_neon.h>
	#endif
#endif

#endif
