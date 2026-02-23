//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
//	along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	Linux port of modemtcp.cpp — POSIX sockets + poll() + eventfd replaces
//	Winsock async model. Telnet protocol logic is identical to Windows.

#include <stdafx.h>
#include <bitset>
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include "modemtcp.h"
#include "rs232.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdarg>

namespace ATNetTelnetOptions {
	enum ATTelnetOption : uint8 {
		TransmitBinary = 0,
		Echo = 1,
		SuppressGoAhead = 3,
		Status = 5,
		TimingMark = 6,
		TerminalType = 24,
		Naws = 31,
		TerminalSpeed = 32,
		ToggleFlowControl = 33,
		LineMode = 34,
		XDisplayLocation = 35,
		Environ = 36,
		Authentication = 37,
		Encrypt = 38,
		NewEnviron = 39
	};
}

namespace {
	const char *GetTelnetOptionName(uint8 c) {
		using namespace ATNetTelnetOptions;

		switch(c) {
		case TransmitBinary:	return "TRANSMIT-BINARY";
		case Echo:				return "ECHO";
		case SuppressGoAhead:	return "SUPPRESS-GO-AHEAD";
		case Status:			return "STATUS";
		case TimingMark:		return "TIMING-MARK";
		case TerminalType:		return "TERMINAL-TYPE";
		case Naws:				return "NAWS";
		case TerminalSpeed:		return "TERMINAL-SPEED";
		case ToggleFlowControl:	return "TOGGLE-FLOW-CONTROL";
		case LineMode:			return "LINEMODE";
		case XDisplayLocation:	return "X-DISPLAY-LOCATION";
		case Environ:			return "ENVIRON";
		case Authentication:	return "AUTHENTICATION";
		case Encrypt:			return "ENCRYPT";
		case NewEnviron:		return "NEW-ENVIRON";
		default:				return "?";
		}
	}

	void SetNonBlocking(int fd) {
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags >= 0)
			fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	}
}

class ATModemDriverTCP final : public IATModemDriver, public VDThread {
public:
	ATModemDriverTCP();
	~ATModemDriverTCP();

	bool Init(const char *address, const char *service, uint32 port, bool loggingEnabled, IATModemDriverCallback *callback) override;
	void Shutdown() override;

	bool GetLastIncomingAddress(VDStringA& address, uint32& port) override;

	void SetLoggingEnabled(bool enabled) override;
	void SetConfig(const ATRS232Config& config) override;

	uint32 Write(const void *data, uint32 len) override;
	uint32 Write(const void *data, uint32 len, bool escapeChars);

	uint32 Read(void *buf, uint32 len) override;
	bool ReadLogMessages(VDStringA& messages) override;

protected:
	void ThreadRun() override;

	void WorkerShutdown();
	void OnCommandLocked();
	void OnRead(uint32 bytes);
	void OnWrite();
	void OnError(int code);
	void QueueRead();
	void QueueWrite();
	void FlushSpecialReplies();

	void SendDo(uint8 c);
	void SendDont(uint8 c);
	void SendWill(uint8 c);
	void SendWont(uint8 c);
	void SendCommand(uint8 cmd, uint8 opt);

	void Log(const char *msg);
	void LogF(const char *format, ...);

	void SignalCommand();

	IATModemDriverCallback *mpCB;
	VDStringA mAddress;
	VDStringA mService;
	uint32 mPort;

	VDStringA mIncomingAddress;
	uint32 mIncomingPort;

	VDSignal	mThreadInited;
	int mSocket;
	int mSocket2;
	int mEventFD;	// eventfd for command signaling (replaces WSAEvent)
	bool	mbReadEOF;
	bool	mbConnected;
	bool	mbListenIPv6;
	VDStringA	mTelnetTermType;

	VDAtomicInt	mbTelnetEmulation;

	// begin mutex protected members
	VDCriticalSection	mMutex;
	uint32	mWriteQueuedBytes;
	bool	mbExit;

	VDStringA	mLogMessages;
	bool	mbLoggingEnabled;

	uint32	mReadIndex;
	uint32	mReadLevel;

	uint8	mReadBuffer[4096];
	uint8	mWriteBuffer[4096];
	// end mutex protected members

	vdfastvector<uint8> mSpecialReplies;
	uint32	mSpecialReplyIndex;

	VDStringA	mWorkerLog;
	bool	mbWorkerLoggingEnabled;

	enum TelnetState {
		kTS_Disabled,
		kTS_WaitingForIAC,
		kTS_WaitingForCommandByte,
		kTS_WaitingForDoOptionByte,
		kTS_WaitingForDontOptionByte,
		kTS_WaitingForWillOptionByte,
		kTS_WaitingForWontOptionByte
	};

