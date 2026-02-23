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

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/atomic.h>
#include <at/ataudio/audioout.h>
#include <SDL.h>
#include <cstring>
#include <algorithm>

class VDAudioOutputSDL2 final : public IVDAudioOutput {
public:
	VDAudioOutputSDL2();
	~VDAudioOutputSDL2() override;

	uint32 GetPreferredSamplingRate(const wchar_t *preferredDevice) const override;

	bool Init(uint32 bufsize, uint32 bufcount, const tWAVEFORMATEX *wf, const wchar_t *preferredDevice) override;
	void Shutdown() override;
	void GoSilent() override;

	bool IsSilent() override;
	bool IsFrozen() override;
	uint32 GetAvailSpace() override;
	uint32 GetBufferLevel() override;
	uint32 EstimateHWBufferLevel(bool *underflowDetected) override;
	sint32 GetPosition() override;
	sint32 GetPositionBytes() override;
	double GetPositionTime() override;
	uint32 GetMixingRate() const override;

	bool Start() override;
	bool Stop() override;
	bool Flush() override;

	bool Write(const void *data, uint32 len) override;
	bool Finalize(uint32 timeout) override;

private:
	static void AudioCallback(void *userdata, Uint8 *stream, int len);
	void FillBuffer(Uint8 *stream, int len);

	SDL_AudioDeviceID mDeviceID = 0;
	uint32 mMixingRate = 48000;
	uint32 mBlockAlign = 4;    // 2 channels * 2 bytes

	// Ring buffer (power-of-two sized)
	vdblock<uint8> mRingBuffer;
	uint32 mRingMask = 0;       // size - 1
	VDAtomicInt mReadPos{0};
	VDAtomicInt mWritePos{0};

	// Total bytes written/read for position tracking
	VDAtomicInt mTotalWritten{0};
	VDAtomicInt mTotalRead{0};

	bool mbSilent = true;
	bool mbStarted = false;
	bool mbInitialized = false;
};

VDAudioOutputSDL2::VDAudioOutputSDL2() {
}

VDAudioOutputSDL2::~VDAudioOutputSDL2() {
	Shutdown();
}

uint32 VDAudioOutputSDL2::GetPreferredSamplingRate(const wchar_t *preferredDevice) const {
	return 48000;
}

bool VDAudioOutputSDL2::Init(uint32 bufsize, uint32 bufcount, const tWAVEFORMATEX *wf, const wchar_t *preferredDevice) {
	if (mbInitialized)
		Shutdown();

	// Extract format info from WAVEFORMATEX-compatible struct
	// The struct is cast from a platform-specific WAVEFORMATEX; we access
	// fields at known offsets: nChannels(+2), nSamplesPerSec(+4), nBlockAlign(+12), wBitsPerSample(+14)
	struct WaveFormatCompat {
		uint16 wFormatTag;
		uint16 nChannels;
		uint32 nSamplesPerSec;
		uint32 nAvgBytesPerSec;
		uint16 nBlockAlign;
		uint16 wBitsPerSample;
		uint16 cbSize;
	};

	const WaveFormatCompat *fmt = reinterpret_cast<const WaveFormatCompat *>(wf);
	mMixingRate = fmt->nSamplesPerSec;
	mBlockAlign = fmt->nBlockAlign;

	// Initialize ring buffer — round up to power of two
	uint32 totalSize = bufsize * bufcount;
	uint32 ringSize = 1;
	while (ringSize < totalSize * 2)
		ringSize <<= 1;

	mRingBuffer.resize(ringSize);
	mRingMask = ringSize - 1;
	mReadPos = 0;
	mWritePos = 0;
	mTotalWritten = 0;
	mTotalRead = 0;
	memset(mRingBuffer.data(), 0, ringSize);

	// Open SDL2 audio device
	SDL_AudioSpec desired {};
	desired.freq = mMixingRate;
	desired.format = AUDIO_S16SYS;
	desired.channels = fmt->nChannels;
	desired.samples = 1024;  // ~21ms at 48kHz
	desired.callback = AudioCallback;
	desired.userdata = this;

	SDL_AudioSpec obtained {};
	mDeviceID = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
	if (mDeviceID == 0)
		return false;

	// Update mixing rate to what we actually got
	mMixingRate = obtained.freq;
	mbSilent = false;
	mbInitialized = true;

	return true;
}

