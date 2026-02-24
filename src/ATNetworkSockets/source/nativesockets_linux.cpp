//	Altirra - Atari 800/800XL/5200 emulator
//	Linux native sockets implementation (epoll + POSIX)
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

#include <stdafx.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <vd2/system/refcount.h>
#include <vd2/system/thread.h>
#include <vd2/system/VDString.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atnetwork/socket.h>
#include <at/atnetworksockets/nativesockets.h>

///////////////////////////////////////////////////////////////////////////
// Socket address utilities
///////////////////////////////////////////////////////////////////////////

namespace {

void ATSocketToNative(const ATSocketAddress& addr, sockaddr_storage& sa, socklen_t& len) {
	memset(&sa, 0, sizeof sa);

	if (addr.mType == ATSocketAddressType::IPv4) {
		auto& sin = reinterpret_cast<sockaddr_in&>(sa);
		sin.sin_family = AF_INET;
		sin.sin_port = htons(addr.mPort);
		sin.sin_addr.s_addr = htonl(addr.mIPv4Address);
		len = sizeof(sockaddr_in);
	} else if (addr.mType == ATSocketAddressType::IPv6) {
		auto& sin6 = reinterpret_cast<sockaddr_in6&>(sa);
		sin6.sin6_family = AF_INET6;
		sin6.sin6_port = htons(addr.mPort);
		sin6.sin6_flowinfo = 0;
		memcpy(sin6.sin6_addr.s6_addr, addr.mIPv6.mAddress, 16);
		sin6.sin6_scope_id = addr.mIPv6.mScopeId;
		len = sizeof(sockaddr_in6);
	} else {
		len = 0;
	}
}

ATSocketAddress ATSocketFromNative(const sockaddr *sa) {
	ATSocketAddress addr {};

	if (sa) {
		if (sa->sa_family == AF_INET) {
			auto& sin = *reinterpret_cast<const sockaddr_in *>(sa);
			addr.mType = ATSocketAddressType::IPv4;
			addr.mIPv4Address = ntohl(sin.sin_addr.s_addr);
			addr.mPort = ntohs(sin.sin_port);
		} else if (sa->sa_family == AF_INET6) {
			auto& sin6 = *reinterpret_cast<const sockaddr_in6 *>(sa);
			addr.mType = ATSocketAddressType::IPv6;
			memcpy(addr.mIPv6.mAddress, sin6.sin6_addr.s6_addr, 16);
			addr.mIPv6.mScopeId = sin6.sin6_scope_id;
			addr.mPort = ntohs(sin6.sin6_port);
		}
	}

	return addr;
}

bool SetNonBlocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return false;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // anonymous namespace

///////////////////////////////////////////////////////////////////////////
// Epoll dispatch tag — stored in each socket, registered with epoll
///////////////////////////////////////////////////////////////////////////

struct ATEpollTag {
	void (*mHandler)(void *self, uint32_t events);
	void *mSelf;
};

///////////////////////////////////////////////////////////////////////////
// Forward declarations
///////////////////////////////////////////////////////////////////////////

class ATNetSocketWorkerLinux;
class ATNetStreamSocketLinux;
class ATNetListenSocketLinux;
class ATNetDatagramSocketLinux;
class ATNetLookupWorkerLinux;

static ATNetSocketWorkerLinux *g_pSocketWorker = nullptr;
static ATNetLookupWorkerLinux *g_pLookupWorker = nullptr;

///////////////////////////////////////////////////////////////////////////
// ATNetSocketWorkerLinux - epoll-based socket event worker
///////////////////////////////////////////////////////////////////////////

class ATNetSocketWorkerLinux : public VDThread {
public:
	ATNetSocketWorkerLinux();
	~ATNetSocketWorkerLinux();

	bool Init();
	void Shutdown();

	bool RegisterSocket(int fd, ATEpollTag *tag);
	void UnregisterSocket(int fd);
	void ModifyEvents(int fd, uint32_t events, ATEpollTag *tag);

	void Wake();

private:
	void ThreadRun() override;

	int mEpollFd = -1;
	int mWakeFd = -1;
	bool mbExitRequested = false;
};

ATNetSocketWorkerLinux::ATNetSocketWorkerLinux() {
}

ATNetSocketWorkerLinux::~ATNetSocketWorkerLinux() {
	Shutdown();
}

bool ATNetSocketWorkerLinux::Init() {
	mEpollFd = epoll_create1(EPOLL_CLOEXEC);
	if (mEpollFd < 0)
		return false;

	mWakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (mWakeFd < 0) {
		close(mEpollFd);
		mEpollFd = -1;
		return false;
	}

	epoll_event ev {};
	ev.events = EPOLLIN;
	ev.data.ptr = nullptr;
	epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mWakeFd, &ev);

