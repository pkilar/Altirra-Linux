//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#include <stdafx.h>
#include "console.h"
#include <vd2/system/file.h>
#include <cstdio>
#include <cstdarg>

// Log file support
static VDFileStream *g_pLogFile = nullptr;

void ATConsoleOpenLogFile(const wchar_t *path) {
	ATConsoleCloseLogFileNT();
	try {
		g_pLogFile = new VDFileStream(path, nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways);
	} catch (...) {
		g_pLogFile = nullptr;
	}
}

void ATConsoleCloseLogFileNT() {
	if (g_pLogFile) {
		delete g_pLogFile;
		g_pLogFile = nullptr;
	}
}

void ATConsoleCloseLogFile() {
	ATConsoleCloseLogFileNT();
}

void ATConsoleWrite(const char *s) {
	if (g_pLogFile) {
		g_pLogFile->write(s, strlen(s));
	}
	fputs(s, stderr);
}

void ATConsolePrintfImpl(const char *format, ...) {
	char buf[4096];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	ATConsoleWrite(buf);
}

void ATConsoleTaggedPrintfImpl(const char *format, ...) {
	char buf[4096];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	ATConsoleWrite(buf);
}

// Font management — stub (no GDI fonts on Linux)
void ATConsoleGetFont(struct tagLOGFONTW& font, int& pointSizeTenths) {
	memset(&font, 0, sizeof(font));
	pointSizeTenths = 100;
}

void ATConsoleGetCharMetrics(int& charWidth, int& lineHeight) {
	charWidth = 8;
	lineHeight = 16;
}

void ATConsoleSetFont(const struct tagLOGFONTW& font, int pointSizeTenths) {
}

void ATConsoleSetFontDpi(unsigned dpi) {
}

bool ATConsoleShowSource(uint32 addr) {
	return false;
}

void ATShowConsole() {
}

void ATOpenConsole() {
}

void ATCloseConsole() {
}

bool ATIsDebugConsoleActive() {
	return false;
}

// Source windows — stub
IATSourceWindow *ATGetSourceWindow(const wchar_t *s) {
	return nullptr;
}

IATSourceWindow *ATOpenSourceWindow(const wchar_t *path) {
	return nullptr;
}

IATSourceWindow *ATOpenSourceWindow(const ATDebuggerSourceFileInfo& sourceFileInfo, bool searchPaths) {
	return nullptr;
}

void ATUIShowSourceListDialog() {
}

// UI pane management — stub
void ATGetUIPanes(vdfastvector<ATUIPane *>& panes) {
}

ATUIPane *ATGetUIPane(uint32 id) {
	return nullptr;
}

void *ATGetUIPaneAs(uint32 id, uint32 iid) {
	return nullptr;
}

ATUIPane *ATGetUIPaneByFrame(ATFrameWindow *frame) {
	return nullptr;
}

void ATCloseUIPane(uint32 id) {
}

ATUIPane *ATUIGetActivePane() {
	return nullptr;
}

void *ATUIGetActivePaneAs(uint32 iid) {
	return nullptr;
}

uint32 ATUIGetActivePaneId() {
	return 0;
}

// Win32-specific font handles — return null handles
VDZHFONT ATGetConsoleFontW32() {
	return nullptr;
}

int ATGetConsoleFontLineHeightW32() {
	return 16;
}

VDZHFONT ATConsoleGetPropFontW32() {
	return nullptr;
}

int ATConsoleGetPropFontLineHeightW32() {
	return 16;
}

VDZHMENU ATUIGetSourceContextMenuW32() {
	return nullptr;
}

void ATConsoleAddFontNotification(const vdfunction<void()> *callback) {
}

void ATConsoleRemoveFontNotification(const vdfunction<void()> *callback) {
}

void ATConsolePingBeamPosition(uint32 frame, uint32 vpos, uint32 hpos) {
}

// Pane layout — stub
bool ATRestorePaneLayout(const char *name) {
	return false;
}

void ATSavePaneLayout(const char *name) {
}

void ATLoadDefaultPaneLayout() {
}
