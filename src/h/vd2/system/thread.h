//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#ifndef f_VD2_SYSTEM_THREAD_H
#define f_VD2_SYSTEM_THREAD_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>
#include <vd2/system/atomic.h>

typedef void *VDThreadHandle;
typedef uint32 VDThreadID;
typedef uint32 VDThreadId;
typedef uint32 VDProcessId;

#ifdef VD_PLATFORM_WINDOWS

#if defined(__MINGW32__) || defined(__MINGW64__)
	struct _CRITICAL_SECTION;
	typedef _CRITICAL_SECTION VDCriticalSectionW32;
#else
	struct _RTL_CRITICAL_SECTION;
	typedef _RTL_CRITICAL_SECTION VDCriticalSectionW32;
#endif

extern "C" void __declspec(dllimport) __stdcall InitializeCriticalSection(VDCriticalSectionW32 *lpCriticalSection);
extern "C" void __declspec(dllimport) __stdcall LeaveCriticalSection(VDCriticalSectionW32 *lpCriticalSection);
extern "C" void __declspec(dllimport) __stdcall EnterCriticalSection(VDCriticalSectionW32 *lpCriticalSection);
extern "C" void __declspec(dllimport) __stdcall DeleteCriticalSection(VDCriticalSectionW32 *lpCriticalSection);
extern "C" unsigned long __declspec(dllimport) __stdcall WaitForSingleObject(void *hHandle, unsigned long dwMilliseconds);
extern "C" int __declspec(dllimport) __stdcall ReleaseSemaphore(void *hSemaphore, long lReleaseCount, long *lpPreviousCount);

#endif // VD_PLATFORM_WINDOWS

VDThreadID VDGetCurrentThreadID();
VDProcessId VDGetCurrentProcessId();
uint32 VDGetLogicalProcessorCount();

void VDSetThreadDebugName(VDThreadID tid, const char *name);
void VDThreadSleep(int milliseconds);

///////////////////////////////////////////////////////////////////////////
//
//	VDThread
//
//	VDThread is a quick way to portably create threads -- to use it,
//	derive a subclass from it that implements the ThreadRun() function.
//
///////////////////////////////////////////////////////////////////////////

class VDThread {
public:
	VDThread(const char *pszDebugName = NULL);	// NOTE: pszDebugName must have static duration
	~VDThread() throw();

	// external functions

	bool ThreadStart();							// start thread
	void ThreadWait();							// wait for thread to finish

	// Cancel any synchronous I/O pending for the specific thread.
	void ThreadCancelSynchronousIo();

	// return true if thread is still running
	bool isThreadActive();

	// return true if thread is still attached (though it may have exited)
	bool isThreadAttached() const {
		return mhThread != 0;
	}

	// return true if running on this thread
	bool IsCurrentThread() const;

	VDThreadHandle getThreadHandle() const {	// get handle to thread (Win32: HANDLE)
		return mhThread;
	}

	VDThreadID getThreadID() const {			// get ID of thread (Win32: DWORD)
		return mThreadID;
	}

	// thread-local functions

	virtual void ThreadRun() = 0;				// thread, come to life

private:
#ifdef VD_PLATFORM_WINDOWS
	static unsigned __stdcall StaticThreadStart(void *pThis);
#else
	static void *StaticThreadStart(void *pThis);
#endif
	void ThreadDetach();

	const char *mpszDebugName;
	VDThreadHandle	mhThread;
	VDThreadID		mThreadID;
};

///////////////////////////////////////////////////////////////////////////

class VDCriticalSection {
private:
#ifdef VD_PLATFORM_WINDOWS
	struct CritSec {				// This is a clone of CRITICAL_SECTION.
		void	*DebugInfo;
		sint32	LockCount;
		sint32	RecursionCount;
		void	*OwningThread;
		void	*LockSemaphore;
		uint32	SpinCount;
	} csect;
#else
	// Opaque storage for pthread_mutex_t (typically 40 bytes on Linux x64)
	alignas(8) char csect[64];
#endif

	VDCriticalSection(const VDCriticalSection&);
	const VDCriticalSection& operator=(const VDCriticalSection&);
#ifdef VD_PLATFORM_WINDOWS
	static void StructCheck();
#endif
public:
	class AutoLock {
	private:
		VDCriticalSection& cs;
	public:
		AutoLock(VDCriticalSection& csect) : cs(csect) { cs.Lock(); }
		~AutoLock() { cs.Unlock(); }

		inline operator bool() const { return false; }
	};

#ifdef VD_PLATFORM_WINDOWS
	VDCriticalSection() {
		InitializeCriticalSection((VDCriticalSectionW32 *)&csect);
	}

	~VDCriticalSection() {
		DeleteCriticalSection((VDCriticalSectionW32 *)&csect);
	}

