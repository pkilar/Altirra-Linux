// Altirra - Atari 800/XL/XE/5200 emulator
// VD2 System Library - Linux error handling implementation
// Copyright (C) 2024 Avery Lee, All Rights Reserved.
// Linux port additions are licensed under the same terms as the original.

#include <stdafx.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <vd2/system/Error.h>
#include <vd2/system/VDString.h>

/////////////////////////////////////////////////////////////////////////////
// VDWin32Exception - On Linux, the "Win32 error code" is actually an errno.
// We use strerror_r to format the error message.

VDWin32Exception::VDWin32Exception(const char *format, uint32 err, ...)
	: mWin32Error(err)
{
	VDStringA errorMessage;

	va_list val;
	va_start(val, err);
	errorMessage.append_vsprintf(format, val);
	va_end(val);

	if (errorMessage.empty())
		errorMessage = "%s";

	auto lastStrPos = errorMessage.find("%s");
	if (lastStrPos == VDStringA::npos) {
		assign(errorMessage.c_str());
		return;
	}

	for(;;) {
		auto nextStrPos = errorMessage.find("%s", lastStrPos + 2);
		if (nextStrPos == VDStringA::npos)
			break;
		lastStrPos = nextStrPos;
	}

	// Convert errno to string
	char errBuf[256];
	const char *errStr = errBuf;

	// Use the GNU version of strerror_r which returns a char*
	errStr = strerror_r((int)err, errBuf, sizeof(errBuf));

	errorMessage.replace(lastStrPos, 2, errStr, strlen(errStr));
	assign(errorMessage.c_str());
}

VDWin32Exception::VDWin32Exception(const wchar_t *format, uint32 err, ...)
	: mWin32Error(err)
{
	VDStringW errorMessage;

	va_list val;
	va_start(val, err);
	errorMessage.append_vsprintf(format, val);
	va_end(val);

	if (errorMessage.empty())
		errorMessage = L"%s";

	auto lastStrPos = errorMessage.find(L"%s");
	if (lastStrPos == VDStringW::npos) {
		assign(errorMessage.c_str());
		return;
	}

	for(;;) {
		auto nextStrPos = errorMessage.find(L"%s", lastStrPos + 2);
		if (nextStrPos == VDStringW::npos)
			break;
		lastStrPos = nextStrPos;
	}

	// Convert errno to string
	char errBuf[256];
	const char *errStr = strerror_r((int)err, errBuf, sizeof(errBuf));

	// Convert narrow error string to wide
	VDStringW wideErr;
	size_t len = strlen(errStr);
	wideErr.resize(len);
	for (size_t i = 0; i < len; ++i)
		wideErr[i] = (wchar_t)(unsigned char)errStr[i];

	errorMessage.replace(lastStrPos, 2, wideErr.c_str(), wideErr.size());
	assign(errorMessage.c_str());
}

/////////////////////////////////////////////////////////////////////////////

void VDPostException(VDExceptionPostContext context, const char *message, const char *title) {
	(void)context;
	fprintf(stderr, "%s: %s\n", title, message);
}

void VDPostException(VDExceptionPostContext context, const wchar_t *message, const wchar_t *title) {
	(void)context;
	fprintf(stderr, "%ls: %ls\n", title, message);
}
