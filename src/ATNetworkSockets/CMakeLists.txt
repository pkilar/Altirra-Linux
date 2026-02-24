# ATNetworkSockets - Native socket layer

set(ATNETSOCKETS_SOURCES
	source/stdafx.cpp
)

# vxlantunnel uses platform-agnostic IATDatagramSocket interface
list(APPEND ATNETSOCKETS_SOURCES source/vxlantunnel.cpp)

if(WIN32)
	list(APPEND ATNETSOCKETS_SOURCES
		source/lookupworker.cpp
		source/nativesockets.cpp
		source/socketutils_win32.cpp
		source/socketworker.cpp
		source/worker.cpp
	)
else()
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