	enum TelnetSubState {
		kTSS_None,
		kTSS_SubOptionCode,
		kTSS_SubData_Discard,
		kTSS_SubData_TerminalType
	};

	TelnetState mTelnetState;
	TelnetSubState mTelnetSubState;
	bool		mbTelnetListeningMode;
	bool		mbTelnetWaitingForEchoResponse;
	bool		mbTelnetWaitingForSGAResponse;

	bool		mbTelnetLFConversion;
	bool		mbTelnetSawIncomingCR;
	bool		mbTelnetSawOutgoingCR;
	bool		mbTelnetSawIncomingATASCII;
	bool		mbTelnetSentTerminalType;
	bool		mbTelnetBinaryModeIncoming;
	bool		mbTelnetBinaryModeOutgoing;
	uint32		mTelnetBinaryModeIncomingPending;

	std::bitset<256> mTelnetSentDoDont;
};

IATModemDriver *ATCreateModemDriverTCP() {
	return new ATModemDriverTCP;
}

ATModemDriverTCP::ATModemDriverTCP()
	: VDThread("Altirra TCP modem worker")
	, mSocket(-1)
	, mSocket2(-1)
	, mEventFD(-1)
	, mbListenIPv6(true)
	, mbLoggingEnabled(false)
	, mbTelnetEmulation(false)
	, mbTelnetLFConversion(false)
	, mbTelnetSawIncomingCR(false)
	, mbTelnetSawOutgoingCR(false)
	, mbTelnetSawIncomingATASCII(false)
	, mbTelnetBinaryModeIncoming(false)
	, mbTelnetBinaryModeOutgoing(false)
	, mTelnetBinaryModeIncomingPending(0)
{
}

ATModemDriverTCP::~ATModemDriverTCP() {
	Shutdown();
}

bool ATModemDriverTCP::Init(const char *address, const char *service, uint32 port, bool loggingEnabled, IATModemDriverCallback *callback) {
	if (address)
		mAddress = address;
	else
		mAddress.clear();

	if (service)
		mService = service;
	else
		mService.clear();

	mPort = port;

	mIncomingAddress.clear();
	mIncomingPort = 0;

	mpCB = callback;
	mWriteQueuedBytes = 0;
	mReadIndex = 0;
	mReadLevel = 0;

	mbLoggingEnabled = loggingEnabled;
	mbWorkerLoggingEnabled = loggingEnabled;
	mbTelnetListeningMode = mAddress.empty();
	mbTelnetSawIncomingCR = false;
	mbTelnetSawOutgoingCR = false;
	mbTelnetSentTerminalType = false;

	mThreadInited.tryWait(0);

	mbExit = false;
	if (!ThreadStart())
		return false;

	// wait for the worker thread to initialize
	mThreadInited.wait();

	return true;
}

void ATModemDriverTCP::Shutdown() {
	mMutex.Lock();
	mbExit = true;
	mMutex.Unlock();
	SignalCommand();
	ThreadWait();
}

bool ATModemDriverTCP::GetLastIncomingAddress(VDStringA& address, uint32& port) {
	mMutex.Lock();
	address = mIncomingAddress;
	port = mIncomingPort;
	mMutex.Unlock();

	return !address.empty();
}

void ATModemDriverTCP::SetLoggingEnabled(bool enabled) {
	mMutex.Lock();
	mbLoggingEnabled = enabled;
	mMutex.Unlock();
	SignalCommand();
}

void ATModemDriverTCP::SetConfig(const ATRS232Config& config) {
	mbTelnetEmulation = config.mbTelnetEmulation;
	mbTelnetLFConversion = mbTelnetEmulation && config.mbTelnetLFConversion;
	mbListenIPv6 = config.mbListenForIPv6;
	mTelnetTermType = config.mTelnetTermType;

	for(VDStringA::iterator it = mTelnetTermType.begin(), itEnd = mTelnetTermType.end();
		it != itEnd;
		++it)
	{
		*it = toupper((unsigned char)*it);
	}
}

uint32 ATModemDriverTCP::Read(void *buf, uint32 len) {
	if (!len)
		return 0;

	mMutex.Lock();
	uint32 tc = mReadLevel - mReadIndex;

	if (tc > len)
		tc = len;

	memcpy(buf, mReadBuffer + mReadIndex, tc);
	mReadIndex += tc;

	if (tc && mReadIndex >= mReadLevel)
		SignalCommand();
	mMutex.Unlock();

	return tc;
}

