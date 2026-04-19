//	Altirra - Atari 800/800XL/5200 emulator
//	Linux pipe serial port via PTY
//	Copyright (C) 2009-2015 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <vd2/system/refcount.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/device.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/propertyset.h>
#include "debuggerlog.h"

#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#include <sys/eventfd.h>

ATDebuggerLogChannel g_ATLCPipeSerial(false, false, "PIPESERIAL", "Pipe serial port activity");

////////////////////////////////////////////////////////////////////////////////

class ATDevicePipeSerial final : public ATDeviceT<IATDeviceSerial>, public VDThread {
	ATDevicePipeSerial(const ATDevicePipeSerial&) = delete;
	ATDevicePipeSerial& operator=(const ATDevicePipeSerial&) = delete;
public:
	ATDevicePipeSerial();
	~ATDevicePipeSerial();

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;
	bool GetErrorStatus(uint32 idx, VDStringW& error) override;

public:	// IATDeviceSerial
	void SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) override;
	void SetTerminalState(const ATDeviceSerialTerminalState&) override;
	ATDeviceSerialStatus GetStatus() override;
	void SetOnReadReady(vdfunction<void()> fn) override;
	bool Read(uint32 baudRate, uint8& c, bool& framingError) override;
	bool Read(uint32& baudRate, uint8& c) override;
	void Write(uint32 baudRate, uint8 c) override;
	void FlushBuffers() override;

protected:
	void ThreadRun() override;

private:
	void InitPTY();
	void ShutdownPTY();

	VDStringA mPtyName;	// name of the PTY device path
	uint32 mBaudRate = 31250;

	int mMasterFd = -1;
	int mAttentionFd = -1;	// eventfd for signaling the I/O thread

	VDCriticalSection mMutex;

	// Circular receive buffer (PTY -> emulator)
	static constexpr uint32 kBufSize = 4096;
	uint8 mRecvBuffer[kBufSize] {};
	uint32 mRecvLevel = 0;
	uint32 mRecvReadOffset = 0;

	// Circular send buffer (emulator -> PTY)
	uint8 mSendBuffer[kBufSize] {};
	uint32 mSendLevel = 0;
	uint32 mSendWriteOffset = 0;

	bool mbExitRequested = false;
	bool mbConnected = false;

	vdfunction<void()> mpOnReadReady;

	IATAsyncDispatcher *mpAsyncDispatcher = nullptr;
	uint64 mAsyncCallback = 0;

	enum class Status {
		OK,
		PTYError,
		NotConnected
	} mStatus = Status::NotConnected;

	VDStringA mErrorMessage;
};

void ATCreateDevicePipeSerial(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePipeSerial> p(new ATDevicePipeSerial);
	*dev = p;
	(*dev)->AddRef();
}

extern const ATDeviceDefinition g_ATDeviceDefPipeSerial = {
	"pipeserial", "pipeserial", L"PTY serial port", ATCreateDevicePipeSerial
};

ATDevicePipeSerial::ATDevicePipeSerial()
	: VDThread("Altirra PTY serial")
{
}

ATDevicePipeSerial::~ATDevicePipeSerial() {
	Shutdown();
}

void ATDevicePipeSerial::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPipeSerial;
}

void ATDevicePipeSerial::GetSettingsBlurb(VDStringW& buf) {
	if (!mPtyName.empty())
		buf.sprintf(L"PTY: %hs at %u baud", mPtyName.c_str(), mBaudRate);
	else
		buf.sprintf(L"PTY serial at %u baud", mBaudRate);
}

void ATDevicePipeSerial::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("baud_rate", mBaudRate);
}

bool ATDevicePipeSerial::SetSettings(const ATPropertySet& settings) {
	mBaudRate = settings.GetUint32("baud_rate", 31250);
	if (mBaudRate < 1) mBaudRate = 1;
	if (mBaudRate > 1000000) mBaudRate = 1000000;
	return true;
}

void ATDevicePipeSerial::Init() {
	mpAsyncDispatcher = GetService<IATAsyncDispatcher>();
	InitPTY();
}

void ATDevicePipeSerial::Shutdown() {
	ShutdownPTY();

	if (mpAsyncDispatcher) {
		if (mAsyncCallback) {
			mpAsyncDispatcher->Cancel(&mAsyncCallback);
			mAsyncCallback = 0;
		}
		mpAsyncDispatcher = nullptr;
	}
}

