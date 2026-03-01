//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - Bitmap-only enhanced text engine
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

// Bitmap-only enhanced text engine for Linux. Renders ALL characters from
// the Atari's built-in 8x8 font ROM, resampled via CharResampler to display
// resolution. No GDI/FreeType dependency.

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/resample.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/ksyms.h>
#include <at/atui/uiwidget.h>
#include "uienhancedtext.h"
#include "antic.h"
#include "gtia.h"
#include "oshelper.h"
#include "resource.h"
#include "simulator.h"
#include "virtualscreen.h"
#include <SDL3/SDL.h>

class ATUIEnhancedTextEngine final : public IATUIEnhancedTextEngine, public IATDeviceVideoOutput {
	ATUIEnhancedTextEngine(const ATUIEnhancedTextEngine&) = delete;
	ATUIEnhancedTextEngine& operator=(const ATUIEnhancedTextEngine&) = delete;
public:
	ATUIEnhancedTextEngine();
	~ATUIEnhancedTextEngine();

	void Init(IATUIEnhancedTextOutput *output, ATSimulator *sim) override;
	void Shutdown() override;

	bool IsRawInputEnabled() const override;
	IATDeviceVideoOutput *GetVideoOutput() override;

	void OnSize(uint32 w, uint32 h) override;
	void OnChar(int ch) override;
	bool OnKeyDown(uint32 keyCode) override;
	bool OnKeyUp(uint32 keyCode) override;

	void Paste(const wchar_t *s, size_t len) override;

	void Update(bool forceInvalidate) override;

public:
	// IATDeviceVideoOutput
	const char *GetName() const override;
	const wchar_t *GetDisplayName() const override;
	void Tick(uint32 hz300ticks) override;
	void UpdateFrame() override;
	const VDPixmap& GetFrameBuffer() override;
	const ATDeviceVideoInfo& GetVideoInfo() override;
	vdpoint32 PixelToCaretPos(const vdpoint32& pixelPos) override;
	vdrect32 CharToPixelRect(const vdrect32& r) override;
	int ReadRawText(uint8 *dst, int x, int y, int n) override;
	uint32 GetActivityCounter() override;

protected:
	void Paint(const bool *lineRedrawFlags);
	void PaintHWMode(const bool *lineRedrawFlags);
	void PaintSWMode(const bool *lineRedrawFlags);

	void AddToHistory(const char *s);
	void OnInputReady();
	void OnVirtualScreenResized();
	void OnPassThroughChanged();
	void ProcessPastedInput();
	void ResetAttractTimeout();
	void RebuildFontAtlas();
	void BlitChar(uint32 *dst, ptrdiff_t dstPitch, uint8 ch, bool invert, const uint32 *gammaTable);
	void BlitCharColor(uint32 *dst, ptrdiff_t dstPitch, uint8 ch, uint32 color);
	void FillRect(int x, int y, int w, int h, uint32 color);

	class CharResampler;

	IATUIEnhancedTextOutput *mpOutput = nullptr;
	bool	mbLineInputEnabled = false;

	int		mBitmapWidth = 0;
	int		mBitmapHeight = 0;
	int		mTextCharW = 16;
	int		mTextCharH = 16;

	bool	mbInverseEnabled = true;

	sint32	mViewportWidth = 0;
	sint32	mViewportHeight = 0;

	uint32	mTextLastForeColor = 0;
	uint32	mTextLastBackColor = 0;
	uint32	mTextLastPFColors[4] = {};
	uint32	mTextLastBorderColor = 0;
	int		mTextLastTotalHeight = 0;
	int		mTextLastLineCount = 0;
	uint8	mTextLineMode[30] = {};
	uint8	mTextLineCHBASE[30] = {};
	uint8	mTextLastData[30][40] = {};

	// Only used in HW mode.
	sint32	mActiveLineYStart[31] = {};
	uint32	mActiveLineCount = 0;

	vdfastvector<char> mInputBuffer;
	uint32 mInputPos = 0;
	uint32 mInputHistIdx = 0;

	vdfastvector<char> mHistoryBuffer;
	vdfastvector<uint8> mLastScreen;
	bool mbLastScreenValid = false;
	bool mbLastInputLineDirty = false;
	bool mbLastCursorPresent = false;
	uint32 mLastCursorX = 0;
	uint32 mLastCursorY = 0;

	bool mbUsingCustomFont = false;

	uint32 mLastGammaTableForeColor = 0;
	uint32 mLastGammaTableBackColor = 0;

	vdfastdeque<wchar_t> mPasteBuffer;

	ATGTIAEmulator *mpGTIA = nullptr;
	ATAnticEmulator *mpANTIC = nullptr;
	ATSimulator *mpSim = nullptr;

	ATDeviceVideoInfo mVideoInfo = {};
	VDPixmapBuffer mFrameBufferStorage;
	VDPixmap mFrameBuffer = {};

	VDPixmapBuffer mRescaledDefaultFont;
	VDPixmapBuffer mRescaledCustomFont;
	uint8 mRawDefaultFont[1024] {};
	uint8 mCurrentCustomFont[1024] {};
	uint8 mMirroredCustomFont[1024] {};
	uint32 mGammaTable[256] {};

	static const uint8 kInternalToATASCIIXorTab[4];
};

////////////////////////////////////////////////////////////////////////////////

class ATUIEnhancedTextEngine::CharResampler {
public:
	CharResampler(uint32 charWidth, uint32 charHeight);

