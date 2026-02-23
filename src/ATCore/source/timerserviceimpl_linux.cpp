//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <signal.h>
#include <time.h>
#include <vd2/system/math.h>
#include <vd2/system/time.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/timerservice.h>

////////////////////////////////////////////////////////////////////////////////
// ATTimerServiceLinux
//
// Linux implementation of IATTimerService using POSIX timer_create with
// SIGEV_THREAD for asynchronous callbacks. Same min-heap scheduling as the
// Windows implementation, with 50ms tick quantization.

namespace {

class ATTimerServiceLinux final : public IATTimerService {
public:
	using Callback = vdfunction<void()>;

	ATTimerServiceLinux(IATAsyncDispatcher& dispatcher);
	~ATTimerServiceLinux();

	void Request(uint64 *token, float delay, Callback fn) override;
	void Cancel(uint64 *token) override;

private:
	void InternalCancel(uint64 token, Callback& cb);
	void RunCallbacks();
	uint32 Sink(uint32 pos, uint64 val);

	void RearmTimerForTickDelay(uint64 ticks);
	static void StaticTimerCallback(union sigval sv);
	void TimerCallback();

	VDCriticalSection mMutex;

	timer_t mTimer {};
	bool mbTimerCreated = false;
	uint64 mRunToken = 0;
	IATAsyncDispatcher *mpAsyncDispatcher = nullptr;

	struct Slot {
		uint64 mDeadline = 0;
		uint32 mSequenceNo = 0x1234ABCD;
		sint32 mHeapIndex = -1;
		Callback mCallback;
	};

