// Altirra - Atari 800/XL/XE/5200 emulator
// VD2 System Library - Linux debug implementation
// Copyright (C) 2024 Avery Lee, All Rights Reserved.
// Linux port additions are licensed under the same terms as the original.

#include <stdafx.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstdc.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/debug.h>
#include <vd2/system/thread.h>

#ifdef _DEBUG

VDAssertResult VDAssert(const char *exp, const char *file, int line) {
	fprintf(stderr, "%s(%d): Assert failed: %s\n", file, line, exp);
	return kVDAssertIgnore;
}

VDAssertResult VDAssertPtr(const char *exp, const char *file, int line) {
	fprintf(stderr, "%s(%d): Assert failed: %s is not a valid pointer\n", file, line, exp);
	return kVDAssertIgnore;
}

#endif

void VDProtectedAutoScopeICLWorkaround() {}

void VDDebugPrint(const char *format, ...) {
	char buf[4096];

	va_list val;
	va_start(val, format);
	vsnprintf(buf, sizeof buf, format, val);
	va_end(val);
	fputs(buf, stderr);
}

///////////////////////////////////////////////////////////////////////////

namespace {
	IVDExternalCallTrap *g_pExCallTrap;
}

void VDSetExternalCallTrap(IVDExternalCallTrap *trap) {
	g_pExCallTrap = trap;
}

// On Linux x64, we don't have the same FPU state issues as Win32 x86.
// These are all stubs.

bool IsMMXState() {
	return false;
}

void ClearMMXState() {
}

void VDClearEvilCPUStates() {
}

void VDPreCheckExternalCodeCall(const char *file, int line) {
}

void VDPostCheckExternalCodeCall(const wchar_t *mpContext, const char *mpFile, int mLine) {
}
