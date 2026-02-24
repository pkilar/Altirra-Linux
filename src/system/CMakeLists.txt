# VD2 System Library - Linux build

# Portable sources (no Windows dependencies)
set(SYSTEM_PORTABLE_SOURCES
	source/binary.cpp
	source/bitmath.cpp
	source/cache.cpp
	source/cmdline.cpp
	source/constexpr.cpp
	source/Error.cpp
	source/event.cpp
	source/filestream.cpp
	source/Fraction.cpp
	source/function.cpp
	source/halffloat.cpp
	source/hash.cpp
	source/linearalloc.cpp
	source/math.cpp
	source/refcount.cpp
	source/registrymemory.cpp
	source/stdaccel.cpp
	source/strutil.cpp
	source/text.cpp
	source/tls.cpp
	source/vdalloc.cpp
	source/VDString.cpp
	source/vdstl.cpp
	source/vdstl_hash.cpp
	source/vdstl_hashtable.cpp
	source/vectors.cpp
	source/zip.cpp
)

# Platform-specific sources
if(WIN32)
	set(SYSTEM_PLATFORM_SOURCES
		source/cpuaccel.cpp
		source/date.cpp
		source/debug.cpp
		source/debugx86.cpp
		source/error_win32.cpp
		source/file.cpp
		source/fileasync.cpp
		source/filesys.cpp
		source/filewatcher.cpp
		source/int128.cpp
		source/memory.cpp
		source/process.cpp
		source/registry.cpp
		source/stdafx.cpp
		source/thread.cpp
		source/thunk.cpp
		source/time.cpp
		source/w32assist.cpp
	)
else()
	set(SYSTEM_PLATFORM_SOURCES
		source/cpuaccel_linux.cpp
		source/date_linux.cpp
		source/debug_linux.cpp
		source/debugx86.cpp
		source/error_linux.cpp
		source/file_linux.cpp
		source/filesys_linux.cpp
		source/int128.cpp
		source/memory.cpp
		source/registry_linux.cpp
		source/time_linux.cpp
		source/thread_linux.cpp
	)
endif()

add_library(system STATIC
	${SYSTEM_PORTABLE_SOURCES}
	${SYSTEM_PLATFORM_SOURCES}
)

target_include_directories(system
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

# On Linux, we don't use PCH - the stdafx.h is included via the source files
# and our modified version is platform-aware
