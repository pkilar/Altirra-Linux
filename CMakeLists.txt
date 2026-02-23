cmake_minimum_required(VERSION 3.20)
project(Altirra VERSION 4.40 LANGUAGES CXX)

# Include shared compiler flags
include(cmake/AltirraCompilerFlags.cmake)

# Global include paths (mirrors VS solution include dirs)
include_directories(${CMAKE_SOURCE_DIR}/src/h)

# Foundation libraries (Phase 0)
add_subdirectory(src/system)
add_subdirectory(src/vdjson)

# Core emulation libraries (Phase 2)
add_subdirectory(src/Kasumi)
add_subdirectory(src/ATCore)
add_subdirectory(src/ATCPU)
add_subdirectory(src/ATEmulation)
add_subdirectory(src/ATAudio)
add_subdirectory(src/ATIO)
add_subdirectory(src/ATDevices)
add_subdirectory(src/ATNetwork)
add_subdirectory(src/ATNetworkSockets)
add_subdirectory(src/ATDebugger)
add_subdirectory(src/ATVM)
add_subdirectory(src/ATTest)

# Linux frontend (Phase 3-5) + Dear ImGui (Phase 7)
if(NOT WIN32)
	include(cmake/FetchImGui.cmake)
	add_subdirectory(src/AltirraShell)
	add_subdirectory(src/AltirraLinux)
endif()
