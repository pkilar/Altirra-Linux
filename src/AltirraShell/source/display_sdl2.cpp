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

#include <display_sdl2.h>
#include <cstring>

ATDisplaySDL2::ATDisplaySDL2() {
}

ATDisplaySDL2::~ATDisplaySDL2() {
	Shutdown();
}

bool ATDisplaySDL2::Init(SDL_Window *window, SDL_GLContext glContext) {
	mpWindow = window;
	mGLContext = glContext;

	glGenTextures(1, &mTexture);
	glBindTexture(GL_TEXTURE_2D, mTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	return true;
}

void ATDisplaySDL2::Shutdown() {
	if (mTexture) {
		glDeleteTextures(1, &mTexture);
		mTexture = 0;
	}
	mpWindow = nullptr;
	mGLContext = nullptr;
}

void ATDisplaySDL2::PresentFrame() {
	RenderFrame();
	if (mpWindow)
		SDL_GL_SwapWindow(mpWindow);
}

void ATDisplaySDL2::RenderFrame() {
	if (!mpWindow || !mTexture)
		return;

	if (mFrameReady) {
		UploadTexture();
		mFrameReady = false;
	}

	if (mSourceW > 0 && mSourceH > 0)
		RenderQuad();
}

void ATDisplaySDL2::GetWindowSize(int& w, int& h) const {
	if (mpWindow)
		SDL_GetWindowSize(mpWindow, &w, &h);
	else {
		w = 0;
		h = 0;
	}
}

void ATDisplaySDL2::UploadTexture() {
	VDCriticalSection::AutoLock lock(mStagingLock);

	int w = mStagingBuffer.w;
	int h = mStagingBuffer.h;
	const void *data = mStagingBuffer.data;
	vdpixoffset pitch = mStagingBuffer.pitch;

	if (w <= 0 || h <= 0 || !data)
		return;

	// Need to reallocate texture if size changed
	if (w != mTexWidth || h != mTexHeight) {
		mTexWidth = w;
		mTexHeight = h;

		glBindTexture(GL_TEXTURE_2D, mTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
			GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
	} else {
		glBindTexture(GL_TEXTURE_2D, mTexture);
	}

	// Handle non-contiguous pitches
	glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
		GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void ATDisplaySDL2::RenderQuad() {
	int winW, winH;
	GetWindowSize(winW, winH);
	if (winW <= 0 || winH <= 0)
		return;

	// Calculate aspect-correct destination rect
	// Atari pixel aspect ratio: approximately 1.0 for NTSC (depending on mode)
	// The source already accounts for PAR, so we just fit to window
	float srcAspect = (float)mSourceW / (float)mSourceH;
	float winAspect = (float)winW / (float)winH;

	float destX0, destY0, destX1, destY1;
	if (srcAspect > winAspect) {
		// Source is wider — letterbox top/bottom
		float scale = (float)winW / (float)mSourceW;
		float destH = mSourceH * scale;
		destX0 = 0;
		destX1 = (float)winW;
		destY0 = ((float)winH - destH) * 0.5f;
		destY1 = destY0 + destH;
	} else {
		// Source is taller — pillarbox left/right
		float scale = (float)winH / (float)mSourceH;
		float destW = mSourceW * scale;
		destX0 = ((float)winW - destW) * 0.5f;
		destX1 = destX0 + destW;
		destY0 = 0;
		destY1 = (float)winH;
	}

	// Set up orthographic projection
	glViewport(0, 0, winW, winH);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, winW, winH, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Clear with background color
	float bgR = ((mBackgroundColor >> 16) & 0xFF) / 255.0f;
	float bgG = ((mBackgroundColor >> 8) & 0xFF) / 255.0f;
	float bgB = (mBackgroundColor & 0xFF) / 255.0f;
	glClearColor(bgR, bgG, bgB, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Draw textured quad
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

void ATDisplaySDL2::Destroy() {
	Shutdown();
}

void ATDisplaySDL2::Reset() {
	VDCriticalSection::AutoLock lock(mStagingLock);
	mStagingBuffer.clear();
	mSourceW = 0;
	mSourceH = 0;
	mFrameReady = false;
}

void ATDisplaySDL2::SetSourceMessage(const wchar_t *msg) {
	// Stub — message overlay not implemented yet
}

bool ATDisplaySDL2::SetSource(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion) {
	return SetSourcePersistent(bAutoUpdate, src, bAllowConversion, nullptr, nullptr);
}

bool ATDisplaySDL2::SetSourcePersistent(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion,
	const VDVideoDisplayScreenFXInfo *screenFX, IVDVideoDisplayScreenFXEngine *screenFXEngine) {
	VDCriticalSection::AutoLock lock(mStagingLock);

	// Reallocate staging buffer if needed
	if (mStagingBuffer.w != src.w || mStagingBuffer.h != src.h ||
		mStagingBuffer.format != nsVDPixmap::kPixFormat_XRGB8888) {
		mStagingBuffer.init(src.w, src.h, nsVDPixmap::kPixFormat_XRGB8888);
	}

	// Copy pixel data row by row (handles different pitches)
	const uint8 *srcRow = (const uint8 *)src.data;
	uint8 *dstRow = (uint8 *)mStagingBuffer.data;
	const int copyBytes = src.w * 4;

	for (int y = 0; y < src.h; ++y) {
		memcpy(dstRow, srcRow, copyBytes);
		srcRow += src.pitch;
		dstRow += mStagingBuffer.pitch;
	}

	mSourceW = src.w;
	mSourceH = src.h;
	mFrameReady = true;

	return true;
}

void ATDisplaySDL2::SetSourceSubrect(const vdrect32 *r) {
	// Stub — subrect not supported yet
}

void ATDisplaySDL2::SetSourceSolidColor(uint32 color) {
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

void ATDisplaySDL2::SetReturnFocus(bool enable) {}
void ATDisplaySDL2::SetTouchEnabled(bool enable) {}
void ATDisplaySDL2::SetUse16Bit(bool enable) {}
void ATDisplaySDL2::SetHDREnabled(bool hdr) {}

void ATDisplaySDL2::SetFullScreen(bool fs, uint32 width, uint32 height, uint32 refresh) {
	mFullScreen = fs;
	if (mpWindow) {
		if (fs)
			SDL_SetWindowFullscreen(mpWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
		else
			SDL_SetWindowFullscreen(mpWindow, 0);
	}
}

void ATDisplaySDL2::SetCustomDesiredRefreshRate(float hz, float hzmin, float hzmax) {}

void ATDisplaySDL2::SetDestRect(const vdrect32 *r, uint32 backgroundColor) {
	mBackgroundColor = backgroundColor;
}

void ATDisplaySDL2::SetDestRectF(const vdrect32f *r, uint32 backgroundColor) {
	mBackgroundColor = backgroundColor;
}

void ATDisplaySDL2::SetPixelSharpness(float xfactor, float yfactor) {}
void ATDisplaySDL2::SetCompositor(IVDDisplayCompositor *compositor) {}
void ATDisplaySDL2::SetSDRBrightness(float nits) {}

void ATDisplaySDL2::PostBuffer(VDVideoDisplayFrame *frame) {
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
		const int copyBytes = src.w * 4;

		for (int y = 0; y < src.h; ++y) {
			memcpy(dstRow, srcRow, copyBytes);
			srcRow += src.pitch;
			dstRow += mStagingBuffer.pitch;
		}

		mSourceW = src.w;
		mSourceH = src.h;
		mFrameReady = true;
	}

	frame->Release();

	if (mOnFrameStatusUpdated)
		mOnFrameStatusUpdated(0);
}

bool ATDisplaySDL2::RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) {
	if (ppFrame)
		*ppFrame = nullptr;
	return false;
}

void ATDisplaySDL2::FlushBuffers() {
	VDCriticalSection::AutoLock lock(mStagingLock);
	mFrameReady = false;
}

void ATDisplaySDL2::Invalidate() {
	// Mark for redraw on next PresentFrame
}

void ATDisplaySDL2::Update(int mode) {
	// Immediate update — the main loop calls PresentFrame regularly
}

void ATDisplaySDL2::Cache() {}

void ATDisplaySDL2::SetCallback(IVDVideoDisplayCallback *p) {
	mpCallback = p;
}

void ATDisplaySDL2::SetOnFrameStatusUpdated(vdfunction<void(int)> fn) {
	mOnFrameStatusUpdated = std::move(fn);
}

void ATDisplaySDL2::SetAccelerationMode(AccelerationMode mode) {}

IVDVideoDisplay::FilterMode ATDisplaySDL2::GetFilterMode() {
	return mFilterMode;
}

void ATDisplaySDL2::SetFilterMode(FilterMode mode) {
	mFilterMode = mode;

	if (mTexture) {
		glBindTexture(GL_TEXTURE_2D, mTexture);
		GLint filter = (mode == kFilterPoint) ? GL_NEAREST : GL_LINEAR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

float ATDisplaySDL2::GetSyncDelta() const {
	return 0.0f;
}

int ATDisplaySDL2::GetQueuedFrames() const {
	return mFrameReady ? 1 : 0;
}

bool ATDisplaySDL2::IsFramePending() const {
	return mFrameReady;
}

VDDVSyncStatus ATDisplaySDL2::GetVSyncStatus() const {
	VDDVSyncStatus status;
	// Query actual display refresh rate from SDL
	SDL_DisplayMode mode;
	if (mpWindow && SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(mpWindow), &mode) == 0) {
		status.mRefreshRate = (float)mode.refresh_rate;
	}
	return status;
}

vdrect32 ATDisplaySDL2::GetMonitorRect() {
	vdrect32 r;
	if (mpWindow) {
		int idx = SDL_GetWindowDisplayIndex(mpWindow);
		SDL_Rect bounds;
		if (SDL_GetDisplayBounds(idx, &bounds) == 0) {
			r.left = bounds.x;
			r.top = bounds.y;
			r.right = bounds.x + bounds.w;
			r.bottom = bounds.y + bounds.h;
			return r;
		}
	}
	r.left = 0;
	r.top = 0;
	r.right = 1920;
	r.bottom = 1080;
	return r;
}

bool ATDisplaySDL2::IsScreenFXPreferred() const {
	return false;
}

VDDHDRAvailability ATDisplaySDL2::IsHDRCapable() const {
	return VDDHDRAvailability::NoMinidriverSupport;
}

bool ATDisplaySDL2::MapNormSourcePtToDest(vdfloat2& pt) const {
	return true;
}

bool ATDisplaySDL2::MapNormDestPtToSource(vdfloat2& pt) const {
	return true;
}

void ATDisplaySDL2::SetProfileHook(const vdfunction<void(ProfileEvent, uintptr)>& profileHook) {}

void ATDisplaySDL2::RequestCapture(vdfunction<void(const VDPixmap *)> fn) {
	if (fn) {
		VDCriticalSection::AutoLock lock(mStagingLock);
		if (mStagingBuffer.w > 0 && mStagingBuffer.h > 0) {
			fn(&mStagingBuffer);
		} else {
			fn(nullptr);
		}
	}
}
