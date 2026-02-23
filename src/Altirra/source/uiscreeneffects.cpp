//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/math.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <vd2/VDDisplay/displaytypes.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/messageloop.h>
#include "resource.h"
#include "gtia.h"
#include "oshelper.h"
#include "simulator.h"

extern ATSimulator g_sim;

void ATUIShowDialogConfigureSystemDisplay2(VDGUIHandle hParent);

///////////////////////////////////////////////////////////////////////////

class ATUIDialogScreenFXPage : public VDDialogFrameW32 {
public:
	using VDDialogFrameW32::VDDialogFrameW32;

	virtual void ResetToDefaults(
		ATArtifactingParams& artifactingParams,
		const ATArtifactingParams& defaultArtifactingParams,
		VDDScreenMaskParams& screenMaskParams,
		const VDDScreenMaskParams& defaultScreenMaskParams
	) = 0;
};

///////////////////////////////////////////////////////////////////////////

class ATUIDialogScreenFXMain final : public ATUIDialogScreenFXPage {
public:
	ATUIDialogScreenFXMain();

	void ResetToDefaults(
		ATArtifactingParams& artifactingParams,
		const ATArtifactingParams& defaultArtifactingParams,
		VDDScreenMaskParams& screenMaskParams,
		const VDDScreenMaskParams& defaultScreenMaskParams
	) override;

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	void OnHScroll(uint32 id, int code) override;

	void UpdateLabel(uint32 id);
	void UpdateEnables();

	VDUIProxySysLinkControl mWarningNotEnabledView;
};

ATUIDialogScreenFXMain::ATUIDialogScreenFXMain()
	: ATUIDialogScreenFXPage(IDD_ADJUST_SCREENFX_MAIN)
{
	mWarningNotEnabledView.SetOnClicked(
		[this] {
			ATUIShowDialogConfigureSystemDisplay2((VDGUIHandle)mhdlg);
			UpdateEnables();
		}
	);
}

void ATUIDialogScreenFXMain::ResetToDefaults(
	ATArtifactingParams& artifactingParams,
	const ATArtifactingParams& defaultArtifactingParams,
	VDDScreenMaskParams& screenMaskParams,
	const VDDScreenMaskParams& defaultScreenMaskParams
) {
	artifactingParams.mScanlineIntensity = defaultArtifactingParams.mScanlineIntensity;
	artifactingParams.mDistortionViewAngleX = defaultArtifactingParams.mDistortionViewAngleX;
	artifactingParams.mDistortionYRatio = defaultArtifactingParams.mDistortionYRatio;
}

bool ATUIDialogScreenFXMain::OnLoaded() {
	AddProxy(&mWarningNotEnabledView, IDC_WARNING_NOTENABLED);

	TBSetRange(IDC_SCANLINE_INTENSITY, 1, 7);
	TBSetRange(IDC_DISTORTION_X, 0, 180);
	TBSetRange(IDC_DISTORTION_Y, 0, 100);

	OnDataExchange(false);
	return false;
}

void ATUIDialogScreenFXMain::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (!write) {
		const auto& params = gtia.GetArtifactingParams();

		TBSetValue(IDC_SCANLINE_INTENSITY, VDRoundToInt(params.mScanlineIntensity * 8.0f));
		TBSetValue(IDC_DISTORTION_X, VDRoundToInt(params.mDistortionViewAngleX));
		TBSetValue(IDC_DISTORTION_Y, VDRoundToInt(params.mDistortionYRatio * 100.0f));

		UpdateLabel(IDC_SCANLINE_INTENSITY);
		UpdateLabel(IDC_DISTORTION_X);
		UpdateLabel(IDC_DISTORTION_Y);
		UpdateEnables();
	}
}

void ATUIDialogScreenFXMain::OnHScroll(uint32 id, int code) {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool update = false;

	if (id == IDC_SCANLINE_INTENSITY) {
		float v = (float)TBGetValue(IDC_SCANLINE_INTENSITY) / 8.0f;

		if (fabsf(params.mScanlineIntensity - v) > 1e-5f) {
			params.mScanlineIntensity = v;
			update = true;
		}
	} else if (id == IDC_DISTORTION_X) {
		float v = (float)TBGetValue(IDC_DISTORTION_X);

		if (fabsf(params.mDistortionViewAngleX - v) > 1e-5f) {
			params.mDistortionViewAngleX = v;
			update = true;
		}
	} else if (id == IDC_DISTORTION_Y) {
		float v = (float)TBGetValue(IDC_DISTORTION_Y) / 100.0f;

		if (fabsf(params.mDistortionYRatio - v) > 1e-5f) {
			params.mDistortionYRatio = v;
			update = true;
		}
	}

	if (update) {
		gtia.SetArtifactingParams(params);
		UpdateLabel(id);
	}
}

