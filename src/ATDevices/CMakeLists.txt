# ATDevices - Peripheral device implementations

add_library(atdevices STATIC
	source/blockdevwritefilter.cpp
	source/computereyes.cpp
	source/corvus.cpp
	source/devices.cpp
	source/diskdrive.cpp
	source/dongle.cpp
	source/exeloader.cpp
	source/loopback.cpp
	source/modemsound.cpp
	source/netserial.cpp
	source/paralleltoserial.cpp
	source/parfilewriter.cpp
	source/serialsplitter.cpp
	source/sioclock.cpp
	source/stdafx.cpp
	source/supersalt.cpp
	source/testdevicesiohs.cpp
	source/testdevicesiopoll.cpp
	source/videogenerator.cpp
)

target_include_directories(atdevices
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
		${CMAKE_CURRENT_SOURCE_DIR}/autogen
)

target_link_libraries(atdevices PUBLIC system atcore atemulation ataudio)
