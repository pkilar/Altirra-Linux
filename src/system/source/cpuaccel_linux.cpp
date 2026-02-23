// Altirra - Atari 800/XL/XE/5200 emulator
// VD2 System Library - Linux CPU acceleration detection
// Copyright (C) 2024 Avery Lee, All Rights Reserved.
// Linux port additions are licensed under the same terms as the original.

#include <stdafx.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/cpuaccel.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#include <cpuid.h>
#include <immintrin.h>
#endif

#if defined(VD_CPU_ARM64)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif

long g_lCPUExtensionsEnabled;

extern "C" {
	bool FPU_enabled, MMX_enabled, ISSE_enabled, SSE2_enabled;
};

#if VD_CPU_X86 || VD_CPU_X64
long CPUCheckForExtensions() {
	long flags = 0;

	unsigned int eax, ebx, ecx, edx;

	// Check for CPUID support and get max function
	if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx))
		return flags;

	if (eax == 0)
		return flags;

	// Get feature bits
	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return flags;

	if (edx & (1 << 23))
		flags |= CPUF_SUPPORTS_MMX;

	if (edx & (1 << 25)) {
		flags |= CPUF_SUPPORTS_SSE | CPUF_SUPPORTS_INTEGER_SSE;

		if (edx & (1 << 26))
			flags |= CPUF_SUPPORTS_SSE2;

		if (ecx & 0x00000001)
			flags |= CPUF_SUPPORTS_SSE3;

		if (ecx & 0x00000200)
			flags |= CPUF_SUPPORTS_SSSE3;

		if (ecx & 0x00001000)
			flags |= VDCPUF_SUPPORTS_FMA;

		if (ecx & 0x00080000) {
			flags |= CPUF_SUPPORTS_SSE41;

			if (ecx & (1 << 20)) {
				flags |= CPUF_SUPPORTS_SSE42;

				if (ecx & (1 << 1))
					flags |= CPUF_SUPPORTS_CLMUL;
			}
		}

		if (ecx & (1 << 23))
			flags |= VDCPUF_SUPPORTS_POPCNT;

		// Check OSXSAVE and AVX bits
		if ((ecx & ((1 << 27) | (1 << 28))) == ((1 << 27) | (1 << 28))) {
			// Check OS support for AVX via xgetbv
			unsigned int xcr0_lo, xcr0_hi;
			__asm__ __volatile__("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));

			if ((xcr0_lo & 0x06) == 0x06) {
				flags |= CPUF_SUPPORTS_AVX;

				unsigned int max_func;
				__get_cpuid(0, &max_func, &ebx, &ecx, &edx);

				if (max_func >= 7) {
					unsigned int eax7, ebx7, ecx7, edx7;
					__cpuid_count(7, 0, eax7, ebx7, ecx7, edx7);

					static constexpr uint32 BMI1 = UINT32_C(1) << 3;
					static constexpr uint32 AVX2 = UINT32_C(1) << 5;
					static constexpr uint32 BMI2 = UINT32_C(1) << 8;
					if ((ebx7 & (AVX2 | BMI1 | BMI2)) == (AVX2 | BMI1 | BMI2)) {
						flags |= CPUF_SUPPORTS_AVX2;

						if (ebx7 & (1 << 29))
							flags |= CPUF_SUPPORTS_SHA;
					}
				}
			}
		}
	}

	// Check for AMD extensions
	if (__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx) && eax >= 0x80000001U) {
		__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx);

		if (edx & (1 << 22))
			flags |= CPUF_SUPPORTS_INTEGER_SSE;

		if (ecx & (1 << 5))
			flags |= CPUF_SUPPORTS_LZCNT;
	}

	return flags;
}
#elif VD_CPU_ARM64
long CPUCheckForExtensions() {
	long flags = 0;

	unsigned long hwcap = getauxval(AT_HWCAP);

	if (hwcap & HWCAP_AES)
		flags |= VDCPUF_SUPPORTS_CRYPTO;

	if (hwcap & HWCAP_CRC32)
		flags |= VDCPUF_SUPPORTS_CRC32;

	return flags;
}
#else
long CPUCheckForExtensions() {
	return 0;
}
#endif

long CPUEnableExtensions(long lEnableFlags) {
	g_lCPUExtensionsEnabled = lEnableFlags;

#if VD_CPU_X86 || VD_CPU_X64
	MMX_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_MMX);
	ISSE_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_INTEGER_SSE);
	SSE2_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_SSE2);
#endif

	return g_lCPUExtensionsEnabled;
}

void VDCPUCleanupExtensions() {
#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	#if defined(VD_CPU_X86)
		if (ISSE_enabled)
			_mm_sfence();

		if (MMX_enabled)
			_mm_empty();
	#elif defined(VD_CPU_AMD64)
		_mm_sfence();
	#endif

	if (g_lCPUExtensionsEnabled & CPUF_SUPPORTS_AVX)
		__asm__ __volatile__("vzeroupper" ::: "memory");
#endif
}