void ATUIDialogScreenFXMain::UpdateLabel(uint32 id) {
	const auto& params = g_sim.GetGTIA().GetArtifactingParams();

	switch(id) {
		case IDC_SCANLINE_INTENSITY:
			SetControlTextF(IDC_STATIC_SCANLINE_INTENSITY, L"%d%%", (int)(params.mScanlineIntensity * 100.0f + 0.5f));
			break;
		case IDC_DISTORTION_X:
			SetControlTextF(IDC_STATIC_DISTORTION_X, L"%.0f\u00B0", params.mDistortionViewAngleX);
			break;
		case IDC_DISTORTION_Y:
			SetControlTextF(IDC_STATIC_DISTORTION_Y, L"%.0f%%", params.mDistortionYRatio * 100.0f);
			break;
	}
}

void ATUIDialogScreenFXMain::UpdateEnables() {
	const auto av = g_sim.GetGTIA().GetAcceleratedEffectsAvailability();
	const bool hwSupport = av == ATGTIAEmulator::AccelFXAvailability::Available;

	ShowControl(IDC_DISTORTION_X, hwSupport);
	ShowControl(IDC_DISTORTION_Y, hwSupport);
	ShowControl(IDC_LABEL_DISTORTION_X, hwSupport);
	ShowControl(IDC_LABEL_DISTORTION_Y, hwSupport);
	ShowControl(IDC_STATIC_DISTORTION_X, hwSupport);
	ShowControl(IDC_STATIC_DISTORTION_Y, hwSupport);

	ShowControl(IDC_WARNING, av == ATGTIAEmulator::AccelFXAvailability::NoSupport);
	mWarningNotEnabledView.SetVisible(av == ATGTIAEmulator::AccelFXAvailability::NotEnabled);
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogScreenFXBloom final : public ATUIDialogScreenFXPage {
public:
	ATUIDialogScreenFXBloom();

	void ResetToDefaults(
		ATArtifactingParams& artifactingParams,
		const ATArtifactingParams& defaultArtifactingParams,
		VDDScreenMaskParams& screenMaskParams,
		const VDDScreenMaskParams& defaultScreenMaskParams
	) override;

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	void OnHScroll(uint32 id, int code) override;

	void UpdateLabel(uint32 id);
	void UpdateEnables();

	float TickToBloomRadius(sint32 tick) const;
	sint32 BloomRadiusToTick(float radius) const;

	void OnBloomEnableChanged();
	void OnBloomScanlineCompensationChanged();

	VDUIProxyButtonControl mBloomEnableView;
	VDUIProxyButtonControl mBloomScanlineCompensationView;
};

ATUIDialogScreenFXBloom::ATUIDialogScreenFXBloom()
	: ATUIDialogScreenFXPage(IDD_ADJUST_SCREENFX_BLOOM)
{
	mBloomEnableView.SetOnClicked(
		[this] { OnBloomEnableChanged(); }
	);

	mBloomScanlineCompensationView.SetOnClicked(
		[this] { OnBloomScanlineCompensationChanged(); }
	);
}

void ATUIDialogScreenFXBloom::ResetToDefaults(
	ATArtifactingParams& artifactingParams,
	const ATArtifactingParams& defaultArtifactingParams,
	VDDScreenMaskParams& screenMaskParams,
	const VDDScreenMaskParams& defaultScreenMaskParams
) {
	artifactingParams.mbEnableBloom = defaultArtifactingParams.mbEnableBloom;
	artifactingParams.mbBloomScanlineCompensation = defaultArtifactingParams.mbBloomScanlineCompensation;
	artifactingParams.mBloomRadius = defaultArtifactingParams.mBloomRadius;
	artifactingParams.mBloomDirectIntensity = defaultArtifactingParams.mBloomDirectIntensity;
	artifactingParams.mBloomIndirectIntensity = defaultArtifactingParams.mBloomIndirectIntensity;
}

bool ATUIDialogScreenFXBloom::OnLoaded() {
	AddProxy(&mBloomEnableView, IDC_ENABLE_BLOOM);
	AddProxy(&mBloomScanlineCompensationView, IDC_SCANLINECOMPENSATION);

	TBSetRange(IDC_BLOOM_RADIUS, -75, 75);
	TBSetRange(IDC_BLOOM_DIRECT_INTENSITY, 0, 200);
	TBSetRange(IDC_BLOOM_INDIRECT_INTENSITY, 0, 200);

	OnDataExchange(false);
	return false;
}

void ATUIDialogScreenFXBloom::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (!write) {
		const auto& params = gtia.GetArtifactingParams();

		mBloomEnableView.SetChecked(params.mbEnableBloom);
		mBloomScanlineCompensationView.SetChecked(params.mbBloomScanlineCompensation);

		TBSetValue(IDC_BLOOM_RADIUS, BloomRadiusToTick(params.mBloomRadius));
		TBSetValue(IDC_BLOOM_DIRECT_INTENSITY, VDRoundToInt(params.mBloomDirectIntensity * 100.0f));
		TBSetValue(IDC_BLOOM_INDIRECT_INTENSITY, VDRoundToInt(params.mBloomIndirectIntensity * 100.0f));

		UpdateLabel(IDC_BLOOM_RADIUS);
		UpdateLabel(IDC_BLOOM_DIRECT_INTENSITY);
		UpdateLabel(IDC_BLOOM_INDIRECT_INTENSITY);

		UpdateEnables();
	}
}

