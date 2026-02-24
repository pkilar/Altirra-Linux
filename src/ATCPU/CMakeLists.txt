# ATCPU - CPU emulators (6502, 65C02, 65C816, 6809, Z80, 8048, 8051)

add_library(atcpu STATIC
	source/co6502.cpp
	source/co6502machine.cpp
	source/co65802.cpp
	source/co6809.cpp
	source/co8048.cpp
	source/co8051.cpp
	source/coz80.cpp
	source/decode6502.cpp
	source/decode65816.cpp
	source/decode6809.cpp
	source/decodez80.cpp
	source/memorymap.cpp
	source/stdafx.cpp
)

target_include_directories(atcpu
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(atcpu PUBLIC system)
