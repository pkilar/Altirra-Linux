#include <stdafx.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/VDString.h>
#include <vd2/system/strutil.h>
#include <vd2/system/text.h>
#include <vd2/system/vdstl.h>

#include "test.h"

#include <vector>
#include <utility>

extern void ATTestInitBlobHandler();

namespace {
	struct TestInfo {
		ATTestFn	mpTestFn;
		const char	*mpName;
		bool		mbAutoRun;
	};

	typedef vdfastvector<TestInfo> Tests;

	Tests& GetTests() {
		static Tests g_tests;
		return g_tests;
	}
}

void ATTestAddTest(ATTestFn f, const char *name, bool autoRun) {
	TestInfo ti;
	ti.mpTestFn = f;
	ti.mpName = name;
	ti.mbAutoRun = autoRun;
	GetTests().push_back(ti);
}

bool ATTestShouldBreak() {
	return false;
}

bool g_ATTestTracingEnabled = false;
static bool g_ATTestLoopEnabled = false;
static VDStringW g_ATTestArguments;

void ATTestBeginTestLoop() {
	g_ATTestLoopEnabled = true;
}

bool ATTestShouldContinueTestLoop() {
	return g_ATTestLoopEnabled;
}

void ATTestSetArguments(const wchar_t *args) {
	g_ATTestArguments = args;
}

const wchar_t *ATTestGetArguments() {
	return g_ATTestArguments.c_str();
}

void ATTestTrace(const char *msg) {
	puts(msg);
}

void ATTestTraceF(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	putchar('\n');
}

void ATTestHelp() {
	printf("\n");
	printf("Usage: attest [options] tests... | all\n");
	printf("\nOptions:\n");
	printf("    --verbose, -v   Enable verbose test output\n");
	printf("    --loop          Loop tests endlessly\n");
	printf("\n");

	printf("Available tests:\n");

	auto tests = GetTests();

	std::sort(tests.begin(), tests.end(),
		[](const TestInfo& x, const TestInfo& y) {
			return vdstricmp(x.mpName, y.mpName) < 0;
		}
	);

	for(const TestInfo& ent : tests) {
		printf("\t%s%s\n", ent.mpName, ent.mbAutoRun ? "" : "*");
	}
	printf("\tall\n");
}

int main(int argc, char **argv) {
	signal(SIGABRT, [](int) {
		const char str[] = "Fatal error: abort() has been called\n";
		fwrite(str, 1, sizeof(str) - 1, stderr);
		_exit(20);
	});

	printf("Altirra test harness (Linux)\n");
	printf("Copyright (C) 2016-2025 Avery Lee. Licensed under GNU General Public License, version 2 or later\n\n");

	ATTestInitBlobHandler();

	struct SelectedTest {
		const TestInfo *testInfo = nullptr;
	};

	vdfastvector<SelectedTest> selectedTests;
	bool loop = false;

	if (argc <= 1) {
		ATTestHelp();
		return 0;
	}

	for(int i = 1; i < argc; ++i) {
		const char *arg = argv[i];

		if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
			g_ATTestTracingEnabled = true;
			continue;
		}

		if (strcmp(arg, "--loop") == 0) {
			loop = true;
			continue;
		}

		if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
			ATTestHelp();
			return 0;
		}

		if (vdstricmp(arg, "all") == 0) {
			for(const TestInfo& ent : GetTests()) {
				if (ent.mbAutoRun)
					selectedTests.push_back({&ent});
			}
			break;
		}

		bool found = false;
		for(const TestInfo& ent : GetTests()) {
			if (vdstricmp(ent.mpName, arg) == 0) {
				selectedTests.push_back({&ent});
				found = true;
				break;
			}
		}

		if (!found) {
			printf("\nUnknown test: %s\n", arg);
			ATTestHelp();
			return 5;
		}
	}

	long exts = CPUCheckForExtensions();
	CPUEnableExtensions(exts);

	int failedTests = 0;

	do {
		for(const SelectedTest& selTest : selectedTests) {
			printf("Running test: %s\n", selTest.testInfo->mpName);

			try {
				ATTestSetArguments(L"");

				if (selTest.testInfo->mpTestFn())
					throw ATTestAssertionException("Test returned non-zero code");
			} catch(const ATTestAssertionException& e) {
				printf("    TEST FAILED: %ls\n", e.wc_str());
				++failedTests;
			} catch(const VDException& e) {
				printf("    TEST FAILED: Uncaught exception: %ls\n", e.wc_str());
				++failedTests;
			}
		}
	} while(loop);

	printf("Tests complete. Failures: %u\n", failedTests);

	return failedTests;
}
