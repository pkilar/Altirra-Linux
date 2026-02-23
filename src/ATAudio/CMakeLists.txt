# ATAudio - POKEY sound chip emulation and audio pipeline

set(ATAUDIO_SOURCES
	source/audioconvolutionplayer.cpp
	source/audiofilters.cpp
	source/audiosamplebuffer.cpp
	source/audiosampleplayer.cpp
	source/audiosamplepool.cpp
	source/pokey.cpp
	source/pokeyrenderer.cpp
	source/pokeysavestate.cpp
	source/pokeytables.cpp
	source/stdafx.cpp
)

# audiooutput.cpp has Win32 audio backend code — needs platform guard
if(WIN32)
	list(APPEND ATAUDIO_SOURCES
		source/audiooutput.cpp
		source/audiooutwasapi.cpp
		source/audiooutwaveout.cpp
		source/audiooutxa2.cpp
	)
else()
	# audiooutput.cpp excluded for now — Linux audio backend comes in Phase 4
	# The POKEY emulation itself is fully portable
endif()

add_library(ataudio STATIC ${ATAUDIO_SOURCES})

target_include_directories(ataudio
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(ataudio PUBLIC system atcore)