	mbExitRequested = false;
	if (!ThreadStart()) {
		close(mWakeFd);
		close(mEpollFd);
		mWakeFd = -1;
		mEpollFd = -1;
		return false;
	}

	return true;
}

void ATNetSocketWorkerLinux::Shutdown() {
	if (mEpollFd >= 0) {
		mbExitRequested = true;
		Wake();
		ThreadWait();

		close(mWakeFd);
		close(mEpollFd);
		mWakeFd = -1;
		mEpollFd = -1;
	}
}

bool ATNetSocketWorkerLinux::RegisterSocket(int fd, ATEpollTag *tag) {
	epoll_event ev {};
	ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
	ev.data.ptr = tag;
	return epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &ev) == 0;
}

void ATNetSocketWorkerLinux::UnregisterSocket(int fd) {
	epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, nullptr);
}

void ATNetSocketWorkerLinux::ModifyEvents(int fd, uint32_t events, ATEpollTag *tag) {
	epoll_event ev {};
	ev.events = events;
	ev.data.ptr = tag;
	epoll_ctl(mEpollFd, EPOLL_CTL_MOD, fd, &ev);
}

void ATNetSocketWorkerLinux::Wake() {
	uint64_t val = 1;
	[[maybe_unused]] auto r = write(mWakeFd, &val, sizeof val);
}

