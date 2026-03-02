//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include "console.h"
#include "debugger.h"
#include <debugger_wx.h>

#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>

#include <cstdio>
#include <cstdarg>
#include <vector>

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
	ATWxDebuggerAppendConsole(s);
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
	// TODO: Navigate wx source view when implemented
	return false;
}

bool ATConsoleCheckBreak() {
	// TODO: Check for Ctrl+C/Ctrl+Break via wxWidgets
	return false;
}

void ATShowConsole() {
	// Open the debugger window (which contains the console pane)
	// ATWxDebuggerOpen requires a parent, but we don't have one here.
	// The console is already visible in the debugger if it's open.
}

void ATOpenConsole() {
	ATShowConsole();
}

void ATCloseConsole() {
}

bool ATIsDebugConsoleActive() {
	return ATWxDebuggerIsOpen();
}

// Source window implementation — bridges IATSourceWindow
// (Simplified for wx: just tracks file paths, no ImGui integration)
class ATWxSourceWindow final : public IATSourceWindow {
public:
	ATWxSourceWindow(const wchar_t *path, const wchar_t *fullPath)
		: mPath(path), mFullPath(fullPath) {}

	bool Load() {
		mLines.clear();

		VDStringA u8path = VDTextWToU8(mFullPath);
		FILE *f = fopen(u8path.c_str(), "r");
		if (!f)
			return false;

		char buf[4096];
		VDStringA line;
		while (fgets(buf, sizeof(buf), f)) {
			line += buf;
			if (!line.empty() && line.back() == '\n') {
				line.pop_back();
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				mLines.push_back(VDTextU8ToW(line));
				line.clear();
			}
		}
		if (!line.empty())
			mLines.push_back(VDTextU8ToW(line));

		fclose(f);
		return true;
	}

	const wchar_t *GetFullPath() const override { return mFullPath.c_str(); }
	const wchar_t *GetPath() const override { return mPath.c_str(); }
	const wchar_t *GetPathAlias() const override { return mPathAlias.empty() ? nullptr : mPathAlias.c_str(); }

	void SetPathAlias(const wchar_t *alias) { mPathAlias = alias; }

	void FocusOnLine(int line) override {
		// TODO: Navigate wx source view
	}

	void ActivateLine(int line) override {
		FocusOnLine(line);
	}

	VDStringW ReadLine(int lineIndex) override {
		if (lineIndex < 0 || lineIndex >= (int)mLines.size())
			return VDStringW();
		return mLines[lineIndex] + L"\n";
	}

private:
	VDStringW mPath;
	VDStringW mFullPath;
	VDStringW mPathAlias;
	std::vector<VDStringW> mLines;
};

static std::vector<ATWxSourceWindow *> g_sourceWindows;

IATSourceWindow *ATGetSourceWindow(const wchar_t *s) {
	for (ATWxSourceWindow *w : g_sourceWindows) {
		if (!w) continue;
		if (VDFileIsPathEqual(s, w->GetPath()))
			return w;
		const wchar_t *alias = w->GetPathAlias();
		if (alias && VDFileIsPathEqual(s, alias))
			return w;
	}

	const wchar_t *sName = VDFileSplitPath(s);
	for (ATWxSourceWindow *w : g_sourceWindows) {
		if (!w) continue;
		if (VDFileIsPathEqual(sName, VDFileSplitPath(w->GetPath())))
			return w;
		const wchar_t *alias = w->GetPathAlias();
		if (alias && VDFileIsPathEqual(sName, VDFileSplitPath(alias)))
			return w;
	}

	return nullptr;
}

IATSourceWindow *ATOpenSourceWindow(const wchar_t *path) {
	ATDebuggerSourceFileInfo info;
	info.mSourcePath = path;
	return ATOpenSourceWindow(info, true);
}

IATSourceWindow *ATOpenSourceWindow(const ATDebuggerSourceFileInfo& sourceFileInfo, bool searchPaths) {
	IATSourceWindow *existing = ATGetSourceWindow(sourceFileInfo.mSourcePath.c_str());
	if (existing)
		return existing;

	VDStringW fullPath;
	bool found = false;

	if (searchPaths) {
		if (!found && !sourceFileInfo.mModulePath.empty()) {
			VDStringW moduleDir = VDFileSplitPathLeft(sourceFileInfo.mModulePath);
			if (!moduleDir.empty()) {
				fullPath = VDMakePath(
					VDStringSpanW(moduleDir),
					VDStringSpanW(VDFileSplitPath(sourceFileInfo.mSourcePath.c_str()))
				);
				found = VDDoesPathExist(fullPath.c_str());
			}
		}

		if (!found) {
			for (ATWxSourceWindow *sw : g_sourceWindows) {
				if (!sw) continue;
				VDStringW dir = VDFileSplitPathLeft(VDStringW(sw->GetFullPath()));
				if (dir.empty()) continue;
				fullPath = VDMakePath(
					VDStringSpanW(dir),
					VDStringSpanW(VDFileSplitPath(sourceFileInfo.mSourcePath.c_str()))
				);
				if (VDDoesPathExist(fullPath.c_str())) {
					found = true;
					break;
				}
			}
		}

		if (!found) {
			fullPath = sourceFileInfo.mSourcePath;
			found = VDDoesPathExist(fullPath.c_str());
		}
	} else {
		fullPath = sourceFileInfo.mSourcePath;
		found = VDDoesPathExist(fullPath.c_str());
	}

	if (!found)
		return nullptr;

	ATWxSourceWindow *w = new ATWxSourceWindow(
		sourceFileInfo.mSourcePath.c_str(), fullPath.c_str());

	if (!w->Load()) {
		delete w;
		return nullptr;
	}

	g_sourceWindows.push_back(w);
	return w;
}

void ATUIShowSourceListDialog() {
	// TODO: Show source file list in wx debugger
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
