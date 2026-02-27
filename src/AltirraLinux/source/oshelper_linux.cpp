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
#include <oshelper.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/filesys.h>
#include <at/atcore/enumparseimpl.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/system/file.h>
#include "encode_png.h"
#include "decode_png.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <SDL.h>

// Resource loading — on Linux, kernel ROMs are not embedded as Win32 resources.
// The firmware manager handles loading from external files.
// STUFF and PNG resources are embedded at build time via EmbedResources.cmake.

extern bool ATEmbeddedLoadMiscResource(int id, vdfastvector<uint8>& data);
extern bool ATEmbeddedLoadImageResource(uint32 id, const void *& pData, size_t& size);

const void *ATLockResource(uint32 id, size_t& size) {
	size = 0;
	return nullptr;
}

bool ATLoadKernelResource(int id, void *dst, uint32 offset, uint32 size, bool allowPartial) {
	return false;
}

bool ATLoadKernelResource(int id, vdfastvector<uint8>& data) {
	return false;
}

bool ATLoadKernelResourceLZPacked(int id, vdfastvector<uint8>& data) {
	return false;
}

bool ATLoadMiscResource(int id, vdfastvector<uint8>& data) {
	return ATEmbeddedLoadMiscResource(id, data);
}

bool ATLoadImageResource(uint32 id, VDPixmapBuffer& buf) {
	const void *pngData;
	size_t pngSize;
	if (!ATEmbeddedLoadImageResource(id, pngData, pngSize))
		return false;

	vdautoptr<IVDImageDecoderPNG> decoder(VDCreateImageDecoderPNG());
	if (decoder->Decode(pngData, (uint32)pngSize))
		return false;

	buf.assign(decoder->GetFrameBuffer());
	return true;
}

// File attributes
void ATFileSetReadOnlyAttribute(const wchar_t *path, bool readOnly) {
	VDStringA u8path = VDTextWToU8(VDStringW(path));
	struct stat st;
	if (stat(u8path.c_str(), &st) != 0)
		return;

	mode_t newMode = readOnly
		? (st.st_mode & ~(S_IWUSR | S_IWGRP | S_IWOTH))
		: (st.st_mode | S_IWUSR);

	chmod(u8path.c_str(), newMode);
}

// Clipboard — copy frame as PNG via xclip
void ATCopyFrameToClipboard(const VDPixmap& px) {
	if (!px.data || px.w <= 0 || px.h <= 0)
		return;

	VDPixmapBuffer pxbuf(px.w, px.h, nsVDPixmap::kPixFormat_RGB888);
	VDPixmapBlt(pxbuf, px);

	vdautoptr<IVDImageEncoderPNG> encoder(VDCreateImageEncoderPNG());
	const void *mem;
	uint32 len;
	encoder->Encode(pxbuf, mem, len, false);

	// Pipe PNG data to xclip for clipboard
	pid_t pid;
	int pipefd[2];
	if (pipe(pipefd) != 0)
		return;

	pid = fork();
	if (pid == 0) {
		// Child: read from pipe, redirect stdout/stderr to /dev/null
		close(pipefd[1]);
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execlp("xclip", "xclip", "-selection", "clipboard", "-t", "image/png", nullptr);
		_exit(1);
	} else if (pid > 0) {
		// Parent: write PNG data to pipe
		close(pipefd[0]);
		write(pipefd[1], mem, len);
		close(pipefd[1]);
		// Don't wait — xclip runs asynchronously (it forks itself to serve clipboard)
	} else {
		close(pipefd[0]);
		close(pipefd[1]);
	}
}

void ATCopyTextToClipboard(void *hwnd, const char *s) {
	SDL_SetClipboardText(s);
}

void ATCopyTextToClipboard(void *hwnd, const wchar_t *s) {
	VDStringA u8 = VDTextWToU8(VDStringW(s));
	SDL_SetClipboardText(u8.c_str());
}

// Frame I/O — PNG decoding using built-in decoder
void ATLoadFrame(VDPixmapBuffer& px, const wchar_t *filename) {
	VDFile f(filename);

	sint64 size = f.size();
	if (size > 256*1024*1024)
		throw MyError("File is too large to load.");

	vdblock<unsigned char> buf((uint32)size);
	f.read(buf.data(), (long)size);
	f.close();

	ATLoadFrameFromMemory(px, buf.data(), size);
}

