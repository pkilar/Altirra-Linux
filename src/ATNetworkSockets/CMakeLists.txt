# ATNetworkSockets - Native socket layer

set(ATNETSOCKETS_SOURCES
	source/stdafx.cpp
)

# vxlantunnel uses Win32 window messaging (WM_USER, HWND, LRESULT)
if(WIN32)
	list(APPEND ATNETSOCKETS_SOURCES source/vxlantunnel.cpp)
endif()

if(WIN32)
	list(APPEND ATNETSOCKETS_SOURCES
		source/lookupworker.cpp
		source/nativesockets.cpp
		source/socketutils_win32.cpp
		source/socketworker.cpp
		source/worker.cpp
	)
else()
	# The Win32 socket files use Winsock2 API heavily.
	# Linux BSD socket implementation will be added in a later phase.
	# For now, include stubs so the library links.
	list(APPEND ATNETSOCKETS_SOURCES
		source/nativesockets_linux.cpp
	)
endif()

add_library(atnetsockets STATIC ${ATNETSOCKETS_SOURCES})

target_include_directories(atnetsockets
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(atnetsockets PUBLIC system atcore atnetwork)