void VDAudioOutputSDL2::Shutdown() {
	if (mDeviceID) {
		SDL_CloseAudioDevice(mDeviceID);
		mDeviceID = 0;
	}
	mbStarted = false;
	mbInitialized = false;
	mbSilent = true;
}

void VDAudioOutputSDL2::GoSilent() {
	mbSilent = true;
	if (mDeviceID)
		SDL_PauseAudioDevice(mDeviceID, 1);
}

bool VDAudioOutputSDL2::IsSilent() {
	return mbSilent;
}

bool VDAudioOutputSDL2::IsFrozen() {
	return false;
}

uint32 VDAudioOutputSDL2::GetAvailSpace() {
	uint32 used = (uint32)(mWritePos - mReadPos) & mRingMask;
	uint32 avail = mRingMask + 1 - used - 1;  // Leave one byte gap to distinguish full from empty
	return avail;
}

uint32 VDAudioOutputSDL2::GetBufferLevel() {
	return (uint32)(mWritePos - mReadPos) & mRingMask;
}

uint32 VDAudioOutputSDL2::EstimateHWBufferLevel(bool *underflowDetected) {
	if (underflowDetected)
		*underflowDetected = false;
	return GetBufferLevel();
}

sint32 VDAudioOutputSDL2::GetPosition() {
	return (sint32)(mTotalRead / mBlockAlign);
}

sint32 VDAudioOutputSDL2::GetPositionBytes() {
	return (sint32)(uint32)mTotalRead;
}

double VDAudioOutputSDL2::GetPositionTime() {
	return (double)(sint32)(uint32)mTotalRead / (double)(mMixingRate * mBlockAlign);
}

uint32 VDAudioOutputSDL2::GetMixingRate() const {
	return mMixingRate;
}

bool VDAudioOutputSDL2::Start() {
	if (!mDeviceID)
		return false;

	SDL_PauseAudioDevice(mDeviceID, 0);
	mbStarted = true;
	mbSilent = false;
	return true;
}

bool VDAudioOutputSDL2::Stop() {
	if (!mDeviceID)
		return false;

	SDL_PauseAudioDevice(mDeviceID, 1);
	mbStarted = false;
	return true;
}

bool VDAudioOutputSDL2::Flush() {
	// SDL2 callback model doesn't need explicit flush
	return true;
}

bool VDAudioOutputSDL2::Write(const void *data, uint32 len) {
	if (!mbInitialized || mbSilent)
		return true;

	const uint8 *src = (const uint8 *)data;
	uint32 avail = GetAvailSpace();

	if (len > avail) {
		// Drop excess rather than blocking
		len = avail;
	}

	uint32 wp = (uint32)mWritePos;
	uint32 ringSize = mRingMask + 1;

	// Write in up to two chunks (wrap-around)
	uint32 firstChunk = std::min(len, ringSize - wp);
	memcpy(mRingBuffer.data() + wp, src, firstChunk);

	if (len > firstChunk)
		memcpy(mRingBuffer.data(), src + firstChunk, len - firstChunk);

	mWritePos = (wp + len) & mRingMask;
	mTotalWritten += (sint32)len;

	return true;
}

bool VDAudioOutputSDL2::Finalize(uint32 timeout) {
	return true;
}

void VDAudioOutputSDL2::AudioCallback(void *userdata, Uint8 *stream, int len) {
	static_cast<VDAudioOutputSDL2 *>(userdata)->FillBuffer(stream, len);
}

void VDAudioOutputSDL2::FillBuffer(Uint8 *stream, int len) {
	uint32 available = (uint32)(mWritePos - mReadPos) & mRingMask;
	uint32 toRead = std::min((uint32)len, available);
	uint32 rp = (uint32)mReadPos;
	uint32 ringSize = mRingMask + 1;

	if (toRead > 0) {
		uint32 firstChunk = std::min(toRead, ringSize - rp);
		memcpy(stream, mRingBuffer.data() + rp, firstChunk);

		if (toRead > firstChunk)
			memcpy(stream + firstChunk, mRingBuffer.data(), toRead - firstChunk);

		mReadPos = (rp + toRead) & mRingMask;
		mTotalRead += (sint32)toRead;
	}

	// Zero-fill any remaining space (underflow)
	if ((uint32)len > toRead)
		memset(stream + toRead, 0, len - toRead);
}

IVDAudioOutput *VDCreateAudioOutputSDL2() {
	return new VDAudioOutputSDL2;
}