void ATLoadFrameFromMemory(VDPixmapBuffer& px, const void *mem, size_t len) {
	// Check for PNG signature
	if (len >= 8 && !memcmp(mem, "\x89PNG\r\n\x1a\n", 8)) {
		vdautoptr<IVDImageDecoderPNG> decoder(VDCreateImageDecoderPNG());
		PNGDecodeError err = decoder->Decode(mem, (uint32)len);
		if (err != kPNGDecodeOK)
			throw MyError("PNG decode error (%d).", (int)err);

		const VDPixmap& src = decoder->GetFrameBuffer();
		px.init(src.w, src.h, nsVDPixmap::kPixFormat_XRGB8888);
		VDPixmapBlt(px, src);
		return;
	}

	throw MyError("Unsupported image format.");
}

void ATSaveFrame(const VDPixmap& px, const wchar_t *filename) {
	VDPixmapBuffer pxbuf(px.w, px.h, nsVDPixmap::kPixFormat_RGB888);
	VDPixmapBlt(pxbuf, px);

	vdautoptr<IVDImageEncoderPNG> encoder(VDCreateImageEncoderPNG());
	const void *mem;
	uint32 len;
	encoder->Encode(pxbuf, mem, len, false);

	VDFile f(filename, nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways);
	f.write(mem, len);
}

// Window placement — not applicable on Linux (window manager handles this)
void ATUISaveWindowPlacement(void *hwnd, const char *name) {
}

void ATUISaveWindowPlacement(const char *name, const vdrect32& r, bool isMaximized, uint32 dpi) {
}

void ATUIRestoreWindowPlacement(void *hwnd, const char *name, int nCmdShow, bool sizeOnly) {
}

void ATUIEnableEditControlAutoComplete(void *hwnd) {
}

// Help system
VDStringW ATGetHelpPath() {
	return VDStringW();
}

void ATShowHelp(void *hwnd, const wchar_t *filename) {
}

// URL/file launching via xdg-open
static void LaunchXdgOpen(const char *arg) {
	pid_t pid = fork();
	if (pid == 0) {
		// Child: redirect stdout/stderr to /dev/null
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execlp("xdg-open", "xdg-open", arg, nullptr);
		_exit(1);
	}
	// Parent: don't wait — xdg-open runs asynchronously
}

void ATLaunchURL(const wchar_t *url) {
	VDStringA u8 = VDTextWToU8(VDStringW(url));
	LaunchXdgOpen(u8.c_str());
}

void ATLaunchFileForEdit(const wchar_t *file) {
	VDStringA u8 = VDTextWToU8(VDStringW(file));
	LaunchXdgOpen(u8.c_str());
}

// Privilege checking
bool ATIsUserAdministrator() {
	return geteuid() == 0;
}

// GUID generation using /dev/urandom
void ATGenerateGuid(uint8 guid[16]) {
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		ssize_t r = read(fd, guid, 16);
		close(fd);
		if (r == 16) {
			// Set version 4 (random) and variant 1
			guid[6] = (guid[6] & 0x0F) | 0x40;
			guid[8] = (guid[8] & 0x3F) | 0x80;
			return;
		}
	}
	// Fallback: use rand()
	for (int i = 0; i < 16; ++i)
		guid[i] = (uint8)(rand() & 0xFF);
	guid[6] = (guid[6] & 0x0F) | 0x40;
	guid[8] = (guid[8] & 0x3F) | 0x80;
}

void ATShowFileInSystemExplorer(const wchar_t *filename) {
	// Open the parent directory containing the file
	VDStringW dir(filename);
	VDStringW parentDir = VDFileSplitPathLeft(dir);
	if (parentDir.empty())
		parentDir = L".";
	VDStringA u8 = VDTextWToU8(parentDir);
	LaunchXdgOpen(u8.c_str());
}

void ATRelaunchElevated(VDGUIHandle parent, const wchar_t *params) {
}

void ATRelaunchElevatedWithEscapedArgs(VDGUIHandle parent, vdspan<const wchar_t *> args) {
}

AT_DEFINE_ENUM_TABLE_BEGIN(ATProcessEfficiencyMode)
	{ ATProcessEfficiencyMode::Default, "default" },
	{ ATProcessEfficiencyMode::Performance, "performance" },
	{ ATProcessEfficiencyMode::Efficiency, "efficiency" },
AT_DEFINE_ENUM_TABLE_END(ATProcessEfficiencyMode, ATProcessEfficiencyMode::Default)

void ATSetProcessEfficiencyMode(ATProcessEfficiencyMode mode) {
}