void ATNetSocketWorkerLinux::ThreadRun() {
	epoll_event events[64];

	while (!mbExitRequested) {
		int n = epoll_wait(mEpollFd, events, 64, 500);

		for (int i = 0; i < n; i++) {
			if (events[i].data.ptr == nullptr) {
				uint64_t val;
				[[maybe_unused]] auto r = read(mWakeFd, &val, sizeof val);
				continue;
			}

			auto *tag = static_cast<ATEpollTag *>(events[i].data.ptr);
			tag->mHandler(tag->mSelf, events[i].events);
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// ATNetSocketCoreLinux - common socket state (composition helper)
///////////////////////////////////////////////////////////////////////////

class ATNetSocketCoreLinux {
public:
	ATNetSocketCoreLinux();
	~ATNetSocketCoreLinux();

	void InitFd(int fd);
	int GetFd() const { return mFd; }

	void SetOnEvent(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady);
	ATSocketStatus GetSocketStatus() const;
	void CloseSocket(bool force);
	void PollSocket();
	void FireEvent();

	void SetEpollTag(ATEpollTag tag) { mEpollTag = tag; }
	ATEpollTag *GetEpollTag() { return &mEpollTag; }

	void RegisterWithEpoll();
	void UpdateEpollEvents();

	int mFd = -1;
	mutable VDCriticalSection mMutex;
	ATSocketStatus mStatus {};
	bool mbWantWrite = false;

private:
	ATEpollTag mEpollTag {};

	IATAsyncDispatcher *mpDispatcher = nullptr;
	uint64 mDispatcherToken = 0;
	vdfunction<void(const ATSocketStatus&)> mpEventFn;
};

ATNetSocketCoreLinux::ATNetSocketCoreLinux() {
}

ATNetSocketCoreLinux::~ATNetSocketCoreLinux() {
	if (mFd >= 0) {
		if (g_pSocketWorker)
			g_pSocketWorker->UnregisterSocket(mFd);
		close(mFd);
		mFd = -1;
	}
}

void ATNetSocketCoreLinux::InitFd(int fd) {
	mFd = fd;
	SetNonBlocking(fd);
}

void ATNetSocketCoreLinux::SetOnEvent(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) {
	vdsynchronized(mMutex) {
		if (mpDispatcher && mDispatcherToken) {
			mpDispatcher->Cancel(&mDispatcherToken);
			mDispatcherToken = 0;
		}

		mpDispatcher = dispatcher;
		mpEventFn = std::move(fn);

		if (callIfReady && mpEventFn) {
			ATSocketStatus status = mStatus;
			if (status.mbCanRead || status.mbCanWrite || status.mbClosed || status.mbCanAccept) {
				if (mpDispatcher) {
					mpDispatcher->Queue(&mDispatcherToken, [this, status]() {
						vdsynchronized(mMutex) {
							if (mpEventFn)
								mpEventFn(status);
						}
					});
				} else {
					mpEventFn(status);
				}
			}
		}
	}
}

ATSocketStatus ATNetSocketCoreLinux::GetSocketStatus() const {
	vdsynchronized(mMutex) {
		return mStatus;
	}
}

void ATNetSocketCoreLinux::CloseSocket(bool force) {
	vdsynchronized(mMutex) {
		if (mFd >= 0) {
			if (g_pSocketWorker)
				g_pSocketWorker->UnregisterSocket(mFd);

			if (force) {
				struct linger li = { 1, 0 };
				setsockopt(mFd, SOL_SOCKET, SO_LINGER, &li, sizeof li);
			} else {
				shutdown(mFd, SHUT_WR);
			}

			close(mFd);
			mFd = -1;

			mStatus.mbClosed = true;
			mStatus.mbCanRead = false;
			mStatus.mbCanWrite = false;
			mStatus.mbCanAccept = false;
		}
	}

	FireEvent();
}

void ATNetSocketCoreLinux::PollSocket() {
	FireEvent();
}

void ATNetSocketCoreLinux::FireEvent() {
	vdsynchronized(mMutex) {
		if (mpEventFn) {
			ATSocketStatus status = mStatus;
			if (mpDispatcher) {
				mpDispatcher->Queue(&mDispatcherToken, [this, status]() {
					vdsynchronized(mMutex) {
						if (mpEventFn)
							mpEventFn(status);
					}
				});
			} else {
				mpEventFn(status);
			}
		}
	}
}

void ATNetSocketCoreLinux::RegisterWithEpoll() {
	if (mFd >= 0 && g_pSocketWorker)
		g_pSocketWorker->RegisterSocket(mFd, &mEpollTag);
}

void ATNetSocketCoreLinux::UpdateEpollEvents() {
	if (mFd < 0 || !g_pSocketWorker)
		return;

	uint32_t events = EPOLLIN | EPOLLHUP | EPOLLERR;
	if (mbWantWrite)
		events |= EPOLLOUT;

	g_pSocketWorker->ModifyEvents(mFd, events, &mEpollTag);
}

///////////////////////////////////////////////////////////////////////////
// ATNetStreamSocketLinux - TCP stream socket
///////////////////////////////////////////////////////////////////////////

class ATNetStreamSocketLinux final : public vdrefcounted<IATStreamSocket> {
public:
	ATNetStreamSocketLinux();
	ATNetStreamSocketLinux(int connectedFd, const ATSocketAddress& remoteAddr);
	~ATNetStreamSocketLinux();

	void Connect(const ATSocketAddress& address, bool dualStack);

	void SetOnEvent(IATAsyncDispatcher *d, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) override;
	ATSocketStatus GetSocketStatus() const override;
	void CloseSocket(bool force) override;
	void PollSocket() override;

	ATSocketAddress GetLocalAddress() const override;
	ATSocketAddress GetRemoteAddress() const override;
	sint32 Recv(void *buf, uint32 len) override;
	sint32 Send(const void *buf, uint32 len) override;
	void ShutdownSocket(bool send, bool receive) override;

private:
	static void OnEpollEventStatic(void *self, uint32_t events) {
		static_cast<ATNetStreamSocketLinux *>(self)->OnEpollEvent(events);
	}
	void OnEpollEvent(uint32_t events);

	ATNetSocketCoreLinux mCore;
	ATSocketAddress mRemoteAddress {};
	bool mbConnecting = false;
};

ATNetStreamSocketLinux::ATNetStreamSocketLinux() {
	mCore.SetEpollTag({ &OnEpollEventStatic, this });
}

ATNetStreamSocketLinux::ATNetStreamSocketLinux(int connectedFd, const ATSocketAddress& remoteAddr)
	: mRemoteAddress(remoteAddr)
{
	mCore.SetEpollTag({ &OnEpollEventStatic, this });
	mCore.InitFd(connectedFd);

	mCore.mStatus.mbCanRead = true;
	mCore.mStatus.mbCanWrite = true;

	mCore.RegisterWithEpoll();
}

ATNetStreamSocketLinux::~ATNetStreamSocketLinux() {
}

void ATNetStreamSocketLinux::Connect(const ATSocketAddress& address, bool dualStack) {
	int family = (address.mType == ATSocketAddressType::IPv6) ? AF_INET6 : AF_INET;
	int fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
	if (fd < 0) {
		mCore.mStatus.mbClosed = true;
		mCore.mStatus.mError = ATSocketError::Unknown;
		mCore.FireEvent();
		return;
	}

	if (dualStack && family == AF_INET6) {
		int off = 0;
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
	}

	int on = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);

	mCore.InitFd(fd);
	mRemoteAddress = address;

	sockaddr_storage sa;
	socklen_t salen;
	ATSocketToNative(address, sa, salen);

	int rc = connect(fd, (const sockaddr *)&sa, salen);
	if (rc == 0) {
		mCore.mStatus.mbCanRead = true;
		mCore.mStatus.mbCanWrite = true;
		mCore.RegisterWithEpoll();
		mCore.FireEvent();
	} else if (errno == EINPROGRESS) {
		mbConnecting = true;
		mCore.mStatus.mbConnecting = true;
		mCore.mbWantWrite = true;
		mCore.RegisterWithEpoll();
		mCore.UpdateEpollEvents();
	} else {
		close(fd);
		mCore.mFd = -1;
		mCore.mStatus.mbClosed = true;
		mCore.mStatus.mError = ATSocketError::Unknown;
		mCore.FireEvent();
	}
}

void ATNetStreamSocketLinux::SetOnEvent(IATAsyncDispatcher *d, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) {
	mCore.SetOnEvent(d, std::move(fn), callIfReady);
}

ATSocketStatus ATNetStreamSocketLinux::GetSocketStatus() const {
	return mCore.GetSocketStatus();
}

void ATNetStreamSocketLinux::CloseSocket(bool force) {
	mCore.CloseSocket(force);
}

void ATNetStreamSocketLinux::PollSocket() {
	mCore.PollSocket();
}

ATSocketAddress ATNetStreamSocketLinux::GetLocalAddress() const {
	if (mCore.GetFd() < 0)
		return {};

	sockaddr_storage sa {};
	socklen_t len = sizeof sa;
	if (getsockname(mCore.GetFd(), (sockaddr *)&sa, &len) == 0)
		return ATSocketFromNative((const sockaddr *)&sa);
	return {};
}

ATSocketAddress ATNetStreamSocketLinux::GetRemoteAddress() const {
	return mRemoteAddress;
}

sint32 ATNetStreamSocketLinux::Recv(void *buf, uint32 len) {
	if (mCore.GetFd() < 0)
		return -1;

	ssize_t n = recv(mCore.GetFd(), buf, len, 0);
	if (n > 0) {
		return (sint32)n;
	} else if (n == 0) {
		vdsynchronized(mCore.mMutex) {
			mCore.mStatus.mbRemoteClosed = true;
			mCore.mStatus.mbCanRead = false;
		}
		return 0;
	} else {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			vdsynchronized(mCore.mMutex) {
				mCore.mStatus.mbCanRead = false;
			}
			return 0;
		}
		return -1;
	}
}

sint32 ATNetStreamSocketLinux::Send(const void *buf, uint32 len) {
	if (mCore.GetFd() < 0)
		return -1;

	ssize_t n = send(mCore.GetFd(), buf, len, MSG_NOSIGNAL);
	if (n >= 0) {
		if (n == 0 && len > 0) {
			vdsynchronized(mCore.mMutex) {
				mCore.mStatus.mbCanWrite = false;
				mCore.mbWantWrite = true;
			}
			mCore.UpdateEpollEvents();
		}
		return (sint32)n;
	} else {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			vdsynchronized(mCore.mMutex) {
				mCore.mStatus.mbCanWrite = false;
				mCore.mbWantWrite = true;
			}
			mCore.UpdateEpollEvents();
			return 0;
		}
		return -1;
	}
}

