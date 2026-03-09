//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>

#include <wx/frame.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/slider.h>
#include <wx/panel.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/msgdlg.h>
#include <wx/menu.h>
#include <wx/toolbar.h>
#include <wx/statusbr.h>
#include <wx/scrolbar.h>
#include <wx/filedlg.h>
#include <wx/graphics.h>
#include <wx/rawbmp.h>
#include <wx/textdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcmemory.h>

#include <cmath>
#include <algorithm>
#include <vector>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/refcount.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/VDString.h>
#include <at/atio/cassetteimage.h>
#include <at/atcore/fft.h>

#include "simulator.h"
#include "cassette.h"

#include "dialogs_wx.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////
// Constants
///////////////////////////////////////////////////////////////////////////

static constexpr int kZoomMin = -20;
static constexpr int kZoomMax = 16;
static constexpr uint32 kUndoLimit = 50;
static constexpr float kSamplingRate = (float)kATCassetteDataSampleRateD;
static constexpr float kBinWidth = kSamplingRate / 128.0f;
static constexpr float kSpaceTone = kSamplingRate / 8.0f;
static constexpr float kMarkTone = kSamplingRate / 6.0f;
static constexpr float kSpecGain = 18.0f;

static constexpr uint32 kRegionColors[] = {
	0x1D1D1D,	// blank
	0x000060,	// mark/standard
	0x003A00,	// decoded/raw
};

///////////////////////////////////////////////////////////////////////////
// Enums and Structs
///////////////////////////////////////////////////////////////////////////

enum class DrawMode : uint8 {
	Scroll,
	Select,
	Draw,
	Insert,
	Analyze
};

enum class Decoder : uint8 {
	FSK_Sync,
	FSK_PLL,
	T2000
};

enum class FilterMode : uint8 {
	FSKDirectSample2000Baud,
	FSKDirectSample1000Baud
};

enum class WaveformMode : uint8 {
	None,
	Waveform,
	Spectrogram
};

enum class UndoSelectionMode : uint8 {
	None,
	SelectionIsRange,
	SelectionToEnd,
	EndToSelection
};

enum class DecodedByteFlags : uint8 {
	None = 0x00,
	FramingError = 0x01
};

inline DecodedByteFlags operator&(DecodedByteFlags a, DecodedByteFlags b) {
	return static_cast<DecodedByteFlags>(static_cast<uint8>(a) & static_cast<uint8>(b));
}

inline DecodedByteFlags operator|(DecodedByteFlags a, DecodedByteFlags b) {
	return static_cast<DecodedByteFlags>(static_cast<uint8>(a) | static_cast<uint8>(b));
}

struct UndoEntry {
	uint32 mStart = 0;
	uint32 mLength = 0;
	vdrefptr<IATTapeImageClip> mpData;
	UndoSelectionMode mSelectionMode {};
};

struct DecodedBlock {
	uint32 mSampleStart {};
	uint32 mSampleEnd {};
	uint32 mSampleValidEnd {};
	float mBaudRate {};
	uint32 mStartByte {};
	uint32 mByteCount {};
	uint32 mChecksumPos {};
	bool mbValidFrame {};
	uint8 mSuspiciousBit {};
	bool mbSuspiciousBitPolarity {};
};

struct DecodedByte {
	uint32 mStartSample {};
	uint16 mBitSampleOffsets[10] {};
	uint8 mData {};
	DecodedByteFlags mFlags {};
};

struct DecodedByteStartPred {
	bool operator()(const DecodedByte& dbyte, uint32 start) const {
		return dbyte.mStartSample < start;
	}
	bool operator()(uint32 start, const DecodedByte& dbyte) const {
		return start < dbyte.mStartSample;
	}
};

struct DecodedBlocks {
	vdfastvector<DecodedBlock> mBlocks;
	vdfastvector<DecodedByte> mByteData;
	void Clear() { mBlocks.clear(); mByteData.clear(); }
};

struct AnalysisChannel {
	DecodedBlocks mDecodedBlocks;
	uint32 mSampleStart = 0;
	uint32 mSampleEnd = 0;
};

struct SpecSample {
	uint32 mBinOffset;
	float mCoeffs[4];
};

///////////////////////////////////////////////////////////////////////////
// Forward declarations
///////////////////////////////////////////////////////////////////////////

class ATTapeViewPanel;
class ATTapeEditorFrame;

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — main canvas for tape waveform/spectrogram/editing
///////////////////////////////////////////////////////////////////////////

class ATTapeViewPanel : public wxPanel {
public:
	ATTapeViewPanel(wxWindow *parent);
	~ATTapeViewPanel();

	void SetCassetteEmulator(ATCassetteEmulator *emu);
	IATCassetteImage *GetImage() const { return mpImage; }
	void SetImage(IATCassetteImage *image);
	void LockViewReset();
	void UnlockViewReset();
	void OnTapeModified();

	DrawMode GetDrawMode() const { return mDrawMode; }
	void SetDrawMode(DrawMode mode);

	Decoder GetAnalysisDecoder() const { return mAnalysisDecoder; }
	void SetAnalysisDecoder(Decoder d) { mAnalysisDecoder = d; }

	WaveformMode GetWaveformMode() const { return mWaveformMode; }
	void SetWaveformMode(WaveformMode mode);

	bool GetFrequencyGuidelinesEnabled() const { return mbShowFrequencyGuidelines; }
	void SetFrequencyGuidelinesEnabled(bool e) { mbShowFrequencyGuidelines = e; Refresh(false); }

	bool GetShowTurboData() const { return mbShowTurboData; }
	void SetShowTurboData(bool e) { mbShowTurboData = e; Refresh(false); }

	bool GetStoreWaveformOnLoad() const { return mbStoreWaveformOnLoad; }
	void SetStoreWaveformOnLoad(bool e) { mbStoreWaveformOnLoad = e; }

	bool GetSIOMonitorEnabled() const { return mbSIOMonitorEnabled; }
	void SetSIOMonitorEnabled(bool e);

	bool HasSelection() const { return mbSelectionValid; }
	bool HasNonEmptySelection() const { return mbSelectionValid && mSelSortedStartSample != mSelSortedEndSample; }
	uint32 GetSelectionSortedStart() const { return mSelSortedStartSample; }
	uint32 GetSelectionSortedEnd() const { return mSelSortedEndSample; }
	void ClearSelection();
	void SetSelection(uint32 start, uint32 end);
	void SelectAll();
	void EnsureSelectionVisible();
	void EnsureRangeVisible(uint32 startSample, uint32 endSample);

	void Insert();
	void Delete();
	void Cut();
	void Copy();
	void Paste();
	bool HasClip() const { return mpImageClip != nullptr; }
	void ConvertToStdBlock();
	void ConvertToRawBlock();
	void ExtractSelectionAsCFile(vdfastvector<uint8>& data) const;

	bool HasDecodedData() const;
	void CopyDecodedData() const;

	void ReAnalyze();
	void ReAnalyzeFlip();
	void Filter(FilterMode filterMode);

	bool CanUndo() const { return !mUndoQueue.empty(); }
	bool CanRedo() const { return !mRedoQueue.empty(); }
	void Undo();
	void Redo();
	void ClearUndoRedo();

	void UpdateHeadState();
	void UpdateHeadPosition();

	std::function<void()> mFnOnDrawModeChanged;
	std::function<void()> mFnOnSelectionChanged;

private:
	void OnPaint(wxPaintEvent& evt);
	void OnSize(wxSizeEvent& evt);
	void OnMouseMove(wxMouseEvent& evt);
	void OnMouseLeftDown(wxMouseEvent& evt);
	void OnMouseLeftUp(wxMouseEvent& evt);
	void OnMouseRightDown(wxMouseEvent& evt);
	void OnMouseRightUp(wxMouseEvent& evt);
	void OnMouseWheel(wxMouseEvent& evt);
	void OnMouseLeave(wxMouseEvent& evt);
	void OnScrollBar(wxScrollEvent& evt);
	void OnKeyDown(wxKeyEvent& evt);

	// Coordinate conversions
	uint32 ClientXToSampleEdge(int x, bool clampToLength) const;
	uint32 ClientXToSample(int x) const;
	sint64 SampleEdgeToClientXFloorRaw(uint32 sample) const;
	sint64 SampleEdgeToClientXCeilRaw(uint32 sample) const;
	int SampleEdgeToClientXFloor(uint32 sample) const;
	int SampleEdgeToClientXCeil(uint32 sample) const;
	sint64 SampleToGlobalX(uint32 sample) const;
	sint64 SampleToClientXRaw(uint32 sample) const;

	void SetScrollX(sint64 x);
	void ScrollDeltaX(sint64 dx);
	void SetZoom(int newZoom, int centerClientX);

	uint32 PreModify();
	void PostModify(uint32 newPos);

	void PushUndo(uint32 start, uint32 len, uint32 newLen, UndoSelectionMode selMode);
	void ExecuteUndoRedo(UndoEntry& ue);

	void UpdateScrollLimit();
	void UpdateHorizScroll();
	void UpdatePalettes();
	void UpdateDivisionSpacing();

	void PaintSpectrogram(wxDC& dc, uint32 pos, uint32 posinc, int x, int xinc, int n, int ywfhi, int ywflo);
	void PaintAnalysisChannel(wxDC& dc, const AnalysisChannel& ch, uint32 posStart, uint32 posEnd, int x1, int x2, int y);

	void Analyze(uint32 start, uint32 end);
	void OnByteDecoded(uint32 startPos, uint32 endPos, uint8 data, bool framingError, uint32 cyclesPerHalfBit);

	void DecodeFSK(uint32 start, uint32 end, bool stopOnFramingError, DecodedBlocks& output) const;
	void DecodeFSK2(uint32 start, uint32 end, bool stopOnFramingError, DecodedBlocks& output) const;
	void DecodeT2000(uint32 start, uint32 end, DecodedBlocks& output) const;
	static bool TryIdentifySuspiciousBit(const DecodedBlocks& dblocks, DecodedBlock& dblock, uint32 forcedSyncBytes, uint32 checksumPos, uint8 receivedSum);

	void SortSelection();

	// State
	vdrefptr<IATCassetteImage> mpImage;
	vdrefptr<IATTapeImageClip> mpImageClip;
	ATCassetteEmulator *mpCasEmu = nullptr;

	sint64 mScrollX = 0;
	sint64 mScrollMax = 0;
	int mWidth = 0;
	int mHeight = 0;
	int mCenterX = 0;
	int mScrollShift = 0;
	int mPaletteShift = 0;
	int mZoom = 0;
	float mZoomAccum = 0;
	uint32 mSampleCount = 0;

	DrawMode mDrawMode = DrawMode::Scroll;
	DrawMode mActiveDragMode = DrawMode::Scroll;
	Decoder mAnalysisDecoder = Decoder::FSK_Sync;

	bool mbDragging = false;
	int mDragOriginX = 0;

	WaveformMode mWaveformMode = WaveformMode::Waveform;
	bool mbShowFrequencyGuidelines = false;
	bool mbStoreWaveformOnLoad = false;
	bool mbShowTurboData = false;

	bool mbSelectionValid = false;
	uint32 mSelStartSample = 0;
	uint32 mSelEndSample = 0;
	uint32 mSelSortedStartSample = 0;
	uint32 mSelSortedEndSample = 0;

	bool mbDrawValid = false;
	bool mbDrawPolarity = false;
	uint32 mDrawStartSample = 0;
	uint32 mDrawEndSample = 0;

	uint32 mHeadPosition = 0;
	bool mbHeadPlay = false;
	bool mbHeadRecord = false;

	bool mbSIOMonitorEnabled = false;
	uint8 mSIOMonChecksum = 0;
	uint32 mSIOMonFramingErrors = 0;
	static constexpr uint32 kInvalidChecksumPos = ~UINT32_C(0);
	uint32 mSIOMonChecksumPos = kInvalidChecksumPos;

	double mCurrentPixelsPerTimeMarker = 100;
	bool mbTimeMarkerShowMS = false;

	uint32 mTapeChangedLock = 0;
	uint32 mViewResetLock = 0;

	vdvector<UndoEntry> mUndoQueue;
	vdvector<UndoEntry> mRedoQueue;

	AnalysisChannel mAnalysisChannels[2];

	uint32 mPalette[257] {};
	float mFFTWindow[128] {};
	uint32 mSpectrogramPalette[256] {};
	vdfastvector<SpecSample> mSpectrogramInterp;
	std::unique_ptr<ATFFT<128>> mpFFT;

	wxScrollBar *mpScrollBar = nullptr;

	vdfunction<void()> mFnOnPositionChanged;
	vdfunction<void()> mFnOnPlayStateChanged;
	vdfunction<void(uint32, uint32, uint8, bool, uint32)> mFnByteDecoded;
};

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — constructor / destructor
///////////////////////////////////////////////////////////////////////////

static float BesselI0Approx(float x) {
	float x2 = x*x;
	float sum = 1.0f;
	float term = x2 / 4.0f;

	for(int i=1; i<5; ++i) {
		sum += term;
		term *= x2 * 0.25f / ((float)i * (float)i);
	}

	return sum;
}

static uint8 LinearToSRGB(float v) {
	v = std::clamp(v, 0.0f, 1.0f);
	if (v <= 0.0031308f)
		return (uint8)std::clamp((int)(v * 12.92f * 255.0f + 0.5f), 0, 255);
	return (uint8)std::clamp((int)((1.055f * powf(v, 1.0f / 2.4f) - 0.055f) * 255.0f + 0.5f), 0, 255);
}

ATTapeViewPanel::ATTapeViewPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	// Create scrollbar
	mpScrollBar = new wxScrollBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSB_HORIZONTAL);

	// Init FFT
	mpFFT = std::make_unique<ATFFT<128>>(false);

	// Init Kaiser window (alpha=4)
	{
		float denom = BesselI0Approx(3.14159265f * 4.0f);
		for (int i = 0; i < 128; ++i) {
			float t = (float)i / 64.0f;
			float arg = 3.14159265f * 4.0f * sqrtf(std::max(0.0f, (2.0f - t) * t));
			mFFTWindow[i] = BesselI0Approx(arg) * 0.25f * 127.0f / denom;
		}
	}

	// Init spectrogram palette (Oklch-inspired)
	for (int i = 0; i < 256; ++i) {
		float x = (float)i / 255.0f;
		float L = x;
		float C = 0.5f * x * (1.0f - x);
		float H = 6.28318530f * (x + 0.5f);
		float a = C * cosf(H);
		float b = C * sinf(H);

		// Oklab -> linear sRGB (approximate)
		float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
		float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
		float s_ = L - 0.0894841775f * a - 1.2914855480f * b;
		float l3 = l_ * l_ * l_;
		float m3 = m_ * m_ * m_;
		float s3 = s_ * s_ * s_;
		float r = +4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
		float g = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
		float bv = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;

		uint8 sr = LinearToSRGB(r);
		uint8 sg = LinearToSRGB(g);
		uint8 sb = LinearToSRGB(bv);
		mSpectrogramPalette[i] = (0xFF << 24) | (sr << 16) | (sg << 8) | sb;
	}

	// Bind events
	Bind(wxEVT_PAINT, &ATTapeViewPanel::OnPaint, this);
	Bind(wxEVT_SIZE, &ATTapeViewPanel::OnSize, this);
	Bind(wxEVT_MOTION, &ATTapeViewPanel::OnMouseMove, this);
	Bind(wxEVT_LEFT_DOWN, &ATTapeViewPanel::OnMouseLeftDown, this);
	Bind(wxEVT_LEFT_UP, &ATTapeViewPanel::OnMouseLeftUp, this);
	Bind(wxEVT_RIGHT_DOWN, &ATTapeViewPanel::OnMouseRightDown, this);
	Bind(wxEVT_RIGHT_UP, &ATTapeViewPanel::OnMouseRightUp, this);
	Bind(wxEVT_MOUSEWHEEL, &ATTapeViewPanel::OnMouseWheel, this);
	Bind(wxEVT_LEAVE_WINDOW, &ATTapeViewPanel::OnMouseLeave, this);
	Bind(wxEVT_KEY_DOWN, &ATTapeViewPanel::OnKeyDown, this);
	mpScrollBar->Bind(wxEVT_SCROLL_THUMBTRACK, &ATTapeViewPanel::OnScrollBar, this);
	mpScrollBar->Bind(wxEVT_SCROLL_CHANGED, &ATTapeViewPanel::OnScrollBar, this);
	mpScrollBar->Bind(wxEVT_SCROLL_LINEUP, &ATTapeViewPanel::OnScrollBar, this);
	mpScrollBar->Bind(wxEVT_SCROLL_LINEDOWN, &ATTapeViewPanel::OnScrollBar, this);
	mpScrollBar->Bind(wxEVT_SCROLL_PAGEUP, &ATTapeViewPanel::OnScrollBar, this);
	mpScrollBar->Bind(wxEVT_SCROLL_PAGEDOWN, &ATTapeViewPanel::OnScrollBar, this);
}

