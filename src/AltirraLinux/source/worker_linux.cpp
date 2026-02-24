//	Altirra - Atari 800/800XL/5200 emulator
//	Linux TCP/UDP bridge worker (replaces HWND-based worker.cpp)
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
#include <fstream>
#include <string>
#include <arpa/inet.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atnetwork/socket.h>
#include <at/atnetwork/emusocket.h>
#include <at/atnetworksockets/worker.h>
#include <at/atnetworksockets/nativesockets.h>

///////////////////////////////////////////////////////////////////////////
// ATNetSockBridgeHandlerLinux
//
// Bidirectional bridge between a native host TCP socket (IATStreamSocket
// from ATNetConnect) and an emulated Atari TCP socket (IATStreamSocket
// from the emulated TCP/IP stack).
///////////////////////////////////////////////////////////////////////////

class ATNetSockWorkerLinux;

class ATNetSockBridgeHandlerLinux final : public vdrefcounted<IATSocketHandler> {
public:
	ATNetSockBridgeHandlerLinux(ATNetSockWorkerLinux *parent,
		vdrefptr<IATStreamSocket> nativeSocket,
		IATStreamSocket *localSocket,
		uint32 srcIpAddr, uint16 srcPort,
		uint32 dstIpAddr, uint16 dstPort);

	uint32 GetSrcIpAddr() const { return mSrcIpAddr; }
	uint16 GetSrcPort() const { return mSrcPort; }
	uint32 GetDstIpAddr() const { return mDstIpAddr; }
	uint16 GetDstPort() const { return mDstPort; }

	ATSocketAddress GetHostAddr() const;

	void Shutdown();
	void SetLocalSocket(IATStreamSocket *s2);
	void SetSrcAddress(const ATSocketAddress& addr);

	// IATSocketHandler — called by emulated socket
	void OnSocketOpen() override;
	void OnSocketReadReady(uint32 len) override;
	void OnSocketWriteReady(uint32 len) override;
	void OnSocketClose() override;
	void OnSocketError() override;

private:
	void OnNativeSocketEvent(const ATSocketStatus& status);
	void TryCopyToNative();
	void TryCopyFromNative();

	uint32 mSrcIpAddr = 0;
	uint16 mSrcPort = 0;
	uint32 mDstIpAddr = 0;
	uint16 mDstPort = 0;
	ATNetSockWorkerLinux *mpParent;

	vdrefptr<IATStreamSocket> mpNativeSocket;
	vdrefptr<IATStreamSocket> mpLocalSocket;
	bool mbLocalClosed = false;
	bool mbNativeConnected = false;
	bool mbNativeClosed = false;

	uint32 mRecvBase = 0;
	uint32 mRecvLimit = 0;
	uint32 mSendBase = 0;
	uint32 mSendLimit = 0;

	char mRecvBuf[1024];
	char mSendBuf[1024];
};

///////////////////////////////////////////////////////////////////////////
// ATNetSockWorkerLinux
//
// Bridges the emulated TCP/IP stack to native host networking using
// platform-agnostic ATNet* socket interfaces.
///////////////////////////////////////////////////////////////////////////

