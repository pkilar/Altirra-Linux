# ATCore - Core emulation framework

set(ATCORE_SOURCES
	source/address.cpp
	source/asyncdispatcherimpl.cpp
	source/atascii.cpp
	source/bussignal.cpp
	source/checksum.cpp
	source/checksum_intrin.cpp
	source/configvar.cpp
	source/consoleoutput.cpp
	source/crc.cpp
	source/decmath.cpp
	source/deferredevent.cpp
	source/deviceimpl.cpp
	source/deviceparentimpl.cpp
	source/devicesioimpl.cpp
	source/devicestorageimpl.cpp
	source/enumparse.cpp
	source/fft.cpp
	source/fft_scalar.cpp
	source/logging.cpp
	source/md5.cpp
	source/memoryutils.cpp
	source/notifylist.cpp
	source/profile.cpp
	source/progress.cpp
	source/propertyset.cpp
	source/randomization.cpp
	source/savestate.cpp
	source/scheduler.cpp
	source/serialization.cpp
	source/sioutils.cpp
	source/snapshotimpl.cpp
	source/stdafx.cpp
	source/vfs.cpp
)

# Platform-specific SIMD sources
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
	list(APPEND ATCORE_SOURCES
		source/fft_sse2.cpp
		source/fft_avx2.cpp
	)
	set_source_files_properties(source/fft_sse2.cpp PROPERTIES COMPILE_OPTIONS "-msse2")
	set_source_files_properties(source/fft_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
	list(APPEND ATCORE_SOURCES
		source/fft_arm64.cpp
		source/fft_neon.cpp
	)
endif()

# Platform-specific sources
if(WIN32)
	list(APPEND ATCORE_SOURCES
		source/timerserviceimpl_win32.cpp
		source/checksum_arm64.cpp
	)
else()
	# checksum_arm64.cpp uses windows.h + intrin.h; skip on Linux for now
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
		# TODO: Linux ARM64 checksum implementation
	endif()
endif()

add_library(atcore STATIC ${ATCORE_SOURCES})

target_include_directories(atcore
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(atcore PUBLIC system vdjson)
