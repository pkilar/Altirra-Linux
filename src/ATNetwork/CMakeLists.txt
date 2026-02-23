# ATNetwork - Ethernet emulation and TCP/IP stack

add_library(atnetwork STATIC
	source/dhcpd.cpp
	source/ethernetbus.cpp
	source/ethernetframe.cpp
	source/gatewayserver.cpp
	source/ipstack.cpp
	source/socket.cpp
	source/stdafx.cpp
	source/tcp.cpp
	source/tcpstack.cpp
	source/udp.cpp
	source/udpstack.cpp
)

target_include_directories(atnetwork
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(atnetwork PUBLIC system atcore)
