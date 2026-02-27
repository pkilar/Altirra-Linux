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
#include "debugger.h"
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <imgui_manager.h>
#include <debugger_imgui.h>
#include <SDL.h>
#include <cstdio>
#include <cstdarg>
#include <vector>

// External reference to ImGui manager (owned by main_linux.cpp)
extern ATImGuiManager *g_pImGui;

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
	ATImGuiDebuggerAppendConsole(s);
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
	ATImGuiDebuggerNavigateSource(addr);
	return ATImGuiDebuggerShowSourceCode();
}

bool ATConsoleCheckBreak() {
	SDL_PumpEvents();
	const Uint8 *state = SDL_GetKeyboardState(nullptr);
	bool ctrl = state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_RCTRL];
	return ctrl && (state[SDL_SCANCODE_PAUSE] || state[SDL_SCANCODE_C]);
}

void ATShowConsole() {
	if (g_pImGui)
		g_pImGui->SetVisible(true);
}

void ATOpenConsole() {
	if (g_pImGui)
		g_pImGui->SetVisible(true);
}

void ATCloseConsole() {
	if (g_pImGui)
		g_pImGui->SetVisible(false);
}

bool ATIsDebugConsoleActive() {
	return g_pImGui && g_pImGui->IsVisible();
}

// Source window implementation — bridges IATSourceWindow to ImGui source view
class ATImGuiSourceWindow final : public IATSourceWindow {
public:
	ATImGuiSourceWindow(const wchar_t *path, const wchar_t *fullPath)
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
			// Check if we got a complete line
			if (!line.empty() && line.back() == '\n') {
				line.pop_back();
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				mLines.push_back(VDTextU8ToW(line));
				line.clear();
			}
		}
		// Handle last line without newline
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
		// Navigate the ImGui source window to this line
		ATImGuiDebuggerNavigateSourceLine(mPath.c_str(), line);
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

static std::vector<ATImGuiSourceWindow *> g_sourceWindows;

IATSourceWindow *ATGetSourceWindow(const wchar_t *s) {
	// Exact path match
	for (ATImGuiSourceWindow *w : g_sourceWindows) {
		if (!w) continue;
		if (VDFileIsPathEqual(s, w->GetPath()))
			return w;
		const wchar_t *alias = w->GetPathAlias();
		if (alias && VDFileIsPathEqual(s, alias))
			return w;
	}

	// Loose match on filename only
	const wchar_t *sName = VDFileSplitPath(s);
	for (ATImGuiSourceWindow *w : g_sourceWindows) {
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
	// Return existing window if already open
	IATSourceWindow *existing = ATGetSourceWindow(sourceFileInfo.mSourcePath.c_str());
	if (existing)
		return existing;

	VDStringW fullPath;
	bool found = false;

	if (searchPaths) {
		// Try module-relative path
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

		// Try paths from other open source windows
		if (!found) {
			for (ATImGuiSourceWindow *sw : g_sourceWindows) {
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

		// Try the path as-is (for absolute paths or paths relative to CWD)
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

	ATImGuiSourceWindow *w = new ATImGuiSourceWindow(
		sourceFileInfo.mSourcePath.c_str(), fullPath.c_str());

	if (!w->Load()) {
		delete w;
		return nullptr;
	}

	g_sourceWindows.push_back(w);
	return w;
}

void ATUIShowSourceListDialog() {
	// Toggle the ImGui source code window visibility
	ATImGuiDebuggerShowSourceCode() = true;
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