	void ResampleChar(VDPixmap& dst, uint8 ch, const uint8 rawChar[8]);

private:
	vdautoptr<IVDPixmapResampler> mpResampler;
	VDPixmapBuffer mExpand8x8Buffer;

	const uint32 mCharWidth;
	const uint32 mCharHeight;
};

ATUIEnhancedTextEngine::CharResampler::CharResampler(uint32 charWidth, uint32 charHeight)
	: mpResampler(VDCreatePixmapResampler())
	, mCharWidth(charWidth)
	, mCharHeight(charHeight)
{
	mpResampler->SetFilters(IVDPixmapResampler::kFilterSharpLinear, IVDPixmapResampler::kFilterSharpLinear, false);
	mpResampler->SetSharpnessFactors(1.5f, 1.5f);
	VDVERIFY(mpResampler->Init(charWidth, charHeight, nsVDPixmap::kPixFormat_Y8_FR, 8, 8, nsVDPixmap::kPixFormat_Y8_FR));

	mExpand8x8Buffer.init(8, 8, nsVDPixmap::kPixFormat_Y8_FR);
}

void ATUIEnhancedTextEngine::CharResampler::ResampleChar(VDPixmap& dst, uint8 ch, const uint8 rawChar[8]) {
	VDPixmap pxdst = VDPixmapClip(dst, 0, mCharHeight * (uint32)ch, mCharWidth, mCharHeight);
	VDPixmap pxsrc {};
	pxsrc.data = (void *)rawChar;
	pxsrc.pitch = 1;
	pxsrc.w = 8;
	pxsrc.h = 8;
	pxsrc.format = nsVDPixmap::kPixFormat_Pal1;

	static constexpr uint32 kPal[2] { 0, 0xFFFFFF };
	pxsrc.palette = kPal;

	VDPixmapBlt(mExpand8x8Buffer, pxsrc);

	mpResampler->Process(pxdst, mExpand8x8Buffer);
}

////////////////////////////////////////////////////////////////////////////////

constexpr uint8 ATUIEnhancedTextEngine::kInternalToATASCIIXorTab[4]={
	0x20, 0x60, 0x40, 0x00
};

ATUIEnhancedTextEngine::ATUIEnhancedTextEngine() {
	mVideoInfo.mPixelAspectRatio = 1.0f;
	mVideoInfo.mbSignalValid = true;
	mVideoInfo.mbSignalPassThrough = false;
	mVideoInfo.mHorizScanRate = 15735.0f;
	mVideoInfo.mVertScanRate = 59.94f;
	mVideoInfo.mbForceExactPixels = true;

	memset(mTextLineMode, 0, sizeof mTextLineMode);
	memset(mTextLineCHBASE, 0, sizeof mTextLineCHBASE);
	memset(mTextLastData, 0, sizeof mTextLastData);
}

ATUIEnhancedTextEngine::~ATUIEnhancedTextEngine() {
	Shutdown();
}

void ATUIEnhancedTextEngine::Init(IATUIEnhancedTextOutput *output, ATSimulator *sim) {
	mpOutput = output;
	mpSim = sim;
	mpGTIA = &sim->GetGTIA();
	mpANTIC = &sim->GetAntic();

	// Load the raw default font from AltirraOS ($E000-E3FF from $D800-FFFF)
	VDVERIFY(ATLoadKernelResource(IDR_KERNEL, mRawDefaultFont, 0x0800, sizeof mRawDefaultFont, true));

	// Swizzle font to ATASCII order
	std::rotate(mRawDefaultFont, mRawDefaultFont + 0x200, mRawDefaultFont + 0x300);

	auto *vs = sim->GetVirtualScreenHandler();
	if (vs) {
		vs->SetReadyCallback([this] { OnInputReady(); });
		vs->SetResizeCallback([this] { OnVirtualScreenResized(); });
		vs->SetPassThroughChangedCallback([this] { OnPassThroughChanged(); });
	}

	OnPassThroughChanged();
}

void ATUIEnhancedTextEngine::Shutdown() {
	if (mpSim) {
		IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
		if (vs) {
			vs->SetReadyCallback(nullptr);
			vs->SetResizeCallback(nullptr);
			vs->SetPassThroughChangedCallback(nullptr);
		}
	}

	mpSim = nullptr;
	mpGTIA = nullptr;
	mpANTIC = nullptr;
}

bool ATUIEnhancedTextEngine::IsRawInputEnabled() const {
	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

	return !vs || vs->IsRawInputActive();
}

IATDeviceVideoOutput *ATUIEnhancedTextEngine::GetVideoOutput() {
	return this;
}