	void operator++() {
		EnterCriticalSection((VDCriticalSectionW32 *)&csect);
	}

	void operator--() {
		LeaveCriticalSection((VDCriticalSectionW32 *)&csect);
	}

	void Lock() {
		EnterCriticalSection((VDCriticalSectionW32 *)&csect);
	}

	void Unlock() {
		LeaveCriticalSection((VDCriticalSectionW32 *)&csect);
	}
#else
	VDCriticalSection();
	~VDCriticalSection();
	void operator++();
	void operator--();
	void Lock();
	void Unlock();
#endif
};

// 'vdsynchronized' keyword
//
// The vdsynchronized(lock) keyword emulates Java's 'synchronized' keyword, which
// protects the following statement or block from race conditions by obtaining a
// lock during its execution:
//
//		vdsynchronized(list_lock) {
//			mList.pop_back();
//			if (mList.empty())
//				return false;
//		}
//
// The construct is exception safe and will release the lock even if a return,
// continue, break, or thrown exception exits the block.  However, hardware
// exceptions (access violations) may not work due to synchronous model
// exception handling.

#define vdsynchronized2(lock) if(VDCriticalSection::AutoLock vd__lock=(lock))VDNEVERHERE;else
#define vdsynchronized1(lock) vdsynchronized2(lock)
#define vdsynchronized(lock) vdsynchronized1(lock)

///////////////////////////////////////////////////////////////////////////

class VDSignalBase {
	VDSignalBase(const VDSignalBase&) = delete;
	VDSignalBase& operator=(const VDSignalBase&) = delete;
protected:
	void *hEvent;

public:
	VDSignalBase() = default;
	~VDSignalBase();

	void signal();
	bool check();
	void wait();
	int wait(VDSignalBase *second);
	int wait(VDSignalBase *second, VDSignalBase *third);
	static int waitMultiple(const VDSignalBase *const *signals, int count);

	bool tryWait(uint32 timeoutMillisec);

	void *getHandle() { return hEvent; }

	void operator()() { signal(); }
};

class VDSignal : public VDSignalBase {
public:
	VDSignal();
};

class VDSignalPersistent : public VDSignalBase {
public:
	VDSignalPersistent();

	void unsignal();
};

///////////////////////////////////////////////////////////////////////////

class VDSemaphore {
public:
	VDSemaphore(int initial);
	~VDSemaphore();

	void *GetHandle() const {
		return mKernelSema;
	}

	void Reset(int count);

#ifdef VD_PLATFORM_WINDOWS
	void Wait() {
		WaitForSingleObject(mKernelSema, 0xFFFFFFFFU);
	}

	bool Wait(int timeout) {
		return 0 == WaitForSingleObject(mKernelSema, timeout);
	}

	bool TryWait() {
		return 0 == WaitForSingleObject(mKernelSema, 0);
	}

	void Post() {
		ReleaseSemaphore(mKernelSema, 1, NULL);
	}
#else
	void Wait();
	bool Wait(int timeout);
	bool TryWait();
	void Post();
#endif

private:
	void *mKernelSema;
};

///////////////////////////////////////////////////////////////////////////

class VDRWLock {
	VDRWLock(const VDRWLock&) = delete;
	VDRWLock& operator=(const VDRWLock&) = delete;

public:
#ifdef VD_PLATFORM_LINUX
	VDRWLock();
	~VDRWLock();
#else
	VDRWLock() = default;
#endif

	void LockExclusive() noexcept;
	void UnlockExclusive() noexcept;

private:
	friend class VDConditionVariable;

	void *mpSRWLock = nullptr;
};

///////////////////////////////////////////////////////////////////////////

struct VDRWAutoLockExclusive {
	VDRWAutoLockExclusive(VDRWLock& rwLock)
		: mRWLock(rwLock)
	{
		mRWLock.LockExclusive();
	}

	~VDRWAutoLockExclusive() {
		mRWLock.UnlockExclusive();
	}

	VDRWLock& mRWLock;
};

#define vdsyncexclusive(rwlock) if (VDRWAutoLockExclusive autoLock{rwlock}; false); else 

///////////////////////////////////////////////////////////////////////////

class VDConditionVariable {
	VDConditionVariable(const VDConditionVariable&) = delete;
	VDConditionVariable& operator=(const VDConditionVariable&) = delete;
public:
#ifdef VD_PLATFORM_LINUX
	VDConditionVariable();
	~VDConditionVariable();
#else
	VDConditionVariable() = default;
#endif

	void Wait(VDRWLock& rwLock) noexcept;
	void NotifyOne() noexcept;
	void NotifyAll() noexcept;

private:
	void *mpCondVar = nullptr;
};

#endif