bool ATModemDriverTCP::ReadLogMessages(VDStringA& messages) {
	mMutex.Lock();
	messages = mLogMessages;
	mLogMessages.clear();
	mMutex.Unlock();

	return !messages.empty();
}

uint32 ATModemDriverTCP::Write(const void *data, uint32 len) {
	return Write(data, len, true);
}

uint32 ATModemDriverTCP::Write(const void *data, uint32 len, bool escapeChars) {
	if (!len)
		return 0;

	mMutex.Lock();
	bool wasZero = (mWriteQueuedBytes == 0);

	uint32 tc;
	if (escapeChars) {
		const uint8 *data8 = (const uint8 *)data;

		while(len && mWriteQueuedBytes < sizeof mWriteBuffer) {
			uint8 c = *data8++;
			--len;

			if (mbTelnetEmulation) {
				if (mbTelnetLFConversion && !mbTelnetSawIncomingATASCII) {
					if (c == 0x0D)
						mbTelnetSawOutgoingCR = true;
					else if (mbTelnetSawOutgoingCR) {
						mbTelnetSawOutgoingCR = false;

						if (c == 0x0A)
							continue;
					}
				} else if (!mbTelnetBinaryModeOutgoing) {
					if (c == 0x0D) {
						if (mWriteQueuedBytes >= (sizeof mWriteBuffer) - 1)
							break;

						mWriteBuffer[mWriteQueuedBytes++] = c;
						c = 0;
					}
				}

				if (c == 0xFF) {
					if (mWriteQueuedBytes >= (sizeof mWriteBuffer) - 1)
						break;

					mWriteBuffer[mWriteQueuedBytes++] = 0xFF;
				}
			}

			mWriteBuffer[mWriteQueuedBytes++] = c;

			if (mbTelnetEmulation && mbTelnetLFConversion) {
				if (c == 0x0D && !mbTelnetSawIncomingATASCII) {
					if (mWriteQueuedBytes < sizeof mWriteBuffer)
						mWriteBuffer[mWriteQueuedBytes++] = 0x0A;
				}
			}
		}

		tc = (uint32)(data8 - (const uint8 *)data);
	} else {
		tc = sizeof mWriteBuffer - mWriteQueuedBytes;

		if (tc > len)
			tc = len;

		memcpy(mWriteBuffer + mWriteQueuedBytes, data, tc);
		mWriteQueuedBytes += tc;
	}

	if (wasZero)
		SignalCommand();
	mMutex.Unlock();

	return tc;
}

void ATModemDriverTCP::SignalCommand() {
	if (mEventFD >= 0) {
		uint64 val = 1;
		::write(mEventFD, &val, sizeof(val));
	}
}