class ATNetSockWorkerLinux final : public vdrefcounted<IATNetSockWorker>,
	public IATEmuNetSocketListener,
	public IATEmuNetUdpSocketListener
{
	friend class ATNetSockBridgeHandlerLinux;
public:
	ATNetSockWorkerLinux();
	~ATNetSockWorkerLinux();

	IATEmuNetSocketListener *AsSocketListener() override { return this; }
	IATEmuNetUdpSocketListener *AsUdpListener() override { return this; }

	bool Init(IATEmuNetUdpStack *udp, IATEmuNetTcpStack *tcp, bool externalAccess, uint32 forwardingAddr, uint16 forwardingPort);
	void Shutdown();

	void ResetAllConnections() override;
	bool GetHostAddressesForLocalAddress(bool tcp, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, ATSocketAddress& hostAddr, ATSocketAddress& remoteAddr) const override;

	// IATEmuNetSocketListener
	bool OnSocketIncomingConnection(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, IATStreamSocket *socket, IATSocketHandler **handler) override;

	// IATEmuNetUdpSocketListener
	void OnUdpDatagram(const ATEthernetAddr& srcHwAddr, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, const void *data, uint32 dataLen) override;

	void RemoveConnection(ATNetSockBridgeHandlerLinux *handler);

private:
	uint32 LookupDnsGateway() const;

	struct UdpConnection {
		uint32 mSrcIpAddr;
		uint32 mDstIpAddr;
		uint16 mSrcPort;
		uint16 mDstPort;

		bool operator==(const UdpConnection& o) const = default;
	};

	struct UdpConnectionHash {
		size_t operator()(const UdpConnection& c) const {
			return c.mSrcIpAddr + c.mDstIpAddr + c.mSrcPort + c.mDstPort;
		}
	};

	IATEmuNetTcpStack *mpTcpStack = nullptr;
	IATEmuNetUdpStack *mpUdpStack = nullptr;
	bool mbAllowExternalAccess = false;

	uint32 mForwardingAddr = 0;
	uint16 mForwardingPort = 0;

	vdrefptr<IATListenSocket> mpTcpListenSocket;

	std::vector<vdrefptr<ATNetSockBridgeHandlerLinux>> mTcpConnections;

	struct UdpEntry {
		UdpConnection conn;
		vdrefptr<IATDatagramSocket> socket;
	};
	std::vector<UdpEntry> mUdpSockets;
};

///////////////////////////////////////////////////////////////////////////
// ATNetSockBridgeHandlerLinux implementation
///////////////////////////////////////////////////////////////////////////

ATNetSockBridgeHandlerLinux::ATNetSockBridgeHandlerLinux(
	ATNetSockWorkerLinux *parent,
	vdrefptr<IATStreamSocket> nativeSocket,
	IATStreamSocket *localSocket,
	uint32 srcIpAddr, uint16 srcPort,
	uint32 dstIpAddr, uint16 dstPort)
	: mSrcIpAddr(srcIpAddr)
	, mSrcPort(srcPort)
	, mDstIpAddr(dstIpAddr)
	, mDstPort(dstPort)
	, mpParent(parent)
	, mpNativeSocket(std::move(nativeSocket))
	, mpLocalSocket(localSocket)
{
	if (!localSocket)
		mbNativeConnected = true;

	// Monitor native socket events
	mpNativeSocket->SetOnEvent(nullptr,
		[this](const ATSocketStatus& status) {
			OnNativeSocketEvent(status);
		},
		true
	);
}

ATSocketAddress ATNetSockBridgeHandlerLinux::GetHostAddr() const {
	if (mpNativeSocket)
		return mpNativeSocket->GetLocalAddress();
	return {};
}

void ATNetSockBridgeHandlerLinux::Shutdown() {
	if (mpNativeSocket) {
		mpNativeSocket->SetOnEvent(nullptr, nullptr, false);
		mpNativeSocket->CloseSocket(true);
		mpNativeSocket = nullptr;
	}

	mbNativeConnected = false;

	if (mpLocalSocket) {
		mpLocalSocket->CloseSocket(true);
		mpLocalSocket = nullptr;
	}
}

void ATNetSockBridgeHandlerLinux::SetLocalSocket(IATStreamSocket *s2) {
	mpLocalSocket = s2;
}

void ATNetSockBridgeHandlerLinux::SetSrcAddress(const ATSocketAddress& addr) {
	if (addr.GetType() == ATSocketAddressType::IPv4) {
		mSrcIpAddr = addr.mIPv4Address;
		mSrcPort = addr.mPort;
	}
}

void ATNetSockBridgeHandlerLinux::OnSocketOpen() {
}

void ATNetSockBridgeHandlerLinux::OnSocketReadReady(uint32 len) {
	TryCopyToNative();
}

void ATNetSockBridgeHandlerLinux::OnSocketWriteReady(uint32 len) {
	TryCopyFromNative();
}