void ATUIDialogScreenFXBloom::OnHScroll(uint32 id, int code) {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool update = false;

	if (id == IDC_BLOOM_RADIUS) {
		float v = TickToBloomRadius(TBGetValue(IDC_BLOOM_RADIUS));

		if (fabsf(params.mBloomRadius - v) > 1e-5f) {
			params.mBloomRadius = v;
			update = true;
		}
	} else if (id == IDC_BLOOM_DIRECT_INTENSITY) {
		float v = (float)TBGetValue(IDC_BLOOM_DIRECT_INTENSITY) / 100.0f;

		if (fabsf(params.mBloomDirectIntensity - v) > 1e-5f) {
			params.mBloomDirectIntensity = v;
			update = true;
		}
	} else if (id == IDC_BLOOM_INDIRECT_INTENSITY) {
		float v = (float)TBGetValue(IDC_BLOOM_INDIRECT_INTENSITY) / 100.0f;

		if (fabsf(params.mBloomIndirectIntensity - v) > 1e-5f) {
			params.mBloomIndirectIntensity = v;
			update = true;
		}
	}

	if (update) {
		gtia.SetArtifactingParams(params);
		UpdateLabel(id);
	}
}

void ATUIDialogScreenFXBloom::UpdateLabel(uint32 id) {
	const auto& params = g_sim.GetGTIA().GetArtifactingParams();

	switch(id) {
		case IDC_BLOOM_RADIUS:
			SetControlTextF(IDC_STATIC_BLOOM_RADIUS, L"%.2f", params.mBloomRadius);
			break;
		case IDC_BLOOM_DIRECT_INTENSITY:
			SetControlTextF(IDC_STATIC_BLOOM_DIRECT_INTENSITY, L"%.2f", params.mBloomDirectIntensity);
			break;
		case IDC_BLOOM_INDIRECT_INTENSITY:
			SetControlTextF(IDC_STATIC_BLOOM_INDIRECT_INTENSITY, L"%.2f", params.mBloomIndirectIntensity);
			break;
	}
}

void ATUIDialogScreenFXBloom::UpdateEnables() {
	const bool hwSupport = g_sim.GetGTIA().AreAcceleratedEffectsAvailable();
	const bool bloomEnabled = hwSupport && mBloomEnableView.GetChecked();

	mBloomEnableView.SetVisible(hwSupport);
	mBloomScanlineCompensationView.SetVisible(hwSupport);
	mBloomScanlineCompensationView.SetEnabled(bloomEnabled);
	ShowControl(IDC_BLOOM_RADIUS, hwSupport);
	ShowControl(IDC_BLOOM_DIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_BLOOM_INDIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_STATIC_BLOOM_RADIUS, hwSupport);
	ShowControl(IDC_STATIC_BLOOM_DIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_STATIC_BLOOM_INDIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_LABEL_BLOOM_RADIUS, hwSupport);
	ShowControl(IDC_LABEL_BLOOM_DIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_LABEL_BLOOM_INDIRECT_INTENSITY, hwSupport);
	EnableControl(IDC_BLOOM_RADIUS, bloomEnabled);
	EnableControl(IDC_BLOOM_DIRECT_INTENSITY, bloomEnabled);
	EnableControl(IDC_BLOOM_INDIRECT_INTENSITY, bloomEnabled);
}

float ATUIDialogScreenFXBloom::TickToBloomRadius(sint32 tick) const {
	return powf(10.0f, 0.01f * (float)std::clamp<sint32>(tick, -75, 75));
}

sint32 ATUIDialogScreenFXBloom::BloomRadiusToTick(float radius) const {
	return std::clamp<sint32>((sint32)(0.5f + 100.0f * log10f(std::clamp<float>(radius, 0.1f, 10.0f))), -75, 75);
}

void ATUIDialogScreenFXBloom::OnBloomEnableChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool enable = mBloomEnableView.GetChecked();

	if (params.mbEnableBloom != enable) {
		params.mbEnableBloom = enable;

		gtia.SetArtifactingParams(params);

		UpdateEnables();
	}
}