void ATNetStreamSocketLinux::ShutdownSocket(bool doSend, bool doRecv) {
	if (mCore.GetFd() < 0)
		return;

	int how = -1;
	if (doSend && doRecv)
		how = SHUT_RDWR;
	else if (doSend)
		how = SHUT_WR;
	else if (doRecv)
		how = SHUT_RD;

	if (how >= 0)
		shutdown(mCore.GetFd(), how);
}

void ATNetStreamSocketLinux::OnEpollEvent(uint32_t events) {
	bool needFire = false;

	vdsynchronized(mCore.mMutex) {
		if (mbConnecting) {
			int err = 0;
			socklen_t errLen = sizeof err;
			getsockopt(mCore.GetFd(), SOL_SOCKET, SO_ERROR, &err, &errLen);

			mbConnecting = false;
			mCore.mStatus.mbConnecting = false;
			mCore.mbWantWrite = false;

			if (err == 0) {
				mCore.mStatus.mbCanRead = true;
				mCore.mStatus.mbCanWrite = true;
			} else {
				mCore.mStatus.mbClosed = true;
				mCore.mStatus.mError = ATSocketError::Unknown;
			}

			mCore.UpdateEpollEvents();
			needFire = true;
		} else {
			if (events & EPOLLIN) {
				mCore.mStatus.mbCanRead = true;
				needFire = true;
			}

			if (events & EPOLLOUT) {
				mCore.mStatus.mbCanWrite = true;
				mCore.mbWantWrite = false;
				mCore.UpdateEpollEvents();
				needFire = true;
			}

			if (events & (EPOLLHUP | EPOLLERR)) {
				if (events & EPOLLHUP)
					mCore.mStatus.mbRemoteClosed = true;
				mCore.mStatus.mbClosed = true;
				needFire = true;
			}
		}
	}

	if (needFire)
		mCore.FireEvent();
}

