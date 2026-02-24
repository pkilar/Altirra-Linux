// Altirra - Atari 800/XL/XE/5200 emulator
// VD2 System Library - Linux date/time implementation
// Copyright (C) 2024 Avery Lee, All Rights Reserved.
// Linux port additions are licensed under the same terms as the original.

#include <stdafx.h>
#include <time.h>
#include <cstdio>
#include <cwchar>

#include <vd2/system/date.h>
#include <vd2/system/VDString.h>

// Keep the static_asserts from the Windows version — they validate
// the VDDate/VDDateInterval constexpr logic and are platform-independent.
static_assert(+VDDateInterval{0} == VDDateInterval{0});
static_assert(+VDDateInterval{1} == VDDateInterval{1});
static_assert(-VDDateInterval{0} == VDDateInterval{0});
static_assert(-VDDateInterval{1} == VDDateInterval{-1});

static_assert(VDDateInterval{0}.Abs().mDeltaTicks == 0);
static_assert(VDDateInterval{1}.Abs().mDeltaTicks == 1);
static_assert(VDDateInterval{-1}.Abs().mDeltaTicks == 1);

static_assert(VDDateInterval{ 1} != VDDateInterval{0});
static_assert(VDDateInterval{ 0} == VDDateInterval{0});
static_assert(VDDateInterval{ 0} >= VDDateInterval{0});
static_assert(VDDateInterval{ 1} >= VDDateInterval{0});
static_assert(VDDateInterval{ 1} >  VDDateInterval{0});
static_assert(VDDateInterval{ 0} <= VDDateInterval{0});
static_assert(VDDateInterval{-1} <= VDDateInterval{0});
static_assert(VDDateInterval{-1} <  VDDateInterval{0});

static_assert(!(VDDateInterval{ 0} != VDDateInterval{0}));
static_assert(!(VDDateInterval{ 1} == VDDateInterval{0}));
static_assert(!(VDDateInterval{-1} >= VDDateInterval{0}));
static_assert(!(VDDateInterval{ 0} >  VDDateInterval{0}));
static_assert(!(VDDateInterval{ 1} <= VDDateInterval{0}));
static_assert(!(VDDateInterval{ 0} <  VDDateInterval{0}));

static_assert(VDDateInterval{0}.ToSeconds() == 0.0f);
static_assert(VDDateInterval{10000000}.ToSeconds() == 1.0f);
static_assert(VDDateInterval{-10000000}.ToSeconds() == -1.0f);
static_assert(VDDateInterval::FromSeconds(0).mDeltaTicks == 0);
static_assert(VDDateInterval::FromSeconds(1.0f).mDeltaTicks == 10000000);
static_assert(VDDateInterval::FromSeconds(-1.0f).mDeltaTicks == -10000000);

static_assert(VDDate{0} - VDDate{0} == VDDateInterval{0});
static_assert(VDDate{0} - VDDate{1} == VDDateInterval{-1});
static_assert(VDDate{1} - VDDate{0} == VDDateInterval{1});
static_assert(VDDate{1000} + VDDateInterval{1} == VDDate{1001});
static_assert(VDDateInterval{1} + VDDate{1000} == VDDate{1001});
static_assert(VDDate{1000} - VDDateInterval{1} == VDDate{999});

///////////////////////////////////////////////////////////////////////////////
// Epoch offset: number of 100ns ticks between 1601-01-01 and 1970-01-01
///////////////////////////////////////////////////////////////////////////////

static constexpr uint64 kUnixEpochOffset = UINT64_C(116444736000000000);

VDDate VDGetCurrentDate() {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	uint64 ticks = (uint64)ts.tv_sec * 10000000ULL + (uint64)ts.tv_nsec / 100ULL;
	return VDDate { ticks + kUnixEpochOffset };
}

sint64 VDGetDateAsTimeT(const VDDate& date) {
	return ((sint64)date.mTicks - (sint64)kUnixEpochOffset) / 10000000;
}

VDExpandedDate VDGetLocalDate(const VDDate& date) {
	VDExpandedDate r = {};

	// Convert VDDate ticks to Unix time
	time_t unixTime = (time_t)VDGetDateAsTimeT(date);
	uint64 subSecondTicks = (date.mTicks - kUnixEpochOffset) % 10000000ULL;

	struct tm local;
	if (localtime_r(&unixTime, &local)) {
		r.mYear = (uint32)(local.tm_year + 1900);
		r.mMonth = (uint8)(local.tm_mon + 1);
		r.mDayOfWeek = (uint8)local.tm_wday;
		r.mDay = (uint8)local.tm_mday;
		r.mHour = (uint8)local.tm_hour;
		r.mMinute = (uint8)local.tm_min;
		r.mSecond = (uint8)local.tm_sec;
		r.mMilliseconds = (uint16)(subSecondTicks / 10000ULL);
	}

	return r;
}

VDDate VDDateFromLocalDate(const VDExpandedDate& date) {
	struct tm local = {};
	local.tm_year = (int)date.mYear - 1900;
	local.tm_mon = (int)date.mMonth - 1;
	local.tm_mday = (int)date.mDay;
	local.tm_hour = (int)date.mHour;
	local.tm_min = (int)date.mMinute;
	local.tm_sec = (int)date.mSecond;
	local.tm_isdst = -1;

	time_t unixTime = mktime(&local);
	if (unixTime == (time_t)-1)
		return {};

	uint64 ticks = (uint64)unixTime * 10000000ULL + kUnixEpochOffset;
	ticks += (uint64)date.mMilliseconds * 10000ULL;

	return VDDate { ticks };
}

void VDAppendLocalDateString(VDStringW& dst, const VDExpandedDate& ed) {
	// Use strftime for locale-aware date formatting
	struct tm local = {};
	local.tm_year = (int)ed.mYear - 1900;
	local.tm_mon = (int)ed.mMonth - 1;
	local.tm_mday = (int)ed.mDay;
	local.tm_wday = (int)ed.mDayOfWeek;

	char buf[64];
	size_t len = strftime(buf, sizeof(buf), "%x", &local);
	if (len > 0) {
		// Convert narrow to wide (ASCII date strings)
		for (size_t i = 0; i < len; ++i)
			dst += (wchar_t)(unsigned char)buf[i];
	}
}

void VDAppendLocalTimeString(VDStringW& dst, const VDExpandedDate& ed) {
	struct tm local = {};
	local.tm_hour = (int)ed.mHour;
	local.tm_min = (int)ed.mMinute;
	local.tm_sec = (int)ed.mSecond;

	char buf[64];
	size_t len = strftime(buf, sizeof(buf), "%X", &local);
	if (len > 0) {
		for (size_t i = 0; i < len; ++i)
			dst += (wchar_t)(unsigned char)buf[i];
	}
}