void ATNetSockBridgeHandlerLinux::OnSocketClose() {
	mbLocalClosed = true;

	if (mpNativeSocket)
		mpNativeSocket->ShutdownSocket(true, false);

	if (mbNativeClosed)
		Shutdown();
}

void ATNetSockBridgeHandlerLinux::OnSocketError() {
	Shutdown();
}

void ATNetSockBridgeHandlerLinux::OnNativeSocketEvent(const ATSocketStatus& status) {
	if (status.mbClosed) {
		mbNativeClosed = true;

		TryCopyFromNative();

		if (mRecvBase == mRecvLimit && mpLocalSocket)
			mpLocalSocket->ShutdownSocket(true, false);

		if (mbLocalClosed) {
			if (mpLocalSocket) {
				mpLocalSocket->CloseSocket(false);
				mpLocalSocket = nullptr;
			}
			Shutdown();
		}
		return;
	}

	if (!status.mbConnecting && !mbNativeConnected) {
		mbNativeConnected = true;
		TryCopyToNative();
		TryCopyFromNative();
		return;
	}

	if (status.mbCanRead)
		TryCopyFromNative();

	if (status.mbCanWrite)
		TryCopyToNative();
}

void ATNetSockBridgeHandlerLinux::TryCopyToNative() {
	if (!mbNativeConnected || !mpNativeSocket || !mpLocalSocket)
		return;

	for (;;) {
		if (mSendBase == mSendLimit) {
			if (mbLocalClosed)
				break;

			sint32 actual = mpLocalSocket->Recv(mSendBuf, sizeof mSendBuf);
			if (actual <= 0)
				break;

			mSendBase = 0;
			mSendLimit = actual;
		}

		sint32 actual2 = mpNativeSocket->Send(mSendBuf + mSendBase, mSendLimit - mSendBase);
		if (actual2 <= 0)
			break;

		mSendBase += actual2;
	}
}

void ATNetSockBridgeHandlerLinux::TryCopyFromNative() {
	if (!mbNativeConnected || mbNativeClosed || !mpNativeSocket || !mpLocalSocket)
		return;

	for (;;) {
		if (mRecvBase == mRecvLimit) {
			sint32 actual = mpNativeSocket->Recv(mRecvBuf, sizeof mRecvBuf);
			if (actual <= 0)
				break;

			mRecvBase = 0;
			mRecvLimit = actual;
		}

		sint32 actual2 = mpLocalSocket->Send(mRecvBuf + mRecvBase, mRecvLimit - mRecvBase);
		if (actual2 <= 0)
			break;

		mRecvBase += actual2;
	}
}

///////////////////////////////////////////////////////////////////////////
// ATNetSockWorkerLinux implementation
///////////////////////////////////////////////////////////////////////////

ATNetSockWorkerLinux::ATNetSockWorkerLinux() {
}

ATNetSockWorkerLinux::~ATNetSockWorkerLinux() {
	Shutdown();
}

bool ATNetSockWorkerLinux::Init(IATEmuNetUdpStack *udp, IATEmuNetTcpStack *tcp, bool externalAccess, uint32 forwardingAddr, uint16 forwardingPort) {
	mpUdpStack = udp;
	mpTcpStack = tcp;
	mbAllowExternalAccess = externalAccess;

	udp->Bind(53, this);

	mForwardingAddr = forwardingAddr;
	mForwardingPort = forwardingPort;

	if (mForwardingAddr && mForwardingPort) {
		mpTcpListenSocket = ATNetListen(ATSocketAddressType::IPv4, mForwardingPort, false);

		if (mpTcpListenSocket) {
			mpTcpListenSocket->SetOnEvent(nullptr,
				[this](const ATSocketStatus& status) {
					if (!status.mbCanAccept)
						return;

					auto accepted = mpTcpListenSocket->Accept();
					if (!accepted)
						return;

					// Create a bridge: native incoming → emulated outbound
					ATSocketAddress remoteAddr = accepted->GetRemoteAddress();
					uint32 srcIp = 0;
					uint16 srcPort = 0;
					if (remoteAddr.GetType() == ATSocketAddressType::IPv4) {
						srcIp = remoteAddr.mIPv4Address;
						srcPort = remoteAddr.mPort;
					}

					vdrefptr<ATNetSockBridgeHandlerLinux> h(
						new ATNetSockBridgeHandlerLinux(
							this, std::move(accepted), nullptr,
							srcIp, srcPort,
							mForwardingAddr, mForwardingPort));

					vdrefptr<IATStreamSocket> emuSocket;
					if (!mpTcpStack->Connect(mForwardingAddr, mForwardingPort, *h, ~emuSocket)) {
						h->Shutdown();
						return;
					}

					h->SetLocalSocket(emuSocket);
					h->SetSrcAddress(emuSocket->GetLocalAddress());
					mTcpConnections.push_back(std::move(h));
				},
				true
			);
		}
	}

	return true;
}

