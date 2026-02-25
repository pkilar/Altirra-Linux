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

#ifndef AT_DISPLAY_SDL2_H
#define AT_DISPLAY_SDL2_H

#include <vd2/system/thread.h>
#include <vd2/system/atomic.h>
#include <vd2/system/VDString.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDDisplay/display.h>
#include "uitypes.h"
#include <SDL.h>
#include <GL/gl.h>

class ATDisplaySDL2 final : public IVDVideoDisplay {
public:
	ATDisplaySDL2();
	~ATDisplaySDL2();

	bool Init(SDL_Window *window, SDL_GLContext glContext);
	void Shutdown();

	// Called from main loop — uploads new frame to GL texture and renders
	void PresentFrame();

	// Split render path: upload texture + draw quad without swapping.
	// Used when main loop needs to composite ImGui on top before swap.
	void RenderFrame();

	// Get drawable dimensions in physical pixels (HiDPI-aware)
	void GetWindowSize(int& w, int& h) const;

	// IVDVideoDisplay
	void Destroy() override;
	void Reset() override;
	void SetSourceMessage(const wchar_t *msg) override;
	bool SetSource(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion) override;
	bool SetSourcePersistent(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion, const VDVideoDisplayScreenFXInfo *screenFX, IVDVideoDisplayScreenFXEngine *screenFXEngine) override;
	void SetSourceSubrect(const vdrect32 *r) override;
	void SetSourceSolidColor(uint32 color) override;

	void SetReturnFocus(bool enable) override;
	void SetTouchEnabled(bool enable) override;
	void SetUse16Bit(bool enable) override;
	void SetHDREnabled(bool hdr) override;

	void SetFullScreen(bool fs, uint32 width, uint32 height, uint32 refresh) override;
	void SetCustomDesiredRefreshRate(float hz, float hzmin, float hzmax) override;
	void SetDestRect(const vdrect32 *r, uint32 backgroundColor) override;
	void SetDestRectF(const vdrect32f *r, uint32 backgroundColor) override;
	void SetPixelSharpness(float xfactor, float yfactor) override;
	void SetCompositor(IVDDisplayCompositor *compositor) override;
	void SetSDRBrightness(float nits) override;

	void PostBuffer(VDVideoDisplayFrame *) override;
	bool RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) override;
	void FlushBuffers() override;

	void Invalidate() override;
	void Update(int mode) override;
	void Cache() override;
	void SetCallback(IVDVideoDisplayCallback *p) override;
	void SetOnFrameStatusUpdated(vdfunction<void(int)> fn) override;

	void SetAccelerationMode(AccelerationMode mode) override;

	FilterMode GetFilterMode() override;
	void SetFilterMode(FilterMode) override;

	ATDisplayStretchMode GetStretchMode() const { return mStretchMode; }
	void SetStretchMode(ATDisplayStretchMode mode) { mStretchMode = mode; }
	void SetPixelAspectRatio(double par) { mPixelAspectRatio = par; }
	void SetBottomMargin(int pixels) { mBottomMargin = pixels; }
	const char *GetSourceMessage() const { return mSourceMessage.empty() ? nullptr : mSourceMessage.c_str(); }
	float GetSyncDelta() const override;

	int GetQueuedFrames() const override;
	bool IsFramePending() const override;
	VDDVSyncStatus GetVSyncStatus() const override;

	vdrect32 GetMonitorRect() override;

	bool IsScreenFXPreferred() const override;
	VDDHDRAvailability IsHDRCapable() const override;

	bool MapNormSourcePtToDest(vdfloat2& pt) const override;
	bool MapNormDestPtToSource(vdfloat2& pt) const override;

	void SetProfileHook(const vdfunction<void(ProfileEvent, uintptr)>& profileHook) override;
	void RequestCapture(vdfunction<void(const VDPixmap *)> fn) override;

private:
	void UploadTexture();
	void RenderQuad();

	SDL_Window *mpWindow = nullptr;
	SDL_GLContext mGLContext = nullptr;

	GLuint mTexture = 0;
	int mTexWidth = 0;
	int mTexHeight = 0;

	// Staging buffer — written by emulation thread, read by render thread
	VDCriticalSection mStagingLock;
	VDPixmapBuffer mStagingBuffer;
	VDAtomicBool mFrameReady{false};

	// Source dimensions from last SetSourcePersistent/PostBuffer
	int mSourceW = 0;
	int mSourceH = 0;

	FilterMode mFilterMode = kFilterBilinear;
	ATDisplayStretchMode mStretchMode = kATDisplayStretchMode_PreserveAspectRatio;
	double mPixelAspectRatio = 1.0;
	bool mFullScreen = false;
	uint32 mBackgroundColor = 0;
	int mBottomMargin = 0;

	VDStringA mSourceMessage;

	IVDVideoDisplayCallback *mpCallback = nullptr;
	vdfunction<void(int)> mOnFrameStatusUpdated;
};

#endif // AT_DISPLAY_SDL2_H
