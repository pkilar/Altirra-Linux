# AltirraShell - SDL2/OpenGL Linux frontend library

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

set(ALTIRRASHELL_SOURCES
	source/display_sdl2.cpp
	source/input_sdl2.cpp
	source/joystick_sdl2.cpp
)

add_library(altirrashell STATIC ${ALTIRRASHELL_SOURCES})

target_include_directories(altirrashell
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
		${CMAKE_SOURCE_DIR}/src/Altirra/h
		${CMAKE_CURRENT_SOURCE_DIR}/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(altirrashell
	PUBLIC
		system
		atcore
		ataudio
		SDL2::SDL2
		OpenGL::GL
)
