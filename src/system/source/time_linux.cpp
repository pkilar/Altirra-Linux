#include <stdafx.h>
#include <time.h>
#include <vd2/system/time.h>
#include <vd2/system/thread.h>
#include <vd2/system/atomic.h>

namespace {
	uint64 GetMonotonicNs() {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (uint64)ts.tv_sec * 1000000000ULL + (uint64)ts.tv_nsec;
	}
}

uint32 VDGetCurrentTick() {
	return (uint32)(GetMonotonicNs() / 1000000ULL);
}

uint64 VDGetCurrentTick64() {
	return GetMonotonicNs() / 1000000ULL;
}

uint64 VDGetPreciseTick() {
	return GetMonotonicNs();
}

uint64 VDGetPreciseTicksPerSecondI() {
	return 1000000000ULL;
}

double VDGetPreciseTicksPerSecond() {
	return 1000000000.0;
}

double VDGetPreciseSecondsPerTick() {
	return 1e-9;
}

uint32 VDGetAccurateTick() {
	return (uint32)(GetMonotonicNs() / 1000000ULL);
}

///////////////////////////////////////////////////////////////////////////

VDCallbackTimer::VDCallbackTimer()
	: mpCB(nullptr)
	, mTimerAccuracy(0)
	, mTimerPeriod(0)
	, mbExit(false)
	, mbPrecise(false)
{
}

VDCallbackTimer::~VDCallbackTimer() {
	Shutdown();
}

bool VDCallbackTimer::Init(IVDTimerCallback *pCB, uint32 period_ms) {
	return Init3(pCB, period_ms * 10000, period_ms * 10000, false);
}

bool VDCallbackTimer::Init2(IVDTimerCallback *pCB, uint32 period_100ns) {
	return Init3(pCB, period_100ns, period_100ns, false);
}

bool VDCallbackTimer::Init3(IVDTimerCallback *pCB, uint32 period_100ns, uint32 accuracy_100ns, bool precise) {
	Shutdown();

	mpCB = pCB;
	mTimerPeriod = period_100ns;
	mTimerAccuracy = accuracy_100ns;
	mbPrecise = precise;
	mbExit = false;
	mTimerPeriodDelta = 0;
	mTimerPeriodAdjustment = 0;

	if (!ThreadStart())
		return false;

	return true;
}

void VDCallbackTimer::Shutdown() {
	if (isThreadActive()) {
		mbExit = true;
		msigExit.signal();
		ThreadWait();
	}
}

void VDCallbackTimer::SetRateDelta(int delta_100ns) {
	mTimerPeriodDelta = delta_100ns;
}

void VDCallbackTimer::AdjustRate(int adjustment_100ns) {
	mTimerPeriodAdjustment.add(adjustment_100ns);
}

bool VDCallbackTimer::IsTimerRunning() const {
	return const_cast<VDCallbackTimer*>(this)->isThreadActive();
}

void VDCallbackTimer::ThreadRun() {
	struct timespec sleepTime;
	uint64 nextWakeNs = GetMonotonicNs();

	while (!mbExit) {
		int32 period = (int32)mTimerPeriod + (int32)mTimerPeriodDelta;
		period += mTimerPeriodAdjustment.xchg(0);

		if (period < 1000)
			period = 1000;

		// period is in 100ns units; convert to ns
		nextWakeNs += (uint64)period * 100ULL;

		sleepTime.tv_sec = (time_t)(nextWakeNs / 1000000000ULL);
		sleepTime.tv_nsec = (long)(nextWakeNs % 1000000000ULL);

		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sleepTime, nullptr);

		if (!mbExit)
			mpCB->TimerCallback();
	}
}

///////////////////////////////////////////////////////////////////////////

VDLazyTimer::VDLazyTimer()
	: mTimerId(0)
	, mbPeriodic(false)
	, mpThunk(nullptr)
{
}

VDLazyTimer::~VDLazyTimer() {
	Stop();
}

void VDLazyTimer::SetOneShot(IVDTimerCallback *pCB, uint32 delay) {
	// Stub — requires event loop integration (Phase 6)
}

void VDLazyTimer::SetOneShotFn(const vdfunction<void()>& fn, uint32 delay) {
	// Stub — requires event loop integration (Phase 6)
}

void VDLazyTimer::SetPeriodic(IVDTimerCallback *pCB, uint32 delay) {
	// Stub — requires event loop integration (Phase 6)
}

void VDLazyTimer::SetPeriodicFn(const vdfunction<void()>& fn, uint32 delay) {
	// Stub — requires event loop integration (Phase 6)
}

void VDLazyTimer::Stop() {
	mTimerId = 0;
}

void VDLazyTimer::StaticTimeCallback(VDZHWND hwnd, VDZUINT msg, VDZUINT_PTR id, VDZDWORD time) {
	// Stub — requires event loop integration (Phase 6)
}
