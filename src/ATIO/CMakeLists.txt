# ATIO - I/O subsystem (disk/cassette/cartridge image formats, WAV, FLAC, Vorbis)

add_library(atio STATIC
	source/atfs.cpp
	source/audioreader.cpp
	source/audioreaderflac.cpp
	source/audioreaderflac_x86.cpp
	source/audioreadervorbis.cpp
	source/audioreaderwav.cpp
	source/audioutils.cpp
	source/blobimage.cpp
	source/cartridgeimage.cpp
	source/cartridgetypes.cpp
	source/cassetteaudiofilters.cpp
	source/cassetteblock.cpp
	source/cassettedecoder.cpp
	source/cassetteimage.cpp
	source/diskfs.cpp
	source/diskfsarc.cpp
	source/diskfscpm.cpp
	source/diskfsdos2.cpp
	source/diskfsdos3.cpp
	source/diskfssdx2.cpp
	source/diskfssdx2util.cpp
	source/diskfsutil.cpp
	source/diskimage.cpp
	source/image.cpp
	source/partitiondiskview.cpp
	source/partitiontable.cpp
	source/programimage.cpp
	source/savestate.cpp
	source/stdafx.cpp
	source/tapepiecetable.cpp
	source/vorbisbitreader.cpp
	source/vorbisdecoder.cpp
	source/vorbismisc.cpp
	source/wav.cpp
)

# ARM64-specific FLAC
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
	target_sources(atio PRIVATE source/audioreaderflac_arm64.cpp)
endif()

target_include_directories(atio
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
		${CMAKE_CURRENT_SOURCE_DIR}/autogen
)

target_link_libraries(atio PUBLIC system atcore)