void ATUIEnhancedTextEngine::OnSize(uint32 w, uint32 h) {
	if (!w || !h)
		return;

	if (w > 32767)
		w = 32767;

	if (h > 32767)
		h = 32767;

	if (mViewportWidth == (sint32)w && mViewportHeight == (sint32)h)
		return;

	mViewportWidth = w;
	mViewportHeight = h;

	uint32 charW;
	uint32 charH;

	if (IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler()) {
		const vdsize32& termSize = vs->GetTerminalSize();
		charW = termSize.w;
		charH = termSize.h;
	} else {
		charW = 40;
		charH = 30;
	}

	// Compute char cell size from viewport and terminal dimensions
	mTextCharW = std::max<int>(4, w / charW);
	mTextCharH = std::max<int>(4, h / charH);

	// Keep square-ish aspect ratio for the font cells
	int cellSize = std::min(mTextCharW, mTextCharH);
	mTextCharW = cellSize;
	mTextCharH = cellSize;

	uint32 adjustedW = (uint32)(charW * mTextCharW);
	uint32 adjustedH = (uint32)(charH * mTextCharH);

	if (mBitmapWidth == (int)adjustedW && mBitmapHeight == (int)adjustedH)
		return;

	mBitmapWidth = adjustedW;
	mBitmapHeight = adjustedH;

	++mVideoInfo.mFrameBufferLayoutChangeCount;
	++mVideoInfo.mFrameBufferChangeCount;

	// Allocate VDPixmapBuffer (top-down, positive pitch)
	mFrameBufferStorage.init(adjustedW, adjustedH, nsVDPixmap::kPixFormat_XRGB8888);
	memset(mFrameBufferStorage.base(), 0, mFrameBufferStorage.size());

	mFrameBuffer.data = mFrameBufferStorage.data;
	mFrameBuffer.w = adjustedW;
	mFrameBuffer.h = adjustedH;
	mFrameBuffer.format = nsVDPixmap::kPixFormat_XRGB8888;
	mFrameBuffer.pitch = mFrameBufferStorage.pitch;

	mVideoInfo.mTextColumns = charW;
	mVideoInfo.mTextRows = charH;
	mVideoInfo.mDisplayArea.set(0, 0, adjustedW, adjustedH);

	mbLastScreenValid = false;
	++mVideoInfo.mFrameBufferChangeCount;

	// Rebuild rescaled font atlas for new char size
	RebuildFontAtlas();

	Update(true);

	if (mpOutput)
		mpOutput->InvalidateTextOutput();
}

void ATUIEnhancedTextEngine::OnChar(int ch) {
	ResetAttractTimeout();

	if (ch >= 0x20 && ch < 0x7F) {
		IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
		if (vs) {
			if (vs->GetShiftLockState()) {
				if (ch >= 'a' && ch <= 'z')
					ch &= 0xdf;
			}
		}

		mInputBuffer.insert(mInputBuffer.begin() + mInputPos, (char)ch);
		++mInputPos;
		mbLastInputLineDirty = true;
	}
}