ATTapeViewPanel::~ATTapeViewPanel() {
	SetCassetteEmulator(nullptr);
}

void ATTapeViewPanel::SetCassetteEmulator(ATCassetteEmulator *emu) {
	if (mpCasEmu == emu)
		return;

	if (mpCasEmu) {
		mpCasEmu->PositionChanged -= &mFnOnPositionChanged;
		mpCasEmu->PlayStateChanged -= &mFnOnPlayStateChanged;
		if (mFnByteDecoded)
			mpCasEmu->ByteDecoded.Remove(&mFnByteDecoded);
	}

	mpCasEmu = emu;

	if (mpCasEmu) {
		mFnOnPositionChanged = [this]() {
			UpdateHeadPosition();
			Refresh(false);
		};
		mpCasEmu->PositionChanged += &mFnOnPositionChanged;

		mFnOnPlayStateChanged = [this]() {
			UpdateHeadState();
			Refresh(false);
		};
		mpCasEmu->PlayStateChanged += &mFnOnPlayStateChanged;

		SetImage(mpCasEmu->GetImage());
	}
}

void ATTapeViewPanel::SetImage(IATCassetteImage *image) {
	if (mpImage != image) {
		mpImage = image;

		OnTapeModified();

		if (!mViewResetLock) {
			SetZoom(-12, 0);
			SetScrollX(mSampleCount >> (12 + 1));
		}
	}
}

void ATTapeViewPanel::LockViewReset() {
	++mViewResetLock;
}

void ATTapeViewPanel::UnlockViewReset() {
	--mViewResetLock;
}

void ATTapeViewPanel::OnTapeModified() {
	if (!mTapeChangedLock) {
		ClearUndoRedo();

		if (mpImage) {
			mSampleCount = mpImage->GetDataLength();
		} else {
			mSampleCount = 0;
		}

		UpdateScrollLimit();
		UpdateHorizScroll();
		UpdatePalettes();
		Refresh(false);
	}
}

void ATTapeViewPanel::SetDrawMode(DrawMode mode) {
	if (mDrawMode != mode) {
		mDrawMode = mode;
		if (mFnOnDrawModeChanged)
			mFnOnDrawModeChanged();
		Refresh(false);
	}
}

void ATTapeViewPanel::SetWaveformMode(WaveformMode mode) {
	if (mWaveformMode != mode) {
		mWaveformMode = mode;
		Refresh(false);
	}
}

void ATTapeViewPanel::SetSIOMonitorEnabled(bool e) {
	if (mbSIOMonitorEnabled != e) {
		mbSIOMonitorEnabled = e;
		if (mpCasEmu) {
			if (e) {
				mFnByteDecoded = [this](uint32 sp, uint32 ep, uint8 d, bool fe, uint32 c) {
					OnByteDecoded(sp, ep, d, fe, c);
				};
				mpCasEmu->ByteDecoded.Add(&mFnByteDecoded);
			} else {
				mpCasEmu->ByteDecoded.Remove(&mFnByteDecoded);
				mFnByteDecoded = {};
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — coordinate conversions
///////////////////////////////////////////////////////////////////////////

uint32 ATTapeViewPanel::ClientXToSampleEdge(int x, bool clampToLength) const {
	sint64 gpx = mScrollX + (sint64)x - mCenterX;
	if (mZoom <= 0)
		gpx <<= -mZoom;
	else
		gpx = ((gpx >> (mZoom - 1)) + 1) >> 1;

	uint32 limit = clampToLength ? mSampleCount : kATCassetteDataLimit;
	if (gpx < 0) return 0;
	if ((uint64)gpx > limit) return limit;
	return (uint32)gpx;
}

uint32 ATTapeViewPanel::ClientXToSample(int x) const {
	sint64 gpx = mScrollX + (sint64)x - mCenterX;
	if (mZoom <= 0)
		gpx <<= -mZoom;
	else
		gpx >>= mZoom;

	if (gpx < 0) return 0;
	if ((uint64)gpx > mSampleCount) return mSampleCount;
	return (uint32)gpx;
}

sint64 ATTapeViewPanel::SampleEdgeToClientXFloorRaw(uint32 sample) const {
	sint64 gpx = (sint64)sample;
	if (mZoom <= 0)
		gpx >>= -mZoom;
	else
		gpx <<= mZoom;
	return gpx + mCenterX - mScrollX;
}

sint64 ATTapeViewPanel::SampleEdgeToClientXCeilRaw(uint32 sample) const {
	sint64 gpx = (sint64)sample;
	if (mZoom <= 0)
		gpx = -(-(sint64)sample >> -mZoom);
	else
		gpx <<= mZoom;
	return gpx + mCenterX - mScrollX;
}

int ATTapeViewPanel::SampleEdgeToClientXFloor(uint32 sample) const {
	sint64 v = SampleEdgeToClientXFloorRaw(sample);
	return (int)std::clamp<sint64>(v, -32768, 32767);
}

int ATTapeViewPanel::SampleEdgeToClientXCeil(uint32 sample) const {
	sint64 v = SampleEdgeToClientXCeilRaw(sample);
	return (int)std::clamp<sint64>(v, -32768, 32767);
}

sint64 ATTapeViewPanel::SampleToGlobalX(uint32 sample) const {
	sint64 gpx = (sint64)sample;
	if (mZoom < 0)
		gpx = ((gpx >> (-mZoom - 1)) + 1) >> 1;
	else
		gpx <<= mZoom;
	return gpx;
}

sint64 ATTapeViewPanel::SampleToClientXRaw(uint32 sample) const {
	return SampleToGlobalX(sample) + mCenterX - mScrollX;
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — zoom/scroll
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::SetScrollX(sint64 x) {
	if (x < 0) x = 0;
	if (x > mScrollMax) x = mScrollMax;
	if (mScrollX != x) {
		mScrollX = x;
		UpdateHorizScroll();
		Refresh(false);
	}
}

void ATTapeViewPanel::ScrollDeltaX(sint64 dx) {
	SetScrollX(mScrollX + dx);
}

void ATTapeViewPanel::SetZoom(int newZoom, int centerClientX) {
	newZoom = std::clamp(newZoom, kZoomMin, kZoomMax);
	if (newZoom == mZoom)
		return;

	int hw = mWidth / 2;
	sint64 xoff = (sint64)centerClientX - hw;
	int zoomChange = newZoom - mZoom;

	if (zoomChange < 0)
		mScrollX = ((mScrollX + xoff) >> -zoomChange) - xoff;
	else
		mScrollX = ((mScrollX + xoff) << zoomChange) - xoff;

	mZoom = newZoom;

	UpdateScrollLimit();
	if (mScrollX < 0) mScrollX = 0;
	if (mScrollX > mScrollMax) mScrollX = mScrollMax;
	UpdateHorizScroll();
	UpdatePalettes();
	UpdateDivisionSpacing();
	Refresh(false);
}

void ATTapeViewPanel::UpdateScrollLimit() {
	sint64 len = mSampleCount;
	if (mZoom < 0)
		len = ((len - 1) >> -mZoom) + 1;
	else
		len <<= mZoom;

	mScrollMax = len;
	mScrollShift = 0;
	sint64 maxPos64 = mScrollMax;
	while (maxPos64 > 0x1FFFFFFF) {
		++mScrollShift;
		maxPos64 >>= 1;
	}
}

void ATTapeViewPanel::UpdateHorizScroll() {
	if (!mpScrollBar)
		return;

	int thumbSize = std::max(1, (int)(mWidth >> mScrollShift));
	int range = std::max(1, (int)((mScrollMax >> mScrollShift) + thumbSize));
	int pos = (int)(mScrollX >> mScrollShift);
	mpScrollBar->SetScrollbar(pos, thumbSize, range, thumbSize);
}

void ATTapeViewPanel::UpdatePalettes() {
	if (mZoom >= 0)
		return;

	int palBits = std::min(8, -mZoom);
	int n = 1 << palBits;
	float xinc = (float)(1 << (8 - palBits)) / 256.0f * 2.0f;
	float x = 0;

	for (int i = 0; i <= n; ++i) {
		float v = 191.0f * powf(std::min(1.0f, x), 1.0f / 2.2f);
		int c = (int)(v + 0.5f);
		mPalette[i] = 0xFF404040 + 0x010101 * c;
		x += xinc;
	}

	mPaletteShift = -mZoom - palBits;
}

void ATTapeViewPanel::UpdateDivisionSpacing() {
	mCurrentPixelsPerTimeMarker = kATCassetteDataSampleRateD * pow(2.0, (double)mZoom) / 1000.0;

	static const double kSteps[] = {
		1, 2, 5, 10, 20, 50, 100, 200, 500,
		1000, 2000, 5000, 10000, 20000, 30000,
		60000, 120000, 300000, 600000, 1200000, 1800000
	};

	double bestStep = 1.0;
	for (double step : kSteps) {
		if (mCurrentPixelsPerTimeMarker * step >= 80.0) {
			bestStep = step;
			break;
		}
	}

	mCurrentPixelsPerTimeMarker *= bestStep;
	mbTimeMarkerShowMS = (bestStep < 1000.0);
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — selection, undo/redo, editing
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::SortSelection() {
	mSelSortedStartSample = std::min(mSelStartSample, mSelEndSample);
	mSelSortedEndSample = std::max(mSelStartSample, mSelEndSample);
}

void ATTapeViewPanel::ClearSelection() {
	if (mbSelectionValid) {
		mbSelectionValid = false;
		mSelStartSample = mSelEndSample = 0;
		mSelSortedStartSample = mSelSortedEndSample = 0;
		if (mFnOnSelectionChanged) mFnOnSelectionChanged();
		Refresh(false);
	}
}

void ATTapeViewPanel::SetSelection(uint32 start, uint32 end) {
	mbSelectionValid = true;
	mSelStartSample = start;
	mSelEndSample = end;
	SortSelection();
	if (mFnOnSelectionChanged) mFnOnSelectionChanged();
	Refresh(false);
}

void ATTapeViewPanel::SelectAll() {
	if (mpImage)
		SetSelection(0, mSampleCount);
}

void ATTapeViewPanel::EnsureSelectionVisible() {
	if (mbSelectionValid)
		EnsureRangeVisible(mSelSortedStartSample, mSelSortedEndSample);
}

void ATTapeViewPanel::EnsureRangeVisible(uint32 startSample, uint32 endSample) {
	sint64 x1 = SampleToClientXRaw(startSample);
	sint64 x2 = SampleToClientXRaw(endSample);

	if (x1 < 0)
		ScrollDeltaX(x1 - 20);
	else if (x2 > mWidth)
		ScrollDeltaX(x2 - mWidth + 20);
}

void ATTapeViewPanel::ClearUndoRedo() {
	mUndoQueue.clear();
	mRedoQueue.clear();
}

void ATTapeViewPanel::PushUndo(uint32 start, uint32 len, uint32 newLen, UndoSelectionMode selMode) {
	UndoEntry ue;
	ue.mStart = start;
	ue.mLength = newLen;
	ue.mSelectionMode = selMode;

	if (len > 0 && mpImage)
		ue.mpData = mpImage->CopyRange(start, len);

	mRedoQueue.clear();
	mUndoQueue.push_back(std::move(ue));

	if (mUndoQueue.size() > kUndoLimit)
		mUndoQueue.erase(mUndoQueue.begin());
}

void ATTapeViewPanel::ExecuteUndoRedo(UndoEntry& ue) {
	if (!mpImage)
		return;

	uint32 newPos = PreModify();

	// Save current state for redo
	vdrefptr<IATTapeImageClip> oldClip;
	if (ue.mLength > 0)
		oldClip = mpImage->CopyRange(ue.mStart, ue.mLength);

	// Delete current range
	if (ue.mLength > 0)
		mpImage->DeleteRange(ue.mStart, ue.mStart + ue.mLength);

	// Insert old data
	uint32 oldLen = 0;
	if (ue.mpData) {
		oldLen = ue.mpData->GetLength();
		mpImage->InsertRange(ue.mStart, *ue.mpData);
	}

	// Update undo entry to reflect new state
	ue.mpData = std::move(oldClip);
	ue.mLength = oldLen;

	// Update selection
	switch (ue.mSelectionMode) {
	case UndoSelectionMode::SelectionIsRange:
		SetSelection(ue.mStart, ue.mStart + oldLen);
		break;
	case UndoSelectionMode::SelectionToEnd:
		SetSelection(ue.mStart + oldLen, ue.mStart + oldLen);
		break;
	case UndoSelectionMode::EndToSelection:
		SetSelection(ue.mStart, ue.mStart);
		break;
	default:
		break;
	}

	PostModify(newPos);
	Refresh(false);
}

void ATTapeViewPanel::Undo() {
	if (mUndoQueue.empty())
		return;

	UndoEntry ue = std::move(mUndoQueue.back());
	mUndoQueue.pop_back();
	ExecuteUndoRedo(ue);
	mRedoQueue.push_back(std::move(ue));
}

void ATTapeViewPanel::Redo() {
	if (mRedoQueue.empty())
		return;

	UndoEntry ue = std::move(mRedoQueue.back());
	mRedoQueue.pop_back();
	ExecuteUndoRedo(ue);
	mUndoQueue.push_back(std::move(ue));
}

uint32 ATTapeViewPanel::PreModify() {
	++mTapeChangedLock;
	return mpCasEmu ? mpCasEmu->OnPreModifyTape() : 0;
}

void ATTapeViewPanel::PostModify(uint32 newPos) {
	if (mpCasEmu)
		mpCasEmu->OnPostModifyTape(newPos);
	if (mpImage)
		mSampleCount = mpImage->GetDataLength();
	--mTapeChangedLock;
	UpdateScrollLimit();
	UpdateHorizScroll();
}

void ATTapeViewPanel::Insert() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample) {
		if (mpImage) {
			uint32 deckPos = PreModify();

			PushUndo(mSelSortedStartSample, 0, mSelSortedEndSample - mSelSortedStartSample, UndoSelectionMode::EndToSelection);

			ATCassetteWriteCursor cursor {};
			cursor.mPosition = mSelSortedStartSample;

			mpImage->WriteBlankData(cursor, mSelSortedEndSample - mSelSortedStartSample, true);

			if (deckPos >= mSelSortedStartSample)
				deckPos += cursor.mPosition - mSelSortedStartSample;

			PostModify(deckPos);
		}

		SetSelection(mSelSortedEndSample, mSelSortedEndSample);
		Refresh(false);
	}
}

void ATTapeViewPanel::Delete() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample && mpImage) {
		uint32 deckPos = PreModify();

		PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, 0, UndoSelectionMode::SelectionIsRange);

		mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

		PostModify(deckPos >= mSelSortedEndSample ? mSelSortedStartSample + (deckPos - mSelSortedEndSample)
			: deckPos >= mSelSortedStartSample ? mSelSortedStartSample
			: deckPos);

		SetSelection(mSelSortedStartSample, mSelSortedStartSample);
		Refresh(false);
	}
}

void ATTapeViewPanel::Cut() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample && mpImage) {
		mpImageClip = mpImage->CopyRange(mSelSortedStartSample, mSelSortedEndSample);

		uint32 deckPos = PreModify();

		PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, 0, UndoSelectionMode::SelectionIsRange);

		mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

		PostModify(deckPos >= mSelSortedEndSample ? mSelSortedStartSample + (deckPos - mSelSortedEndSample)
			: deckPos >= mSelSortedStartSample ? mSelSortedStartSample
			: deckPos);

		SetSelection(mSelSortedStartSample, mSelSortedStartSample);
		Refresh(false);
	}
}