void ATModemDriverTCP::ThreadRun() {
	mbConnected = false;
	mbReadEOF = false;
	mTelnetState = kTS_WaitingForIAC;
	mTelnetSubState = kTSS_None;
	mbTelnetBinaryModeIncoming = false;
	mTelnetBinaryModeIncomingPending = 0;
	mbTelnetBinaryModeOutgoing = false;

	mSpecialReplies.clear();
	mSpecialReplyIndex = 0;

	mEventFD = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (mEventFD < 0) {
		VDDEBUG("ModemTCP: Unable to create eventfd.\n");
		if (mpCB)
			mpCB->OnEvent(this, kATModemPhase_Init, kATModemEvent_AllocFail);
		return;
	}

	mThreadInited.signal();

	if (mAddress.empty()) {
		// === Listen mode ===
		mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (mSocket < 0) {
			VDDEBUG("ModemTCP: Unable to create socket.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Init, kATModemEvent_AllocFail);
			WorkerShutdown();
			return;
		}

		int reuse = 1;
		setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

		sockaddr_in sa {};
		sa.sin_port = htons(mPort);
		sa.sin_addr.s_addr = INADDR_ANY;
		sa.sin_family = AF_INET;
		if (bind(mSocket, (sockaddr *)&sa, sizeof sa)) {
			VDDEBUG("ModemTCP: Unable to bind socket.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Listen, kATModemEvent_GenericError);
			WorkerShutdown();
			return;
		}

		if (listen(mSocket, 1)) {
			int err = errno;
			VDDEBUG("ModemTCP: Unable to listen on socket.\n");
			if (mpCB) {
				ATModemEvent event = kATModemEvent_GenericError;
				if (err == EADDRINUSE)
					event = kATModemEvent_LineInUse;
				else if (err == ENETDOWN)
					event = kATModemEvent_NoDialTone;
				mpCB->OnEvent(this, kATModemPhase_Listen, event);
			}
			WorkerShutdown();
			return;
		}

		SetNonBlocking(mSocket);

		// Create IPv6 listening socket (OK for this to fail)
		if (mbListenIPv6) {
			mSocket2 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
			if (mSocket2 >= 0) {
				int reuse6 = 1;
				setsockopt(mSocket2, SOL_SOCKET, SO_REUSEADDR, &reuse6, sizeof reuse6);

				sockaddr_in6 sa6 {};
				sa6.sin6_port = htons(mPort);
				sa6.sin6_family = AF_INET6;
				if (!bind(mSocket2, (sockaddr *)&sa6, sizeof sa6)) {
					if (!listen(mSocket2, 1)) {
						SetNonBlocking(mSocket2);
					} else {
						::close(mSocket2);
						mSocket2 = -1;
					}
				} else {
					::close(mSocket2);
					mSocket2 = -1;
				}
			}
		}

		// Wait for incoming connection
		for(;;) {
			union {
				char buf[256];
				sockaddr addr;
			} sa2 {};
			socklen_t salen = sizeof(sa2);
			int sock2 = accept(mSocket, &sa2.addr, &salen);

			if (sock2 < 0 && mSocket2 >= 0) {
				salen = sizeof(sa2);
				sock2 = accept(mSocket2, &sa2.addr, &salen);
			}

			if (sock2 >= 0) {
				::close(mSocket);
				if (mSocket2 >= 0) {
					::close(mSocket2);
					mSocket2 = -1;
				}

				mSocket = sock2;

				int nodelay = 1;
				setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
				SetNonBlocking(mSocket);

				// Grab incoming address
				char namebuf[NI_MAXHOST] {};
				char servbuf[NI_MAXSERV] {};
				int revresult = getnameinfo(&sa2.addr, salen, namebuf, sizeof namebuf, servbuf, sizeof servbuf, NI_NUMERICHOST | NI_NUMERICSERV);

				mMutex.Lock();
				if (!revresult) {
					mIncomingAddress = namebuf;
					mIncomingPort = atoi(servbuf);
				} else {
					mIncomingAddress.clear();
					mIncomingPort = 0;
				}
				mMutex.Unlock();

				VDDEBUG("ModemTCP: Inbound connection accepted.\n");
				break;
			}

			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				VDDEBUG("ModemTCP: accept() call failed.\n");
				if (mpCB)
					mpCB->OnEvent(this, kATModemPhase_Accept, kATModemEvent_GenericError);
				WorkerShutdown();
				return;
			}

			// Poll for command event or socket readiness
			int nfds = (mSocket2 >= 0) ? 3 : 2;
			struct pollfd pfds[3] {};
			pfds[0].fd = mEventFD;
			pfds[0].events = POLLIN;
			pfds[1].fd = mSocket;
			pfds[1].events = POLLIN;
			if (mSocket2 >= 0) {
				pfds[2].fd = mSocket2;
				pfds[2].events = POLLIN;
			}

			int pr = ::poll(pfds, nfds, -1);
			if (pr <= 0)
				continue;

			if (pfds[0].revents & POLLIN) {
				uint64 val;
				::read(mEventFD, &val, sizeof(val));

				mMutex.Lock();
				OnCommandLocked();
				bool exit = mbExit;
				mMutex.Unlock();

				if (exit) {
					WorkerShutdown();
					return;
				}
			}
			// Socket readiness handled by looping back to accept()
		}
	} else {
		// === Connect mode ===
		VDDEBUG("ModemTCP: Looking up %s:%s\n", mAddress.c_str(), mService.c_str());

		addrinfo hint {};
		hint.ai_family = AF_UNSPEC;
		hint.ai_socktype = SOCK_STREAM;

		addrinfo *results = nullptr;
		if (getaddrinfo(mAddress.c_str(), mService.c_str(), &hint, &results)) {
			VDDEBUG("ModemTCP: Name lookup failed.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_NameLookup, kATModemEvent_NameLookupFailed);
			WorkerShutdown();
			return;
		}

		VDDEBUG("ModemTCP: Contacting %s:%s\n", mAddress.c_str(), mService.c_str());

		int cr = -1;

		for(addrinfo *p = results; p; p = p->ai_next) {
			mMutex.Lock();
			bool exit = mbExit;
			mMutex.Unlock();

			if (exit) {
				freeaddrinfo(results);
				WorkerShutdown();
				return;
			}

			if (p->ai_socktype != SOCK_STREAM)
				continue;

			if (p->ai_family != PF_INET && p->ai_family != PF_INET6)
				continue;

			mSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
			if (mSocket >= 0) {
				int nodelay = 1;
				setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);

				SetNonBlocking(mSocket);
				cr = connect(mSocket, p->ai_addr, p->ai_addrlen);
			}

			if (!cr || errno == EINPROGRESS)
				break;

			::close(mSocket);
			mSocket = -1;
		}

		freeaddrinfo(results);

		if (mSocket < 0) {
			VDDEBUG("ModemTCP: Unable to connect.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Connecting, kATModemEvent_ConnectFailed);
			WorkerShutdown();
			return;
		}

		// If connect is still in progress, wait for it to complete
		if (cr != 0) {
			struct pollfd pfds[2] {};
			pfds[0].fd = mEventFD;
			pfds[0].events = POLLIN;
			pfds[1].fd = mSocket;
			pfds[1].events = POLLOUT;

			for(;;) {
				int pr = ::poll(pfds, 2, -1);
				if (pr <= 0)
					continue;

				if (pfds[0].revents & POLLIN) {
					uint64 val;
					::read(mEventFD, &val, sizeof(val));

					mMutex.Lock();
					OnCommandLocked();
					bool exit = mbExit;
					mMutex.Unlock();

					if (exit) {
						WorkerShutdown();
						return;
					}
				}

				if (pfds[1].revents & (POLLOUT | POLLERR | POLLHUP)) {
					int err = 0;
					socklen_t errlen = sizeof(err);
					getsockopt(mSocket, SOL_SOCKET, SO_ERROR, &err, &errlen);

					if (err) {
						VDDEBUG("ModemTCP: Connect failed (err=%d).\n", err);
						if (mpCB)
							mpCB->OnEvent(this, kATModemPhase_Connecting, kATModemEvent_ConnectFailed);
						WorkerShutdown();
						return;
					}
					break;
				}
			}
		}

		VDDEBUG("ModemTCP: Connected to %s\n", mAddress.c_str());
	}

	// Make out of band data inline for reliable Telnet
	int oobinline = 1;
	setsockopt(mSocket, SOL_SOCKET, SO_OOBINLINE, &oobinline, sizeof oobinline);

	mbTelnetWaitingForEchoResponse = false;
	mbTelnetWaitingForSGAResponse = false;
	mTelnetSentDoDont.reset();
	mbTelnetSawIncomingCR = false;
	mbTelnetSawIncomingATASCII = false;

	// Signal connected for connect mode
	bool justConnected = !mAddress.empty();

	QueueRead();
	QueueWrite();

	for(;;) {
		if (!mbConnected && mbReadEOF) {
			mMutex.Lock();
			bool readDone = (mReadIndex >= mReadLevel);
			mMutex.Unlock();

			if (readDone) {
				if (mpCB)
					mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_ConnectionDropped);
				break;
			}
		}

		if (justConnected) {
			justConnected = false;
			mbConnected = true;

			if (mbTelnetListeningMode && mbTelnetEmulation) {
				mSpecialReplies.push_back(0xFF);	// IAC
				mSpecialReplies.push_back(0xFB);	// WILL
				mSpecialReplies.push_back(ATNetTelnetOptions::Echo);
				mSpecialReplies.push_back(0xFF);	// IAC
				mSpecialReplies.push_back(0xFD);	// DO
				mSpecialReplies.push_back(ATNetTelnetOptions::SuppressGoAhead);
				mSpecialReplies.push_back(0xFF);	// IAC
				mSpecialReplies.push_back(0xFD);	// DO
				mSpecialReplies.push_back(ATNetTelnetOptions::LineMode);
				mbTelnetWaitingForEchoResponse = true;
				mbTelnetWaitingForSGAResponse = true;

				FlushSpecialReplies();
			}

			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_Connected);
		}

		// Poll for events
		struct pollfd pfds[2] {};
		pfds[0].fd = mEventFD;
		pfds[0].events = POLLIN;
		pfds[1].fd = mSocket;
		pfds[1].events = POLLIN;

		// Only watch for write readiness if we have data to send
		mMutex.Lock();
		if (mWriteQueuedBytes > 0 || mSpecialReplyIndex < mSpecialReplies.size())
			pfds[1].events |= POLLOUT;
		mMutex.Unlock();

		int pr = ::poll(pfds, 2, -1);
		if (pr <= 0)
			continue;

		// Handle command event
		if (pfds[0].revents & POLLIN) {
			uint64 val;
			::read(mEventFD, &val, sizeof(val));

			mMutex.Lock();
			OnCommandLocked();
			bool exit = mbExit;
			bool shouldWrite = mWriteQueuedBytes > 0;
			bool shouldRead = mReadIndex >= mReadLevel;
			mMutex.Unlock();

			if (exit) {
				WorkerShutdown();
				return;
			}

			if (shouldWrite)
				QueueWrite();
			if (shouldRead)
				QueueRead();
		}

		// Handle socket events
		if (pfds[1].revents & POLLIN)
			QueueRead();

		if (pfds[1].revents & POLLOUT)
			QueueWrite();

		if (pfds[1].revents & (POLLERR | POLLHUP)) {
			mbConnected = false;
			mbReadEOF = true;

			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_ConnectionClosing);
		}
	}

	WorkerShutdown();
}