///////////////////////////////////////////////////////////////////////////
// ATNetListenSocketLinux - TCP listen socket
///////////////////////////////////////////////////////////////////////////

class ATNetListenSocketLinux final : public vdrefcounted<IATListenSocket> {
public:
	ATNetListenSocketLinux(const ATSocketAddress& bindAddr, bool dualStack);
	~ATNetListenSocketLinux();

	bool IsValid() const { return mCore.GetFd() >= 0; }

	void SetOnEvent(IATAsyncDispatcher *d, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) override;
	ATSocketStatus GetSocketStatus() const override;
	void CloseSocket(bool force) override;
	void PollSocket() override;
	vdrefptr<IATStreamSocket> Accept() override;

private:
	static void OnEpollEventStatic(void *self, uint32_t events) {
		static_cast<ATNetListenSocketLinux *>(self)->OnEpollEvent(events);
	}
	void OnEpollEvent(uint32_t events);

	ATNetSocketCoreLinux mCore;
};

ATNetListenSocketLinux::ATNetListenSocketLinux(const ATSocketAddress& bindAddr, bool dualStack) {
	mCore.SetEpollTag({ &OnEpollEventStatic, this });

	int family = (bindAddr.mType == ATSocketAddressType::IPv6) ? AF_INET6 : AF_INET;
	int fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
	if (fd < 0)
		return;

	if (dualStack && family == AF_INET6) {
		int off = 0;
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
	}

	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

	sockaddr_storage sa;
	socklen_t salen;
	ATSocketToNative(bindAddr, sa, salen);

	if (bind(fd, (const sockaddr *)&sa, salen) < 0 || listen(fd, SOMAXCONN) < 0) {
		close(fd);
		return;
	}

	mCore.InitFd(fd);
	mCore.RegisterWithEpoll();
}

ATNetListenSocketLinux::~ATNetListenSocketLinux() {
}

void ATNetListenSocketLinux::SetOnEvent(IATAsyncDispatcher *d, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) {
	mCore.SetOnEvent(d, std::move(fn), callIfReady);
}

ATSocketStatus ATNetListenSocketLinux::GetSocketStatus() const {
	return mCore.GetSocketStatus();
}

void ATNetListenSocketLinux::CloseSocket(bool force) {
	mCore.CloseSocket(force);
}

void ATNetListenSocketLinux::PollSocket() {
	mCore.PollSocket();
}

vdrefptr<IATStreamSocket> ATNetListenSocketLinux::Accept() {
	if (mCore.GetFd() < 0)
		return nullptr;

	sockaddr_storage sa {};
	socklen_t salen = sizeof sa;
	int newfd = accept4(mCore.GetFd(), (sockaddr *)&sa, &salen, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (newfd < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			vdsynchronized(mCore.mMutex) {
				mCore.mStatus.mbCanAccept = false;
			}
		}
		return nullptr;
	}

	int on = 1;
	setsockopt(newfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);

	ATSocketAddress remoteAddr = ATSocketFromNative((const sockaddr *)&sa);
	return vdrefptr<IATStreamSocket>(new ATNetStreamSocketLinux(newfd, remoteAddr));
}

void ATNetListenSocketLinux::OnEpollEvent(uint32_t events) {
	bool needFire = false;

	vdsynchronized(mCore.mMutex) {
		if (events & EPOLLIN) {
			mCore.mStatus.mbCanAccept = true;
			needFire = true;
		}

		if (events & (EPOLLHUP | EPOLLERR)) {
			mCore.mStatus.mbClosed = true;
			needFire = true;
		}
	}

	if (needFire)
		mCore.FireEvent();
}