void ATDevicePipeSerial::ColdReset() {
	FlushBuffers();
}

bool ATDevicePipeSerial::GetErrorStatus(uint32 idx, VDStringW& error) {
	if (idx != 0)
		return false;

	vdsynchronized(mMutex) {
		switch (mStatus) {
			case Status::OK:
				return false;
			case Status::PTYError:
				error = VDTextU8ToW(VDStringSpanA(mErrorMessage.c_str(), mErrorMessage.c_str() + mErrorMessage.size()));
				return true;
			case Status::NotConnected:
				if (!mPtyName.empty())
					error.sprintf(L"Listening on %hs", mPtyName.c_str());
				else
					error = L"No PTY connection";
				return true;
		}
	}
	return false;
}

void ATDevicePipeSerial::SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) {
}

void ATDevicePipeSerial::SetTerminalState(const ATDeviceSerialTerminalState&) {
}

ATDeviceSerialStatus ATDevicePipeSerial::GetStatus() {
	return ATDeviceSerialStatus{};
}

void ATDevicePipeSerial::SetOnReadReady(vdfunction<void()> fn) {
	vdsynchronized(mMutex) {
		mpOnReadReady = std::move(fn);
	}
}

bool ATDevicePipeSerial::Read(uint32 baudRate, uint8& c, bool& framingError) {
	framingError = false;

	uint32 actualBaud;
	if (!Read(actualBaud, c))
		return false;

	// Flag framing error if baud rate mismatch > 5%
	if (baudRate) {
		uint32 delta = (baudRate > actualBaud) ? baudRate - actualBaud : actualBaud - baudRate;
		if (delta * 20 > baudRate)
			framingError = true;
	}

	return true;
}

bool ATDevicePipeSerial::Read(uint32& baudRate, uint8& c) {
	vdsynchronized(mMutex) {
		if (!mRecvLevel)
			return false;

		c = mRecvBuffer[mRecvReadOffset];
		mRecvReadOffset = (mRecvReadOffset + 1) & (kBufSize - 1);
		--mRecvLevel;
		baudRate = mBaudRate;
	}
	return true;
}

void ATDevicePipeSerial::Write(uint32 baudRate, uint8 c) {
	// Drop bytes if baud rate mismatch > 5%
	if (baudRate) {
		uint32 delta = (baudRate > mBaudRate) ? baudRate - mBaudRate : mBaudRate - baudRate;
		if (delta * 20 > baudRate)
			return;
	}

	vdsynchronized(mMutex) {
		if (mSendLevel >= kBufSize)
			return;	// drop if full

		uint32 writePos = (mSendWriteOffset + mSendLevel) & (kBufSize - 1);
		mSendBuffer[writePos] = c;
		++mSendLevel;
	}

	// Wake I/O thread to send
	if (mAttentionFd >= 0) {
		uint64_t val = 1;
		[[maybe_unused]] auto r = ::write(mAttentionFd, &val, sizeof(val));
	}
}

void ATDevicePipeSerial::FlushBuffers() {
	vdsynchronized(mMutex) {
		mRecvLevel = 0;
		mRecvReadOffset = 0;
		mSendLevel = 0;
		mSendWriteOffset = 0;
	}
}

void ATDevicePipeSerial::InitPTY() {
	ShutdownPTY();

	int master = -1, slave = -1;
	if (openpty(&master, &slave, nullptr, nullptr, nullptr) < 0) {
		vdsynchronized(mMutex) {
			mStatus = Status::PTYError;
			mErrorMessage = "Failed to create PTY pair";
		}
		return;
	}

	// Get slave device name
	const char *slaveName = ptsname(master);
	if (slaveName)
		mPtyName = slaveName;

	// Close slave side — external process will open it
	::close(slave);

	// Set master to non-blocking
	int flags = fcntl(master, F_GETFL);
	fcntl(master, F_SETFL, flags | O_NONBLOCK);

	// Set raw terminal mode on master
	struct termios tio;
	if (tcgetattr(master, &tio) == 0) {
		cfmakeraw(&tio);
		tcsetattr(master, TCSANOW, &tio);
	}

	mMasterFd = master;

	// Create eventfd for attention signaling
	mAttentionFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

	vdsynchronized(mMutex) {
		mbExitRequested = false;
		mStatus = Status::NotConnected;
	}

	g_ATLCPipeSerial("PTY serial port created: %s\n", mPtyName.c_str());

	ThreadStart();
}