void ATModemDriverTCP::WorkerShutdown() {
	if (mSocket2 >= 0) {
		shutdown(mSocket2, SHUT_WR);
		::close(mSocket2);
		mSocket2 = -1;
	}

	if (mSocket >= 0) {
		shutdown(mSocket, SHUT_WR);
		::close(mSocket);
		mSocket = -1;
	}

	if (mEventFD >= 0) {
		::close(mEventFD);
		mEventFD = -1;
	}
}

void ATModemDriverTCP::OnCommandLocked() {
	mbWorkerLoggingEnabled = mbLoggingEnabled;
}

///////////////////////////////////////////////////////////////////////////
// Telnet protocol handling — identical to Windows implementation
///////////////////////////////////////////////////////////////////////////

void ATModemDriverTCP::OnRead(uint32 bytes) {
	if (!bytes) {
		mbReadEOF = true;
		return;
	}

	uint8 *dst = mReadBuffer;
	TelnetState state = mTelnetState;
	TelnetSubState substate = mTelnetSubState;

	if (!mbTelnetEmulation) {
		state = kTS_WaitingForIAC;
	} else {
		for(uint32 i=0; i<bytes; ++i) {
			uint8 c = mReadBuffer[i];

			switch(state) {
				case kTS_WaitingForIAC:
					if (c == 0xFF) {
						state = kTS_WaitingForCommandByte;
						continue;
					}
					break;

				case kTS_WaitingForCommandByte:
					switch(c) {
						case 0xF0:	// SE
							substate = kTSS_None;
							state = kTS_WaitingForIAC;
							continue;
						case 0xFA:	// SB
							substate = kTSS_SubOptionCode;
							state = kTS_WaitingForIAC;
							continue;
						case 0xFB:	// WILL
							state = kTS_WaitingForWillOptionByte;
							continue;
						case 0xFC:	// WONT
							state = kTS_WaitingForWontOptionByte;
							continue;
						case 0xFD:	// DO
							state = kTS_WaitingForDoOptionByte;
							continue;
						case 0xFE:	// DONT
							state = kTS_WaitingForDontOptionByte;
							continue;
						case 0xFF:	// escape
							state = kTS_WaitingForIAC;
							break;
						default:
							state = kTS_WaitingForIAC;
							continue;
					}
					break;

				case kTS_WaitingForDoOptionByte:
					LogF("Received DO %u (%s)\n", c, GetTelnetOptionName(c));
					switch(c) {
						case ATNetTelnetOptions::TransmitBinary:
							if (mbTelnetLFConversion) {
								SendWont(ATNetTelnetOptions::TransmitBinary);
							} else {
								SendWill(ATNetTelnetOptions::TransmitBinary);

								if (!mbTelnetBinaryModeOutgoing) {
									mbTelnetBinaryModeOutgoing = true;
									mbTelnetSawOutgoingCR = false;

									if (!mbTelnetBinaryModeIncoming) {
										SendDo(ATNetTelnetOptions::TransmitBinary);
										++mTelnetBinaryModeIncomingPending;
									}
								}
							}
							break;

						case ATNetTelnetOptions::Echo:
							if (mbTelnetWaitingForEchoResponse) {
								mbTelnetWaitingForEchoResponse = false;
								break;
							}

							if (mbTelnetListeningMode)
								SendWill(ATNetTelnetOptions::Echo);
							else
								SendWont(ATNetTelnetOptions::Echo);
							break;

						case ATNetTelnetOptions::SuppressGoAhead:
							SendWill(ATNetTelnetOptions::SuppressGoAhead);
							break;

						case ATNetTelnetOptions::TerminalType:
							if (mbTelnetListeningMode || mTelnetTermType.empty())
								SendWont(ATNetTelnetOptions::TerminalType);
							else
								SendWill(ATNetTelnetOptions::TerminalType);
							break;

						default:
							SendWont(c);
							break;
					}
					state = kTS_WaitingForIAC;
					continue;

				case kTS_WaitingForDontOptionByte:
					LogF("Received DONT %u (%s)\n", c, GetTelnetOptionName(c));
					switch(c) {
						case ATNetTelnetOptions::TransmitBinary:
							SendWont(ATNetTelnetOptions::TransmitBinary);
							mbTelnetBinaryModeOutgoing = false;

							if (mbTelnetBinaryModeIncoming) {
								SendDont(ATNetTelnetOptions::TransmitBinary);
								++mTelnetBinaryModeIncomingPending;
							}
							break;

						default:
							SendWont(c);
							break;
					}
					state = kTS_WaitingForIAC;
					continue;

				case kTS_WaitingForWillOptionByte:
					LogF("Received WILL %u (%s)\n", c, GetTelnetOptionName(c));
					switch(c) {
						case ATNetTelnetOptions::TransmitBinary:
							if (mTelnetBinaryModeIncomingPending) {
								--mTelnetBinaryModeIncomingPending;
								mbTelnetBinaryModeIncoming = true;
								mbTelnetSawIncomingCR = false;
							}
							break;

						case ATNetTelnetOptions::Echo:
							if (mbTelnetListeningMode)
								SendDont(ATNetTelnetOptions::Echo);
							break;

						case ATNetTelnetOptions::SuppressGoAhead:
							if (mbTelnetWaitingForSGAResponse) {
								mbTelnetWaitingForSGAResponse = false;
								break;
							}
							SendDo(ATNetTelnetOptions::SuppressGoAhead);
							break;

						case ATNetTelnetOptions::LineMode:
							if (mbTelnetListeningMode) {
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xFA);	// SB
								mSpecialReplies.push_back(ATNetTelnetOptions::LineMode);
								mSpecialReplies.push_back(0x01);	// MODE
								mSpecialReplies.push_back(0x00);	// 0
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xF0);	// SE
							}
							SendDont(c);
							break;

						default:
							SendDont(c);
							break;
					}
					state = kTS_WaitingForIAC;
					continue;

				case kTS_WaitingForWontOptionByte:
					LogF("Received WONT %u (%s)\n", c, GetTelnetOptionName(c));
					switch(c) {
						case ATNetTelnetOptions::TransmitBinary:
							if (mbTelnetListeningMode) {
								if (mTelnetBinaryModeIncomingPending) {
									--mTelnetBinaryModeIncomingPending;
									if (mbTelnetBinaryModeIncoming)
										mbTelnetBinaryModeIncoming = false;
								}
							}
							break;

						default:
							break;
					}
					state = kTS_WaitingForIAC;
					continue;
			}

			switch(substate) {
			case kTSS_SubOptionCode:
				if (c == 0x18)
					substate = kTSS_SubData_TerminalType;
				else
					substate = kTSS_SubData_Discard;
				break;

			case kTSS_SubData_TerminalType:
				if (!mbTelnetListeningMode && c == 0x01) {
					Log("Received TERMINAL-TYPE SEND\n");

					if (mTelnetTermType.empty())
						mbTelnetSentTerminalType = true;

					const uint8 *s = mbTelnetSentTerminalType ? (const uint8 *)"UNKNOWN" : (const uint8 *)mTelnetTermType.data();
					const size_t len = mbTelnetSentTerminalType ? 7 : mTelnetTermType.size();
					mbTelnetSentTerminalType = true;

					mSpecialReplies.push_back(0xFF);		// IAC
					mSpecialReplies.push_back(0xFA);		// SB
					mSpecialReplies.push_back(0x18);		// TERMINAL-TYPE
					mSpecialReplies.push_back(0x00);		// IS
					mSpecialReplies.insert(mSpecialReplies.end(), s, s + len);
					mSpecialReplies.push_back(0xFF);		// IAC
					mSpecialReplies.push_back(0xF0);		// SE
				}
				substate = kTSS_SubData_Discard;
				break;

			case kTSS_SubData_Discard:
				break;

			case kTSS_None:
				if (mbTelnetLFConversion && !mbTelnetSawIncomingATASCII) {
					if (c == 0x9B)
						mbTelnetSawIncomingATASCII = true;
					else if (c == 0x0D)
						mbTelnetSawIncomingCR = true;
					else if (mbTelnetSawIncomingCR) {
						mbTelnetSawIncomingCR = false;

						if (c == 0x0A || (c == 0x00 && !mbTelnetBinaryModeIncoming))
							continue;
					}
				} else if (!mbTelnetBinaryModeIncoming) {
					if (c == 0x0D)
						mbTelnetSawIncomingCR = true;
					else if (mbTelnetSawIncomingCR) {
						mbTelnetSawIncomingCR = false;

						if (c == 0x00)
							continue;
					}
				}

				*dst++ = c;
				break;
			}
		}

		bytes = (uint32)(dst - mReadBuffer);
	}

	mTelnetState = state;
	mTelnetSubState = substate;

	bool logs = false;

	mMutex.Lock();
	mReadIndex = 0;
	mReadLevel = bytes;

	if (!mWorkerLog.empty()) {
		logs = true;
		mLogMessages.append(mWorkerLog);
		mWorkerLog.clear();
	}

	mMutex.Unlock();

	FlushSpecialReplies();

	if (mpCB && (bytes || logs))
		mpCB->OnReadAvail(this, bytes);
}

