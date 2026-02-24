//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/vdtypes.h>
#include <at/atcore/vfs.h>
#include <test.h>

DEFINE_TEST(Core_VFSMakePath) {
	VDStringW s;

#define TEST_PATH_CASE(basePath, relPath, result) \
	if ((s = ATMakeVFSPath((basePath), (relPath))) != (result)) { \
		printf("Test failed:\n" \
			"\tBase path:       [%ls]\n" \
			"\tRelative path:   [%ls]\n" \
			"\tResolved path:   [%ls]\n" \
			"\tExpected result: [%ls]\n", basePath, relPath, s.c_str(), result); \
		TEST_ASSERT(false); \
	 }

#ifdef VD_PLATFORM_WINDOWS
	TEST_PATH_CASE(L"c:", L"x", L"c:x");
	TEST_PATH_CASE(L"c:\\", L"x", L"c:\\x");
	TEST_PATH_CASE(L"c:\\y", L"x", L"c:\\y\\x");
	TEST_PATH_CASE(L"c:\\y\\", L"x", L"c:\\y\\x");
	TEST_PATH_CASE(L"c:\\a", L"b\\c", L"c:\\a\\b\\c");
	TEST_PATH_CASE(L"gz://c:/test", L"b\\c", L"gz://c:/test/b/c");
	TEST_PATH_CASE(L"zip://c:/test.zip!dir", L"b\\c", L"zip://c:/test.zip!dir/b/c");
	TEST_PATH_CASE(L"atfs://c:/test.zip!dir", L"b\\c", L"atfs://c:/test.zip!dir/b/c");
	TEST_PATH_CASE(L"c:\\", L"zip://x/test.zip!foo.bin", L"zip://c:/x/test.zip!foo.bin");
#else
	TEST_PATH_CASE(L"/home", L"x", L"/home/x");
	TEST_PATH_CASE(L"/home/", L"x", L"/home/x");
	TEST_PATH_CASE(L"/home/user", L"x", L"/home/user/x");
	TEST_PATH_CASE(L"gz:///tmp/test", L"b/c", L"gz:///tmp/test/b/c");
	TEST_PATH_CASE(L"zip:///tmp/test.zip!dir", L"b/c", L"zip:///tmp/test.zip!dir/b/c");
	TEST_PATH_CASE(L"atfs:///tmp/test.zip!dir", L"b/c", L"atfs:///tmp/test.zip!dir/b/c");
	TEST_PATH_CASE(L"/tmp/", L"zip://x/test.zip!foo.bin", L"zip:///tmp/x/test.zip!foo.bin");
#endif

#undef TEST_PATH_CASE

	return 0;
}

DEFINE_TEST(Core_VFSEncodePath) {
	// On Linux (wchar_t=4), supplementary code points are single wchar_t values,
	// not surrogate pairs. Use direct U+ escapes instead.
#if WCHAR_MAX > 0xFFFF
	VDStringSpanW rawPath(L"\u0080\u0081\u0BAF\u20AC\U00010000\U00010402");
#else
	VDStringSpanW rawPath(L"\u0080\u0081\u0BAF\u20AC\xD800\xDC00\xD801\xDC02");
#endif

	VDStringW url;
	ATEncodeVFSPath(url, rawPath, true);

	VDStringW path;
	TEST_ASSERT(ATDecodeVFSPath(path, url));

	TEST_ASSERT(path == rawPath);

	return 0;
}

