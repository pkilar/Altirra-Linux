# ATTest - Test executable

set(ATTEST_SOURCES
	source/blob.cpp
	source/stdafx.cpp
	source/TestCoProc_6502.cpp
	source/TestCore_FFT.cpp
	source/TestCore_MD5.cpp
	source/TestCore_VFS.cpp
	source/TestDebugger_HistoryTree.cpp
	source/TestDebugger_SymbolIO.cpp
	source/TestEmu_PokeyPots.cpp
	source/TestEmu_PokeyTimers.cpp
	source/TestIO_DiskImage.cpp
	source/TestIO_FLAC.cpp
	source/TestIO_TapeWrite.cpp
	source/TestKasumi_Pixmap.cpp
	source/TestKasumi_Resampler.cpp
	source/TestKasumi_Uberblit.cpp
	source/TestSystem_CRC.cpp
	source/TestSystem_Exception.cpp
	source/TestSystem_FastVector.cpp
	source/TestSystem_Filesys.cpp
	source/TestSystem_Function.cpp
	source/TestSystem_HashMap.cpp
	source/TestSystem_HashSet.cpp
	source/TestSystem_Int128.cpp
	source/TestSystem_Math.cpp
	source/TestSystem_VecMath.cpp
	source/TestSystem_Vector.cpp
	source/TestSystem_Zip.cpp
)

if(WIN32)
	list(APPEND ATTEST_SOURCES
		source/main.cpp
		source/utils.cpp              # Win32 test utilities (IsDebuggerPresent, etc.)
		source/TestCore_Checksum.cpp  # Uses Windows BCrypt API
		source/TestEmu_PCLink.cpp     # Uses Altirra UI headers
		source/TestIO_Vorbis.cpp      # Uses COM/mmsystem
		source/TestIO_VirtFAT32.cpp   # Uses Altirra UI headers
		source/TestMisc_TTF.cpp       # Uses Altirra UI headers
		source/TestNet_NativeDatagramLiveTest.cpp
		source/TestNet_NativeSockets.cpp  # Uses Winsock2
		source/TestTrace_CPU.cpp      # Uses Altirra UI headers
		source/TestTrace_IO.cpp       # Uses Altirra UI headers
		source/TestUI_TextDOM.cpp     # Uses Altirra UI headers
	)
else()
	list(APPEND ATTEST_SOURCES source/main_linux.cpp)
endif()

add_executable(attest ${ATTEST_SOURCES})

target_include_directories(attest
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

# Enable test infrastructure macros
target_compile_definitions(attest PRIVATE AT_TESTS_FORCE_ENABLED)

target_link_libraries(attest
	system
	vdjson
	atcore
	atcpu
	ataudio
	atemulation
	atio
	atdevices
	atnetwork
	atnetsockets
	atdebugger
	atvm
	kasumi
	pthread
)