void ATUIDialogScreenFXBloom::OnBloomScanlineCompensationChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool enable = mBloomScanlineCompensationView.GetChecked();

	if (params.mbBloomScanlineCompensation != enable) {
		params.mbBloomScanlineCompensation = enable;

		gtia.SetArtifactingParams(params);
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogScreenFXHDR final : public ATUIDialogScreenFXPage {
public:
	ATUIDialogScreenFXHDR();

	void ResetToDefaults(
		ATArtifactingParams& artifactingParams,
		const ATArtifactingParams& defaultArtifactingParams,
		VDDScreenMaskParams& screenMaskParams,
		const VDDScreenMaskParams& defaultScreenMaskParams
	) override;

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	void OnHScroll(uint32 id, int code) override;
	void UpdateLabel(uint32 id);
	void UpdateEnables();
	void OnHDREnableChanged();
	void OnUseSystemIntensitySDRChanged();
	void OnUseSystemIntensityHDRChanged();

	bool mbLinkToWindowsSettings = false;

	ATGTIAEmulator::HDRAvailability mLastHDRAvailability = ATGTIAEmulator::HDRAvailability::Available;
	VDUIProxyButtonControl mHDREnableView;
	VDUIProxyButtonControl mUseSystemIntensitySDRView;
	VDUIProxyButtonControl mUseSystemIntensityHDRView;
	VDUIProxySysLinkControl mHDRWarningView;
};

ATUIDialogScreenFXHDR::ATUIDialogScreenFXHDR()
	: ATUIDialogScreenFXPage(IDD_ADJUST_SCREENFX_HDR)
{
	mHDREnableView.SetOnClicked(
		[this] { OnHDREnableChanged(); }
	);

	mUseSystemIntensitySDRView.SetOnClicked(
		[this] { OnUseSystemIntensitySDRChanged(); }
	);

	mUseSystemIntensityHDRView.SetOnClicked(
		[this] { OnUseSystemIntensityHDRChanged(); }
	);

	mHDRWarningView.SetOnClicked(
		[this] {
			if (mbLinkToWindowsSettings)
				ATLaunchURL(L"ms-settings:display");
			else {
				ATUIShowDialogConfigureSystemDisplay2((VDGUIHandle)mhdlg);
				UpdateEnables();
			}
		}
	);
}

void ATUIDialogScreenFXHDR::ResetToDefaults(
	ATArtifactingParams& artifactingParams,
	const ATArtifactingParams& defaultArtifactingParams,
	VDDScreenMaskParams& screenMaskParams,
	const VDDScreenMaskParams& defaultScreenMaskParams
) {
	artifactingParams.mSDRIntensity = defaultArtifactingParams.mSDRIntensity;
	artifactingParams.mHDRIntensity = defaultArtifactingParams.mHDRIntensity;
	artifactingParams.mbEnableHDR = defaultArtifactingParams.mbEnableHDR;
	artifactingParams.mbUseSystemSDR = defaultArtifactingParams.mbUseSystemSDR;
	artifactingParams.mbUseSystemSDRAsHDR = defaultArtifactingParams.mbUseSystemSDRAsHDR;
}

bool ATUIDialogScreenFXHDR::OnLoaded() {
	AddProxy(&mHDREnableView, IDC_ENABLEHDR);
	AddProxy(&mUseSystemIntensitySDRView, IDC_USESYSTEMSDR);
	AddProxy(&mUseSystemIntensityHDRView, IDC_USESYSTEMHDR);
	AddProxy(&mHDRWarningView, IDC_HDRWARNING);

	TBSetRange(IDC_SDRBRIGHTNESS, 0, 400);
	TBSetRange(IDC_HDRBRIGHTNESS, 0, 400);

	OnDataExchange(false);
	return false;
}

void ATUIDialogScreenFXHDR::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (!write) {
		const auto& params = gtia.GetArtifactingParams();

		mHDREnableView.SetChecked(params.mbEnableHDR);
		mUseSystemIntensitySDRView.SetChecked(params.mbUseSystemSDR);
		mUseSystemIntensityHDRView.SetChecked(params.mbUseSystemSDRAsHDR);

		TBSetValue(IDC_SDRBRIGHTNESS, VDRoundToInt(logf(params.mSDRIntensity / 80.0f) / logf(2.0f) * 100.0f));
		TBSetValue(IDC_HDRBRIGHTNESS, VDRoundToInt(logf(params.mHDRIntensity / 80.0f) / logf(2.0f) * 100.0f));

		UpdateLabel(IDC_SDRBRIGHTNESS);
		UpdateLabel(IDC_HDRBRIGHTNESS);
		UpdateEnables();
	}
}

void ATUIDialogScreenFXHDR::OnHScroll(uint32 id, int code) {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool update = false;

	if (id == IDC_SDRBRIGHTNESS) {
		float v = 80.0f * powf(2.0f, (float)TBGetValue(id) / 100.0f);

		if (fabsf(params.mSDRIntensity - v) > 1e-5f) {
			params.mSDRIntensity = v;
			update = true;
		}
	} else if (id == IDC_HDRBRIGHTNESS) {
		float v = 80.0f * powf(2.0f, (float)TBGetValue(id) / 100.0f);

		if (fabsf(params.mHDRIntensity - v) > 1e-5f) {
			params.mHDRIntensity = v;
			update = true;
		}
	}

	if (update) {
		gtia.SetArtifactingParams(params);
		UpdateLabel(id);
	}
}