///////////////////////////////////////////////////////////////////////////
// ATNetDatagramSocketLinux - UDP datagram socket
///////////////////////////////////////////////////////////////////////////

class ATNetDatagramSocketLinux final : public vdrefcounted<IATDatagramSocket> {
public:
	ATNetDatagramSocketLinux(const ATSocketAddress& bindAddr, bool dualStack);
	~ATNetDatagramSocketLinux();

	bool IsValid() const { return mCore.GetFd() >= 0; }

	void SetOnEvent(IATAsyncDispatcher *d, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) override;
	ATSocketStatus GetSocketStatus() const override;
	void CloseSocket(bool force) override;
	void PollSocket() override;

	sint32 RecvFrom(ATSocketAddress& address, void *data, uint32 maxlen) override;
	bool SendTo(const ATSocketAddress& address, const void *data, uint32 len) override;

private:
	static void OnEpollEventStatic(void *self, uint32_t events) {
		static_cast<ATNetDatagramSocketLinux *>(self)->OnEpollEvent(events);
	}
	void OnEpollEvent(uint32_t events);

	ATNetSocketCoreLinux mCore;
};

ATNetDatagramSocketLinux::ATNetDatagramSocketLinux(const ATSocketAddress& bindAddr, bool dualStack) {
	mCore.SetEpollTag({ &OnEpollEventStatic, this });

	int family = (bindAddr.mType == ATSocketAddressType::IPv6) ? AF_INET6 : AF_INET;
	int fd = socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
	if (fd < 0)
		return;

	if (dualStack && family == AF_INET6) {
		int off = 0;
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
	}

	sockaddr_storage sa;
	socklen_t salen;
	ATSocketToNative(bindAddr, sa, salen);

	if (bind(fd, (const sockaddr *)&sa, salen) < 0) {
		close(fd);
		return;
	}

	mCore.InitFd(fd);
	mCore.mStatus.mbCanWrite = true;
	mCore.mStatus.mbCanRead = true;
	mCore.RegisterWithEpoll();
}

ATNetDatagramSocketLinux::~ATNetDatagramSocketLinux() {
}

void ATNetDatagramSocketLinux::SetOnEvent(IATAsyncDispatcher *d, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) {
	mCore.SetOnEvent(d, std::move(fn), callIfReady);
}

ATSocketStatus ATNetDatagramSocketLinux::GetSocketStatus() const {
	return mCore.GetSocketStatus();
}

void ATNetDatagramSocketLinux::CloseSocket(bool force) {
	mCore.CloseSocket(force);
}

void ATNetDatagramSocketLinux::PollSocket() {
	mCore.PollSocket();
}

sint32 ATNetDatagramSocketLinux::RecvFrom(ATSocketAddress& address, void *data, uint32 maxlen) {
	if (mCore.GetFd() < 0)
		return -1;

	sockaddr_storage sa {};
	socklen_t salen = sizeof sa;
	ssize_t n = recvfrom(mCore.GetFd(), data, maxlen, 0, (sockaddr *)&sa, &salen);
	if (n >= 0) {
		address = ATSocketFromNative((const sockaddr *)&sa);
		return (sint32)n;
	} else {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			vdsynchronized(mCore.mMutex) {
				mCore.mStatus.mbCanRead = false;
			}
		}
		return -1;
	}
}

bool ATNetDatagramSocketLinux::SendTo(const ATSocketAddress& address, const void *data, uint32 len) {
	if (mCore.GetFd() < 0)
		return false;

	sockaddr_storage sa;
	socklen_t salen;
	ATSocketToNative(address, sa, salen);

	ssize_t n = sendto(mCore.GetFd(), data, len, MSG_NOSIGNAL, (const sockaddr *)&sa, salen);
	return n >= 0;
}

void ATNetDatagramSocketLinux::OnEpollEvent(uint32_t events) {
	bool needFire = false;

	vdsynchronized(mCore.mMutex) {
		if (events & EPOLLIN) {
			mCore.mStatus.mbCanRead = true;
			needFire = true;
		}

		if (events & EPOLLOUT) {
			mCore.mStatus.mbCanWrite = true;
			needFire = true;
		}

		if (events & (EPOLLHUP | EPOLLERR)) {
			mCore.mStatus.mbClosed = true;
			needFire = true;
		}
	}

	if (needFire)
		mCore.FireEvent();
}

