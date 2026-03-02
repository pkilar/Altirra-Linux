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
#include <display_wx.h>
#include <wx/dcclient.h>
#include <wx/frame.h>
#include <vd2/system/text.h>
#include <algorithm>
#include <cmath>
#include <cstring>

///////////////////////////////////////////////////////////////////////////
// ATDisplayCanvas — wxGLCanvas subclass
///////////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ATDisplayCanvas, wxGLCanvas)
	EVT_PAINT(ATDisplayCanvas::OnPaint)
	EVT_SIZE(ATDisplayCanvas::OnSize)
wxEND_EVENT_TABLE()

wxGLAttributes ATDisplayCanvas::GetGLAttribs() {
	wxGLAttributes attrs;
	attrs.PlatformDefaults().RGBA().DoubleBuffer().Depth(16).EndList();
	return attrs;
}

ATDisplayCanvas::ATDisplayCanvas(wxWindow *parent)
	: wxGLCanvas(parent, GetGLAttribs(), wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
{
	// Ensure the canvas can receive keyboard focus
	SetCanFocus(true);
}

bool ATDisplayCanvas::InitGL() {
	wxGLContextAttrs ctxAttrs;
	ctxAttrs.CompatibilityProfile().OGLVersion(2, 1).EndList();

	mpGLContext = new wxGLContext(this, nullptr, &ctxAttrs);
	if (!mpGLContext->IsOK()) {
		delete mpGLContext;
		mpGLContext = nullptr;
		return false;
	}

	return true;
}

void ATDisplayCanvas::ShutdownGL() {
	delete mpGLContext;
	mpGLContext = nullptr;
}

void ATDisplayCanvas::OnPaint(wxPaintEvent&) {
	// wxPaintDC is required even if we don't use it, to validate the window
	wxPaintDC dc(this);

	// On GTK/X11, GL content may not be visible unless rendered within
	// a paint handler. Render the current frame so the display stays
	// up-to-date after expose/resize events.
	if (mpDisplay)
		mpDisplay->PresentFrame();
}

void ATDisplayCanvas::OnSize(wxSizeEvent& evt) {
	Refresh(false);
	evt.Skip();
}

///////////////////////////////////////////////////////////////////////////
// ATDisplayWx — IVDVideoDisplay implementation
///////////////////////////////////////////////////////////////////////////

ATDisplayWx::ATDisplayWx(ATDisplayCanvas *canvas)
	: mpCanvas(canvas)
{
	wxGLContext *ctx = canvas->GetGLContext();
	if (ctx) {
		canvas->SetCurrent(*ctx);

		glGenTextures(1, &mTexture);
		glBindTexture(GL_TEXTURE_2D, mTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

ATDisplayWx::~ATDisplayWx() {
	if (mTexture && mpCanvas && mpCanvas->GetGLContext()) {
		mpCanvas->SetCurrent(*mpCanvas->GetGLContext());
		glDeleteTextures(1, &mTexture);
		mTexture = 0;
	}
}

void ATDisplayWx::PresentFrame() {
	RenderFrame();
	if (mpCanvas)
		mpCanvas->SwapBuffers();
}

void ATDisplayWx::RenderFrame() {
	if (!mpCanvas || !mpCanvas->GetGLContext() || !mTexture)
		return;

	mpCanvas->SetCurrent(*mpCanvas->GetGLContext());

	if (mFrameReady) {
		UploadTexture();
		mFrameReady = false;
	}

	if (mSourceW > 0 && mSourceH > 0)
		RenderQuad();
}

void ATDisplayWx::GetWindowSize(int& w, int& h) const {
	if (!mpCanvas) {
		w = 0;
		h = 0;
		return;
	}
	wxSize sz = mpCanvas->GetClientSize();
	double scale = mpCanvas->GetContentScaleFactor();
	w = (int)(sz.GetWidth() * scale);
	h = (int)(sz.GetHeight() * scale);
}

void ATDisplayWx::UploadTexture() {
	VDCriticalSection::AutoLock lock(mStagingLock);

	int w = mStagingBuffer.w;
	int h = mStagingBuffer.h;
	const void *data = mStagingBuffer.data;
	vdpixoffset pitch = mStagingBuffer.pitch;

	if (w <= 0 || h <= 0 || !data)
		return;

	if (w != mTexWidth || h != mTexHeight) {
		mTexWidth = w;
		mTexHeight = h;

		glBindTexture(GL_TEXTURE_2D, mTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
			GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
	} else {
		glBindTexture(GL_TEXTURE_2D, mTexture);
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
		GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void ATDisplayWx::RenderQuad() {
	int winW, winH;
	GetWindowSize(winW, winH);
	if (winW <= 0 || winH <= 0)
		return;

	int availH = winH - mBottomMargin;
	if (availH <= 0)
		availH = winH;

	float destX0, destY0, destX1, destY1;

	if (mStretchMode == kATDisplayStretchMode_Unconstrained) {
		destX0 = 0;
		destY0 = 0;
		destX1 = (float)winW;
		destY1 = (float)availH;
	} else if (mStretchMode == kATDisplayStretchMode_SquarePixels
		|| mStretchMode == kATDisplayStretchMode_Integral) {
		float srcW = (float)mSourceW;
		float srcH = (float)mSourceH;

		if (mStretchMode == kATDisplayStretchMode_Integral) {
			int scale = std::min(winW / mSourceW, availH / mSourceH);
			if (scale < 1) scale = 1;
			srcW *= scale;
			srcH *= scale;
		}

		destX0 = ((float)winW - srcW) * 0.5f;
		destY0 = ((float)availH - srcH) * 0.5f;
		destX1 = destX0 + srcW;
		destY1 = destY0 + srcH;
	} else {
		float fsw = (float)mSourceW * (float)mPixelAspectRatio;
		float fsh = (float)mSourceH;
		float zoom = std::min((float)winW / fsw, (float)availH / fsh);

		if (mStretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio && zoom > 1.0f)
			zoom = floorf(zoom * 1.0001f);

		float destW = fsw * zoom;
		float destH = fsh * zoom;
		destX0 = ((float)winW - destW) * 0.5f;
		destY0 = ((float)availH - destH) * 0.5f;
		destX1 = destX0 + destW;
		destY1 = destY0 + destH;
	}

	glViewport(0, 0, winW, winH);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, winW, winH, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	float bgR = ((mBackgroundColor >> 16) & 0xFF) / 255.0f;
	float bgG = ((mBackgroundColor >> 8) & 0xFF) / 255.0f;
	float bgB = (mBackgroundColor & 0xFF) / 255.0f;
	glClearColor(bgR, bgG, bgB, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, mTexture);

	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(destX0, destY0);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(destX1, destY0);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(destX1, destY1);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(destX0, destY1);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
}

// IVDVideoDisplay implementation

void ATDisplayWx::Destroy() {
	if (mTexture && mpCanvas && mpCanvas->GetGLContext()) {
		mpCanvas->SetCurrent(*mpCanvas->GetGLContext());
		glDeleteTextures(1, &mTexture);
		mTexture = 0;
	}
}

void ATDisplayWx::Reset() {
	VDCriticalSection::AutoLock lock(mStagingLock);
	mStagingBuffer.clear();
	mSourceW = 0;
	mSourceH = 0;
	mFrameReady = false;
}

void ATDisplayWx::SetSourceMessage(const wchar_t *msg) {
	if (msg)
		mSourceMessage = VDTextWToU8(VDStringW(msg));
	else
		mSourceMessage.clear();
}

bool ATDisplayWx::SetSource(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion) {
	return SetSourcePersistent(bAutoUpdate, src, bAllowConversion, nullptr, nullptr);
}

bool ATDisplayWx::SetSourcePersistent(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion,
	const VDVideoDisplayScreenFXInfo *screenFX, IVDVideoDisplayScreenFXEngine *screenFXEngine) {
	VDCriticalSection::AutoLock lock(mStagingLock);

	if (mStagingBuffer.w != src.w || mStagingBuffer.h != src.h ||
		mStagingBuffer.format != nsVDPixmap::kPixFormat_XRGB8888) {
		mStagingBuffer.init(src.w, src.h, nsVDPixmap::kPixFormat_XRGB8888);
	}

	const uint8 *srcRow = (const uint8 *)src.data;
	uint8 *dstRow = (uint8 *)mStagingBuffer.data;

	if (src.format == nsVDPixmap::kPixFormat_Pal8 && src.palette) {
		for (int y = 0; y < src.h; ++y) {
			uint32 *dst32 = (uint32 *)dstRow;
			for (int x = 0; x < src.w; ++x)
				dst32[x] = src.palette[srcRow[x]];
			srcRow += src.pitch;
			dstRow += mStagingBuffer.pitch;
		}
	} else {
		const int copyBytes = src.w * 4;
		for (int y = 0; y < src.h; ++y) {
			memcpy(dstRow, srcRow, copyBytes);
			srcRow += src.pitch;
			dstRow += mStagingBuffer.pitch;
		}
	}

	mSourceW = src.w;
	mSourceH = src.h;
	mFrameReady = true;

	return true;
}

void ATDisplayWx::SetSourceSubrect(const vdrect32 *) {}

void ATDisplayWx::SetSourceSolidColor(uint32 color) {
	VDCriticalSection::AutoLock lock(mStagingLock);

	if (mStagingBuffer.w > 0 && mStagingBuffer.h > 0) {
		uint32 *row = (uint32 *)mStagingBuffer.data;
		for (int y = 0; y < mStagingBuffer.h; ++y) {
			for (int x = 0; x < mStagingBuffer.w; ++x)
				row[x] = color;
			row = (uint32 *)((uint8 *)row + mStagingBuffer.pitch);
		}
		mFrameReady = true;
	}
}

void ATDisplayWx::SetReturnFocus(bool) {}
void ATDisplayWx::SetTouchEnabled(bool) {}
void ATDisplayWx::SetUse16Bit(bool) {}
void ATDisplayWx::SetHDREnabled(bool) {}

void ATDisplayWx::SetFullScreen(bool fs, uint32, uint32, uint32) {
	if (mpCanvas) {
		wxFrame *frame = wxDynamicCast(mpCanvas->GetParent(), wxFrame);
		if (frame)
			frame->ShowFullScreen(fs);
	}
}

void ATDisplayWx::SetCustomDesiredRefreshRate(float, float, float) {}

void ATDisplayWx::SetDestRect(const vdrect32 *, uint32 backgroundColor) {
	mBackgroundColor = backgroundColor;
}

void ATDisplayWx::SetDestRectF(const vdrect32f *, uint32 backgroundColor) {
	mBackgroundColor = backgroundColor;
}

void ATDisplayWx::SetPixelSharpness(float, float) {}
void ATDisplayWx::SetCompositor(IVDDisplayCompositor *) {}
void ATDisplayWx::SetSDRBrightness(float) {}

void ATDisplayWx::PostBuffer(VDVideoDisplayFrame *frame) {
	if (!frame)
		return;

	frame->AddRef();

	{
		VDCriticalSection::AutoLock lock(mStagingLock);

		const VDPixmap& src = frame->mPixmap;

		if (mStagingBuffer.w != src.w || mStagingBuffer.h != src.h ||
			mStagingBuffer.format != nsVDPixmap::kPixFormat_XRGB8888) {
			mStagingBuffer.init(src.w, src.h, nsVDPixmap::kPixFormat_XRGB8888);
		}

		const uint8 *srcRow = (const uint8 *)src.data;
		uint8 *dstRow = (uint8 *)mStagingBuffer.data;

		if (src.format == nsVDPixmap::kPixFormat_Pal8 && src.palette) {
			for (int y = 0; y < src.h; ++y) {
				uint32 *dst32 = (uint32 *)dstRow;
				for (int x = 0; x < src.w; ++x)
					dst32[x] = src.palette[srcRow[x]];
				srcRow += src.pitch;
				dstRow += mStagingBuffer.pitch;
			}
		} else {
			const int copyBytes = src.w * 4;
			for (int y = 0; y < src.h; ++y) {
				memcpy(dstRow, srcRow, copyBytes);
				srcRow += src.pitch;
				dstRow += mStagingBuffer.pitch;
			}
		}

		mSourceW = src.w;
		mSourceH = src.h;
		mFrameReady = true;
	}

	frame->Release();

	if (mOnFrameStatusUpdated)
		mOnFrameStatusUpdated(0);
}

bool ATDisplayWx::RevokeBuffer(bool, VDVideoDisplayFrame **ppFrame) {
	if (ppFrame)
		*ppFrame = nullptr;
	return false;
}

void ATDisplayWx::FlushBuffers() {
	VDCriticalSection::AutoLock lock(mStagingLock);
	mFrameReady = false;
}

void ATDisplayWx::Invalidate() {
	if (mpCanvas)
		mpCanvas->CallAfter([this] { mpCanvas->Refresh(false); });
}

void ATDisplayWx::Update(int) {}

void ATDisplayWx::Cache() {}

void ATDisplayWx::SetCallback(IVDVideoDisplayCallback *p) {
	mpCallback = p;
}

void ATDisplayWx::SetOnFrameStatusUpdated(vdfunction<void(int)> fn) {
	mOnFrameStatusUpdated = std::move(fn);
}

void ATDisplayWx::SetAccelerationMode(AccelerationMode) {}

IVDVideoDisplay::FilterMode ATDisplayWx::GetFilterMode() {
	return mFilterMode;
}

void ATDisplayWx::SetFilterMode(FilterMode mode) {
	mFilterMode = mode;

	if (mTexture && mpCanvas && mpCanvas->GetGLContext()) {
		mpCanvas->SetCurrent(*mpCanvas->GetGLContext());
		glBindTexture(GL_TEXTURE_2D, mTexture);
		GLint filter = (mode == kFilterPoint) ? GL_NEAREST : GL_LINEAR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

float ATDisplayWx::GetSyncDelta() const {
	return 0.0f;
}

int ATDisplayWx::GetQueuedFrames() const {
	return mFrameReady ? 1 : 0;
}

bool ATDisplayWx::IsFramePending() const {
	return mFrameReady;
}

VDDVSyncStatus ATDisplayWx::GetVSyncStatus() const {
	return {};
}

vdrect32 ATDisplayWx::GetMonitorRect() {
	if (mpCanvas) {
		wxDisplay display(wxDisplay::GetFromWindow(mpCanvas));
		wxRect geom = display.GetGeometry();
		vdrect32 r;
		r.left = geom.GetLeft();
		r.top = geom.GetTop();
		r.right = geom.GetRight();
		r.bottom = geom.GetBottom();
		return r;
	}
	vdrect32 r;
	r.left = 0;
	r.top = 0;
	r.right = 1920;
	r.bottom = 1080;
	return r;
}

bool ATDisplayWx::IsScreenFXPreferred() const {
	return false;
}

VDDHDRAvailability ATDisplayWx::IsHDRCapable() const {
	return VDDHDRAvailability::NoMinidriverSupport;
}

bool ATDisplayWx::MapNormSourcePtToDest(vdfloat2&) const {
	return true;
}

bool ATDisplayWx::MapNormDestPtToSource(vdfloat2&) const {
	return true;
}

void ATDisplayWx::SetProfileHook(const vdfunction<void(ProfileEvent, uintptr)>&) {}

void ATDisplayWx::RequestCapture(vdfunction<void(const VDPixmap *)> fn) {
	if (fn) {
		VDCriticalSection::AutoLock lock(mStagingLock);
		if (mStagingBuffer.w > 0 && mStagingBuffer.h > 0) {
			fn(&mStagingBuffer);
		} else {
			fn(nullptr);
		}
	}
}
