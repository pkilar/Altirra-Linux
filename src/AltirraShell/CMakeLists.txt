# AltirraShell - SDL2/OpenGL Linux frontend library

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

set(ALTIRRASHELL_SOURCES
	source/display_sdl2.cpp
	source/input_sdl2.cpp
	source/joystick_sdl2.cpp
	source/imgui_manager.cpp
	source/debugger_imgui.cpp
	source/emulator_imgui.cpp
	source/enhancedtext_linux.cpp
	source/filedialog_linux.cpp
	source/commands_linux.cpp
)

add_library(altirrashell STATIC ${ALTIRRASHELL_SOURCES})

target_include_directories(altirrashell
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/AltirraLinux/h
		${CMAKE_SOURCE_DIR}/src/h
		${CMAKE_SOURCE_DIR}/src/Altirra/h
		${CMAKE_CURRENT_SOURCE_DIR}/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
		${CMAKE_SOURCE_DIR}/src/Altirra/autobuild_default
		${CMAKE_SOURCE_DIR}/src/Altirra/res
)

target_link_libraries(altirrashell
	PUBLIC
		system
		atcore
		atdebugger
		ataudio
		atio
		atui
		SDL2::SDL2
		OpenGL::GL
		imgui
)

if(AT_HAVE_FFMPEG)
	target_compile_definitions(altirrashell PRIVATE AT_HAVE_FFMPEG)
endif()