///////////////////////////////////////////////////////////////////////////
// ATNetLookupWorkerLinux - async DNS resolution
///////////////////////////////////////////////////////////////////////////

class ATNetLookupResultLinux final : public vdrefcounted<IATNetLookupResult> {
public:
	ATNetLookupResultLinux(const wchar_t *hostname, const wchar_t *service);

	void Resolve();

	void SetOnCompleted(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketAddress&)> fn, bool callIfReady) override;
	bool Completed() const override;
	bool Succeeded() const override;
	const ATSocketAddress& Address() const override;

private:
	VDStringA mHostname;
	VDStringA mService;

	mutable VDCriticalSection mMutex;
	bool mbCompleted = false;
	bool mbSucceeded = false;
	ATSocketAddress mAddress {};

	IATAsyncDispatcher *mpDispatcher = nullptr;
	uint64 mDispatcherToken = 0;
	vdfunction<void(const ATSocketAddress&)> mpCompleteFn;
};

ATNetLookupResultLinux::ATNetLookupResultLinux(const wchar_t *hostname, const wchar_t *service) {
	if (hostname) {
		VDStringW wh(hostname);
		for (auto c : wh)
			mHostname += (char)(unsigned char)c;
	}
	if (service) {
		VDStringW ws(service);
		for (auto c : ws)
			mService += (char)(unsigned char)c;
	}
}

void ATNetLookupResultLinux::Resolve() {
	addrinfo hints {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	addrinfo *result = nullptr;
	int rc = getaddrinfo(
		mHostname.empty() ? nullptr : mHostname.c_str(),
		mService.empty() ? nullptr : mService.c_str(),
		&hints, &result
	);

	ATSocketAddress addr {};
	bool succeeded = false;

	if (rc == 0 && result) {
		addr = ATSocketFromNative(result->ai_addr);
		succeeded = addr.IsValid();
		freeaddrinfo(result);
	}

	vdfunction<void(const ATSocketAddress&)> completeFn;
	IATAsyncDispatcher *dispatcher = nullptr;

	vdsynchronized(mMutex) {
		mAddress = addr;
		mbSucceeded = succeeded;
		mbCompleted = true;
		completeFn = mpCompleteFn;
		dispatcher = mpDispatcher;
	}

	if (completeFn) {
		if (dispatcher) {
			dispatcher->Queue(&mDispatcherToken, [completeFn, addr]() {
				completeFn(addr);
			});
		} else {
			completeFn(addr);
		}
	}
}

void ATNetLookupResultLinux::SetOnCompleted(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketAddress&)> fn, bool callIfReady) {
	vdsynchronized(mMutex) {
		mpDispatcher = dispatcher;
		mpCompleteFn = std::move(fn);

		if (callIfReady && mbCompleted && mpCompleteFn) {
			ATSocketAddress addr = mAddress;
			auto completeFn = mpCompleteFn;

			if (mpDispatcher) {
				mpDispatcher->Queue(&mDispatcherToken, [completeFn, addr]() {
					completeFn(addr);
				});
			} else {
				completeFn(addr);
			}
		}
	}
}

bool ATNetLookupResultLinux::Completed() const {
	vdsynchronized(mMutex) {
		return mbCompleted;
	}
}

bool ATNetLookupResultLinux::Succeeded() const {
	vdsynchronized(mMutex) {
		return mbSucceeded;
	}
}

const ATSocketAddress& ATNetLookupResultLinux::Address() const {
	return mAddress;
}

class ATNetLookupWorkerLinux : public VDThread {
public:
	ATNetLookupWorkerLinux();
	~ATNetLookupWorkerLinux();

	bool Init();
	void Shutdown();

	vdrefptr<IATNetLookupResult> Lookup(const wchar_t *hostname, const wchar_t *service);

private:
	void ThreadRun() override;

	VDCriticalSection mMutex;
	VDSignal mWakeSignal;
	bool mbExitRequested = false;
	std::vector<vdrefptr<ATNetLookupResultLinux>> mPendingQueue;
};

ATNetLookupWorkerLinux::ATNetLookupWorkerLinux() {
}

ATNetLookupWorkerLinux::~ATNetLookupWorkerLinux() {
	Shutdown();
}

bool ATNetLookupWorkerLinux::Init() {
	mbExitRequested = false;
	return ThreadStart();
}

void ATNetLookupWorkerLinux::Shutdown() {
	vdsynchronized(mMutex) {
		mbExitRequested = true;
	}
	mWakeSignal.signal();
	ThreadWait();
}