	vdfastvector<uint32> mHeap;
	vdvector<Slot> mSlots;
	vdfastvector<uint32> mFreeSlots;
};

ATTimerServiceLinux::ATTimerServiceLinux(IATAsyncDispatcher& dispatcher)
	: mpAsyncDispatcher(&dispatcher)
{
	struct sigevent sev {};
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = StaticTimerCallback;
	sev.sigev_value.sival_ptr = this;

	if (timer_create(CLOCK_MONOTONIC, &sev, &mTimer) == 0)
		mbTimerCreated = true;
}

ATTimerServiceLinux::~ATTimerServiceLinux() {
	if (mpAsyncDispatcher) {
		mpAsyncDispatcher->Cancel(&mRunToken);
		mpAsyncDispatcher = nullptr;
	}

	if (mbTimerCreated) {
		// Disarm the timer first
		struct itimerspec its {};
		timer_settime(mTimer, 0, &its, nullptr);

		timer_delete(mTimer);
		mbTimerCreated = false;
	}
}

void ATTimerServiceLinux::Request(uint64 *token, float delay, Callback fn) {
	if (!fn)
		return;

	// clamp delay to a reasonable value and compute deadline in 50ms ticks
	const uint64 t = VDGetCurrentTick64();
	const uint64 deadline = (t + VDRoundToInt64(std::clamp(delay, 0.0f, 60.0f) * 1000.0f)) / 50;

	vdsynchronized(mMutex) {
		// allocate a slot index
		uint32 slotIndex;
		if (mFreeSlots.empty()) {
			slotIndex = mSlots.size();
			mSlots.emplace_back();
		} else {
			slotIndex = mFreeSlots.back();
			mFreeSlots.pop_back();
		}

		Slot& slot = mSlots[slotIndex];
		VDASSERT(slot.mHeapIndex < 0);

		slot.mDeadline = deadline;
		slot.mCallback = std::move(fn);
		++slot.mSequenceNo;

		uint32 curPos = (uint32)mHeap.size();
		mHeap.push_back(slotIndex);

		// bubble up
		while (curPos) {
			const uint32 parentPos = (curPos - 1) >> 1;
			uint32 parentIdx = mHeap[parentPos];
			Slot& parentSlot = mSlots[parentIdx];

			if (parentSlot.mDeadline <= deadline)
				break;

			parentSlot.mHeapIndex = curPos;
			mHeap[curPos] = parentIdx;

			curPos = parentPos;
		}

		slot.mHeapIndex = curPos;
		mHeap[curPos] = slotIndex;

		// if it ended at the root, readjust the timer
		if (curPos == 0)
			RearmTimerForTickDelay(deadline * 50 - t);

		if (token) {
			InternalCancel(*token, fn);
			*token = ((uint64)slot.mSequenceNo << 32) + slotIndex + 1;
		}
	}
}

void ATTimerServiceLinux::Cancel(uint64 *tokenPtr) {
	if (tokenPtr) {
		Callback cb;

		vdsynchronized(mMutex) {
			InternalCancel(*tokenPtr, cb);
			*tokenPtr = 0;
		}
	}
}

void ATTimerServiceLinux::InternalCancel(uint64 token, Callback& cb) {
	const uint32 slotIdx = (uint32)(token - 1);
	if (slotIdx >= mSlots.size())
		return;

	Slot& slot = mSlots[slotIdx];

	if ((token >> 32) != slot.mSequenceNo)
		return;

	if (slot.mHeapIndex < 0)
		return;

	uint32 pos = slot.mHeapIndex;
	slot.mHeapIndex = -1;

	// move the callback out to be deleted
	cb = std::move(slot.mCallback);
	slot.mCallback = nullptr;

	// add the slot to the free list
	mFreeSlots.push_back(slotIdx);

	// bump the sequence number on the slot to invalidate the handle
	++slot.mSequenceNo;

	// delete from heap
	const uint32 tailIdx = mHeap.back();
	mHeap.pop_back();

	if (slotIdx != tailIdx) {
		Slot& tailSlot = mSlots[tailIdx];

		VDASSERT(tailSlot.mHeapIndex == (sint32)mHeap.size());

		uint32 pos2 = Sink(pos, tailSlot.mDeadline);

		mHeap[pos2] = tailIdx;
		tailSlot.mHeapIndex = pos2;
	}
}

void ATTimerServiceLinux::RunCallbacks() {
	static constexpr uint64 kEternity = ~UINT64_C(0);

	const uint64 t = VDGetCurrentTick64();
	const uint64 now = t / 50;

	Callback cb;
	uint64 slotDeadline;
	for (;;) {
		slotDeadline = kEternity;

		vdsynchronized(mMutex) {
			if (mHeap.empty())
				break;

			uint32 slotIdx = mHeap.front();
			Slot& slot = mSlots[slotIdx];
			slotDeadline = slot.mDeadline;

			if (slotDeadline > now)
				break;

			cb = std::move(slot.mCallback);
			slot.mCallback = nullptr;

			VDASSERT(slot.mHeapIndex == 0);
			slot.mHeapIndex = -1;

			mFreeSlots.push_back(slotIdx);

			uint32 tailIdx = mHeap.back();
			mHeap.pop_back();

			if (!mHeap.empty()) {
				Slot& tailSlot = mSlots[tailIdx];
				VDASSERT(tailSlot.mHeapIndex == (sint32)mHeap.size());
				uint32 pos = Sink(0, tailSlot.mDeadline);

				mHeap[pos] = tailIdx;
				tailSlot.mHeapIndex = pos;
			}
		}

		if (cb)
			cb();
	}

	if (slotDeadline != kEternity)
		RearmTimerForTickDelay(slotDeadline * 50 - t);
}

uint32 ATTimerServiceLinux::Sink(uint32 pos, uint64 val) {
	uint32 heapCnt = (uint32)mHeap.size();

	for (;;) {
		uint32 childPos = pos * 2 + 1;

		if (childPos >= heapCnt)
			break;

		uint32 childIdx = mHeap[childPos];
		auto childVal = mSlots[childIdx].mDeadline;
		uint32 rightPos = childPos + 1;
		if (rightPos < heapCnt) {
			uint32 rightIdx = mHeap[rightPos];
			auto rightVal = mSlots[rightIdx].mDeadline;

			if (childVal < rightVal) {
				childVal = rightVal;
				childIdx = rightIdx;
				childPos = rightPos;
			}
		}

		if (val < childVal)
			break;

		mSlots[childIdx].mHeapIndex = pos;
		mHeap[pos] = childIdx;

		pos = childPos;
	}

	return pos;
}

void ATTimerServiceLinux::RearmTimerForTickDelay(uint64 ticks) {
	if (!mbTimerCreated)
		return;

	// Convert millisecond ticks to timespec
	uint64 ms = ticks;
	if (ms == 0)
		ms = 1;  // minimum 1ms

	struct itimerspec its {};
	its.it_value.tv_sec = ms / 1000;
	its.it_value.tv_nsec = (ms % 1000) * 1000000;
	// no repeat (it_interval stays zero)

	timer_settime(mTimer, 0, &its, nullptr);
}

void ATTimerServiceLinux::StaticTimerCallback(union sigval sv) {
	static_cast<ATTimerServiceLinux *>(sv.sival_ptr)->TimerCallback();
}

void ATTimerServiceLinux::TimerCallback() {
	if (mpAsyncDispatcher)
		mpAsyncDispatcher->Queue(&mRunToken, [this] { RunCallbacks(); });
}

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

IATTimerService *ATCreateTimerService(IATAsyncDispatcher& dispatcher) {
	return new ATTimerServiceLinux(dispatcher);
}