void ATUIDialogScreenFXHDR::UpdateLabel(uint32 id) {
	const auto& params = g_sim.GetGTIA().GetArtifactingParams();

	switch(id) {
		case IDC_SDRBRIGHTNESS:
			SetControlTextF(IDC_SDRBRIGHTNESS_VALUE, L"%.0f nits", params.mSDRIntensity);
			break;
		case IDC_HDRBRIGHTNESS:
			SetControlTextF(IDC_HDRBRIGHTNESS_VALUE, L"%.0f nits", params.mHDRIntensity);
			break;
	}
}

void ATUIDialogScreenFXHDR::UpdateEnables() {
	const ATGTIAEmulator::HDRAvailability hdrAvailability = g_sim.GetGTIA().IsHDRRenderingAvailable();
	const bool hwHdrSupport = hdrAvailability == ATGTIAEmulator::HDRAvailability::Available;

	if (mLastHDRAvailability != hdrAvailability) {
		mLastHDRAvailability = hdrAvailability;

		switch(hdrAvailability) {
			case ATGTIAEmulator::HDRAvailability::NoMinidriverSupport:
				mHDRWarningView.SetCaption(L"HDR display is not available with the currently selected graphics API. DirectX 11 is required for HDR support.");
				break;

			case ATGTIAEmulator::HDRAvailability::NoSystemSupport:
				mHDRWarningView.SetCaption(L"HDR display is not available with the current operating system. DXGI 1.6 (Windows 10 1703+) is required for HDR support.");
				break;

			case ATGTIAEmulator::HDRAvailability::NoHardwareSupport:
				mHDRWarningView.SetCaption(L"HDR display is not available with the current graphics driver.");
				break;

			case ATGTIAEmulator::HDRAvailability::NotEnabledOnDisplay:
				mHDRWarningView.SetCaption(L"HDR is supported but not enabled on the current display. HDR must be enabled in Windows display settings. <a>Open Windows Settings</a>");
				mbLinkToWindowsSettings = true;
				break;

			case ATGTIAEmulator::HDRAvailability::NoDisplaySupport:
				mHDRWarningView.SetCaption(L"HDR is not available with the current display. HDR must be supported and enabled in Windows display settings. <a>Open Windows Settings</a>");
				mbLinkToWindowsSettings = true;
				break;

			case ATGTIAEmulator::HDRAvailability::AccelNotEnabled:
				mHDRWarningView.SetCaption(L"HDR display is not available because hardware accelerated display effects are not enabled in Configure System, Display. <a>Open Configure System</a>");
				mbLinkToWindowsSettings = false;
				break;

			case ATGTIAEmulator::HDRAvailability::Available:
				break;
		}
	}

	const bool hdrEnabled = hwHdrSupport && mHDREnableView.GetChecked();

	ShowControl(IDC_HDRWARNING, !hwHdrSupport);
	ShowControl(IDC_ENABLEHDR, hwHdrSupport);
	ShowControl(IDC_STATIC_SDRBRIGHTNESS, hwHdrSupport);
	ShowControl(IDC_STATIC_HDRBRIGHTNESS, hwHdrSupport);
	ShowControl(IDC_SDRBRIGHTNESS, hwHdrSupport);
	ShowControl(IDC_HDRBRIGHTNESS, hwHdrSupport);
	ShowControl(IDC_SDRBRIGHTNESS_VALUE, hwHdrSupport);
	ShowControl(IDC_HDRBRIGHTNESS_VALUE, hwHdrSupport);

	const bool sdrManual = !mUseSystemIntensitySDRView.GetChecked();
	const bool hdrManual = !mUseSystemIntensityHDRView.GetChecked();
	mUseSystemIntensitySDRView.SetVisible(hwHdrSupport);
	mUseSystemIntensitySDRView.SetEnabled(hdrEnabled);
	mUseSystemIntensityHDRView.SetVisible(hwHdrSupport);
	mUseSystemIntensityHDRView.SetEnabled(hdrEnabled);

	EnableControl(IDC_STATIC_SDRBRIGHTNESS,	hdrEnabled && sdrManual);
	EnableControl(IDC_SDRBRIGHTNESS,		hdrEnabled && sdrManual);
	EnableControl(IDC_SDRBRIGHTNESS_VALUE,	hdrEnabled && sdrManual);
	EnableControl(IDC_STATIC_HDRBRIGHTNESS,	hdrEnabled && hdrManual);
	EnableControl(IDC_HDRBRIGHTNESS,		hdrEnabled && hdrManual);
	EnableControl(IDC_HDRBRIGHTNESS_VALUE,	hdrEnabled && hdrManual);
}

void ATUIDialogScreenFXHDR::OnHDREnableChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool enable = mHDREnableView.GetChecked();

	if (params.mbEnableHDR != enable) {
		params.mbEnableHDR = enable;

		gtia.SetArtifactingParams(params);

		UpdateEnables();
	}
}

void ATUIDialogScreenFXHDR::OnUseSystemIntensitySDRChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool enable = mUseSystemIntensitySDRView.GetChecked();

	if (params.mbUseSystemSDR != enable) {
		params.mbUseSystemSDR = enable;

		gtia.SetArtifactingParams(params);
		UpdateEnables();
	}
}