void ATTapeViewPanel::Copy() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample && mpImage)
		mpImageClip = mpImage->CopyRange(mSelSortedStartSample, mSelSortedEndSample);
}

void ATTapeViewPanel::Paste() {
	if (mbSelectionValid && mpImage && mpImageClip) {
		uint32 deckPos = PreModify();

		PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, mpImageClip->GetLength(), UndoSelectionMode::SelectionIsRange);

		if (mSelSortedEndSample > mSelSortedStartSample) {
			mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

			if (deckPos >= mSelSortedEndSample)
				deckPos = mSelSortedStartSample + (deckPos - mSelSortedEndSample);
			else if (deckPos >= mSelSortedStartSample)
				deckPos = mSelSortedStartSample;

			SetSelection(mSelSortedStartSample, mSelSortedStartSample);
		}

		const uint32 newPos = mpImage->InsertRange(mSelSortedStartSample, *mpImageClip);

		if (deckPos > mSelSortedStartSample)
			deckPos = newPos + (deckPos - mSelSortedStartSample);

		PostModify(deckPos);

		SetSelection(newPos, newPos);
		Refresh(false);
	}
}

void ATTapeViewPanel::ConvertToStdBlock() {
	if (!mbSelectionValid || !mpImage)
		return;

	// capture range is 450-900 baud
	static constexpr uint32 kMinBitLen = (uint32)(kATCassetteDataSampleRate / 900.0f);
	static constexpr uint32 kMaxBitLen = (uint32)(kATCassetteDataSampleRate / 450.0f);

	const uint32 start = mSelSortedStartSample;
	const uint32 end = mSelSortedEndSample;
	if (start >= end)
		return;

	const uint32 deckPos = PreModify();
	PushUndo(start, end - start, end - start, UndoSelectionMode::SelectionIsRange);

	DecodedBlocks dblocks;
	DecodeFSK(start, end, true, dblocks);

	ATCassetteWriteCursor writeCursor;
	writeCursor.mPosition = start;

	for(const DecodedBlock& dblock : dblocks.mBlocks) {
		// blank tape to block start
		if (writeCursor.mPosition <= dblock.mSampleStart) {
			mpImage->WriteBlankData(writeCursor, dblock.mSampleStart - writeCursor.mPosition, false);
		}

		// write standard block
		for(uint32 i = 0; i < dblock.mByteCount; ++i)
			mpImage->WriteStdData(writeCursor, dblocks.mByteData[dblock.mStartByte + i].mData, dblock.mBaudRate, false);

		// if we stopped short of the original block, clear to that point
		if (writeCursor.mPosition < dblock.mSampleEnd)
			mpImage->WriteBlankData(writeCursor, dblock.mSampleEnd - writeCursor.mPosition, false);
	}

	PostModify(deckPos);

	Refresh(false);
}

void ATTapeViewPanel::ConvertToRawBlock() {
	vdfastvector<uint16> rleData;

	if (!mbSelectionValid || !mpImage)
		return;

	const uint32 start = mSelSortedStartSample;
	const uint32 end = std::min(mSelSortedEndSample, mSampleCount);
	if (start >= end)
		return;

	bool nextPolarity = false;
	for(uint32 pos = start; pos < end; ) {
		auto nextTransition = mpImage->FindNextBit(pos, end - 1, nextPolarity, mbShowTurboData);
		uint32 pulseEnd = std::min<uint32>(nextTransition.mPos, mSampleCount);
		uint32 pulseLen = pulseEnd - pos;

		if (pulseLen) {
			while(pulseLen > 0xFFFF) {
				rleData.push_back(0xFFFF);
				rleData.push_back(0);

				pulseLen -= 0xFFFF;
			}

			rleData.push_back(pulseLen);
		}

		nextPolarity = !nextPolarity;
		pos = pulseEnd;
	}

	const uint32 deckPos = PreModify();
	PushUndo(start, end - start, end - start, UndoSelectionMode::SelectionIsRange);

	ATCassetteWriteCursor cursor;
	cursor.mPosition = start;

	nextPolarity = true;
	for(uint16 pulseLen : rleData) {
		mpImage->WritePulse(cursor, nextPolarity, pulseLen, false, true);
		nextPolarity = !nextPolarity;
	}

	PostModify(deckPos);
	Refresh(false);
}

void ATTapeViewPanel::ExtractSelectionAsCFile(vdfastvector<uint8>& data) const {
	data.clear();

	if (!mbSelectionValid || !mpImage)
		return;

	DecodedBlocks dblocks;
	DecodeFSK(mSelSortedStartSample, mSelSortedEndSample, true, dblocks);

	if (dblocks.mBlocks.empty())
		return;

	int blockNo = 1;
	for(const DecodedBlock& dblock : dblocks.mBlocks) {
		const DecodedByte *byteInfo = dblocks.mByteData.data() + dblock.mStartByte;

		if (dblock.mByteCount >= 3) {
			if (byteInfo[0].mData != 0x55 || byteInfo[1].mData != 0x55)
				throw MyError("Sync error on block #%d", blockNo);
		}

		if (dblock.mByteCount < 132)
			throw MyError("Block #%d is too short", blockNo);

		if (std::find_if(byteInfo, byteInfo + dblock.mByteCount,
			[](const DecodedByte& info) {
				return (info.mFlags & DecodedByteFlags::FramingError) != DecodedByteFlags::None;
			}) != byteInfo + dblock.mByteCount)
			throw MyError("Block #%d has a framing error.", blockNo);

		uint32 blockLen = 128;
		if (byteInfo[2].mData == 0xFE)
			break;
		else if (byteInfo[2].mData == 0xFA) {
			blockLen = byteInfo[130].mData;

			if (blockLen >= 128)
				throw MyError("Block #%d has invalid length for a partial block.", blockNo);
		} else if (byteInfo[2].mData != 0xFC)
			throw MyError("Block #%d has unrecognized control byte.", blockNo);

		uint32 chksum32 = 0;
		for(int i=0; i<131; ++i)
			chksum32 += byteInfo[i].mData;

		uint8 chksum = chksum32 ? (chksum32 - 1) % 255 + 1 : 0;
		if (chksum != byteInfo[131].mData)
			throw MyError("Block #%d has a checksum error.", blockNo);

		data.resize(data.size() + blockLen);

		for(uint32 i=0; i<blockLen; ++i)
			(data.end() - blockLen)[i] = byteInfo[3 + i].mData;

		++blockNo;
	}
}

bool ATTapeViewPanel::HasDecodedData() const {
	for (auto& ch : mAnalysisChannels) {
		if (!ch.mDecodedBlocks.mByteData.empty())
			return true;
	}
	return false;
}

void ATTapeViewPanel::CopyDecodedData() const {
	VDStringA text;
	for (auto& ch : mAnalysisChannels) {
		for (auto& db : ch.mDecodedBlocks.mByteData) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02X ", db.mData);
			text += buf;
		}
	}

	if (!text.empty() && wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text.c_str())));
		wxTheClipboard->Close();
	}
}

void ATTapeViewPanel::UpdateHeadState() {
	if (!mpCasEmu) {
		mbHeadPlay = false;
		mbHeadRecord = false;
		return;
	}
	mbHeadPlay = mpCasEmu->IsPlayEnabled();
	mbHeadRecord = mpCasEmu->IsRecordEnabled();
}