void ATModemDriverTCP::OnWrite() {
	FlushSpecialReplies();

	if (mpCB)
		mpCB->OnWriteAvail(this);
}

void ATModemDriverTCP::OnError(int err) {
	if (!err || err == EAGAIN || err == EWOULDBLOCK)
		return;

	if (mpCB) {
		ATModemEvent ev = kATModemEvent_GenericError;

		if (err == ECONNABORTED || err == ECONNRESET) {
			ev = kATModemEvent_ConnectionDropped;
			mbConnected = false;
			mbReadEOF = true;
		}

		mpCB->OnEvent(this, kATModemPhase_Connected, ev);
	}
}

void ATModemDriverTCP::QueueRead() {
	for(;;) {
		mMutex.Lock();

		if (mbReadEOF || mReadIndex < mReadLevel) {
			mMutex.Unlock();
			return;
		}

		mReadIndex = 0;
		mReadLevel = 0;
		mMutex.Unlock();

		int actual = recv(mSocket, (char *)mReadBuffer, sizeof mReadBuffer, 0);

		if (actual >= 0) {
			OnRead(actual);
		} else {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			OnError(errno);
			break;
		}
	}
}

void ATModemDriverTCP::QueueWrite() {
	mMutex.Lock();
	for(;;) {
		if (!mbConnected) {
			mWriteQueuedBytes = 0;
			break;
		}

		if (!mWriteQueuedBytes)
			break;

		const uint32 bytesQueued = mWriteQueuedBytes;

		mMutex.Unlock();

		int actual = send(mSocket, (char *)mWriteBuffer, bytesQueued, MSG_NOSIGNAL);

		mMutex.Lock();

		if (actual <= 0) {
			if (actual < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				mMutex.Unlock();
				OnError(errno);
				return;
			}

			break;
		}

		if ((uint32)actual >= mWriteQueuedBytes) {
			mWriteQueuedBytes = 0;
		} else {
			memmove(mWriteBuffer, mWriteBuffer + actual, mWriteQueuedBytes - actual);
			mWriteQueuedBytes -= actual;
		}

		mMutex.Unlock();

		OnWrite();

		mMutex.Lock();
	}

	mMutex.Unlock();
}