void ATUIDialogScreenFXHDR::OnUseSystemIntensityHDRChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool enable = mUseSystemIntensityHDRView.GetChecked();

	if (params.mbUseSystemSDRAsHDR != enable) {
		params.mbUseSystemSDRAsHDR = enable;

		gtia.SetArtifactingParams(params);
		UpdateEnables();
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogScreenFXMask final : public ATUIDialogScreenFXPage {
public:
	ATUIDialogScreenFXMask();

	void ResetToDefaults(
		ATArtifactingParams& artifactingParams,
		const ATArtifactingParams& defaultArtifactingParams,
		VDDScreenMaskParams& screenMaskParams,
		const VDDScreenMaskParams& defaultScreenMaskParams
	) override;

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;

	void OnMaskTypeChanged(int selection);
	void OnDotPitchChanged(sint32 value);
	void OnHoleSizeChanged(sint32 value);
	void OnIntensityCompensationChanged();
	
	void UpdateLabelDotPitch();
	void UpdateLabelHoleSize();
	void UpdateEnables();

	VDUIProxyComboBoxControl mTypeView;
	VDUIProxyTrackbarControl mDotPitchView;
	VDUIProxyTrackbarControl mHoleSizeView;
	VDUIProxyControl mDotPitchValueView;
	VDUIProxyControl mHoleSizeValueView;
	VDUIProxyControl mDotPitchEx12View;
	VDUIProxyButtonControl mIntensityCompensationView;
};

ATUIDialogScreenFXMask::ATUIDialogScreenFXMask()
	: ATUIDialogScreenFXPage(IDD_ADJUST_SCREENFX_MASK)
{
	mTypeView.SetOnSelectionChanged(
		[this](int sel) {
			OnMaskTypeChanged(sel);
		}
	);

	mDotPitchView.SetOnValueChanged(
		[this](sint32 value, bool tracking) {
			OnDotPitchChanged(value);
		}
	);

	mHoleSizeView.SetOnValueChanged(
		[this](sint32 value, bool tracking) {
			OnHoleSizeChanged(value);
		}
	);

	mIntensityCompensationView.SetOnClicked(
		[this] {
			OnIntensityCompensationChanged();
		}
	);
}

void ATUIDialogScreenFXMask::ResetToDefaults(
	ATArtifactingParams& artifactingParams,
	const ATArtifactingParams& defaultArtifactingParams,
	VDDScreenMaskParams& screenMaskParams,
	const VDDScreenMaskParams& defaultScreenMaskParams
) {
	screenMaskParams = defaultScreenMaskParams;
}

bool ATUIDialogScreenFXMask::OnLoaded() {
	AddProxy(&mTypeView, IDC_MASKTYPE);
	AddProxy(&mDotPitchView, IDC_DOT_PITCH);
	AddProxy(&mHoleSizeView, IDC_HOLE_SIZE);
	AddProxy(&mDotPitchValueView, IDC_DOT_PITCH_VALUE);
	AddProxy(&mDotPitchEx12View, IDC_DOT_PITCH_EX_12);
	AddProxy(&mHoleSizeValueView, IDC_HOLE_SIZE_VALUE);
	AddProxy(&mIntensityCompensationView, IDC_INTENSITY_COMPENSATION);

	mTypeView.AddItem(L"None");
	mTypeView.AddItem(L"Aperture grille (vertical)");
	mTypeView.AddItem(L"Dot mask");
	mTypeView.AddItem(L"Slot mask");

	mDotPitchView.SetRange(-60, 0);
	mDotPitchView.SetPageSize(5.0f);
	mHoleSizeView.SetRange(25, 100);
	mHoleSizeView.SetPageSize(10.0f);

	UpdateEnables();
	OnDataExchange(false);
	return false;
}

void ATUIDialogScreenFXMask::OnDataExchange(bool write) {
	if (!write) {
		auto& gtia = g_sim.GetGTIA();
		const auto& params = gtia.GetScreenMaskParams();

		switch(params.mType) {
			case VDDScreenMaskType::None:
			default:
				mTypeView.SetSelection(0);
				break;

			case VDDScreenMaskType::ApertureGrille:
				mTypeView.SetSelection(1);
				break;

			case VDDScreenMaskType::DotTriad:
				mTypeView.SetSelection(2);
				break;

			case VDDScreenMaskType::SlotMask:
				mTypeView.SetSelection(3);
				break;
		}

		mDotPitchView.SetValue(VDRoundToInt32(log2f(params.mSourcePixelsPerDot) * 20.0f));
		mHoleSizeView.SetValue(VDRoundToInt32(params.mOpenness * 100.0f));

		mIntensityCompensationView.SetChecked(params.mbScreenMaskIntensityCompensation);

		UpdateLabelDotPitch();
		UpdateLabelHoleSize();
	}
}

void ATUIDialogScreenFXMask::OnMaskTypeChanged(int selection) {
	auto& gtia = g_sim.GetGTIA();
	auto maskParams = gtia.GetScreenMaskParams();

	switch(selection) {
		case 0:
		default:
			maskParams.mType = VDDScreenMaskType::None;
			break;

		case 1:
			maskParams.mType = VDDScreenMaskType::ApertureGrille;
			break;

		case 2:
			maskParams.mType = VDDScreenMaskType::DotTriad;
			break;

		case 3:
			maskParams.mType = VDDScreenMaskType::SlotMask;
			break;
	}

	gtia.SetScreenMaskParams(maskParams);

	// update the dot pitch label since the example dot pitch values can
	// change based on mask type
	UpdateLabelDotPitch();
}

void ATUIDialogScreenFXMask::OnDotPitchChanged(sint32 value) {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetScreenMaskParams();

	params.mSourcePixelsPerDot = std::clamp<float>(powf(2.0f, (float)mDotPitchView.GetValue() / 20.0f), 0.01f, 1.0f);

	gtia.SetScreenMaskParams(params);
	UpdateLabelDotPitch();
}

void ATUIDialogScreenFXMask::OnHoleSizeChanged(sint32 value) {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetScreenMaskParams();

	params.mOpenness = std::clamp<float>((float)mHoleSizeView.GetValue() * 0.01f, 0.25f, 1.0f);

	gtia.SetScreenMaskParams(params);
	UpdateLabelHoleSize();
}

void ATUIDialogScreenFXMask::OnIntensityCompensationChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetScreenMaskParams();

	params.mbScreenMaskIntensityCompensation = mIntensityCompensationView.GetChecked();

	gtia.SetScreenMaskParams(params);
}

