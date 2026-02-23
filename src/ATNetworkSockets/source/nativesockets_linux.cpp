// Altirra - Atari 800/XL/XE/5200 emulator
// Linux native sockets stub implementation
// Copyright (C) 2024 Avery Lee, All Rights Reserved.
// Linux port additions are licensed under the same terms as the original.

#include <stdafx.h>
#include <at/atnetworksockets/nativesockets.h>

// Stub implementations — full BSD socket support will be added later.

bool ATSocketInit() {
	// No WSAStartup equivalent needed on Linux
	return true;
}

void ATSocketPreShutdown() {
}

void ATSocketShutdown() {
}

vdrefptr<IATNetLookupResult> ATNetLookup(const wchar_t *hostname, const wchar_t *service) {
	return nullptr;
}

vdrefptr<IATStreamSocket> ATNetConnect(const wchar_t *hostname, const wchar_t *service, bool dualStack) {
	return nullptr;
}

vdrefptr<IATStreamSocket> ATNetConnect(const ATSocketAddress& address, bool dualStack) {
	return nullptr;
}

vdrefptr<IATListenSocket> ATNetListen(const ATSocketAddress& address, bool dualStack) {
	return nullptr;
}

vdrefptr<IATListenSocket> ATNetListen(ATSocketAddressType addressType, uint16 port, bool dualStack) {
	return nullptr;
}

vdrefptr<IATDatagramSocket> ATNetBind(const ATSocketAddress& address, bool dualStack) {
	return nullptr;
}