void ATModemDriverTCP::FlushSpecialReplies() {
	uint32 sn = (uint32)mSpecialReplies.size();
	uint32 si = mSpecialReplyIndex;
	if (si < sn) {
		si += Write(mSpecialReplies.data() + si, sn - si, false);

		if (si >= sn) {
			si = 0;
			mSpecialReplies.clear();
		}

		mSpecialReplyIndex = si;
		QueueWrite();
	}
}

void ATModemDriverTCP::SendDo(uint8 c) {
	if (mTelnetSentDoDont[c])
		return;

	mTelnetSentDoDont[c] = true;
	LogF("Sending  DO %u (%s)\n", c, GetTelnetOptionName(c));
	SendCommand(0xFD, c);
}

void ATModemDriverTCP::SendDont(uint8 c) {
	if (mTelnetSentDoDont[c])
		return;

	mTelnetSentDoDont[c] = true;
	LogF("Sending  DONT %u (%s)\n", c, GetTelnetOptionName(c));
	SendCommand(0xFE, c);
}

void ATModemDriverTCP::SendWill(uint8 c) {
	LogF("Sending  WILL %u (%s)\n", c, GetTelnetOptionName(c));
	SendCommand(0xFB, c);
}

void ATModemDriverTCP::SendWont(uint8 c) {
	LogF("Sending  WONT %u (%s)\n", c, GetTelnetOptionName(c));
	SendCommand(0xFC, c);
}

void ATModemDriverTCP::SendCommand(uint8 cmd, uint8 opt) {
	uint8 c[3] = { 0xFF, cmd, opt };
	mSpecialReplies.insert(mSpecialReplies.end(), c, c+3);
}

void ATModemDriverTCP::Log(const char *msg) {
	if (mbWorkerLoggingEnabled)
		mWorkerLog.append(msg);
}

void ATModemDriverTCP::LogF(const char *format, ...) {
	if (mbWorkerLoggingEnabled) {
		va_list val;
		va_start(val, format);
		mWorkerLog.append_vsprintf(format, val);
		va_end(val);
	}
}