void ATUIDialogScreenFXMask::UpdateLabelDotPitch() {
	auto& gtia = g_sim.GetGTIA();
	const auto& maskParams = gtia.GetScreenMaskParams();

	VDStringW s;
	s.sprintf(L"%.2f cc", maskParams.mSourcePixelsPerDot);
	mDotPitchValueView.SetCaption(s.c_str());

	// Estimate from a C1702 monitor, with ~12" diag and ~11" (280mm) horiz.
	//
	// For aperture grille or slot mask, the pitch is measured horizontally. We
	// map 160 color clocks to the horiz, so 1.75mm/cc.
	//
	// Dot triad is more complicated as the measurement is shortest diagonal
	// between dots of the same color in mm. 30-60-90 triangle gives a ratio
	// of 2/sqrt(3) from horizontal to diagonal.

	float estimate12 = 0;

	switch(maskParams.mType) {
		case VDDScreenMaskType::ApertureGrille:
		case VDDScreenMaskType::SlotMask:
			estimate12 = maskParams.mSourcePixelsPerDot * 1.75f;
			break;

		case VDDScreenMaskType::DotTriad:
			estimate12 = maskParams.mSourcePixelsPerDot * 1.75f * 2 / sqrtf(3.0f);
			break;
	}

	if (estimate12 > 0)
		s.sprintf(L"12\" monitor: ~%.2f mm", estimate12);
	else
		s.clear();

	mDotPitchEx12View.SetCaption(s.c_str());
}

void ATUIDialogScreenFXMask::UpdateLabelHoleSize() {
	auto& gtia = g_sim.GetGTIA();
	const auto& maskParams = gtia.GetScreenMaskParams();

	VDStringW s;
	s.sprintf(L"%.0f%%", maskParams.mOpenness * 100.0f);
	mHoleSizeValueView.SetCaption(s.c_str());
}

void ATUIDialogScreenFXMask::UpdateEnables() {
	const bool hwSupport = g_sim.GetGTIA().AreAcceleratedEffectsAvailable();

	mTypeView.SetEnabled(hwSupport);
	mDotPitchView.SetEnabled(hwSupport);
	mHoleSizeView.SetEnabled(hwSupport);
	mDotPitchValueView.SetEnabled(hwSupport);
	mDotPitchEx12View.SetEnabled(hwSupport);
	mHoleSizeValueView.SetEnabled(hwSupport);
	mIntensityCompensationView.SetEnabled(hwSupport);
}

///////////////////////////////////////////////////////////////////////////

class ATAdjustScreenEffectsDialog final : public VDDialogFrameW32 {
public:
	ATAdjustScreenEffectsDialog();

private:
	bool PreNCDestroy() override;
	bool OnLoaded() override;
	void OnDestroy() override;
	void OnDataExchange(bool write) override;
	void OnEnable(bool enable) override;
	void OnTabChanged(VDUIProxyTabControl *, int index);
	void SetPageIndex(int pageIndex);
	void ResetToDefaults();

	VDUIProxyButtonControl mResetToDefaultsView;
	VDUIProxyTabControl mTabView;

	int mPageIndex = -1;
	vdautoptr<ATUIDialogScreenFXPage> mpPageView;

	VDDelegate mDelTabChanged;

	static inline int sLastPageIndex = 0;
};

ATAdjustScreenEffectsDialog *g_pATAdjustScreenEffectsDialog;