void ATTapeViewPanel::UpdateHeadPosition() {
	if (mpCasEmu)
		mHeadPosition = mpCasEmu->GetSamplePos();
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — mouse handlers
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::OnMouseLeftDown(wxMouseEvent& evt) {
	SetFocus();
	CaptureMouse();
	mbDragging = true;
	mDragOriginX = evt.GetX();

	DrawMode mode = mDrawMode;
	if (evt.ControlDown())
		mode = DrawMode::Draw;

	mActiveDragMode = mode;

	switch (mode) {
	case DrawMode::Scroll:
		SetCursor(wxCursor(wxCURSOR_HAND));
		break;

	case DrawMode::Select:
		{
			uint32 sample = ClientXToSampleEdge(evt.GetX(), true);
			if (evt.ShiftDown() && mbSelectionValid) {
				mSelEndSample = sample;
				SortSelection();
			} else {
				mSelStartSample = sample;
				mSelEndSample = sample;
				mbSelectionValid = true;
				SortSelection();
			}
			if (mFnOnSelectionChanged) mFnOnSelectionChanged();
			Refresh(false);
		}
		break;

	case DrawMode::Draw:
		{
			ClearSelection();
			uint32 sample = ClientXToSampleEdge(evt.GetX(), true);
			int h = GetClientSize().GetHeight();
			mbDrawPolarity = evt.GetY() < h / 2;
			mbDrawValid = true;
			mDrawStartSample = sample;
			mDrawEndSample = sample;
			Refresh(false);
		}
		break;

	case DrawMode::Insert:
		{
			uint32 sample = ClientXToSampleEdge(evt.GetX(), true);
			mSelStartSample = sample;
			mSelEndSample = sample;
			mbSelectionValid = true;
			SortSelection();
			if (mFnOnSelectionChanged) mFnOnSelectionChanged();
			Refresh(false);
		}
		break;

	case DrawMode::Analyze:
		{
			uint32 sample = ClientXToSampleEdge(evt.GetX(), true);
			mSelStartSample = sample;
			mSelEndSample = sample;
			mbSelectionValid = true;
			SortSelection();
			if (mFnOnSelectionChanged) mFnOnSelectionChanged();
			Refresh(false);
		}
		break;
	}
}

void ATTapeViewPanel::OnMouseLeftUp(wxMouseEvent& evt) {
	if (!mbDragging)
		return;

	if (HasCapture())
		ReleaseMouse();
	mbDragging = false;

	switch (mActiveDragMode) {
	case DrawMode::Scroll:
		SetCursor(wxNullCursor);
		break;

	case DrawMode::Draw:
		if (mbDrawValid && mpImage) {
			uint32 start = std::min(mDrawStartSample, mDrawEndSample);
			uint32 end = std::max(mDrawStartSample, mDrawEndSample) + 1;
			if (end > mSampleCount) end = mSampleCount;
			if (start < end) {
				PushUndo(start, end - start, end - start, UndoSelectionMode::None);
				uint32 newPos = PreModify();
				ATCassetteWriteCursor cursor;
				cursor.mPosition = start;
				mpImage->WritePulse(cursor, mbDrawPolarity, end - start, false, true);
				PostModify(newPos);
			}
			mbDrawValid = false;
			Refresh(false);
		}
		break;

	case DrawMode::Insert:
		if (HasNonEmptySelection())
			Insert();
		break;

	case DrawMode::Analyze:
		if (HasNonEmptySelection()) {
			Analyze(mSelSortedStartSample, mSelSortedEndSample);
			ClearSelection();
		}
		break;

	default:
		break;
	}
}

void ATTapeViewPanel::OnMouseMove(wxMouseEvent& evt) {
	if (!mbDragging)
		return;

	switch (mActiveDragMode) {
	case DrawMode::Scroll:
		{
			sint64 dx = (sint64)(evt.GetX() - mDragOriginX);
			mDragOriginX = evt.GetX();
			ScrollDeltaX(-dx);
		}
		break;

	case DrawMode::Select:
	case DrawMode::Analyze:
		{
			uint32 sample = ClientXToSampleEdge(evt.GetX(), true);
			mSelEndSample = sample;
			SortSelection();
			if (mFnOnSelectionChanged) mFnOnSelectionChanged();
			Refresh(false);
		}
		break;

	case DrawMode::Draw:
		{
			uint32 sample = ClientXToSampleEdge(evt.GetX(), true);
			mDrawEndSample = sample;
			Refresh(false);
		}
		break;

	case DrawMode::Insert:
		{
			uint32 sample = ClientXToSampleEdge(evt.GetX(), true);
			if (sample < mSelStartSample)
				sample = mSelStartSample;
			mSelEndSample = sample;
			SortSelection();
			if (mFnOnSelectionChanged) mFnOnSelectionChanged();
			Refresh(false);
		}
		break;
	}
}

void ATTapeViewPanel::OnMouseRightDown(wxMouseEvent& evt) {
	SetFocus();
	if (!mbDragging) {
		CaptureMouse();
		mbDragging = true;
		mActiveDragMode = DrawMode::Scroll;
		mDragOriginX = evt.GetX();
		SetCursor(wxCursor(wxCURSOR_HAND));
	}
}

void ATTapeViewPanel::OnMouseRightUp(wxMouseEvent& evt) {
	if (mbDragging && mActiveDragMode == DrawMode::Scroll) {
		if (HasCapture())
			ReleaseMouse();
		mbDragging = false;
		SetCursor(wxNullCursor);
	}
}

void ATTapeViewPanel::OnMouseWheel(wxMouseEvent& evt) {
	float delta = (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta();
	mZoomAccum += delta;

	int steps = (int)mZoomAccum;
	if (steps != 0) {
		mZoomAccum -= (float)steps;
		SetZoom(mZoom + steps, evt.GetX());
	}
}

void ATTapeViewPanel::OnMouseLeave(wxMouseEvent&) {
	// nothing special needed
}

void ATTapeViewPanel::OnKeyDown(wxKeyEvent& evt) {
	int key = evt.GetKeyCode();
	switch (key) {
	case WXK_LEFT:
		ScrollDeltaX(-(sint64)(mWidth / 4));
		break;
	case WXK_RIGHT:
		ScrollDeltaX((sint64)(mWidth / 4));
		break;
	case WXK_HOME:
		SetScrollX(0);
		break;
	case WXK_END:
		SetScrollX(mScrollMax);
		break;
	case '+': case '=': case WXK_NUMPAD_ADD:
		SetZoom(mZoom + 1, mWidth / 2);
		break;
	case '-': case WXK_NUMPAD_SUBTRACT:
		SetZoom(mZoom - 1, mWidth / 2);
		break;
	default:
		evt.Skip();
		break;
	}
}

void ATTapeViewPanel::OnScrollBar(wxScrollEvent& evt) {
	sint64 pos = (sint64)mpScrollBar->GetThumbPosition() << mScrollShift;
	SetScrollX(pos);
}

void ATTapeViewPanel::OnSize(wxSizeEvent& evt) {
	wxSize sz = GetClientSize();
	mWidth = sz.GetWidth();
	mHeight = sz.GetHeight();
	mCenterX = mWidth / 2;

	// Position scrollbar at bottom
	int sbHeight = mpScrollBar->GetBestSize().GetHeight();
	mpScrollBar->SetSize(0, mHeight - sbHeight, mWidth, sbHeight);

	UpdateScrollLimit();
	UpdateHorizScroll();
	Refresh(false);
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — OnPaint
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	wxSize sz = GetClientSize();
	int w = sz.GetWidth();
	int sbHeight = mpScrollBar ? mpScrollBar->GetBestSize().GetHeight() : 0;
	int h = sz.GetHeight() - sbHeight;

	if (h <= 0 || w <= 0)
		return;

	dc.SetBackground(wxBrush(wxColour(0x1D, 0x1D, 0x1D)));
	dc.Clear();

	if (!mpImage) {
		dc.SetTextForeground(wxColour(128, 128, 128));
		dc.DrawText("No tape image loaded", 10, h / 2 - 8);
		return;
	}

	bool showWaveform = (mWaveformMode != WaveformMode::None);
	bool showSpectrogram = (mWaveformMode == WaveformMode::Spectrogram);

	// Layout
	int yhi, ylo, ywfhi, ywflo;
	if (showWaveform) {
		ywfhi = h / 8;
		ywflo = h * 3 / 8;
		yhi = h * 5 / 8;
		ylo = h * 7 / 8;
	} else {
		yhi = h / 4;
		ylo = h * 3 / 4;
		ywfhi = 0;
		ywflo = 0;
	}

	// Analysis layout
	int analysisY0 = ylo + 5;
	int analysisY1 = analysisY0 + 16;

	// Determine visible sample range
	uint32 posStart = ClientXToSample(0);
	uint32 posEnd = ClientXToSample(w);
	if (posEnd < mSampleCount) ++posEnd;

	// ---- Region coloring ----
	if (mSampleCount > 0) {
		uint32 regionPos = posStart;
		while (regionPos < posEnd) {
			ATCassetteRegionInfo ri = mpImage->GetRegionInfo(regionPos);
			uint32 regionEnd = std::min(ri.mRegionStart + ri.mRegionLen, posEnd);
			int rx1 = SampleEdgeToClientXFloor(regionPos);
			int rx2 = SampleEdgeToClientXCeil(regionEnd);
			if (rx1 < 0) rx1 = 0;
			if (rx2 > w) rx2 = w;

			uint32 color = kRegionColors[ri.mRegionType];
			dc.SetBrush(wxBrush(wxColour((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF)));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle(rx1, yhi, rx2 - rx1, ylo - yhi);

			regionPos = regionEnd;
			if (regionPos <= ri.mRegionStart)
				break;
		}
	}

	// ---- Division markers (time grid) ----
	if (mCurrentPixelsPerTimeMarker > 0) {
		double samplesPerMarker = mCurrentPixelsPerTimeMarker;
		if (mZoom < 0)
			samplesPerMarker *= (double)(1 << -mZoom);
		else
			samplesPerMarker /= (double)(1 << mZoom);

		if (samplesPerMarker > 0) {
			double markerStart = floor((double)posStart / samplesPerMarker) * samplesPerMarker;
			dc.SetPen(wxPen(wxColour(60, 60, 60), 1));
			dc.SetTextForeground(wxColour(100, 100, 100));
			wxFont smallFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
			dc.SetFont(smallFont);

			for (double s = markerStart; s < (double)posEnd; s += samplesPerMarker) {
				int px = (int)SampleToClientXRaw((uint32)s);
				if (px < 0 || px >= w)
					continue;

				dc.DrawLine(px, 0, px, h);

				float secs = (float)(s * kATCassetteSecondsPerDataSample);
				wxString label;
				if (mbTimeMarkerShowMS) {
					int totalMs = (int)(secs * 1000.0f + 0.5f);
					int mins = totalMs / 60000;
					int sec = (totalMs / 1000) % 60;
					int ms = totalMs % 1000;
					label.Printf("%d:%02d.%03d", mins, sec, ms);
				} else {
					int totalSec = (int)(secs + 0.5f);
					int mins = totalSec / 60;
					int sec = totalSec % 60;
					label.Printf("%d:%02d", mins, sec);
				}
				dc.DrawText(label, px + 2, 1);
			}
		}
	}

	// ---- Waveform or Spectrogram ----
	if (showWaveform && ywfhi < ywflo) {
		if (showSpectrogram) {
			// Spectrogram rendering
			if (mZoom <= 0) {
				uint32 posinc = 1 << -mZoom;
				uint32 pos0 = ClientXToSample(0);
				PaintSpectrogram(dc, pos0, posinc, 0, 1, w, ywfhi, ywflo);
			} else {
				// Zoomed in — one sample spans multiple pixels
				uint32 pos0 = ClientXToSample(0);
				PaintSpectrogram(dc, pos0, 1, 0, 1 << mZoom, w, ywfhi, ywflo);
			}
		} else {
			// Waveform rendering
			int wfHeight = ywflo - ywfhi;
			int wfCenter = (ywfhi + ywflo) / 2;

			// Center line
			dc.SetPen(wxPen(wxColour(40, 40, 50), 1));
			dc.DrawLine(0, wfCenter, w, wfCenter);

			if (mZoom <= 0) {
				// Many samples per pixel — draw min/max bars
				dc.SetPen(wxPen(wxColour(80, 180, 80), 1));
				uint32 pos = ClientXToSample(0);
				uint32 samplesPerPixel = 1 << -mZoom;

				for (int x = 0; x < w && pos < mSampleCount; ++x) {
					uint32 count = std::min(samplesPerPixel, mSampleCount - pos);
					if (count > 0 && mpImage->GetWaveformLength() > 0) {
						auto mm = mpImage->ReadWaveformMinMax(pos, count, false);
						int y0 = wfCenter - (int)(mm.mMax * (float)(wfHeight / 2));
						int y1 = wfCenter - (int)(mm.mMin * (float)(wfHeight / 2));
						if (y1 - y0 < 1) y1 = y0 + 1;
						dc.DrawLine(x, y0, x, y1);
					}
					pos += samplesPerPixel;
				}
			} else {
				// Few samples per pixel — draw polyline
				dc.SetPen(wxPen(wxColour(80, 180, 80), 1));
				std::vector<wxPoint> points;

				uint32 pos = posStart;
				while (pos <= posEnd && pos < mSampleCount) {
					float wfBuf[1];
					uint32 read = mpImage->ReadWaveform(wfBuf, pos, 1, false);
					if (read > 0) {
						int px = (int)SampleToClientXRaw(pos);
						int py = wfCenter - (int)(wfBuf[0] * (float)(wfHeight / 2));
						points.push_back(wxPoint(px, py));
					}
					++pos;
				}

				if (points.size() >= 2)
					dc.DrawLines((int)points.size(), points.data());
			}
		}
	}

	// ---- Data track (bit visualization) ----
	int dataHeight = ylo - yhi;
	int dataCenterY = (yhi + ylo) / 2;

	if (mZoom < 0) {
		// Many samples per pixel — use palette-mapped intensity
		uint32 pos = ClientXToSample(0);
		uint32 samplesPerPixel = 1 << -mZoom;

		for (int x = 0; x < w && pos < mSampleCount; ++x) {
			uint32 count = std::min(samplesPerPixel, mSampleCount - pos);
			if (count > 0) {
				auto ti = mpImage->GetTransitionInfo(pos, count, false);

				// Draw 3 rows: mark (top), transition (middle), space (bottom)
				int rowH = dataHeight / 3;
				int ym = yhi;
				int yt = yhi + rowH;
				int ys = yhi + rowH * 2;

				uint32 markIdx = ti.mMarkBits >> mPaletteShift;
				uint32 transIdx = ti.mTransitionBits >> mPaletteShift;
				uint32 spaceIdx = ti.mSpaceBits >> mPaletteShift;

				auto palToColour = [this](uint32 idx) -> wxColour {
					uint32 c = mPalette[std::min(idx, (uint32)256)];
					return wxColour((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
				};

				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(palToColour(markIdx)));
				dc.DrawRectangle(x, ym, 1, rowH);
				dc.SetBrush(wxBrush(palToColour(transIdx)));
				dc.DrawRectangle(x, yt, 1, rowH);
				dc.SetBrush(wxBrush(palToColour(spaceIdx)));
				dc.DrawRectangle(x, ys, 1, rowH);
			}
			pos += samplesPerPixel;
		}

		// Also draw turbo data if enabled
		if (mbShowTurboData) {
			pos = ClientXToSample(0);
			for (int x = 0; x < w && pos < mSampleCount; ++x) {
				uint32 count = std::min(samplesPerPixel, mSampleCount - pos);
				if (count > 0) {
					auto ti = mpImage->GetTransitionInfo(pos, count, true);
					uint32 markIdx = ti.mMarkBits >> mPaletteShift;
					auto c = mPalette[std::min(markIdx, (uint32)256)];
					dc.SetPen(wxPen(wxColour((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF)));
					int y = yhi - 4;
					dc.DrawPoint(x, y);
				}
				pos += samplesPerPixel;
			}
		}
	} else {
		// Few samples per pixel — draw step waveform
		dc.SetPen(wxPen(wxColour(80, 220, 80), 1));
		int prevPx = -1;
		int prevPy = dataCenterY;

		for (uint32 pos = posStart; pos <= posEnd && pos < mSampleCount; ++pos) {
			bool bit = mpImage->GetBit(pos, false);
			int px = (int)SampleToClientXRaw(pos);
			int py = bit ? yhi + 4 : ylo - 4;

			if (prevPx >= 0) {
				// Horizontal line at previous level
				dc.DrawLine(prevPx, prevPy, px, prevPy);
				// Vertical transition
				if (prevPy != py)
					dc.DrawLine(px, prevPy, px, py);
			}
			prevPx = px;
			prevPy = py;
		}

		// Turbo data
		if (mbShowTurboData) {
			dc.SetPen(wxPen(wxColour(220, 160, 80), 1));
			prevPx = -1;
			prevPy = dataCenterY;
			for (uint32 pos = posStart; pos <= posEnd && pos < mSampleCount; ++pos) {
				bool bit = mpImage->GetBit(pos, true);
				int px = (int)SampleToClientXRaw(pos);
				int py = bit ? yhi - 6 : yhi - 2;
				if (prevPx >= 0) {
					dc.DrawLine(prevPx, prevPy, px, prevPy);
					if (prevPy != py)
						dc.DrawLine(px, prevPy, px, py);
				}
				prevPx = px;
				prevPy = py;
			}
		}
	}

	// ---- Analysis overlay ----
	if (!mAnalysisChannels[0].mDecodedBlocks.mBlocks.empty()) {
		PaintAnalysisChannel(dc, mAnalysisChannels[0], posStart, posEnd, 0, w, analysisY0);
	}
	if (!mAnalysisChannels[1].mDecodedBlocks.mBlocks.empty()) {
		PaintAnalysisChannel(dc, mAnalysisChannels[1], posStart, posEnd, 0, w, analysisY1);
	}

	// ---- Head position marker ----
	{
		int headPx = (int)SampleToClientXRaw(mHeadPosition);
		if (headPx >= -20 && headPx <= w + 20) {
			wxColour headColor;
			if (mbHeadRecord)
				headColor = wxColour(0xFF, 0x00, 0x00);
			else if (mbHeadPlay)
				headColor = wxColour(0x40, 0x80, 0xFF);
			else
				headColor = wxColour(0x80, 0x80, 0x80);

			dc.SetPen(wxPen(headColor, 2));
			dc.DrawLine(headPx, 0, headPx, h);

			// Triangle at bottom
			wxPoint tri[3] = {
				wxPoint(headPx, h - 14),
				wxPoint(headPx - 8, h),
				wxPoint(headPx + 8, h)
			};
			dc.SetBrush(wxBrush(headColor));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawPolygon(3, tri);
		}
	}

	// ---- Selection overlay ----
	if (mbSelectionValid) {
		int sx1 = SampleEdgeToClientXFloor(mSelSortedStartSample);
		int sx2 = SampleEdgeToClientXCeil(mSelSortedEndSample);
		if (sx1 < 0) sx1 = 0;
		if (sx2 > w) sx2 = w;

		if (sx2 - sx1 < 1) sx2 = sx1 + 1;

		wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
		if (gc) {
			gc->SetBrush(wxBrush(wxColour(80, 80, 200, 96)));
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->DrawRectangle(sx1, 0, sx2 - sx1, h);
			delete gc;
		}
	}

	// ---- Draw mode preview ----
	if (mbDrawValid) {
		uint32 ds = std::min(mDrawStartSample, mDrawEndSample);
		uint32 de = std::max(mDrawStartSample, mDrawEndSample) + 1;
		int dx1 = SampleEdgeToClientXFloor(ds);
		int dx2 = SampleEdgeToClientXCeil(de);
		if (dx1 < 0) dx1 = 0;
		if (dx2 > w) dx2 = w;

		int dy = mbDrawPolarity ? yhi : (ylo - 4);
		int dh = 4;

		dc.SetBrush(wxBrush(wxColour(255, 0, 0)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(dx1, dy, dx2 - dx1, dh);
	}
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — spectrogram
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::PaintSpectrogram(wxDC& dc, uint32 pos, uint32 posinc, int x, int xinc, int n, int ywfhi, int ywflo) {
	if (!mpImage || !mpFFT)
		return;

	int sh = ywflo - ywfhi;
	if (sh <= 0)
		return;

	float kBinLo = (kSpaceTone - 2.0f * (kMarkTone - kSpaceTone)) / kBinWidth;
	float kBinHi = (kMarkTone + 2.0f * (kMarkTone - kSpaceTone)) / kBinWidth;

	// Precompute vertical interpolation table if needed
	if ((int)mSpectrogramInterp.size() != sh) {
		mSpectrogramInterp.resize(sh);
		for (int y = 0; y < sh; ++y) {
			float binF = kBinLo + (kBinHi - kBinLo) * (1.0f - (float)y / (float)(sh - 1));
			int bin0 = (int)binF - 1;
			float frac = binF - (float)(bin0 + 1);

			SpecSample& ss = mSpectrogramInterp[y];
			ss.mBinOffset = std::max(0, bin0);

			// Cubic interpolation coefficients
			float f2 = frac * frac;
			float f3 = f2 * frac;
			ss.mCoeffs[0] = -0.5f * frac + f2 - 0.5f * f3;
			ss.mCoeffs[1] = 1.0f - 2.5f * f2 + 1.5f * f3;
			ss.mCoeffs[2] = 0.5f * frac + 2.0f * f2 - 1.5f * f3;
			ss.mCoeffs[3] = -0.5f * f2 + 0.5f * f3;
		}
	}

	// Create image for this strip
	wxImage img(n, sh);
	unsigned char *rgb = img.GetData();
	memset(rgb, 0, n * sh * 3);

	float fftBuf[128];
	float powerBuf[65];

	uint32 wfLen = mpImage->GetWaveformLength();

	for (int col = 0; col < n; ++col) {
		if (pos >= wfLen || pos < 64) {
			pos += posinc;
			continue;
		}

		// Read 128 samples centered at pos
		uint32 readStart = pos >= 64 ? pos - 64 : 0;
		uint32 readCount = std::min((uint32)128, wfLen - readStart);

		float rawBuf[128] = {};
		mpImage->ReadWaveform(rawBuf, readStart, readCount, false);

		// Apply Kaiser window
		for (int i = 0; i < 128; ++i)
			fftBuf[i] = rawBuf[i] * mFFTWindow[i];

		// Forward FFT
		mpFFT->Forward(fftBuf);

		// Compute power spectrum (skip DC)
		// fftBuf[0] = DC, fftBuf[1] = Nyquist, then pairs of re/im
		powerBuf[0] = fftBuf[0] * fftBuf[0];
		for (int i = 1; i < 64; ++i) {
			float re = fftBuf[i * 2];
			float im = fftBuf[i * 2 + 1];
			powerBuf[i] = re * re + im * im;
		}
		powerBuf[64] = fftBuf[1] * fftBuf[1];

		// Render column
		for (int y = 0; y < sh; ++y) {
			const SpecSample& ss = mSpectrogramInterp[y];
			uint32 bo = ss.mBinOffset;

			float v = 0;
			if (bo + 3 < 65) {
				v = ss.mCoeffs[0] * powerBuf[bo]
				  + ss.mCoeffs[1] * powerBuf[bo + 1]
				  + ss.mCoeffs[2] * powerBuf[bo + 2]
				  + ss.mCoeffs[3] * powerBuf[bo + 3];
			}

			// Log power -> palette index
			float logP = 0.5f * log10f(std::max(v, 1e-12f)) * kSpecGain + 128.0f;
			int palIdx = std::clamp((int)logP, 0, 255);
			uint32 c = mSpectrogramPalette[palIdx];

			int pixOff = (y * n + col) * 3;
			rgb[pixOff + 0] = (c >> 16) & 0xFF;
			rgb[pixOff + 1] = (c >> 8) & 0xFF;
			rgb[pixOff + 2] = c & 0xFF;
		}

		pos += posinc;
	}

	wxBitmap bmp(img);
	dc.DrawBitmap(bmp, x, ywfhi, false);

	// Frequency guidelines
	if (mbShowFrequencyGuidelines && sh > 0) {
		float spaceBinNorm = 1.0f - (kSpaceTone / kBinWidth - kBinLo) / (kBinHi - kBinLo);
		float markBinNorm = 1.0f - (kMarkTone / kBinWidth - kBinLo) / (kBinHi - kBinLo);
		int spaceY = ywfhi + (int)(spaceBinNorm * (float)sh);
		int markY = ywfhi + (int)(markBinNorm * (float)sh);

		dc.SetPen(wxPen(wxColour(200, 200, 200), 1, wxPENSTYLE_DOT));
		dc.DrawLine(0, spaceY, x + n * xinc, spaceY);
		dc.DrawLine(0, markY, x + n * xinc, markY);
	}
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — analysis channel painting
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::PaintAnalysisChannel(wxDC& dc, const AnalysisChannel& ch, uint32 posStart, uint32 posEnd, int x1, int x2, int y) {
	const auto& blocks = ch.mDecodedBlocks.mBlocks;
	const auto& bytes = ch.mDecodedBlocks.mByteData;

	if (blocks.empty())
		return;

	int barHeight = 12;

	for (const auto& block : blocks) {
		if (block.mSampleEnd <= posStart || block.mSampleStart >= posEnd)
			continue;

		int bx1 = SampleEdgeToClientXFloor(block.mSampleStart);
		int bx2 = SampleEdgeToClientXCeil(block.mSampleEnd);
		if (bx1 < x1) bx1 = x1;
		if (bx2 > x2) bx2 = x2;

		// Block background
		wxColour blockColor = block.mbValidFrame ? wxColour(0, 80, 0) : wxColour(80, 0, 0);
		dc.SetBrush(wxBrush(blockColor));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(bx1, y, bx2 - bx1, barHeight);

		// Block border
		dc.SetPen(wxPen(wxColour(100, 100, 100), 1));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(bx1, y, bx2 - bx1, barHeight);

		// Draw individual bytes if zoomed enough
		uint32 byteStart = block.mStartByte;
		uint32 byteEnd = byteStart + block.mByteCount;

		for (uint32 bi = byteStart; bi < byteEnd && bi < (uint32)bytes.size(); ++bi) {
			const auto& db = bytes[bi];
			int dbx = SampleEdgeToClientXFloor(db.mStartSample);
			int dbxEnd = (bi + 1 < (uint32)bytes.size()) ?
				SampleEdgeToClientXFloor(bytes[bi + 1].mStartSample) : bx2;

			if (dbx < x1 || dbx > x2)
				continue;

			// Byte separator
			dc.SetPen(wxPen(wxColour(80, 80, 80), 1));
			dc.DrawLine(dbx, y, dbx, y + barHeight);

			// Show hex value if there's enough space
			if (dbxEnd - dbx > 16) {
				wxColour textColor(200, 200, 200);

				if ((uint8)db.mFlags & (uint8)DecodedByteFlags::FramingError)
					textColor = wxColour(255, 80, 80);

				// Checksum byte highlight
				if (block.mChecksumPos != kInvalidChecksumPos && bi == block.mChecksumPos) {
					textColor = block.mbValidFrame ? wxColour(80, 255, 80) : wxColour(255, 80, 80);
				}

				// Suspicious bit highlight
				if (block.mSuspiciousBit > 0 && bi == block.mChecksumPos) {
					dc.SetBrush(wxBrush(wxColour(0x99, 0x44, 0xFF)));
					dc.SetPen(*wxTRANSPARENT_PEN);
					dc.DrawRectangle(dbx + 1, y + 1, dbxEnd - dbx - 2, barHeight - 2);
				}

				dc.SetTextForeground(textColor);
				wxFont smallFont(7, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
				dc.SetFont(smallFont);
				wxString hex;
				hex.Printf("%02X", db.mData);
				dc.DrawText(hex, dbx + 2, y + 1);
			}
		}

		// Baud rate label
		if (bx2 - bx1 > 60) {
			wxString baudLabel;
			baudLabel.Printf("%.0f baud", block.mBaudRate);
			dc.SetTextForeground(wxColour(180, 180, 180));
			wxFont tinyFont(6, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
			dc.SetFont(tinyFont);
			dc.DrawText(baudLabel, bx1 + 2, y + barHeight + 1);
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — Analyze dispatch
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::Analyze(uint32 start, uint32 end) {
	AnalysisChannel& ch0 = mAnalysisChannels[0];

	ch0.mDecodedBlocks.Clear();

	switch(mAnalysisDecoder) {
		case Decoder::FSK_Sync:
			DecodeFSK(start, end, false, ch0.mDecodedBlocks);
			break;

		case Decoder::FSK_PLL:
			DecodeFSK2(start, end, false, ch0.mDecodedBlocks);
			break;

		case Decoder::T2000:
			DecodeT2000(start, end, ch0.mDecodedBlocks);
			break;
	}

	ch0.mSampleStart = start;
	ch0.mSampleEnd = end;

	Refresh(false);
}

void ATTapeViewPanel::OnByteDecoded(uint32 startPos, uint32 endPos, uint8 data, bool framingError, uint32 cyclesPerHalfBit) {
	if (!cyclesPerHalfBit)
		return;

	AnalysisChannel& ch1 = mAnalysisChannels[1];

	if (ch1.mSampleStart < ch1.mSampleEnd && startPos < ch1.mSampleEnd) {
		ch1.mSampleStart = startPos;
		ch1.mSampleEnd = startPos;
		ch1.mDecodedBlocks = {};
	}

	// (cycles/halfbit) / (bits/halfbit) = (cycles/bit)
	// (cycles/bit) / (cycles/sample) = (samples/bit)
	float samplesPerBit = (float)cyclesPerHalfBit * 2.0f / kATCassetteCyclesPerDataSample;

	DecodedBlocks& dblocks = ch1.mDecodedBlocks;
	if (dblocks.mBlocks.empty() || startPos - ch1.mSampleEnd > (uint32)(kATCassetteDataSampleRate / 20)) {
		auto& newdblock = dblocks.mBlocks.emplace_back(DecodedBlock());

		newdblock.mSampleStart = startPos;
		newdblock.mSampleEnd = startPos;
		newdblock.mSampleValidEnd = startPos;
		newdblock.mByteCount = 0;
		newdblock.mChecksumPos = 0;

		// (samples/sec) / (samples/bit) = (bits/sec)
		newdblock.mBaudRate = kATCassetteDataSampleRate / samplesPerBit;

		newdblock.mStartByte = (uint32)dblocks.mByteData.size();
		newdblock.mbValidFrame = false;
		newdblock.mSuspiciousBit = 0;
		newdblock.mbSuspiciousBitPolarity = false;

		DecodedByte& dbyte = dblocks.mByteData.emplace_back();
		dbyte.mStartSample = startPos;
		dbyte.mData = 0;
		dbyte.mFlags = DecodedByteFlags::None;

		mSIOMonChecksum = 0;
		mSIOMonFramingErrors = 0;
		mSIOMonChecksumPos = kInvalidChecksumPos;
	}

	if (framingError)
		++mSIOMonFramingErrors;

	DecodedBlock& dblock = dblocks.mBlocks.back();

	// We may not always get the sync bytes since the baud rate is not guaranteed to be
	// set up, so try to autodetect the number of sync bytes we actually got.
	if (dblock.mByteCount == 2) {
		const DecodedByte *dbytes = &dblocks.mByteData[dblock.mStartByte];

		if (dbytes[0].mData == 0xFA || dbytes[0].mData == 0xFC || dbytes[0].mData == 0xFE) {
			mSIOMonChecksumPos = 129;
			mSIOMonChecksum = (mSIOMonChecksum + 0xAA - 1) % 255 + 1;
		} else if (dbytes[1].mData == 0xFA || dbytes[1].mData == 0xFC || dbytes[1].mData == 0xFE) {
			mSIOMonChecksumPos = 130;
			mSIOMonChecksum = (mSIOMonChecksum + 0x55 - 1) % 255 + 1;
		} else
			mSIOMonChecksumPos = 131;
	}

	if (dblock.mByteCount >= mSIOMonChecksumPos && mSIOMonChecksum == data) {
		if (!dblock.mbValidFrame || !mSIOMonFramingErrors)
			dblock.mChecksumPos = dblock.mByteCount;

		if (!mSIOMonFramingErrors) {
			dblock.mbValidFrame = true;
			dblock.mSuspiciousBit = 0;

			if (dblock.mSampleValidEnd < dblock.mSampleEnd)
				dblock.mSampleValidEnd = dblock.mSampleEnd;
		}
	}

	if (dblock.mByteCount == mSIOMonChecksumPos && !dblock.mbValidFrame) {
		TryIdentifySuspiciousBit(dblocks, dblock, 131 - mSIOMonChecksumPos, mSIOMonChecksumPos, data);
	}

	const uint32 sum = (uint32)mSIOMonChecksum + data;
	mSIOMonChecksum = (uint8)(sum + (sum >> 8));

	++dblock.mByteCount;
	dblock.mSampleEnd = endPos;

	DecodedByte& dbyte = dblocks.mByteData.back();

	dbyte.mData = data;

	if (framingError)
		dbyte.mFlags = (DecodedByteFlags)((uint8)dbyte.mFlags | (uint8)DecodedByteFlags::FramingError);

	if (dbyte.mStartSample < startPos)
		dbyte.mStartSample = startPos;

	uint32 bitPos = startPos;
	uint32 bitPosFrac = (kATCassetteCyclesPerDataSample >> 1) + cyclesPerHalfBit;

	for(int i=0; i<10; ++i) {
		bitPos += bitPosFrac / kATCassetteCyclesPerDataSample;
		bitPosFrac %= kATCassetteCyclesPerDataSample;

		dbyte.mBitSampleOffsets[i] = bitPos - startPos;

		bitPosFrac += 2*cyclesPerHalfBit;
	}

	dblocks.mByteData.emplace_back();
	dblocks.mByteData.back().mStartSample = endPos;

	ch1.mSampleEnd = endPos;
	Refresh(false);
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — DecodeFSK (sync-based)
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::DecodeFSK(uint32 start, uint32 end, bool stopOnFramingError, DecodedBlocks& output) const {
	// capture range is 450-900 baud
	static constexpr uint32 kMinBitLen = (uint32)(kATCassetteDataSampleRate / 900.0f);
	static constexpr uint32 kMaxBitLen = (uint32)(kATCassetteDataSampleRate / 450.0f);

	if (!mpImage || !mpCasEmu || start >= end)
		return;

	uint32 pos = start;

	while(pos < end) {
		ATTapeSlidingWindowCursor cursor = mpCasEmu->GetFSKSampleCursor();
		cursor.mbFSKBypass = mbShowTurboData;

		// Poll for 19 sync bits within the allowed baud rates.
		//
		// States:
		//  0		looking for mark
		//  1		looking for space (leading edge of start bit)
		//  2		measure sync byte 1 start bit
		//  3-10	measure sync byte 1 data bits 0-7
		// 11		measure sync byte 1 stop bit
		// 12		measure sync byte 2 start bit
		// 13-20	measure sync byte 2 data bits 0-7
		//
		uint32 syncState = 0;
		uint32 syncStart = pos;

		while(syncState < 21) {
			// find next transition
			const auto nextSyncInfo = cursor.FindNext(*mpImage, pos, (syncState & 1) == 0, end - 1);
			if (nextSyncInfo.mPos >= end) {
				pos = end;
				break;
			}

			uint32 syncBitLen = nextSyncInfo.mPos - pos;

			if (syncState >= 2 && (syncBitLen < kMinBitLen || syncBitLen > kMaxBitLen)) {
				// If we got a long mark tone while looking for space, that's OK; proceed
				// into state 2. However, if we got a long space tone while looking for
				// mark, that's not OK and we should look for the next space.
				if (syncBitLen < kMinBitLen)
					syncState = (syncState + 1) & 1;
				else
					syncState = (syncState & 1) + 1;

				syncStart = pos;
			} else {
				if (syncState == 2)
					syncStart = pos;

				++syncState;
			}

			pos = nextSyncInfo.mPos;
		}

		if (pos >= end)
			break;

		// compute approximate average bit length
		float bitPeriodF = (float)(pos - syncStart) / 19.0f;
		uint32 bitPeriod = (uint32)VDRoundToInt(bitPeriodF);

		ATTapeSlidingWindowCursor bitCursor = mpCasEmu->GetFSKBitCursor(bitPeriod >> 1);
		bitCursor.mbFSKBypass = mbShowTurboData;

		// restart and start parsing out bits
		DecodedBlock& dblock = output.mBlocks.emplace_back();
		dblock.mSampleStart = syncStart;
		dblock.mSampleEnd = syncStart;
		dblock.mSampleValidEnd = syncStart;
		dblock.mStartByte = (uint32)output.mByteData.size();
		dblock.mByteCount = 0;
		dblock.mChecksumPos = 0;
		dblock.mBaudRate = 0;
		dblock.mSuspiciousBit = 0;
		dblock.mbSuspiciousBitPolarity = false;

		uint32 pos2 = syncStart;

		uint32 blockTimeout = bitPeriod * 20;
		bool blockEndedEarly = false;
		uint32 posLastByteEnd = pos2;
		while(pos2 < end) {
			const auto startBitInfo = bitCursor.FindNext(*mpImage, pos2, false, end - 1);

			if (startBitInfo.mPos - pos2 > blockTimeout)
				break;

			// check that we have enough room to fit a full byte
			if (startBitInfo.mPos >= end || end - pos2 < bitPeriod * 10) {
				blockEndedEarly = true;
				break;
			}

			// compute half bit offset to center of start bit
			uint32 startBitPos = startBitInfo.mPos + (bitPeriod >> 1);

			// sample start bit and verify that it's real
			if (bitCursor.GetBit(*mpImage, startBitPos)) {
				// not real -- skip it
				pos2 = startBitPos;
				posLastByteEnd = startBitPos;
				continue;
			}

			// sample data bits
			uint32 dataBitPos[8];
			uint8 v = 0;
			for(int i=0; i<8; ++i) {
				dataBitPos[i] = startBitPos + (i + 1) * bitPeriod;
				v = (v >> 1) + (bitCursor.GetBit(*mpImage, dataBitPos[i]) ? 0x80 : 0);
			}

			// sample stop bit and check for framing error
			uint32 stopBitPos = startBitPos + 9 * bitPeriod;
			pos2 = stopBitPos;
			posLastByteEnd = startBitInfo.mPos + 10 * bitPeriod;

			DecodedByteFlags flags = DecodedByteFlags::None;
			if (!bitCursor.GetBit(*mpImage, pos2)) {
				if (stopOnFramingError) {
					blockEndedEarly = true;
					break;
				}

				flags = DecodedByteFlags::FramingError;
				pos2 = stopBitPos;
			}

			DecodedByte& dbyte = output.mByteData.emplace_back();
			dbyte.mFlags = flags;
			dbyte.mStartSample = startBitInfo.mPos;
			dbyte.mData = v;
			dbyte.mBitSampleOffsets[0] = (uint16)(startBitPos - startBitInfo.mPos);

			for(int i=0; i<8; ++i)
				dbyte.mBitSampleOffsets[i+1] = (uint16)(dataBitPos[i] - startBitInfo.mPos);

			dbyte.mBitSampleOffsets[9] = (uint16)(stopBitPos - startBitInfo.mPos);
		}

		if (output.mByteData.size() == dblock.mStartByte) {
			output.mBlocks.pop_back();
			break;
		}

		dblock.mByteCount = (uint32)output.mByteData.size() - dblock.mStartByte;

		// see if we can spot the checksum for a standard record (128+4 bytes),
		// or a longer record with the same framing
		if (dblock.mByteCount >= 132) {
			const DecodedByte *data = output.mByteData.begin() + dblock.mStartByte;
			uint32 sum = 0;

			for(uint32 i = 0; i < 131; ++i)
				sum += data[i].mData;

			uint8 chk = sum ? (sum - 1) % 255 + 1 : 0;
			bool framingOK = std::find_if(data, data + 131,
				[](const DecodedByte& byteInfo) {
					return (byteInfo.mFlags & DecodedByteFlags::FramingError) != DecodedByteFlags::None;
				}) == data + 131;

			for(uint32 i = 131; i < dblock.mByteCount; ++i) {
				if ((data[i].mFlags & DecodedByteFlags::FramingError) != DecodedByteFlags::None)
					framingOK = false;

				const uint8 c = data[i].mData;

				if (chk == c) {
					if (framingOK || !dblock.mbValidFrame)
						dblock.mChecksumPos = i;

					if (framingOK)
						dblock.mbValidFrame = true;
				}

				sum = (uint32)chk + data[i].mData;
				chk = (uint8)(sum + (sum >> 8));
			}

			if (!dblock.mbValidFrame)
				TryIdentifySuspiciousBit(output, dblock, 0, 131, data[131].mData);
		}

		// push a dummy byte to delimit the last byte
		pos = std::min(posLastByteEnd, end);
		dblock.mSampleEnd = pos;

		if (dblock.mbValidFrame)
			dblock.mSampleValidEnd = pos;

		DecodedByte& dbyte = output.mByteData.emplace_back();
		dbyte.mStartSample = pos;

		// compute ideal baud rate based on estimated bit period
		const uint32 baudRateBitPeriod = (uint32)VDRoundToInt(kATCassetteDataSampleRate / bitPeriodF);

		// compute ideal baud rate based on the sample range
		const uint32 numBytes = dblock.mByteCount;
		const uint32 numBits = 10 * numBytes;
		const uint32 baudRateSampleRange = VDRoundToInt(kATCassetteDataSampleRate * (float)numBits / (float)(dblock.mSampleEnd - dblock.mSampleStart));

		// use the max of the two (smaller block size)
		uint32 baudRate = std::max(baudRateBitPeriod, baudRateSampleRange);

		dblock.mBaudRate = baudRate;

		// if the rounded baud rate causes us to go over, increment it to fit
		ATCassetteWriteCursor writeCursor;
		writeCursor.mPosition = syncStart;
		for(int i=0; i<5; ++i) {
			uint32 neededSamples = mpImage->EstimateWriteStdData(writeCursor, numBytes, baudRate);
			if (neededSamples <= pos - syncStart)
				break;

			++baudRate;
		}

		if (blockEndedEarly)
			break;
	}
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — DecodeFSK2 (PLL-based)
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::DecodeFSK2(uint32 start, uint32 end, bool stopOnFramingError, DecodedBlocks& output) const {
	// capture range is 450-900 baud
	static constexpr uint32 kMinBitLen = (uint32)(kATCassetteDataSampleRate / 900.0f + 0.5f);
	static constexpr uint32 kMaxBitLen = (uint32)(kATCassetteDataSampleRate / 450.0f + 0.5f);

	if (!mpImage || !mpCasEmu || start >= end)
		return;

	uint32 pos = start;

	static constexpr uint32 kStdBitLenX256 = (uint32)(kATCassetteDataSampleRate * 256.0f / 600.0f + 0.5f);
	static constexpr uint32 kStdByteLen = (uint32)(kATCassetteDataSampleRate * 10.0f / 600.0f + 0.5f);
	uint32 bitWidthX256 = kStdBitLenX256;

	sint32 bitError = 0;
	ATTapeSlidingWindowCursor cursor = mpCasEmu->GetFSKSampleCursor();
	cursor.mbFSKBypass = mbShowTurboData;

	while(pos < end) {
		bool blockEndedEarly = false;
		DecodedBlock *dblock = nullptr;
		uint32 lastByteEnd = pos;

		while(pos < end) {
			// find start transition
			const auto nextSyncInfo = cursor.FindNext(*mpImage, pos, false, end - 1);
			if (nextSyncInfo.mPos >= end) {
				pos = end;
				break;
			}

			// if we're beyond a byte's worth, reset the rate and close the last block if one was open
			if (nextSyncInfo.mPos - lastByteEnd > kStdByteLen) {
				bitWidthX256 = kStdBitLenX256;
				pos = nextSyncInfo.mPos;
				break;
			}

			// sample start bit and check if it is still low
			uint32 bitSamplePos[10];

			const uint32 startBitPos = nextSyncInfo.mPos;
			pos = startBitPos + (bitWidthX256 >> 9);

			if (pos >= end)
				break;

			bitSamplePos[0] = pos;
			bool startBit = cursor.GetBit(*mpImage, pos);
			if (startBit)
				continue;

			// read out 8 data bits and stop bit
			uint32 accumX256 = 128;
			uint32 shifter = 0;
			bool lastBit = false;

			for(int i=0; i<9; ++i) {
				accumX256 += bitWidthX256;
				uint32 next = pos + (accumX256 >> 8);
				if (next >= end) {
					pos = next;
					break;
				}

				bitSamplePos[i+1] = next;
				accumX256 &= 0xFF;
				const auto sumAndNextInfo = cursor.GetBitSumAndNext(*mpImage, pos, next);

				if (lastBit != sumAndNextInfo.mNextBit) {
					lastBit = sumAndNextInfo.mNextBit;

					uint32 bitLen = next - pos;
					uint32 bitLenLo = (bitLen * 3 + 4) / 8;
					uint32 bitLenHi = (bitLen * 5 + 4) / 8;
					uint32 edgeSum = lastBit ? bitLen - sumAndNextInfo.mSum : sumAndNextInfo.mSum;

					if (edgeSum < bitLenLo) {
						if (bitError > 0)
							bitError = 0;
						else if (bitError > -10)
							--bitError;

						if (bitError <= -3) {
							bitWidthX256 += 10 * (bitError + 2);
							--pos;
						}
					} else if (edgeSum > bitLenHi) {
						if (bitError < 0)
							bitError = 0;
						else if (bitError < 10)
							++bitError;

						if (bitError >= 3) {
							bitWidthX256 += 10 * (bitError - 2);
							++pos;
						}
					} else
						bitError = 0;
				}

				shifter >>= 1;
				if (sumAndNextInfo.mNextBit)
					shifter += 0x100;

				pos = next;
				lastByteEnd = pos;
			}

			if (stopOnFramingError && !(shifter & 0x100)) {
				blockEndedEarly = true;
				break;
			}

			// open block if we don't already have one
			if (!dblock) {
				dblock = &output.mBlocks.emplace_back();
				dblock->mSampleStart = startBitPos;
				dblock->mSampleEnd = startBitPos;
				dblock->mSampleValidEnd = startBitPos;
				dblock->mStartByte = (uint32)output.mByteData.size();
				dblock->mByteCount = 0;
				dblock->mChecksumPos = 0;
				dblock->mBaudRate = 0;
				dblock->mSuspiciousBit = 0;
				dblock->mbSuspiciousBitPolarity = false;
			}

			DecodedByte& dbyte = output.mByteData.emplace_back();
			dbyte.mData = (uint8)shifter;
			dbyte.mFlags = shifter & 0x100 ? DecodedByteFlags::None : DecodedByteFlags::FramingError;
			dbyte.mStartSample = nextSyncInfo.mPos;

			for(int i=0; i<10; ++i)
				dbyte.mBitSampleOffsets[i] = (uint16)(bitSamplePos[i] - dbyte.mStartSample);

			++dblock->mByteCount;
		}

		if (!dblock)
			continue;

		// close current block
		dblock->mSampleEnd = std::min(lastByteEnd + ((bitWidthX256 + 256) >> 9), pos);

		// push a dummy byte to delimit the last byte
		output.mByteData.emplace_back();
		output.mByteData.back().mStartSample = dblock->mSampleEnd;

		// see if we can spot the checksum for a standard record (128+4 bytes),
		// or a longer record with the same framing
		if (dblock->mByteCount >= 132) {
			const DecodedByte *data = output.mByteData.begin() + dblock->mStartByte;
			uint32 sum = 0;

			for(uint32 i = 0; i < 131; ++i)
				sum += data[i].mData;

			uint8 chk = sum ? (sum - 1) % 255 + 1 : 0;
			bool framingOK = std::find_if(data, data + 131,
				[](const DecodedByte& db) {
					return (db.mFlags & DecodedByteFlags::FramingError) != DecodedByteFlags::None;
				}) == data + 131;

			for(uint32 i = 131; i < dblock->mByteCount; ++i) {
				if ((data[i].mFlags & DecodedByteFlags::FramingError) != DecodedByteFlags::None)
					framingOK = false;

				const uint8 c = data[i].mData;

				if (chk == c) {
					if (framingOK || !dblock->mbValidFrame)
						dblock->mChecksumPos = i;

					if (framingOK) {
						dblock->mbValidFrame = true;
						dblock->mSampleValidEnd = dblock->mSampleEnd;
					}
				}

				sum = (uint32)chk + data[i].mData;
				chk = (uint8)(sum + (sum >> 8));
			}

			if (!dblock->mbValidFrame)
				TryIdentifySuspiciousBit(output, *dblock, 0, 131, data[131].mData);
		}

		// compute ideal baud rate based on the sample range
		const uint32 numBytes = dblock->mByteCount;
		const uint32 numBits = 10 * numBytes;
		uint32 baudRate = VDRoundToInt(kATCassetteDataSampleRate * (float)numBits / (float)(dblock->mSampleEnd - dblock->mSampleStart));

		// if the rounded baud rate causes us to go over, increment it to fit
		ATCassetteWriteCursor writeCursor;
		writeCursor.mPosition = dblock->mSampleStart;
		for(int i=0; i<5; ++i) {
			uint32 neededSamples = mpImage->EstimateWriteStdData(writeCursor, numBytes, baudRate);
			if (neededSamples <= pos - dblock->mSampleStart)
				break;

			++baudRate;
		}

		dblock->mBaudRate = baudRate;

		if (blockEndedEarly)
			break;
	}
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — DecodeT2000 (Turbo 2000)
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::DecodeT2000(uint32 start, uint32 end, DecodedBlocks& output) const {
	if (!mpImage || start >= end)
		return;

	bool polarity = false;
	uint32 lastPulseWidth = 0;
	uint32 pilotWindow[16] {};
	uint32 pilotWindowIndex = 0;
	uint32 pilotWindowSum = 8;
	uint32 pilotCount = 0;

	enum class State : uint8 {
		Pilot,
		Sync1,
		Sync2,
		Data0a,
		Data0b,
		Data1a,
		Data1b,
		Data2a,
		Data2b,
		Data3a,
		Data3b,
		Data4a,
		Data4b,
		Data5a,
		Data5b,
		Data6a,
		Data6b,
		Data7a,
		Data7b,
	} state = State::Pilot;

	uint32 pos = start;
	uint8 c = 0;
	uint32 byteStart = 0;
	while(pos < end) {
		const auto bitInfo = mpImage->FindNextBit(pos, end, polarity, true);
		polarity = !polarity;

		if (bitInfo.mPos >= end)
			break;

		const uint32 pulseWidth = bitInfo.mPos - pos;
		pos = bitInfo.mPos;

		const uint32 cycleWidth = pulseWidth + lastPulseWidth;
		lastPulseWidth = pulseWidth;

		switch(state) {
			case State::Pilot: {
				uint32 avgPilotWidth = pilotWindowSum >> 4;
				pilotWindowSum -= pilotWindow[pilotWindowIndex];
				pilotWindowSum += cycleWidth;
				pilotWindow[pilotWindowIndex] = cycleWidth;
				pilotWindowIndex = (pilotWindowIndex + 1) & 15;

				if (avgPilotWidth > 5) {
					uint32 minPilotWidth = avgPilotWidth - 2;
					uint32 maxPilotWidth = avgPilotWidth + 2;

					if (cycleWidth < minPilotWidth) {
						if (pilotCount >= 16)
							state = State::Sync1;

						pilotCount = 0;
					} else if (cycleWidth > maxPilotWidth)
						pilotCount = 0;
					else
						++pilotCount;
				}
				break;
			}

			case State::Sync1:
				state = State::Sync2;
				break;

			case State::Sync2:
				state = State::Data0a;
				byteStart = pos;
				break;

			case State::Data0a:
			case State::Data1a:
			case State::Data2a:
			case State::Data3a:
			case State::Data4a:
			case State::Data5a:
			case State::Data6a:
			case State::Data7a:
				state = (State)((uint8)state + 1);
				break;

			case State::Data0b:
			case State::Data1b:
			case State::Data2b:
			case State::Data3b:
			case State::Data4b:
			case State::Data5b:
			case State::Data6b:
				c += c;
				if (cycleWidth >= 16)
					++c;
				state = (State)((uint8)state + 1);
				break;

			case State::Data7b: {
				c += c;
				if (cycleWidth >= 16)
					++c;

				if (output.mBlocks.empty()) {
					auto& dblock = output.mBlocks.emplace_back();
					dblock.mSampleStart = byteStart;
					dblock.mSampleEnd = pos;
					dblock.mSampleValidEnd = byteStart;
					dblock.mBaudRate = 0;
					dblock.mStartByte = 0;
					dblock.mByteCount = 0;
					dblock.mChecksumPos = 0;
					dblock.mbValidFrame = false;
					dblock.mSuspiciousBit = 0;
					dblock.mbSuspiciousBitPolarity = false;

					output.mByteData.emplace_back();
					output.mByteData.back().mStartSample = byteStart;
				}

				auto& dblock2 = output.mBlocks.back();
				byteStart = pos;
				dblock2.mSampleEnd = pos;
				++dblock2.mByteCount;

				output.mByteData.back().mData = c;

				output.mByteData.emplace_back();
				output.mByteData.back().mStartSample = pos;

				state = State::Data0a;
				break;
			}
		}
	}

	if (!output.mBlocks.empty()) {
		auto& dblock = output.mBlocks.back();

		uint8 chk = output.mByteData[0].mData;
		uint32 n = dblock.mByteCount;
		for(uint32 i = 1; i < n; ++i) {
			chk ^= output.mByteData[i].mData;

			if (chk == 0) {
				dblock.mChecksumPos = i;
				dblock.mbValidFrame = true;
				dblock.mSampleValidEnd = dblock.mSampleEnd;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — TryIdentifySuspiciousBit
///////////////////////////////////////////////////////////////////////////

bool ATTapeViewPanel::TryIdentifySuspiciousBit(const DecodedBlocks& dblocks, DecodedBlock& dblock, uint32 forcedSyncBytes, uint32 checksumPos, uint8 receivedSum) {
	if (dblock.mByteCount < checksumPos)
		return false;

	// See if we could identify a single bit error.
	//
	// Because the SIO checksum is a simple 1's complement sum, it is not possible to
	// determine the bit position. What we can do instead is identify which bit is
	// probably bad.
	uint32 computedSum32 = forcedSyncBytes * 0x55;

	for(uint32 i = 0; i < checksumPos; ++i)
		computedSum32 += dblocks.mByteData[dblock.mStartByte + i].mData;

	const uint8 computedSum = computedSum32 ? (uint8)((computedSum32 - 1) % 255 + 1) : 0xFF;

	for(int bitPos = 0; bitPos < 8; ++bitPos) {
		uint8 bit = 1 << bitPos;

		if (computedSum == (255 - receivedSum >= bit ? receivedSum + bit : receivedSum + bit - 255)) {
			dblock.mSuspiciousBit = (uint8)(bitPos + 1);
			dblock.mbSuspiciousBitPolarity = true;
			dblock.mChecksumPos = 131;
			return true;
		} else if (computedSum == (receivedSum > bit ? receivedSum - bit : receivedSum + 255 - bit)) {
			dblock.mSuspiciousBit = (uint8)(bitPos + 1);
			dblock.mbSuspiciousBitPolarity = false;
			dblock.mChecksumPos = 131;
			return true;
		}
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////
// ATTapeViewPanel — ReAnalyze, Filter
///////////////////////////////////////////////////////////////////////////

void ATTapeViewPanel::ReAnalyze() {
	AnalysisChannel& ch0 = mAnalysisChannels[0];

	if (ch0.mSampleEnd > ch0.mSampleStart)
		Analyze(ch0.mSampleStart, ch0.mSampleEnd);
}

void ATTapeViewPanel::ReAnalyzeFlip() {
	switch(mAnalysisDecoder) {
		case Decoder::FSK_PLL:
			SetAnalysisDecoder(Decoder::FSK_Sync);
			ReAnalyze();
			break;

		case Decoder::FSK_Sync:
			SetAnalysisDecoder(Decoder::FSK_PLL);
			ReAnalyze();
			break;

		default:
			break;
	}
}

void ATTapeViewPanel::Filter(FilterMode filterMode) {
	if (!HasNonEmptySelection() || !mpImage)
		return;

	uint32 window = 32;
	uint32 threshold = 12;

	switch(filterMode) {
		case FilterMode::FSKDirectSample2000Baud:
			window = 16;
			threshold = 6;
			break;

		case FilterMode::FSKDirectSample1000Baud:
			window = 32;
			threshold = 12;
			break;
	}

	ATTapeSlidingWindowCursor cursor {};
	cursor.mWindow = window;
	cursor.mOffset = window/2;
	cursor.mThresholdLo = threshold;
	cursor.mThresholdHi = window - threshold;
	cursor.mbFSKBypass = false;
	cursor.Reset();

	uint32 deckPos = PreModify();

	const uint32 start = mSelSortedStartSample;
	const uint32 end = mSelSortedEndSample;
	const uint32 len = end - start;
	PushUndo(start, len, len, UndoSelectionMode::SelectionIsRange);

	// read out all pulses -- do this before we modify the tape so as
	// to not mix writes into the sampling
	vdfastvector<uint32> pulses;
	bool polarity = true;

	// the hysteresis introduces a slight amount of delay, so advance the sampling by
	// the expected delay to compensate
	uint32 pos = start + (window / 2 - threshold + 1);

	while(pos < end) {
		auto next = cursor.FindNext(*mpImage, pos, !polarity, end - 1);

		pulses.push_back(next.mPos - pos);

		polarity = !polarity;
		pos = next.mPos;
	}

	// write the new pulses back to the tape
	ATCassetteWriteCursor writeCursor;
	writeCursor.mPosition = mSelSortedStartSample;

	polarity = true;
	for(uint32 pulseWidth : pulses) {
		mpImage->WritePulse(writeCursor, polarity, pulseWidth, false, true);

		polarity = !polarity;
	}

	PostModify(deckPos);

	ClearSelection();
	Refresh(false);
}

///////////////////////////////////////////////////////////////////////////
// ATTapeEditorFrame — non-modal singleton frame
///////////////////////////////////////////////////////////////////////////

enum {
	ID_TE_FILE_NEW = 6000,
	ID_TE_FILE_OPEN,
	ID_TE_FILE_RELOAD,
	ID_TE_FILE_SAVEASCAS,
	ID_TE_FILE_SAVEASWAV,
	ID_TE_FILE_CLOSE,

	ID_TE_EDIT_UNDO,
	ID_TE_EDIT_REDO,
	ID_TE_EDIT_SELECTALL,
	ID_TE_EDIT_DESELECT,
	ID_TE_EDIT_CUT,
	ID_TE_EDIT_COPY,
	ID_TE_EDIT_COPYDECODEDDATA,
	ID_TE_EDIT_PASTE,
	ID_TE_EDIT_DELETE,
	ID_TE_EDIT_CONVERTTOSTD,
	ID_TE_EDIT_CONVERTTORAW,
	ID_TE_EDIT_REPEATLAST,
	ID_TE_EDIT_REPEATLASTFLIP,

	ID_TE_DATA_EXTRACTCFILE,

	ID_TE_VIEW_NOSIGNAL,
	ID_TE_VIEW_WAVEFORM,
	ID_TE_VIEW_SPECTROGRAM,
	ID_TE_VIEW_FREQGUIDELINES,
	ID_TE_VIEW_FSKDATA,
	ID_TE_VIEW_TURBODATA,

	ID_TE_MONITOR_CAPTURESIO,

	ID_TE_OPTIONS_STOREWAVEFORM,

	ID_TE_MODE_SCROLL,
	ID_TE_MODE_SELECT,
	ID_TE_MODE_DRAW,
	ID_TE_MODE_INSERT,
	ID_TE_MODE_ANALYZE,

	ID_TE_ANALYZE_FSKSYNC,
	ID_TE_ANALYZE_FSKPLL,
	ID_TE_ANALYZE_T2000,

	ID_TE_DELETE,
	ID_TE_FILTER_2000,
	ID_TE_FILTER_1000,

	ID_TE_REFRESH_TIMER,
};

class ATTapeEditorFrame : public wxFrame {
public:
	ATTapeEditorFrame(wxWindow *parent);
	~ATTapeEditorFrame();

	void UpdateTitle();

private:
	void OnClose(wxCloseEvent& evt);
	void OnTimer(wxTimerEvent& evt);

	void OnFileNew(wxCommandEvent&);
	void OnFileOpen(wxCommandEvent&);
	void OnFileReload(wxCommandEvent&);
	void OnFileSaveAsCAS(wxCommandEvent&);
	void OnFileSaveAsWAV(wxCommandEvent&);
	void OnFileClose(wxCommandEvent&);

	void OnEditCommand(wxCommandEvent& evt);
	void OnDataExtractCFile(wxCommandEvent&);
	void OnViewCommand(wxCommandEvent& evt);
	void OnMonitorCaptureSIO(wxCommandEvent&);
	void OnOptionsStoreWaveform(wxCommandEvent&);
	void OnModeCommand(wxCommandEvent& evt);
	void OnAnalyzeDecoder(wxCommandEvent& evt);
	void OnDelete(wxCommandEvent&);
	void OnFilter(wxCommandEvent& evt);

	void OnUpdateUI(wxUpdateUIEvent& evt);
	void UpdateStatusMessage();
	void UpdateModeButtons();

	bool OKToDiscard();
	void Load(const wxString& path);

	ATTapeViewPanel *mpTapeView = nullptr;
	wxToolBar *mpToolbar = nullptr;
	wxTimer mRefreshTimer;

	vdfunction<void()> mFnOnTapeDirtyStateChanged;
	vdfunction<void()> mFnOnTapeChanged;
};

static ATTapeEditorFrame *g_pTapeEditorFrame = nullptr;

ATTapeEditorFrame::ATTapeEditorFrame(wxWindow *parent)
	: wxFrame(parent, wxID_ANY, "Tape Editor", wxDefaultPosition, wxSize(900, 550),
		wxDEFAULT_FRAME_STYLE)
	, mRefreshTimer(this, ID_TE_REFRESH_TIMER)
{
	// Menu bar
	wxMenuBar *mb = new wxMenuBar;

	wxMenu *fileMenu = new wxMenu;
	fileMenu->Append(ID_TE_FILE_NEW, "&New\tCtrl+N");
	fileMenu->Append(ID_TE_FILE_OPEN, "&Open...\tCtrl+O");
	fileMenu->Append(ID_TE_FILE_RELOAD, "&Reload");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_TE_FILE_SAVEASCAS, "Save as &CAS...");
	fileMenu->Append(ID_TE_FILE_SAVEASWAV, "Save as &WAV...");
	fileMenu->AppendSeparator();
	fileMenu->Append(ID_TE_FILE_CLOSE, "&Close\tCtrl+W");

	wxMenu *editMenu = new wxMenu;
	editMenu->Append(ID_TE_EDIT_UNDO, "&Undo\tCtrl+Z");
	editMenu->Append(ID_TE_EDIT_REDO, "&Redo\tCtrl+Y");
	editMenu->AppendSeparator();
	editMenu->Append(ID_TE_EDIT_SELECTALL, "Select &All\tCtrl+A");
	editMenu->Append(ID_TE_EDIT_DESELECT, "&Deselect");
	editMenu->AppendSeparator();
	editMenu->Append(ID_TE_EDIT_CUT, "Cu&t\tCtrl+X");
	editMenu->Append(ID_TE_EDIT_COPY, "&Copy\tCtrl+C");
	editMenu->Append(ID_TE_EDIT_COPYDECODEDDATA, "Copy Decoded Data");
	editMenu->Append(ID_TE_EDIT_PASTE, "&Paste\tCtrl+V");
	editMenu->Append(ID_TE_EDIT_DELETE, "&Delete\tDel");
	editMenu->AppendSeparator();
	editMenu->Append(ID_TE_EDIT_CONVERTTOSTD, "Convert to Standard Block");
	editMenu->Append(ID_TE_EDIT_CONVERTTORAW, "Convert to Raw Block");
	editMenu->AppendSeparator();
	editMenu->Append(ID_TE_EDIT_REPEATLAST, "Repeat Last Analysis");
	editMenu->Append(ID_TE_EDIT_REPEATLASTFLIP, "Repeat Last Analysis (Flip)");

	wxMenu *dataMenu = new wxMenu;
	dataMenu->Append(ID_TE_DATA_EXTRACTCFILE, "Extract as C File...");

	wxMenu *viewMenu = new wxMenu;
	viewMenu->AppendRadioItem(ID_TE_VIEW_NOSIGNAL, "No Signal");
	viewMenu->AppendRadioItem(ID_TE_VIEW_WAVEFORM, "Waveform");
	viewMenu->AppendRadioItem(ID_TE_VIEW_SPECTROGRAM, "Spectrogram");
	viewMenu->AppendSeparator();
	viewMenu->AppendCheckItem(ID_TE_VIEW_FREQGUIDELINES, "Show Frequency Guidelines");
	viewMenu->AppendSeparator();
	viewMenu->AppendCheckItem(ID_TE_VIEW_FSKDATA, "FSK Data");
	viewMenu->AppendCheckItem(ID_TE_VIEW_TURBODATA, "Turbo Data");

	wxMenu *monitorMenu = new wxMenu;
	monitorMenu->AppendCheckItem(ID_TE_MONITOR_CAPTURESIO, "Capture SIO");

	wxMenu *optionsMenu = new wxMenu;
	optionsMenu->AppendCheckItem(ID_TE_OPTIONS_STOREWAVEFORM, "Store Waveform on Load");

	mb->Append(fileMenu, "&File");
	mb->Append(editMenu, "&Edit");
	mb->Append(dataMenu, "&Data");
	mb->Append(viewMenu, "&View");
	mb->Append(monitorMenu, "&Monitor");
	mb->Append(optionsMenu, "&Options");
	SetMenuBar(mb);

	// Toolbar
	mpToolbar = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT);
	// Create simple 16x16 placeholder bitmaps for toolbar (text labels are the primary UI)
	wxBitmap emptyBmp(16, 16);
	{
		wxMemoryDC dc(emptyBmp);
		dc.SetBackground(*wxBLACK_BRUSH);
		dc.Clear();
	}
	mpToolbar->AddRadioTool(ID_TE_MODE_SCROLL, "Scroll", emptyBmp);
	mpToolbar->AddRadioTool(ID_TE_MODE_SELECT, "Select", emptyBmp);
	mpToolbar->AddRadioTool(ID_TE_MODE_DRAW, "Draw", emptyBmp);
	mpToolbar->AddRadioTool(ID_TE_MODE_INSERT, "Insert", emptyBmp);
	mpToolbar->AddRadioTool(ID_TE_MODE_ANALYZE, "Analyze", emptyBmp);
	mpToolbar->AddSeparator();
	mpToolbar->AddTool(ID_TE_DELETE, "Delete", emptyBmp);
	mpToolbar->Realize();

	// Status bar
	CreateStatusBar(1);
	SetStatusText("Scroll tool: left-drag to scroll, mouse wheel to zoom");

	// Tape view panel
	mpTapeView = new ATTapeViewPanel(this);

	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(mpTapeView, 1, wxEXPAND);
	SetSizer(sizer);

	// Connect to cassette emulator
	ATCassetteEmulator& cas = g_sim.GetCassette();
	mpTapeView->SetCassetteEmulator(&cas);

	// Set default view mode
	mpTapeView->SetWaveformMode(WaveformMode::Waveform);
	viewMenu->Check(ID_TE_VIEW_WAVEFORM, true);

	// Callbacks
	mpTapeView->mFnOnDrawModeChanged = [this]() {
		UpdateModeButtons();
		UpdateStatusMessage();
	};
	mpTapeView->mFnOnSelectionChanged = [this]() {
		UpdateStatusMessage();
	};

	mFnOnTapeDirtyStateChanged = [this]() { UpdateTitle(); };
	cas.TapeDirtyStateChanged += &mFnOnTapeDirtyStateChanged;

	mFnOnTapeChanged = [this]() {
		mpTapeView->OnTapeModified();
		UpdateTitle();
	};
	cas.TapeChanged.Add(&mFnOnTapeChanged);

	UpdateTitle();
	UpdateModeButtons();

	// Bind events
	Bind(wxEVT_CLOSE_WINDOW, &ATTapeEditorFrame::OnClose, this);
	Bind(wxEVT_TIMER, &ATTapeEditorFrame::OnTimer, this, ID_TE_REFRESH_TIMER);

	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnFileNew, this, ID_TE_FILE_NEW);
	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnFileOpen, this, ID_TE_FILE_OPEN);
	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnFileReload, this, ID_TE_FILE_RELOAD);
	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnFileSaveAsCAS, this, ID_TE_FILE_SAVEASCAS);
	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnFileSaveAsWAV, this, ID_TE_FILE_SAVEASWAV);
	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnFileClose, this, ID_TE_FILE_CLOSE);

	for (int id = ID_TE_EDIT_UNDO; id <= ID_TE_EDIT_REPEATLASTFLIP; ++id)
		Bind(wxEVT_MENU, &ATTapeEditorFrame::OnEditCommand, this, id);

	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnDataExtractCFile, this, ID_TE_DATA_EXTRACTCFILE);

	for (int id = ID_TE_VIEW_NOSIGNAL; id <= ID_TE_VIEW_TURBODATA; ++id)
		Bind(wxEVT_MENU, &ATTapeEditorFrame::OnViewCommand, this, id);

	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnMonitorCaptureSIO, this, ID_TE_MONITOR_CAPTURESIO);
	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnOptionsStoreWaveform, this, ID_TE_OPTIONS_STOREWAVEFORM);

	for (int id = ID_TE_MODE_SCROLL; id <= ID_TE_MODE_ANALYZE; ++id)
		Bind(wxEVT_MENU, &ATTapeEditorFrame::OnModeCommand, this, id);

	for (int id = ID_TE_ANALYZE_FSKSYNC; id <= ID_TE_ANALYZE_T2000; ++id)
		Bind(wxEVT_MENU, &ATTapeEditorFrame::OnAnalyzeDecoder, this, id);

	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnDelete, this, ID_TE_DELETE);
	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnFilter, this, ID_TE_FILTER_2000);
	Bind(wxEVT_MENU, &ATTapeEditorFrame::OnFilter, this, ID_TE_FILTER_1000);

	// Update UI events for menu enable/check state
	Bind(wxEVT_UPDATE_UI, &ATTapeEditorFrame::OnUpdateUI, this);

	mRefreshTimer.Start(200);
}

ATTapeEditorFrame::~ATTapeEditorFrame() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	cas.TapeDirtyStateChanged -= &mFnOnTapeDirtyStateChanged;
	cas.TapeChanged.Remove(&mFnOnTapeChanged);

	if (g_pTapeEditorFrame == this)
		g_pTapeEditorFrame = nullptr;
}

void ATTapeEditorFrame::UpdateTitle() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	wxString title = "Tape Editor";

	if (cas.IsLoaded()) {
		const wchar_t *path = cas.GetPath();
		if (path && *path) {
			VDStringW name(VDFileSplitPath(path));
			title += " - ";
			if (cas.IsImageDirty())
				title += "*";
			title += wxString(name.c_str());
		} else {
			title += " - (new tape)";
		}
	}

	SetTitle(title);
}

void ATTapeEditorFrame::OnClose(wxCloseEvent& evt) {
	if (evt.CanVeto() && !OKToDiscard()) {
		evt.Veto();
		return;
	}

	mRefreshTimer.Stop();
	mpTapeView->SetCassetteEmulator(nullptr);
	Destroy();
}

void ATTapeEditorFrame::OnTimer(wxTimerEvent&) {
	mpTapeView->UpdateHeadPosition();
	mpTapeView->Refresh(false);
}

bool ATTapeEditorFrame::OKToDiscard() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	if (!cas.IsImageDirty())
		return true;

	int result = wxMessageBox(
		"The current tape has unsaved changes. Discard them?",
		"Tape Editor",
		wxYES_NO | wxICON_WARNING, this);

	return result == wxYES;
}

///////////////////////////////////////////////////////////////////////////
// ATTapeEditorFrame — file operations
///////////////////////////////////////////////////////////////////////////

void ATTapeEditorFrame::OnFileNew(wxCommandEvent&) {
	if (!OKToDiscard())
		return;

	ATCassetteEmulator& cas = g_sim.GetCassette();
	cas.LoadNew();
	mpTapeView->SetImage(cas.GetImage());
	mpTapeView->ClearUndoRedo();
	UpdateTitle();
}

void ATTapeEditorFrame::OnFileOpen(wxCommandEvent&) {
	if (!OKToDiscard())
		return;

	wxFileDialog dlg(this, "Open Tape Image", "", "",
		"Cassette images (*.cas;*.wav)|*.cas;*.wav|All files (*)|*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() == wxID_OK)
		Load(dlg.GetPath());
}

void ATTapeEditorFrame::OnFileReload(wxCommandEvent&) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	const wchar_t *path = cas.GetPath();
	if (!path || !*path)
		return;

	if (!OKToDiscard())
		return;

	Load(wxString(path));
}

void ATTapeEditorFrame::Load(const wxString& path) {
	try {
		ATCassetteEmulator& cas = g_sim.GetCassette();
		cas.Unload();

		ATCassetteLoadContext ctx {};
		cas.GetLoadOptions(ctx);
		ctx.mbStoreWaveform = mpTapeView->GetStoreWaveformOnLoad();

		VDStringW wpath(path.wc_str());
		vdrefptr<IATCassetteImage> image = ATLoadCassetteImage(wpath.c_str(), nullptr, ctx);
		cas.Load(image, wpath.c_str(), true);

		mpTapeView->SetImage(cas.GetImage());
		mpTapeView->ClearUndoRedo();
		UpdateTitle();
	} catch (const std::exception& e) {
		wxMessageBox(wxString::Format("Failed to load tape: %s", e.what()),
			"Error", wxOK | wxICON_ERROR, this);
	}
}

void ATTapeEditorFrame::OnFileSaveAsCAS(wxCommandEvent&) {
	IATCassetteImage *image = mpTapeView->GetImage();
	if (!image) return;

	if (image->HasCASIncompatibleStdBlocks()) {
		int r = wxMessageBox(
			"This tape has standard data blocks that have been trimmed and cannot be exactly "
			"represented in a CAS file. The blocks will be re-encoded as FSK data. Continue?",
			"Save as CAS", wxYES_NO | wxICON_WARNING, this);
		if (r != wxYES) return;
	}

	wxFileDialog dlg(this, "Save Tape as CAS", "", "tape.cas",
		"CAS files (*.cas)|*.cas|All files (*)|*",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK)
		return;

	try {
		VDStringW wpath(dlg.GetPath().wc_str());
		VDFileStream fs(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		ATSaveCassetteImageCAS(fs, image);

		ATCassetteEmulator& cas = g_sim.GetCassette();
		cas.SetImagePersistent(wpath.c_str());
		cas.SetImageClean();
		UpdateTitle();
	} catch (const std::exception& e) {
		wxMessageBox(wxString::Format("Failed to save: %s", e.what()),
			"Error", wxOK | wxICON_ERROR, this);
	}
}

void ATTapeEditorFrame::OnFileSaveAsWAV(wxCommandEvent&) {
	IATCassetteImage *image = mpTapeView->GetImage();
	if (!image) return;

	wxFileDialog dlg(this, "Save Tape as WAV", "", "tape.wav",
		"WAV files (*.wav)|*.wav|All files (*)|*",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK)
		return;

	try {
		VDStringW wpath(dlg.GetPath().wc_str());
		VDFileStream fs(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		ATSaveCassetteImageWAV(fs, image);
	} catch (const std::exception& e) {
		wxMessageBox(wxString::Format("Failed to save: %s", e.what()),
			"Error", wxOK | wxICON_ERROR, this);
	}
}

void ATTapeEditorFrame::OnFileClose(wxCommandEvent&) {
	Close();
}

///////////////////////////////////////////////////////////////////////////
// ATTapeEditorFrame — edit/data/view/monitor/options commands
///////////////////////////////////////////////////////////////////////////

void ATTapeEditorFrame::OnEditCommand(wxCommandEvent& evt) {
	switch (evt.GetId()) {
	case ID_TE_EDIT_UNDO:          mpTapeView->Undo(); break;
	case ID_TE_EDIT_REDO:          mpTapeView->Redo(); break;
	case ID_TE_EDIT_SELECTALL:     mpTapeView->SelectAll(); break;
	case ID_TE_EDIT_DESELECT:      mpTapeView->ClearSelection(); break;
	case ID_TE_EDIT_CUT:           mpTapeView->Cut(); break;
	case ID_TE_EDIT_COPY:          mpTapeView->Copy(); break;
	case ID_TE_EDIT_COPYDECODEDDATA: mpTapeView->CopyDecodedData(); break;
	case ID_TE_EDIT_PASTE:         mpTapeView->Paste(); break;
	case ID_TE_EDIT_DELETE:        mpTapeView->Delete(); break;
	case ID_TE_EDIT_CONVERTTOSTD:  mpTapeView->ConvertToStdBlock(); break;
	case ID_TE_EDIT_CONVERTTORAW:  mpTapeView->ConvertToRawBlock(); break;
	case ID_TE_EDIT_REPEATLAST:    mpTapeView->ReAnalyze(); break;
	case ID_TE_EDIT_REPEATLASTFLIP: mpTapeView->ReAnalyzeFlip(); break;
	}
	UpdateTitle();
}

void ATTapeEditorFrame::OnDataExtractCFile(wxCommandEvent&) {
	if (!mpTapeView->HasNonEmptySelection())
		return;

	vdfastvector<uint8> data;
	mpTapeView->ExtractSelectionAsCFile(data);

	if (data.empty()) {
		wxMessageBox("No data decoded from selection.", "Extract C File", wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxFileDialog dlg(this, "Extract as C File", "", "tape_data.c",
		"C files (*.c)|*.c|All files (*)|*",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK)
		return;

	try {
		FILE *f = fopen(dlg.GetPath().utf8_str(), "w");
		if (!f) throw std::runtime_error("Cannot open file");

		fprintf(f, "// Extracted tape data (%u bytes)\n", (unsigned)data.size());
		fprintf(f, "static const unsigned char tape_data[] = {\n");
		for (size_t i = 0; i < data.size(); ++i) {
			if (i % 16 == 0) fprintf(f, "\t");
			fprintf(f, "0x%02X", data[i]);
			if (i + 1 < data.size()) fprintf(f, ",");
			if (i % 16 == 15 || i + 1 == data.size()) fprintf(f, "\n");
		}
		fprintf(f, "};\n");
		fclose(f);
	} catch (const std::exception& e) {
		wxMessageBox(wxString::Format("Failed to write: %s", e.what()),
			"Error", wxOK | wxICON_ERROR, this);
	}
}

void ATTapeEditorFrame::OnViewCommand(wxCommandEvent& evt) {
	switch (evt.GetId()) {
	case ID_TE_VIEW_NOSIGNAL:
		mpTapeView->SetWaveformMode(WaveformMode::None);
		break;
	case ID_TE_VIEW_WAVEFORM:
		mpTapeView->SetWaveformMode(WaveformMode::Waveform);
		break;
	case ID_TE_VIEW_SPECTROGRAM:
		mpTapeView->SetWaveformMode(WaveformMode::Spectrogram);
		break;
	case ID_TE_VIEW_FREQGUIDELINES:
		mpTapeView->SetFrequencyGuidelinesEnabled(!mpTapeView->GetFrequencyGuidelinesEnabled());
		break;
	case ID_TE_VIEW_FSKDATA:
		// FSK data is always shown; this is informational
		break;
	case ID_TE_VIEW_TURBODATA:
		mpTapeView->SetShowTurboData(!mpTapeView->GetShowTurboData());
		break;
	}
}

void ATTapeEditorFrame::OnMonitorCaptureSIO(wxCommandEvent&) {
	mpTapeView->SetSIOMonitorEnabled(!mpTapeView->GetSIOMonitorEnabled());
}

void ATTapeEditorFrame::OnOptionsStoreWaveform(wxCommandEvent&) {
	mpTapeView->SetStoreWaveformOnLoad(!mpTapeView->GetStoreWaveformOnLoad());
}

void ATTapeEditorFrame::OnModeCommand(wxCommandEvent& evt) {
	switch (evt.GetId()) {
	case ID_TE_MODE_SCROLL:  mpTapeView->SetDrawMode(DrawMode::Scroll); break;
	case ID_TE_MODE_SELECT:  mpTapeView->SetDrawMode(DrawMode::Select); break;
	case ID_TE_MODE_DRAW:    mpTapeView->SetDrawMode(DrawMode::Draw); break;
	case ID_TE_MODE_INSERT:  mpTapeView->SetDrawMode(DrawMode::Insert); break;
	case ID_TE_MODE_ANALYZE: mpTapeView->SetDrawMode(DrawMode::Analyze); break;
	}
}

void ATTapeEditorFrame::OnAnalyzeDecoder(wxCommandEvent& evt) {
	switch (evt.GetId()) {
	case ID_TE_ANALYZE_FSKSYNC: mpTapeView->SetAnalysisDecoder(Decoder::FSK_Sync); break;
	case ID_TE_ANALYZE_FSKPLL:  mpTapeView->SetAnalysisDecoder(Decoder::FSK_PLL); break;
	case ID_TE_ANALYZE_T2000:   mpTapeView->SetAnalysisDecoder(Decoder::T2000); break;
	}
}

void ATTapeEditorFrame::OnDelete(wxCommandEvent&) {
	mpTapeView->Delete();
	UpdateTitle();
}

void ATTapeEditorFrame::OnFilter(wxCommandEvent& evt) {
	if (evt.GetId() == ID_TE_FILTER_2000)
		mpTapeView->Filter(FilterMode::FSKDirectSample2000Baud);
	else
		mpTapeView->Filter(FilterMode::FSKDirectSample1000Baud);
	UpdateTitle();
}

void ATTapeEditorFrame::OnUpdateUI(wxUpdateUIEvent& evt) {
	int id = evt.GetId();
	switch (id) {
	case ID_TE_EDIT_UNDO:          evt.Enable(mpTapeView->CanUndo()); break;
	case ID_TE_EDIT_REDO:          evt.Enable(mpTapeView->CanRedo()); break;
	case ID_TE_EDIT_CUT:
	case ID_TE_EDIT_COPY:
	case ID_TE_EDIT_DELETE:
	case ID_TE_EDIT_CONVERTTOSTD:
	case ID_TE_EDIT_CONVERTTORAW:
	case ID_TE_DATA_EXTRACTCFILE:
	case ID_TE_DELETE:
		evt.Enable(mpTapeView->HasNonEmptySelection());
		break;
	case ID_TE_EDIT_PASTE:
		evt.Enable(mpTapeView->HasClip());
		break;
	case ID_TE_EDIT_COPYDECODEDDATA:
		evt.Enable(mpTapeView->HasDecodedData());
		break;
	case ID_TE_VIEW_FREQGUIDELINES:
		evt.Check(mpTapeView->GetFrequencyGuidelinesEnabled());
		break;
	case ID_TE_VIEW_TURBODATA:
		evt.Check(mpTapeView->GetShowTurboData());
		break;
	case ID_TE_MONITOR_CAPTURESIO:
		evt.Check(mpTapeView->GetSIOMonitorEnabled());
		break;
	case ID_TE_OPTIONS_STOREWAVEFORM:
		evt.Check(mpTapeView->GetStoreWaveformOnLoad());
		break;
	case ID_TE_VIEW_NOSIGNAL:
		evt.Check(mpTapeView->GetWaveformMode() == WaveformMode::None);
		break;
	case ID_TE_VIEW_WAVEFORM:
		evt.Check(mpTapeView->GetWaveformMode() == WaveformMode::Waveform);
		break;
	case ID_TE_VIEW_SPECTROGRAM:
		evt.Check(mpTapeView->GetWaveformMode() == WaveformMode::Spectrogram);
		break;
	}
}

void ATTapeEditorFrame::UpdateStatusMessage() {
	DrawMode mode = mpTapeView->GetDrawMode();
	wxString msg;

	if (mpTapeView->HasNonEmptySelection()) {
		uint32 s = mpTapeView->GetSelectionSortedStart();
		uint32 e = mpTapeView->GetSelectionSortedEnd();
		float t0 = (float)s * kATCassetteSecondsPerDataSample;
		float t1 = (float)e * kATCassetteSecondsPerDataSample;
		msg.Printf("Selected %.3fs in range %.3fs-%.3fs | %u sample(s) in %u-%u",
			t1 - t0, t0, t1, e - s, s, e);
	} else {
		switch (mode) {
		case DrawMode::Scroll:
			msg = "Scroll tool: left-drag to scroll, mouse wheel to zoom";
			break;
		case DrawMode::Select:
			msg = "Select tool: left-drag to select a region of bits";
			break;
		case DrawMode::Draw:
			msg = "Draw tool: left-click or drag to set or reset bits";
			break;
		case DrawMode::Insert:
			msg = "Insert tool: left-drag to the right to insert blank tape";
			break;
		case DrawMode::Analyze:
			msg = "Analyze tool: left-drag over range to decode as standard bytes";
			break;
		}
	}

	SetStatusText(msg);
}

void ATTapeEditorFrame::UpdateModeButtons() {
	DrawMode mode = mpTapeView->GetDrawMode();
	mpToolbar->ToggleTool(ID_TE_MODE_SCROLL, mode == DrawMode::Scroll);
	mpToolbar->ToggleTool(ID_TE_MODE_SELECT, mode == DrawMode::Select);
	mpToolbar->ToggleTool(ID_TE_MODE_DRAW, mode == DrawMode::Draw);
	mpToolbar->ToggleTool(ID_TE_MODE_INSERT, mode == DrawMode::Insert);
	mpToolbar->ToggleTool(ID_TE_MODE_ANALYZE, mode == DrawMode::Analyze);
}

///////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////

void ATShowTapeEditorDialog(wxWindow *parent) {
	if (g_pTapeEditorFrame) {
		g_pTapeEditorFrame->Raise();
		return;
	}

	g_pTapeEditorFrame = new ATTapeEditorFrame(parent);
	g_pTapeEditorFrame->Show();
}

void ATCloseTapeEditorDialog() {
	if (g_pTapeEditorFrame) {
		g_pTapeEditorFrame->Destroy();
		g_pTapeEditorFrame = nullptr;
	}
}

// (implementation above)