bool ATUIEnhancedTextEngine::OnKeyDown(uint32 keyCode) {
	if (IsRawInputEnabled())
		return false;

	ResetAttractTimeout();

	switch(keyCode) {
		case kATUIVK_Left:
			if (mInputPos) {
				--mInputPos;
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Right:
			if (mInputPos < mInputBuffer.size()) {
				++mInputPos;
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Up:
			if (mInputHistIdx < mHistoryBuffer.size()) {
				const char *base = mHistoryBuffer.data();
				const char *s = (mHistoryBuffer.end() - mInputHistIdx - 1);

				while(s > base && s[-1])
					--s;

				uint32 len = (uint32)strlen(s);
				mInputHistIdx += len + 1;

				mInputBuffer.assign(s, s + len);
				mInputPos = (uint32)mInputBuffer.size();
				mbLastInputLineDirty = true;
			}

			return true;

		case kATUIVK_Down:
			if (mInputHistIdx) {
				mInputHistIdx -= (1 + (uint32)strlen(&*(mHistoryBuffer.end() - mInputHistIdx)));

				if (mInputHistIdx) {
					const char *s = &*(mHistoryBuffer.end() - mInputHistIdx);

					mInputBuffer.assign(s, s + strlen(s));
				} else {
					mInputBuffer.clear();
				}

				mInputPos = (uint32)mInputBuffer.size();
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Back:
			if (mInputPos) {
				--mInputPos;
				mInputBuffer.erase(mInputBuffer.begin() + mInputPos);
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Delete:
			if (mInputPos < mInputBuffer.size()) {
				mInputBuffer.erase(mInputBuffer.begin() + mInputPos);
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Return:
			{
				IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

				if (vs) {
					mInputBuffer.push_back(0);
					vs->PushLine(mInputBuffer.data());
					AddToHistory(mInputBuffer.data());

					mInputBuffer.clear();
					mInputPos = 0;
					mInputHistIdx = 0;
					mbLastInputLineDirty = true;
				}
			}
			return true;

		case kATUIVK_CapsLock:
			{
				IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

				if (vs) {
					// Use SDL_GetModState() instead of Windows GetKeyState()
					SDL_Keymod mod = SDL_GetModState();
					bool shift = (mod & SDL_KMOD_SHIFT) != 0;
					bool ctrl = (mod & SDL_KMOD_CTRL) != 0;

					if (shift) {
						if (!ctrl) {
							vs->SetShiftControlLockState(true, false);
						}
					} else {
						if (ctrl) {
							vs->SetShiftControlLockState(false, true);
						} else {
							if (vs->GetShiftLockState() || vs->GetControlLockState())
								vs->SetShiftControlLockState(false, false);
							else
								vs->SetShiftControlLockState(true, false);
						}
					}
				}
			}
			return true;
	}

	return false;
}

bool ATUIEnhancedTextEngine::OnKeyUp(uint32 keyCode) {
	if (IsRawInputEnabled())
		return false;

	switch(keyCode) {
		case kATUIVK_Left:
		case kATUIVK_Right:
		case kATUIVK_Up:
		case kATUIVK_Down:
		case kATUIVK_Back:
		case kATUIVK_Delete:
		case kATUIVK_Return:
		case kATUIVK_CapsLock:
			return true;
	}

	return false;
}

void ATUIEnhancedTextEngine::Paste(const wchar_t *s, size_t len) {
	char skipNext = 0;

	while(len--) {
		wchar_t c = *s++;

		if (c == skipNext) {
			skipNext = 0;
			continue;
		}

		if (c == L'\r' || c == L'\n') {
			skipNext = c ^ (L'\r' ^ L'\n');
			c = L'\n';
		}

		mPasteBuffer.push_back(c);
	}

	ProcessPastedInput();
}

void ATUIEnhancedTextEngine::Update(bool forceInvalidate) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();

	if (vs && vs->CheckForBell()) {
		// No-op on Linux (no MessageBeep equivalent needed)
	}

	// Get colors from GTIA — convert from 24-bit playfield colors to XRGB8888
	// GTIA returns colors as 0x00RRGGBB; we need to track them for change detection
	uint32 colorBack = VDSwizzleU32(mpGTIA->GetPlayfieldColor24(2)) >> 8;
	uint32 colorFore = VDSwizzleU32(mpGTIA->GetPlayfieldColorPF2H()) >> 8;
	uint32 colorBorder = VDSwizzleU32(mpGTIA->GetBackgroundColor24()) >> 8;

	if (mTextLastBackColor != colorBack) {
		mTextLastBackColor = colorBack;
		forceInvalidate = true;
	}

	if (mTextLastForeColor != colorFore) {
		mTextLastForeColor = colorFore;
		forceInvalidate = true;
	}

	if (mTextLastBorderColor != colorBorder) {
		mTextLastBorderColor = colorBorder;

		mVideoInfo.mBorderColor = VDSwizzleU32(colorBorder) >> 8;

		forceInvalidate = true;
	}

	for(int i=0; i<4; ++i) {
		uint32 colorPF = VDSwizzleU32(mpGTIA->GetPlayfieldColor24(i)) >> 8;

		if (mTextLastPFColors[i] != colorPF) {
			mTextLastPFColors[i] = colorPF;
			forceInvalidate = true;
		}
	}

	if (vs) {
		if (vs->IsPassThroughEnabled())
			return;

		// Check if a custom font is being used
		ATMemoryManager *mem = mpSim->GetMemoryManager();
		const uint8 chbase = mem->DebugReadByte(ATKernelSymbols::CHBAS);

		if (chbase != 0xE0) {
			bool forceRematchAllChars = false;

			if (!mbUsingCustomFont) {
				mbUsingCustomFont = true;

				forceInvalidate = true;
				forceRematchAllChars = true;
			}

			const uint16 chbase16 = (uint16)(chbase & 0xFC) << 8;
			mem->DebugAnticReadMemory(mMirroredCustomFont+0x000, chbase16+0x200, 256);
			mem->DebugAnticReadMemory(mMirroredCustomFont+0x100, chbase16+0x000, 256);
			mem->DebugAnticReadMemory(mMirroredCustomFont+0x200, chbase16+0x100, 256);
			mem->DebugAnticReadMemory(mMirroredCustomFont+0x300, chbase16+0x300, 256);

			if (memcmp(mCurrentCustomFont, mMirroredCustomFont, 1024)) {
				forceInvalidate = true;

				CharResampler cr(mTextCharW, mTextCharH);

				for(int i=0; i<128; ++i) {
					const uint8 *src = &mMirroredCustomFont[i*8];
					uint8 *dst = &mCurrentCustomFont[i*8];
					if (forceRematchAllChars || memcmp(dst, src, 8)) {
						memcpy(dst, src, 8);

						cr.ResampleChar(mRescaledCustomFont, i, src);
					}
				}
			}
		} else {
			if (mbUsingCustomFont) {
				mbUsingCustomFont = false;

				forceInvalidate = true;
			}
		}

		// Check CHACTL shadow to see if inverse video is enabled
		const uint8 chact = mem->DebugReadByte(ATKernelSymbols::CHACT);
		const bool inverseEnabled = (chact & 2) != 0;

		if (mbInverseEnabled != inverseEnabled) {
			mbInverseEnabled = inverseEnabled;

			forceInvalidate = true;
		}

		bool lineFlags[255] = {false};
		bool *lineFlagsPtr = lineFlags;

		uint32 w, h;
		const uint8 *screen;
		vs->GetScreen(w, h, screen);

		uint32 cursorX, cursorY;
		bool cursorPresent = vs->GetCursorInfo(cursorX, cursorY) && inverseEnabled;

		bool dirty = false;
		uint32 n = w * h;
		if (n != mLastScreen.size() || !mbLastScreenValid || forceInvalidate) {
			mLastScreen.resize(n);
			memcpy(mLastScreen.data(), screen, n);

			mbLastScreenValid = true;
			mbLastInputLineDirty = true;
			lineFlagsPtr = nullptr;
			dirty = true;
		} else {
			uint8 *last = mLastScreen.data();

			for(uint32 y=0; y<h; ++y) {
				if (memcmp(last, screen, w)) {
					memcpy(last, screen, w);
					lineFlagsPtr[y] = true;
					dirty = true;
				}

				last += w;
				screen += w;
			}
		}

		if (cursorX != mLastCursorX || cursorY != mLastCursorY || cursorPresent != mbLastCursorPresent) {
			mbLastInputLineDirty = true;

			if (lineFlagsPtr && mLastCursorY < h && mbLastCursorPresent) {
				lineFlagsPtr[mLastCursorY] = true;
				dirty = true;
			}
		}

		if (mbLastInputLineDirty) {
			mbLastInputLineDirty = false;

			mLastCursorX = cursorX;
			mLastCursorY = cursorY;
			mbLastCursorPresent = cursorPresent;

			if (cursorPresent && cursorY < h && lineFlagsPtr)
				lineFlagsPtr[cursorY] = true;

			dirty = true;
		}

		if (dirty)
			Paint(lineFlagsPtr);
		return;
	}

	// HW mode — update data from ANTIC
	const ATAnticEmulator::DLHistoryEntry *history = mpANTIC->GetDLHistory();

	if (!mbLastScreenValid) {
		mbLastScreenValid = true;
		forceInvalidate = true;
	}

	int line = 0;
	bool redrawFlags[30] = {false};
	bool linesDirty = false;
	for(int y=8; y<240; ++y) {
		const ATAnticEmulator::DLHistoryEntry& hval = history[y];

		if (!hval.mbValid)
			continue;

		const uint8 mode = (hval.mControl & 0x0F);

		if (mode != 2 && mode != 6 && mode != 7)
			continue;

		int pfWidth = hval.mDMACTL & 3;

		if (!pfWidth)
			continue;

		int baseWidth = pfWidth == 1 ? 16 : pfWidth == 2 ? 20 : 24;

		uint32 pfAddr = hval.mPFAddress;

		const int width = (mode == 2 ? baseWidth * 2 : baseWidth);
		uint8 *lastData = mTextLastData[line];
		uint8 data[48] = {0};
		for(int i=0; i<width; ++i) {
			uint8 c = mpSim->DebugAnticReadByte(pfAddr + i);

			data[i] = c;
		}

		if (mode != mTextLineMode[line])
			forceInvalidate = true;

		const uint8 chbase = hval.mCHBASE << 1;
		if (chbase != mTextLineCHBASE[line])
			forceInvalidate = true;

		if (forceInvalidate || line >= mTextLastLineCount || memcmp(data, lastData, width)) {
			mTextLineMode[line] = mode;
			mTextLineCHBASE[line] = chbase;
			memcpy(lastData, data, 40);
			redrawFlags[line] = true;
			linesDirty = true;
		}

		++line;
	}

	mTextLastLineCount = line;

	if (forceInvalidate || linesDirty) {
		Paint(forceInvalidate ? nullptr : redrawFlags);
	}
}

const char *ATUIEnhancedTextEngine::GetName() const {
	return "enhtext";
}

const wchar_t *ATUIEnhancedTextEngine::GetDisplayName() const {
	return L"Enhanced Text";
}

void ATUIEnhancedTextEngine::Tick(uint32 hz300ticks) {
}

void ATUIEnhancedTextEngine::UpdateFrame() {
}

const VDPixmap& ATUIEnhancedTextEngine::GetFrameBuffer() {
	return mFrameBuffer;
}

const ATDeviceVideoInfo& ATUIEnhancedTextEngine::GetVideoInfo() {
	return mVideoInfo;
}

vdpoint32 ATUIEnhancedTextEngine::PixelToCaretPos(const vdpoint32& pixelPos) {
	if (pixelPos.y < 0)
		return vdpoint32(0, 0);

	if (pixelPos.y >= mVideoInfo.mDisplayArea.bottom)
		return vdpoint32(mVideoInfo.mTextColumns - 1, mVideoInfo.mTextRows - 1);

	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();
	if (vs) {
		return vdpoint32(
			pixelPos.x < 0 ? 0
			: pixelPos.x >= mVideoInfo.mDisplayArea.right ? mVideoInfo.mTextColumns - 1
			: ((pixelPos.x * 2 / mTextCharW) + 1) >> 1,
			pixelPos.y / mTextCharH);
	} else {
		auto itBegin = std::begin(mActiveLineYStart);
		auto itEnd = std::begin(mActiveLineYStart) + mTextLastLineCount;
		auto it = std::upper_bound(itBegin, itEnd, pixelPos.y);

		if (it == itBegin)
			return vdpoint32(0, 0);

		if (it == itEnd)
			return vdpoint32(mVideoInfo.mTextColumns - 1, mVideoInfo.mTextRows - 1);

		int row = (int)(it - itBegin) - 1;

		int px = pixelPos.x;
		int col = 0;

		if (px >= 0) {
			if (mTextLineMode[row] == 2)
				col = std::min<sint32>(40, ((px * 2) / mTextCharW + 1) >> 1);
			else
				col = std::min<sint32>(20, (px / mTextCharW + 1) >> 1);
		}

		return vdpoint32(col, row);
	}
}

vdrect32 ATUIEnhancedTextEngine::CharToPixelRect(const vdrect32& r) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();
	if (vs) {
		return vdrect32(r.left * mTextCharW, r.top * mTextCharH, r.right * mTextCharW, r.bottom * mTextCharH);
	} else {
		if (!mTextLastLineCount)
			return vdrect32(0, 0, 0, 0);

		vdrect32 rp;

		if (r.top < 0) {
			rp.left = 0;
			rp.top = mActiveLineYStart[0];
		} else {
			const int charCount = (mTextLineMode[mTextLastLineCount - 1] == 2 ? 40 : 20);

			if (r.top >= mTextLastLineCount) {
				rp.left = charCount * mTextCharW;
				rp.top = mActiveLineYStart[mTextLastLineCount - 1];
			} else {
				rp.left = std::min<int>(charCount, r.left) * (mTextLineMode[r.top] == 2 ? mTextCharW : mTextCharW * 2);
				rp.top = mActiveLineYStart[r.top];
			}
		}

		if (r.bottom <= 0) {
			rp.right = 0;
			rp.bottom = mActiveLineYStart[0];
		} else {
			const int charCount = (mTextLineMode[mTextLastLineCount - 1] == 2 ? 40 : 20);
			if (r.bottom > mTextLastLineCount) {
				rp.right = charCount * mTextCharW;
				rp.bottom = mActiveLineYStart[mTextLastLineCount];
			} else {
				rp.right = std::min<int>(charCount, r.right) * (mTextLineMode[r.bottom - 1] == 2 ? mTextCharW : mTextCharW * 2);
				rp.bottom = mActiveLineYStart[r.bottom];
			}
		}

		return rp;
	}
}

int ATUIEnhancedTextEngine::ReadRawText(uint8 *dst, int x, int y, int n) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();

	if (vs)
		return vs->ReadRawText(dst, x, y, n);

	if (x < 0 || y < 0 || y >= mTextLastLineCount)
		return 0;

	const uint8 mode = mTextLineMode[y];
	int nc = (mode == 2) ? 40 : 20;

	if (x >= nc)
		return 0;

	if (n > nc - x)
		n = nc - x;

	const uint8 *src = mTextLastData[y] + x;
	const uint8 chmask = (mode != 2) ? 0x3F : 0x7F;
	const uint8 choffset = (mode != 2 && (mTextLineCHBASE[y] & 2)) ? 0x40 : 0;
	for(int i=0; i<n; ++i) {
		uint8 c = (src[i] & chmask) + choffset;

		c ^= kInternalToATASCIIXorTab[(c >> 5) & 3];

		if ((uint8)((c & 0x7f) - 0x20) >= 0x5f)
			c = (c & 0x80) + '.';

		dst[i] = c;
	}

	return n;
}

uint32 ATUIEnhancedTextEngine::GetActivityCounter() {
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Rendering
////////////////////////////////////////////////////////////////////////////////

void ATUIEnhancedTextEngine::Paint(const bool *lineRedrawFlags) {
	// If colors changed, recompute gamma table
	if (mLastGammaTableForeColor != mTextLastForeColor ||
		mLastGammaTableBackColor != mTextLastBackColor)
	{
		mLastGammaTableForeColor = mTextLastForeColor;
		mLastGammaTableBackColor = mTextLastBackColor;

		using namespace nsVDVecMath;
		const vdfloat32x3 foreColor = vdfloat32x3(VDColorRGB::FromBGR8(mTextLastForeColor).SRGBToLinear());
		const vdfloat32x3 backColor = vdfloat32x3(VDColorRGB::FromBGR8(mTextLastBackColor).SRGBToLinear());

		for(int i=0; i<256; ++i) {
			float rawAlpha = (float)i / 255.0f;

			// Apply contrast enhancement (S-curve)
			float alpha;
			if (rawAlpha < 0.25f)
				alpha = 0.0f;
			else if (rawAlpha > 0.75f)
				alpha = 1.0f;
			else
				alpha = (rawAlpha - 0.25f) * 2.0f;

			mGammaTable[i] = VDColorRGB(saturate(lerp(backColor, foreColor, alpha))).LinearToSRGB().ToRGB8();
		}
	}

	if (mpSim->GetVirtualScreenHandler())
		PaintSWMode(lineRedrawFlags);
	else
		PaintHWMode(lineRedrawFlags);

	++mVideoInfo.mFrameBufferChangeCount;

	if (mpOutput)
		mpOutput->InvalidateTextOutput();
}

void ATUIEnhancedTextEngine::BlitChar(uint32 *dst, ptrdiff_t dstPitch, uint8 ch, bool invert, const uint32 *gammaTable) {
	const VDPixmap& charSrc = mbUsingCustomFont ? mRescaledCustomFont : mRescaledDefaultFont;
	const uint8 *src = (const uint8 *)charSrc.data + charSrc.pitch * (ch & 0x7F) * mTextCharH;
	const uint8 invertMask = invert ? 0xFF : 0;

	for(int row = 0; row < mTextCharH; ++row) {
		for(int col = 0; col < mTextCharW; ++col)
			dst[col] = gammaTable[src[col] ^ invertMask];

		src += charSrc.pitch;
		dst = (uint32 *)((char *)dst + dstPitch);
	}
}

void ATUIEnhancedTextEngine::BlitCharColor(uint32 *dst, ptrdiff_t dstPitch, uint8 ch, uint32 color) {
	const VDPixmap& charSrc = mbUsingCustomFont ? mRescaledCustomFont : mRescaledDefaultFont;
	const uint8 *src = (const uint8 *)charSrc.data + charSrc.pitch * (ch & 0x7F) * mTextCharH;
	const uint32 borderColor = VDSwizzleU32(mTextLastBorderColor) >> 8;

	for(int row = 0; row < mTextCharH; ++row) {
		for(int col = 0; col < mTextCharW; ++col)
			dst[col] = src[col] > 127 ? color : borderColor;

		src += charSrc.pitch;
		dst = (uint32 *)((char *)dst + dstPitch);
	}
}

void ATUIEnhancedTextEngine::FillRect(int x, int y, int w, int h, uint32 color) {
	if (x < 0 || y < 0 || x + w > mBitmapWidth || y + h > mBitmapHeight)
		return;

	uint32 *dst = (uint32 *)((char *)mFrameBuffer.data + mFrameBuffer.pitch * y) + x;

	for(int row = 0; row < h; ++row) {
		for(int col = 0; col < w; ++col)
			dst[col] = color;

		dst = (uint32 *)((char *)dst + mFrameBuffer.pitch);
	}
}

void ATUIEnhancedTextEngine::PaintHWMode(const bool *lineRedrawFlags) {
	// Convert COLORREF (BGR) values to XRGB8888 for direct framebuffer use
	const uint32 colorBack = VDSwizzleU32(mTextLastBackColor) >> 8;
	const uint32 colorFore = VDSwizzleU32(mTextLastForeColor) >> 8;
	const uint32 colorBorder = VDSwizzleU32(mTextLastBorderColor) >> 8;

	const VDPixmap& charSrc = mRescaledDefaultFont;

	int py = 0;

	for(int line = 0; line < mTextLastLineCount; ++line) {
		uint8 *data = mTextLastData[line];

		const uint8 mode = mTextLineMode[line];
		const uint8 chbase = mTextLineCHBASE[line];
		int charWidth = (mode == 2 ? mTextCharW : mTextCharW*2);
		int charHeight = (mode != 7 ? mTextCharH : mTextCharH*2);

		if (!lineRedrawFlags || lineRedrawFlags[line]) {
			int N = (mode == 2 ? 40 : 20);

			if (mode == 2) {
				// Mode 2: 40-column text, fore/back with inverse
				for(int i = 0; i < N; ++i) {
					uint8 c = data[i];

					c ^= kInternalToATASCIIXorTab[(c >> 5) & 3];

					if ((uint8)((c & 0x7f) - 0x20) >= 0x5f)
						c = (c & 0x80) + '.';

					bool invert = (c & 0x80) != 0;
					uint8 ch = c & 0x7f;

					// Render char at position
					int px = charWidth * i;
					const uint8 *src = (const uint8 *)charSrc.data + charSrc.pitch * ch * mTextCharH;
					uint32 *dst = (uint32 *)((char *)mFrameBuffer.data + mFrameBuffer.pitch * py) + px;
					const uint8 invertMask = invert ? 0xFF : 0;

					for(int row = 0; row < charHeight && (py + row) < mBitmapHeight; ++row) {
						for(int col = 0; col < charWidth && (px + col) < mBitmapWidth; ++col)
							dst[col] = mGammaTable[src[col] ^ invertMask];

						src += charSrc.pitch;
						dst = (uint32 *)((char *)dst + mFrameBuffer.pitch);
					}
				}

				// Clear right margin
				int textEnd = charWidth * N;
				if (textEnd < mBitmapWidth)
					FillRect(textEnd, py, mBitmapWidth - textEnd, charHeight, colorBorder);
			} else {
				// Modes 6/7: 20-column multicolor, 2x wide (7 also 2x tall)
				// Fill entire line with border first
				FillRect(0, py, mBitmapWidth, charHeight, colorBorder);

				for(int i = 0; i < N; ++i) {
					uint8 c = data[i];
					uint8 ch = c & 0x3f;

					if (chbase & 0x02)
						ch |= 0x40;

					ch ^= kInternalToATASCIIXorTab[(ch >> 5) & 3];

					if ((uint8)((ch & 0x7f) - 0x20) >= 0x5f)
						ch = (ch & 0x80) + '.';

					ch &= 0x7f;

					uint32 pfColor = VDSwizzleU32(mTextLastPFColors[c >> 6]) >> 8;

					int px = charWidth * i;
					const uint8 *src = (const uint8 *)charSrc.data + charSrc.pitch * ch * mTextCharH;
					uint32 *dst = (uint32 *)((char *)mFrameBuffer.data + mFrameBuffer.pitch * py) + px;

					for(int row = 0; row < mTextCharH && (py + row * (mode == 7 ? 2 : 1)) < mBitmapHeight; ++row) {
						// For mode 7, each row is doubled vertically
						int rowRepeat = (mode == 7) ? 2 : 1;
						for(int rep = 0; rep < rowRepeat; ++rep) {
							uint32 *rowDst = dst;
							for(int col = 0; col < mTextCharW && (px + col * 2 + 1) < mBitmapWidth; ++col) {
								uint32 pixel = src[col] > 127 ? pfColor : colorBorder;
								rowDst[col * 2] = pixel;
								rowDst[col * 2 + 1] = pixel;
							}
							dst = (uint32 *)((char *)dst + mFrameBuffer.pitch);
						}

						src += charSrc.pitch;
					}
				}
			}
		}

		mActiveLineYStart[line] = py;
		py += charHeight;
	}

	mActiveLineYStart[mTextLastLineCount] = py;

	if (mTextLastTotalHeight != py || !lineRedrawFlags) {
		mTextLastTotalHeight = py;
		++mVideoInfo.mFrameBufferLayoutChangeCount;
		mVideoInfo.mTextRows = mTextLastLineCount;
		mVideoInfo.mDisplayArea.bottom = std::max<sint32>(py, mTextCharH * 24);
		mFrameBuffer.h = std::min<sint32>(mVideoInfo.mDisplayArea.bottom, mBitmapHeight);

		// Clear below text
		if (py < mBitmapHeight)
			FillRect(0, py, mBitmapWidth, mBitmapHeight - py, colorBorder);
	}
}

void ATUIEnhancedTextEngine::PaintSWMode(const bool *lineRedrawFlags) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();
	uint32 w;
	uint32 h;
	const uint8 *screen;

	vs->GetScreen(w, h, screen);

	const uint32 colorBorder = VDSwizzleU32(mTextLastBorderColor) >> 8;
	const int charWidth = mTextCharW;
	const int charHeight = mTextCharH;
	const uint8 invertMask = mbInverseEnabled ? 0xFF : 0x7F;

	const VDPixmap& charSrc = mbUsingCustomFont ? mRescaledCustomFont : mRescaledDefaultFont;

	uint32 cursorX, cursorY;
	if (!vs->GetCursorInfo(cursorX, cursorY)) {
		cursorX = (uint32)0-1;
		cursorY = (uint32)0-1;
	}

	uint8 linedata[255];

	int py = 0;
	for(uint32 y = 0; y < h; ++y, (py += charHeight), (screen += w)) {
		if (lineRedrawFlags && !lineRedrawFlags[y])
			continue;

		for(uint32 x = 0; x < w; ++x) {
			uint8 c = screen[x];

			linedata[x] = c & invertMask;
		}

		if (cursorY == y) {
			uint32 limit = cursorX + (uint32)mInputBuffer.size();

			if (limit > w)
				limit = w;

			for(uint32 x = cursorX; x < limit; ++x)
				linedata[x] = (uint8)mInputBuffer[x - cursorX];

			uint32 cx = cursorX + mInputPos;

			if (cx < w)
				linedata[cx] ^= 0x80;
		}

		for(uint32 x = 0; x < w; ++x) {
			uint8 c = linedata[x];

			bool invert = (c & 0x80) != 0;
			uint8 ch = c & 0x7F;

			int px = charWidth * x;
			const uint8 *src = (const uint8 *)charSrc.data + charSrc.pitch * ch * charHeight;
			uint32 *dst = (uint32 *)((char *)mFrameBuffer.data + mFrameBuffer.pitch * py) + px;
			const uint8 invMask = invert ? 0xFF : 0;

			for(int row = 0; row < charHeight && (py + row) < mBitmapHeight; ++row) {
				for(int col = 0; col < charWidth && (px + col) < mBitmapWidth; ++col)
					dst[col] = mGammaTable[src[col] ^ invMask];

				src += charSrc.pitch;
				dst = (uint32 *)((char *)dst + mFrameBuffer.pitch);
			}
		}
	}

	if (!lineRedrawFlags) {
		// Clear right margin
		int textEnd = charWidth * w;
		if (textEnd < mBitmapWidth && py > 0)
			FillRect(textEnd, 0, mBitmapWidth - textEnd, py, colorBorder);

		// Clear below text
		if (py < mBitmapHeight)
			FillRect(0, py, mBitmapWidth, mBitmapHeight - py, colorBorder);
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATUIEnhancedTextEngine::AddToHistory(const char *s) {
	size_t len = strlen(s);
	size_t hsize = mHistoryBuffer.size();

	if (hsize + len > 4096) {
		if (len >= 4096) {
			mHistoryBuffer.clear();
		} else {
			size_t hreduce = hsize + len - 4096;
			size_t cutidx = 0;

			while(cutidx < hreduce) {
				while(mHistoryBuffer[cutidx++])
					;
			}

			mHistoryBuffer.erase(mHistoryBuffer.begin(), mHistoryBuffer.begin() + cutidx);
		}
	}

	mHistoryBuffer.insert(mHistoryBuffer.end(), s, s + len + 1);
}

void ATUIEnhancedTextEngine::OnInputReady() {
	ProcessPastedInput();
}

void ATUIEnhancedTextEngine::OnVirtualScreenResized() {
	// Reinitialize bitmap
	sint32 w = mViewportWidth;
	sint32 h = mViewportHeight;

	mViewportWidth = 0;
	mViewportHeight = 0;
	OnSize(w, h);
}

void ATUIEnhancedTextEngine::OnPassThroughChanged() {
	auto *vs = mpSim->GetVirtualScreenHandler();

	mVideoInfo.mbSignalPassThrough = !vs || vs->IsPassThroughEnabled();
}

void ATUIEnhancedTextEngine::ProcessPastedInput() {
	auto *vs = mpSim->GetVirtualScreenHandler();
	if (!vs || !vs->IsReadyForInput())
		return;

	while(!mPasteBuffer.empty()) {
		wchar_t c = mPasteBuffer.front();
		mPasteBuffer.pop_front();

		if (c == '\n') {
			OnKeyDown(kATUIVK_Return);
			break;
		} else if (c >= 0x20 && c < 0x7F)
			OnChar(c);
	}
}

void ATUIEnhancedTextEngine::ResetAttractTimeout() {
	mpSim->GetMemoryManager()->WriteByte(ATKernelSymbols::ATRACT, 0);
}

void ATUIEnhancedTextEngine::RebuildFontAtlas() {
	mRescaledDefaultFont.init(mTextCharW, mTextCharH * 128, nsVDPixmap::kPixFormat_Y8);
	memset(mRescaledDefaultFont.base(), 0, mRescaledDefaultFont.size());

	mRescaledCustomFont.init(mTextCharW, mTextCharH * 128, nsVDPixmap::kPixFormat_Y8);
	memset(mRescaledCustomFont.base(), 0, mRescaledCustomFont.size());
	memset(mCurrentCustomFont, 0, sizeof mCurrentCustomFont);

	CharResampler charResampler(mTextCharW, mTextCharH);
	for(int i=0; i<128; ++i)
		charResampler.ResampleChar(mRescaledDefaultFont, i, &mRawDefaultFont[8 * i]);
}

///////////////////////////////////////////////////////////////////////////

IATUIEnhancedTextEngine *ATUICreateEnhancedTextEngine() {
	return new ATUIEnhancedTextEngine;
}