ATAdjustScreenEffectsDialog::ATAdjustScreenEffectsDialog()
	: VDDialogFrameW32(IDD_ADJUST_SCREENFX)
{
	mResetToDefaultsView.SetOnClicked(
		[this] { ResetToDefaults(); }
	);

	mTabView.OnSelectionChanged() += mDelTabChanged.Bind(this, &ATAdjustScreenEffectsDialog::OnTabChanged);
}

bool ATAdjustScreenEffectsDialog::PreNCDestroy() {
	g_pATAdjustScreenEffectsDialog = nullptr;
	return true;
}

bool ATAdjustScreenEffectsDialog::OnLoaded() {
	ATUIRegisterModelessDialog(mhwnd);

	AddProxy(&mResetToDefaultsView, IDC_RESET);
	AddProxy(&mTabView, IDC_TABS);

	mResizer.Add(IDC_TABS, mResizer.kMC);
	mResizer.Add(IDC_RESET, mResizer.kBR);

	mTabView.AddItem(L"Main");
	mTabView.AddItem(L"Bloom");
	mTabView.AddItem(L"HDR");
	mTabView.AddItem(L"Mask");

	OnDataExchange(false);
	SetPageIndex(sLastPageIndex);
	return true;
}

void ATAdjustScreenEffectsDialog::OnDestroy() {
	ATUIUnregisterModelessDialog(mhwnd);

	sLastPageIndex = mPageIndex;

	VDDialogFrameW32::OnDestroy();
}

void ATAdjustScreenEffectsDialog::OnDataExchange(bool write) {
	if (!write) {
		if (mpPageView)
			mpPageView->Sync(false);
	}
}

void ATAdjustScreenEffectsDialog::OnEnable(bool enable) {
	ATUISetGlobalEnableState(enable);
}

void ATAdjustScreenEffectsDialog::OnTabChanged(VDUIProxyTabControl *, int index) {
	SetPageIndex(index);
}

void ATAdjustScreenEffectsDialog::SetPageIndex(int pageIndex) {
	pageIndex = std::clamp(pageIndex, -1, 3);

	if (mPageIndex == pageIndex)
		return;

	mPageIndex = pageIndex;

	if (mpPageView) {
		mResizer.Remove(mpPageView->GetHandleW32());
		mpPageView->Destroy();
		mpPageView = nullptr;
	}

	switch(pageIndex) {
		case 0:
			mpPageView = new ATUIDialogScreenFXMain;
			break;

		case 1:
			mpPageView = new ATUIDialogScreenFXBloom;
			break;

		case 2:
			mpPageView = new ATUIDialogScreenFXHDR;
			break;

		case 3:
			mpPageView = new ATUIDialogScreenFXMask;
			break;
	}

	if (mpPageView) {
		mpPageView->Create(this);

		vdrect32 pageRect = mpPageView->GetArea();
		vdrect32 contentArea = mTabView.GetContentArea();
		vdrect32 tabArea = TransformScreenToClient(mTabView.GetWindowArea());

		contentArea.left = tabArea.left;
		contentArea.right = tabArea.right;
		contentArea.bottom = tabArea.bottom;

		SetSize(GetSize() + (pageRect.size() - contentArea.size()));

		mpPageView->SetPosition(contentArea.top_left());
		mpPageView->Show();

		mResizer.Add(mpPageView->GetHandleW32(), mResizer.kMC);
	}
}

void ATAdjustScreenEffectsDialog::ResetToDefaults() {
	auto& gtia = g_sim.GetGTIA();

	const ATArtifactingParams& defaultArtifactingParams = ATArtifactingParams::GetDefault();
	VDDScreenMaskParams defaultScreenMaskParams = ATGTIAEmulator::GetDefaultScreenMaskParams();

	ATArtifactingParams artifactingParams = gtia.GetArtifactingParams();
	VDDScreenMaskParams screenMaskParams = gtia.GetScreenMaskParams();

	if (mpPageView)
		mpPageView->ResetToDefaults(artifactingParams, defaultArtifactingParams, screenMaskParams, defaultScreenMaskParams);

	gtia.SetArtifactingParams(artifactingParams);
	gtia.SetScreenMaskParams(screenMaskParams);

	OnDataExchange(false);
}

////////////////////////////////////////////////////////////////////////////////

void ATUIOpenAdjustScreenEffectsDialog(VDGUIHandle hParent) {
	if (g_pATAdjustScreenEffectsDialog) {
		g_pATAdjustScreenEffectsDialog->Activate();
	} else {
		g_pATAdjustScreenEffectsDialog = new ATAdjustScreenEffectsDialog;
		if (!g_pATAdjustScreenEffectsDialog->Create(hParent)) {
			delete g_pATAdjustScreenEffectsDialog;
			g_pATAdjustScreenEffectsDialog = nullptr;
		}
	}
}

void ATUICloseAdjustScreenEffectsDialog() {
	if (g_pATAdjustScreenEffectsDialog)
		g_pATAdjustScreenEffectsDialog->Destroy();
}