void ATNetSockWorkerLinux::Shutdown() {
	ResetAllConnections();

	if (mpTcpListenSocket) {
		mpTcpListenSocket->CloseSocket(true);
		mpTcpListenSocket = nullptr;
	}

	if (mpUdpStack) {
		mpUdpStack->Unbind(53, this);
		mpUdpStack = nullptr;
	}
}

void ATNetSockWorkerLinux::ResetAllConnections() {
	for (auto& conn : mTcpConnections)
		conn->Shutdown();
	mTcpConnections.clear();

	for (auto& entry : mUdpSockets) {
		if (entry.socket)
			entry.socket->CloseSocket(true);
	}
	mUdpSockets.clear();
}

bool ATNetSockWorkerLinux::GetHostAddressesForLocalAddress(bool tcp, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, ATSocketAddress& hostAddr, ATSocketAddress& remoteAddr) const {
	if (tcp) {
		for (auto& conn : mTcpConnections) {
			if (conn->GetSrcIpAddr() == srcIpAddr && conn->GetSrcPort() == srcPort &&
				conn->GetDstIpAddr() == dstIpAddr && conn->GetDstPort() == dstPort)
			{
				hostAddr = conn->GetHostAddr();
				return hostAddr.IsValid();
			}
		}
	}

	return false;
}

bool ATNetSockWorkerLinux::OnSocketIncomingConnection(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, IATStreamSocket *socket, IATSocketHandler **handler) {
	uint32 redirectedDstIpAddr = dstIpAddr;
	if (mpUdpStack->GetIpStack()->IsLocalOrBroadcastAddress(dstIpAddr)) {
		redirectedDstIpAddr = 0x7F000001;  // 127.0.0.1 in host byte order
	} else if (!mbAllowExternalAccess) {
		return false;
	}

	// Convert emulated IP (network byte order big-endian) to ATSocketAddress
	ATSocketAddress addr = ATSocketAddress::CreateIPv4(redirectedDstIpAddr, dstPort);
	vdrefptr<IATStreamSocket> nativeSocket = ATNetConnect(addr);
	if (!nativeSocket)
		return false;

	vdrefptr<ATNetSockBridgeHandlerLinux> h(
		new ATNetSockBridgeHandlerLinux(
			this, std::move(nativeSocket), socket,
			srcIpAddr, srcPort, dstIpAddr, dstPort));

	mTcpConnections.push_back(h);

	h->AddRef();
	*handler = h.release();
	return true;
}