vdrefptr<IATNetLookupResult> ATNetLookupWorkerLinux::Lookup(const wchar_t *hostname, const wchar_t *service) {
	vdrefptr<ATNetLookupResultLinux> result(new ATNetLookupResultLinux(hostname, service));

	vdsynchronized(mMutex) {
		mPendingQueue.push_back(result);
	}
	mWakeSignal.signal();

	return vdrefptr<IATNetLookupResult>(result);
}

void ATNetLookupWorkerLinux::ThreadRun() {
	for (;;) {
		vdrefptr<ATNetLookupResultLinux> item;

		vdsynchronized(mMutex) {
			if (mbExitRequested)
				break;

			if (!mPendingQueue.empty()) {
				item = std::move(mPendingQueue.front());
				mPendingQueue.erase(mPendingQueue.begin());
			}
		}

		if (item) {
			item->Resolve();
		} else {
			mWakeSignal.wait();
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// Public factory functions
///////////////////////////////////////////////////////////////////////////

bool ATSocketInit() {
	if (!g_pSocketWorker) {
		g_pSocketWorker = new ATNetSocketWorkerLinux();
		if (!g_pSocketWorker->Init()) {
			delete g_pSocketWorker;
			g_pSocketWorker = nullptr;
			return false;
		}
	}

	if (!g_pLookupWorker) {
		g_pLookupWorker = new ATNetLookupWorkerLinux();
		if (!g_pLookupWorker->Init()) {
			delete g_pLookupWorker;
			g_pLookupWorker = nullptr;
		}
	}

	return true;
}

void ATSocketPreShutdown() {
	delete g_pSocketWorker;
	g_pSocketWorker = nullptr;

	delete g_pLookupWorker;
	g_pLookupWorker = nullptr;
}

void ATSocketShutdown() {
	ATSocketPreShutdown();
}

vdrefptr<IATNetLookupResult> ATNetLookup(const wchar_t *hostname, const wchar_t *service) {
	if (!g_pLookupWorker)
		return nullptr;
	return g_pLookupWorker->Lookup(hostname, service);
}

vdrefptr<IATStreamSocket> ATNetConnect(const wchar_t *hostname, const wchar_t *service, bool dualStack) {
	if (!g_pLookupWorker || !g_pSocketWorker)
		return nullptr;

	vdrefptr<ATNetStreamSocketLinux> s(new ATNetStreamSocketLinux());

	auto lookup = g_pLookupWorker->Lookup(hostname, service);
	if (!lookup)
		return nullptr;

	lookup->SetOnCompleted(nullptr,
		[s, dualStack](const ATSocketAddress& addr) {
			if (addr.IsValid())
				s->Connect(addr, dualStack);
		},
		true
	);

	return vdrefptr<IATStreamSocket>(s);
}

vdrefptr<IATStreamSocket> ATNetConnect(const ATSocketAddress& address, bool dualStack) {
	if (!g_pSocketWorker)
		return nullptr;

	vdrefptr<ATNetStreamSocketLinux> s(new ATNetStreamSocketLinux());
	s->Connect(address, dualStack);
	return vdrefptr<IATStreamSocket>(s);
}

vdrefptr<IATListenSocket> ATNetListen(const ATSocketAddress& address, bool dualStack) {
	if (!g_pSocketWorker)
		return nullptr;

	vdrefptr<ATNetListenSocketLinux> s(new ATNetListenSocketLinux(address, dualStack));
	if (!s->IsValid())
		return nullptr;

	return vdrefptr<IATListenSocket>(s);
}

vdrefptr<IATListenSocket> ATNetListen(ATSocketAddressType addressType, uint16 port, bool dualStack) {
	ATSocketAddress address {};
	address.mType = addressType;
	address.mPort = port;

	if (addressType == ATSocketAddressType::IPv4) {
		address.mIPv4Address = 0;
	} else if (addressType == ATSocketAddressType::IPv6) {
		memset(address.mIPv6.mAddress, 0, sizeof address.mIPv6.mAddress);
		address.mIPv6.mScopeId = 0;
	}

	return ATNetListen(address, dualStack);
}

vdrefptr<IATDatagramSocket> ATNetBind(const ATSocketAddress& address, bool dualStack) {
	if (!g_pSocketWorker)
		return nullptr;

	vdrefptr<ATNetDatagramSocketLinux> s(new ATNetDatagramSocketLinux(address, dualStack));
	if (!s->IsValid())
		return nullptr;

	return vdrefptr<IATDatagramSocket>(s);
}
