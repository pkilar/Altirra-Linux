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
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

// Compiler barrier: prevents the compiler from reordering memory operations
// across this point. On x86/x64 this is sufficient for the SPSC ring buffer
// pattern since the CPU provides strong store/load ordering.
#if defined(VD_COMPILER_GCC) || defined(VD_COMPILER_CLANG)
#define AT_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define AT_COMPILER_BARRIER() do {} while(0)
#endif

class VDAudioOutputSDL3 final : public IVDAudioOutput {
public:
	VDAudioOutputSDL3();
	~VDAudioOutputSDL3() override;

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
	static void SDLCALL StreamCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);
	void FillStream(SDL_AudioStream *stream, int additional_amount);

	SDL_AudioStream *mpStream = nullptr;
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
	VDAtomicInt mUnderflowDetected{0};
};

VDAudioOutputSDL3::VDAudioOutputSDL3() {
}

VDAudioOutputSDL3::~VDAudioOutputSDL3() {
	Shutdown();
}

uint32 VDAudioOutputSDL3::GetPreferredSamplingRate(const wchar_t *preferredDevice) const {
	return 48000;
}

bool VDAudioOutputSDL3::Init(uint32 bufsize, uint32 bufcount, const tWAVEFORMATEX *wf, const wchar_t *preferredDevice) {
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

	// Initialize ring buffer — round up to power of two for efficient masking
	uint32 totalSize = bufsize * bufcount;
	uint32 ringSize = 1;
	while (ringSize < totalSize)
		ringSize <<= 1;

	mRingBuffer.resize(ringSize);
	mRingMask = ringSize - 1;
	mReadPos = 0;
	mWritePos = 0;
	mTotalWritten = 0;
	mTotalRead = 0;
	memset(mRingBuffer.data(), 0, ringSize);

	// Open SDL3 audio device stream.
	// SDL3's PipeWire backend can deadlock if the PipeWire daemon is in a
	// bad state (e.g. after a driver upgrade without restarting the session).
	// Run the blocking call on a helper thread with a timeout so the
	// application can still start in silent mode instead of hanging forever.
	SDL_AudioSpec spec;
	spec.format = SDL_AUDIO_S16;
	spec.channels = fmt->nChannels;
	spec.freq = mMixingRate;

	{
		// Shared state must outlive Init()'s stack frame: on timeout we
		// detach the worker, so the worker may still be blocked inside
		// SDL_OpenAudioDeviceStream after Init() has returned. Capturing
		// stack-local atomics by reference and then returning would leave
		// the worker writing into dead stack memory.
		struct OpenState {
			std::atomic<SDL_AudioStream *> result{nullptr};
			std::atomic<bool> done{false};
			std::atomic<bool> abandoned{false};
		};

		auto state = std::make_shared<OpenState>();

		// Capture state (shared_ptr) and spec by value so the worker is
		// self-contained. `this` is captured by value as well — it is
		// passed to SDL only as opaque userdata and is not dereferenced
		// here; a late-arriving stream is destroyed before any callback
		// can fire (SDL3 streams start paused).
		std::thread opener([state, spec, this]() mutable {
			SDL_AudioStream *s = SDL_OpenAudioDeviceStream(
				SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
				&spec,
				StreamCallback,
				this
			);
			state->result.store(s, std::memory_order_release);
			state->done.store(true, std::memory_order_release);

			// If Init() already gave up on us, dispose of the late-arriving
			// stream to avoid leaking it and to ensure its callback (which
			// holds a potentially dangling `this`) can never fire.
			if (s && state->abandoned.load(std::memory_order_acquire))
				SDL_DestroyAudioStream(s);
		});

		static constexpr auto kAudioOpenTimeout = std::chrono::seconds(5);
		auto deadline = std::chrono::steady_clock::now() + kAudioOpenTimeout;

		while (!state->done.load(std::memory_order_acquire)) {
			if (std::chrono::steady_clock::now() >= deadline) {
				// The open call is stuck (likely a PipeWire deadlock).
				// Mark the operation abandoned before detaching so the
				// worker will destroy any stream it eventually produces.
				// We cannot cancel SDL_OpenAudioDeviceStream directly.
				state->abandoned.store(true, std::memory_order_release);
				opener.detach();
				fprintf(stderr, "Warning: SDL3 audio device open timed out (PipeWire may need restart).\n"
					"  Try: systemctl --user restart pipewire pipewire-pulse\n"
					"  Continuing without audio.\n");
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		opener.join();
		mpStream = state->result.load(std::memory_order_acquire);
	}

	if (!mpStream)
		return false;

	// SDL3 OpenAudioDeviceStream uses the requested format directly
	mbSilent = false;
	mbInitialized = true;

	return true;
}

void VDAudioOutputSDL3::Shutdown() {
	if (mpStream) {
		SDL_DestroyAudioStream(mpStream);
		mpStream = nullptr;
	}
	mbStarted = false;
	mbInitialized = false;
	mbSilent = true;
}

void VDAudioOutputSDL3::GoSilent() {
	mbSilent = true;
	if (mpStream) {
		SDL_PauseAudioStreamDevice(mpStream);

		SDL_LockAudioStream(mpStream);
		mReadPos = 0;
		mWritePos = 0;
		SDL_UnlockAudioStream(mpStream);
	}
}

bool VDAudioOutputSDL3::IsSilent() {
	return mbSilent;
}

bool VDAudioOutputSDL3::IsFrozen() {
	return false;
}

uint32 VDAudioOutputSDL3::GetAvailSpace() {
	uint32 used = (uint32)(mWritePos - mReadPos) & mRingMask;
	uint32 avail = mRingMask + 1 - used - mBlockAlign;
	return avail;
}

uint32 VDAudioOutputSDL3::GetBufferLevel() {
	return (uint32)(mWritePos - mReadPos) & mRingMask;
}

uint32 VDAudioOutputSDL3::EstimateHWBufferLevel(bool *underflowDetected) {
	if (underflowDetected)
		*underflowDetected = (mUnderflowDetected.xchg(0) != 0);
	return GetBufferLevel();
}

sint32 VDAudioOutputSDL3::GetPosition() {
	return (sint32)(mTotalRead / mBlockAlign);
}

sint32 VDAudioOutputSDL3::GetPositionBytes() {
	return (sint32)(uint32)mTotalRead;
}

double VDAudioOutputSDL3::GetPositionTime() {
	return (double)(sint32)(uint32)mTotalRead / (double)(mMixingRate * mBlockAlign);
}

uint32 VDAudioOutputSDL3::GetMixingRate() const {
	return mMixingRate;
}

bool VDAudioOutputSDL3::Start() {
	if (!mpStream)
		return false;

	SDL_ResumeAudioStreamDevice(mpStream);
	mbStarted = true;
	mbSilent = false;
	return true;
}

bool VDAudioOutputSDL3::Stop() {
	if (!mpStream)
		return false;

	SDL_PauseAudioStreamDevice(mpStream);

	SDL_LockAudioStream(mpStream);
	mReadPos = 0;
	mWritePos = 0;
	SDL_UnlockAudioStream(mpStream);

	mbStarted = false;
	return true;
}

bool VDAudioOutputSDL3::Flush() {
	// SDL3 stream model doesn't need explicit flush
	return true;
}

bool VDAudioOutputSDL3::Write(const void *data, uint32 len) {
	if (!mbInitialized || mbSilent)
		return true;

	const uint8 *src = (const uint8 *)data;
	uint32 avail = GetAvailSpace();

	if (len > avail)
		len = avail;

	// Ensure we only write complete frames to maintain stereo alignment
	len -= len % mBlockAlign;
	if (!len)
		return true;

	uint32 wp = (uint32)mWritePos;
	uint32 ringSize = mRingMask + 1;

	// Write in up to two chunks (wrap-around)
	uint32 firstChunk = std::min(len, ringSize - wp);
	memcpy(mRingBuffer.data() + wp, src, firstChunk);

	if (len > firstChunk)
		memcpy(mRingBuffer.data(), src + firstChunk, len - firstChunk);

	// Ensure ring buffer writes complete before the callback can see the
	// updated write position. On x86 the CPU won't reorder stores, but
	// the compiler can reorder the memcpy past the volatile store.
	AT_COMPILER_BARRIER();

	mWritePos = (wp + len) & mRingMask;
	mTotalWritten += (sint32)len;

	return true;
}

bool VDAudioOutputSDL3::Finalize(uint32 timeout) {
	return true;
}

void SDLCALL VDAudioOutputSDL3::StreamCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
	static_cast<VDAudioOutputSDL3 *>(userdata)->FillStream(stream, additional_amount);
}

void VDAudioOutputSDL3::FillStream(SDL_AudioStream *stream, int additional_amount) {
	if (additional_amount <= 0)
		return;

	uint32 rp = (uint32)mReadPos;
	uint32 wp = (uint32)mWritePos;
	uint32 available = (wp - rp) & mRingMask;

	// Only consume complete frames to maintain stereo alignment
	uint32 toRead = std::min((uint32)additional_amount, available);
	toRead -= toRead % mBlockAlign;

	uint32 ringSize = mRingMask + 1;

	if (toRead > 0) {
		uint32 firstChunk = std::min(toRead, ringSize - rp);

		// Push data into the audio stream in up to two chunks
		SDL_PutAudioStreamData(stream, mRingBuffer.data() + rp, firstChunk);

		if (toRead > firstChunk)
			SDL_PutAudioStreamData(stream, mRingBuffer.data(), toRead - firstChunk);

		// Ensure ring buffer reads complete before the writer can see
		// the updated read position and overwrite what we just read.
		AT_COMPILER_BARRIER();

		mReadPos = (rp + toRead) & mRingMask;
		mTotalRead += (sint32)toRead;
	}

	// Zero-fill any remaining space and flag underflow
	uint32 remaining = (uint32)additional_amount - toRead;
	if (remaining > 0) {
		// Push silence into the stream
		uint8 silence[4096];
		while (remaining > 0) {
			uint32 chunk = std::min(remaining, (uint32)sizeof(silence));
			memset(silence, 0, chunk);
			SDL_PutAudioStreamData(stream, silence, chunk);
			remaining -= chunk;
		}

		if (available < (uint32)additional_amount)
			mUnderflowDetected = 1;
	}
}

IVDAudioOutput *VDCreateAudioOutputSDL3() {
	return new VDAudioOutputSDL3;
}