void ATNetSockWorkerLinux::OnUdpDatagram(const ATEthernetAddr& srcHwAddr, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, const void *data, uint32 dataLen) {
	uint32 redirectedDstIpAddr = dstIpAddr;
	bool redirected = false;

	if (dstPort == 53) {
		if (!mbAllowExternalAccess)
			return;

		// Look up system DNS server from /etc/resolv.conf
		uint32 dnsAddr = LookupDnsGateway();
		if (dnsAddr) {
			redirectedDstIpAddr = dnsAddr;
			redirected = true;
		}
	} else {
		if (mpUdpStack->GetIpStack()->IsLocalOrBroadcastAddress(dstIpAddr)) {
			redirectedDstIpAddr = 0x7F000001;
			redirected = true;
		}
	}

	// Find or create UDP socket for this source
	UdpConnection conn {};
	conn.mSrcIpAddr = srcIpAddr;
	conn.mSrcPort = srcPort;
	if (redirected) {
		conn.mDstIpAddr = dstIpAddr;
		conn.mDstPort = dstPort;
	}

	IATDatagramSocket *udpSock = nullptr;
	for (auto& entry : mUdpSockets) {
		if (entry.conn == conn) {
			udpSock = entry.socket;
			break;
		}
	}

	if (!udpSock) {
		// Bind a new UDP socket on an ephemeral port
		uint16 bindPort = (!dstIpAddr && dstPort) ? dstPort : 0;
		auto newSock = ATNetBind(ATSocketAddress::CreateIPv4(bindPort), false);
		if (!newSock)
			return;

		// Set up receive callback to forward replies back to emulated stack
		UdpConnection replyConn = conn;
		newSock->SetOnEvent(nullptr,
			[this, replyConn, sock = newSock.get()](const ATSocketStatus& status) {
				if (!status.mbCanRead)
					return;

				char buf[4096];
				ATSocketAddress fromAddr;
				sint32 len = sock->RecvFrom(fromAddr, buf, sizeof buf);

				if (len > 0 && mpUdpStack) {
					uint32 replySrcIp = replyConn.mDstIpAddr ? replyConn.mDstIpAddr : 0;
					uint16 replySrcPort = replyConn.mDstPort ? replyConn.mDstPort : 0;

					if (!replySrcIp && fromAddr.GetType() == ATSocketAddressType::IPv4)
						replySrcIp = fromAddr.mIPv4Address;
					if (!replySrcPort)
						replySrcPort = fromAddr.mPort;

					mpUdpStack->SendDatagram(
						replySrcIp, replySrcPort,
						replyConn.mSrcIpAddr, replyConn.mSrcPort,
						buf, len);
				}
			},
			false
		);

		UdpEntry entry;
		entry.conn = conn;
		entry.socket = newSock;
		mUdpSockets.push_back(std::move(entry));

		udpSock = newSock;
	}

	// Send the datagram
	// redirectedDstIpAddr is in host byte order (ATSocketAddress stores host order)
	ATSocketAddress dstAddr = ATSocketAddress::CreateIPv4(redirectedDstIpAddr, dstPort);
	udpSock->SendTo(dstAddr, data, dataLen);
}

void ATNetSockWorkerLinux::RemoveConnection(ATNetSockBridgeHandlerLinux *handler) {
	auto it = std::find_if(mTcpConnections.begin(), mTcpConnections.end(),
		[handler](const auto& p) { return p == handler; });
	if (it != mTcpConnections.end())
		mTcpConnections.erase(it);
}

uint32 ATNetSockWorkerLinux::LookupDnsGateway() const {
	std::ifstream f("/etc/resolv.conf");
	if (!f)
		return 0;

	std::string line;
	while (std::getline(f, line)) {
		if (line.compare(0, 11, "nameserver ") == 0) {
			std::string addr = line.substr(11);
			// Trim whitespace
			while (!addr.empty() && (addr.back() == ' ' || addr.back() == '\t' || addr.back() == '\r'))
				addr.pop_back();

			// Parse IPv4 address
			struct in_addr ia;
			if (inet_pton(AF_INET, addr.c_str(), &ia) == 1) {
				return ntohl(ia.s_addr);
			}
		}
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////
// Factory function
///////////////////////////////////////////////////////////////////////////

void ATCreateNetSockWorker(IATEmuNetUdpStack *udp, IATEmuNetTcpStack *tcp, bool externalAccess, uint32 forwardingAddr, uint16 forwardingPort, IATNetSockWorker **pp) {
	ATNetSockWorkerLinux *p = new ATNetSockWorkerLinux;

	if (!p->Init(udp, tcp, externalAccess, forwardingAddr, forwardingPort)) {
		delete p;
		throw MyMemoryError();
	}

	p->AddRef();
	*pp = p;
}
