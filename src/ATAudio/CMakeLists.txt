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

if(WIN32)
	list(APPEND ATAUDIO_SOURCES
		source/audiooutput.cpp
		source/audiooutwasapi.cpp
		source/audiooutwaveout.cpp
		source/audiooutxa2.cpp
	)
else()
	find_package(SDL3 REQUIRED)
	list(APPEND ATAUDIO_SOURCES
		source/audiooutput.cpp
		source/audioout_sdl3.cpp
	)
endif()

add_library(ataudio STATIC ${ATAUDIO_SOURCES})

target_include_directories(ataudio
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
		${CMAKE_CURRENT_SOURCE_DIR}/source
)

target_link_libraries(ataudio PUBLIC system atcore)

if(NOT WIN32)
	target_link_libraries(ataudio PRIVATE SDL3::SDL3)
endif()