void ATDevicePipeSerial::ShutdownPTY() {
	if (isThreadAttached()) {
		vdsynchronized(mMutex) {
			mbExitRequested = true;
		}

		if (mAttentionFd >= 0) {
			uint64_t val = 1;
			[[maybe_unused]] auto r = ::write(mAttentionFd, &val, sizeof(val));
		}

		ThreadWait();
	}

	if (mMasterFd >= 0) {
		::close(mMasterFd);
		mMasterFd = -1;
	}

	if (mAttentionFd >= 0) {
		::close(mAttentionFd);
		mAttentionFd = -1;
	}

	mPtyName.clear();
}

void ATDevicePipeSerial::ThreadRun() {
	uint8 readBuf[256];
	uint8 writeBuf[256];

	for (;;) {
		// Check exit
		{
			vdsynchronized(mMutex) {
				if (mbExitRequested)
					break;
			}
		}

		// Set up poll: master fd + attention fd
		struct pollfd pfds[2] {};
		pfds[0].fd = mMasterFd;
		pfds[0].events = POLLIN;

		// Check if we have data to send
		bool haveSendData = false;
		{
			vdsynchronized(mMutex) {
				haveSendData = mSendLevel > 0;
			}
		}
		if (haveSendData)
			pfds[0].events |= POLLOUT;

		pfds[1].fd = mAttentionFd;
		pfds[1].events = POLLIN;

		int ret = ::poll(pfds, 2, 100);	// 100ms timeout for periodic checks

		if (ret < 0)
			continue;

		// Drain attention eventfd
		if (pfds[1].revents & POLLIN) {
			uint64_t val;
			[[maybe_unused]] auto r = ::read(mAttentionFd, &val, sizeof(val));
		}

		// Read data from PTY master
		if (pfds[0].revents & POLLIN) {
			ssize_t n = ::read(mMasterFd, readBuf, sizeof(readBuf));
			if (n > 0) {
				bool callReadReady = false;

				vdsynchronized(mMutex) {
					mbConnected = true;
					mStatus = Status::OK;

					for (ssize_t i = 0; i < n && mRecvLevel < kBufSize; ++i) {
						uint32 writePos = (mRecvReadOffset + mRecvLevel) & (kBufSize - 1);
						mRecvBuffer[writePos] = readBuf[i];
						++mRecvLevel;
					}

					callReadReady = mRecvLevel > 0 && mpOnReadReady;
				}

				if (callReadReady && mpAsyncDispatcher) {
					mpAsyncDispatcher->Queue(&mAsyncCallback,
						[this] {
							vdfunction<void()> fn;
							vdsynchronized(mMutex) {
								fn = mpOnReadReady;
							}
							if (fn)
								fn();
						}
					);
				}
			} else if (n == 0) {
				// EOF — slave disconnected
				vdsynchronized(mMutex) {
					mbConnected = false;
					mStatus = Status::NotConnected;
					mRecvLevel = 0;
					mRecvReadOffset = 0;
				}
			}
		}

		// Write data to PTY master
		if (pfds[0].revents & POLLOUT) {
			uint32 toSend = 0;
			{
				vdsynchronized(mMutex) {
					toSend = mSendLevel;
					if (toSend > sizeof(writeBuf))
						toSend = sizeof(writeBuf);

					for (uint32 i = 0; i < toSend; ++i) {
						writeBuf[i] = mSendBuffer[(mSendWriteOffset + i) & (kBufSize - 1)];
					}
				}
			}

			if (toSend > 0) {
				ssize_t written = ::write(mMasterFd, writeBuf, toSend);
				if (written > 0) {
					vdsynchronized(mMutex) {
						mSendWriteOffset = (mSendWriteOffset + (uint32)written) & (kBufSize - 1);
						mSendLevel -= (uint32)written;
					}
				}
			}
		}

		// Handle HUP (slave closed)
		if (pfds[0].revents & POLLHUP) {
			vdsynchronized(mMutex) {
				mbConnected = false;
				mStatus = Status::NotConnected;
			}
		}
	}
}
