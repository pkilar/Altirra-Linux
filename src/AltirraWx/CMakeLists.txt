# AltirraWx - wxWidgets-based Linux frontend for Altirra emulator
#
# This target replaces AltirraShell (ImGui) + AltirraLinux (SDL3 main loop)
# with a wxWidgets-based application. SDL3 is kept for audio and gamepad only.

# wxWidgets 3.2+ required for proper CMake integration
find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base gl aui stc propgrid)
include(${wxWidgets_USE_FILE})

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

# Collect all Altirra source files (same as AltirraLinux/CMakeLists.txt)
file(GLOB ALTIRRA_ALL_SOURCES
	${CMAKE_SOURCE_DIR}/src/Altirra/source/*.cpp
)

# Exclude Windows-only files (same list as AltirraLinux/CMakeLists.txt)
set(ALTIRRA_EXCLUDE_PATTERNS
	# Windows entry point
	"main.cpp"

	# Windows implementations (we have Linux replacements)
	"console.cpp"
	"oshelper.cpp"
	"joystick.cpp"

	# Win32-specific implementations
	"asyncdownloader_win32.cpp"
	"devxcmdmountvhd_win32.cpp"
	"pclink_win32.cpp"
	"pipeserial_win32.cpp"
	"updatefeed_win32.cpp"
	"uidiskexplorer_win32.cpp"
	"uidisplayaccessibility_win32.cpp"

	# All ui*.cpp files — Windows UI
	"ui.cpp"
	"uiabout.cpp"
	"uiaccessors.cpp"
	"uiadvancedconfiguration.cpp"
	"uiaudiooptions.cpp"
	"uicalibrationscreen.cpp"
	"uicaptionupdater.cpp"
	"uicartmapper.cpp"
	"uicheater.cpp"
	"uiclipboard.cpp"
	"uicolors.cpp"
	"uicommondialogs.cpp"
	"uicompat.cpp"
	"uicompatdb.cpp"
	"uiconf850.cpp"
	"uiconfblackbox.cpp"
	"uiconfdev1020.cpp"
	"uiconfdev1030.cpp"
	"uiconfdev1400xl.cpp"
	"uiconfdev815.cpp"
	"uiconfdevamdc.cpp"
	"uiconfdevatr8000.cpp"
	"uiconfdevblackboxfloppy.cpp"
	"uiconfdevcomputereyes.cpp"
	"uiconfdevcorvus.cpp"
	"uiconfdevcovox.cpp"
	"uiconfdevcustom.cpp"
	"uiconfdevdongle.cpp"
	"uiconfdevhappy810.cpp"
	"uiconfdevhdvirtfat32.cpp"
	"uiconfdevkmkjzide2.cpp"
	"uiconfdevkmkjzide.cpp"
	"uiconfdevmultiplexer.cpp"
	"uiconfdevnetserial.cpp"
	"uiconfdevparfilewriter.cpp"
	"uiconfdevpercom.cpp"
	"uiconfdevpipeserial.cpp"
	"uiconfdevpocketmodem.cpp"
	"uiconfdevprinter.cpp"
	"uiconfdevside3.cpp"
	"uiconfdevsx212.cpp"
	"uiconfdevvbxe.cpp"
	"uiconfdevvideostillimage.cpp"
	"uiconfdevxep80.cpp"
	"uiconfdiskdrivefull.cpp"
	"uiconfgeneric.cpp"
	"uiconfharddisk.cpp"
	"uiconfiguresystem.cpp"
	"uiconfirm.cpp"
	"uiconfmodem.cpp"
	"uiconfmyide2.cpp"
	"uiconfregister.cpp"
	"uiconfsoundboard.cpp"
	"uiconfveronica.cpp"
	"uidbgbreakpoints.cpp"
	"uidbgcallstack.cpp"
	"uidbgconsole.cpp"
	"uidbgdebugdisplay.cpp"
	"uidbgdisasm.cpp"
	"uidbghelp.cpp"
	"uidbghistory.cpp"
	"uidbgmemory.cpp"
	"uidbgpane.cpp"
	"uidbgprinteroutput.cpp"
	"uidbgregisters.cpp"
	"uidbgsource.cpp"
	"uidbgtargets.cpp"
	"uidbgwatch.cpp"
	"uidebugfont.cpp"
	"uidevices.cpp"
	"uidisk.cpp"
	"uidiskexplorer.cpp"
	"uidisplay.cpp"
	"uidisplayaccessibility.cpp"
	"uidisplaytool.cpp"
	"uidragdrop.cpp"
	"uidragoncart.cpp"
	"uiemuerror.cpp"
	"uienhancedtext.cpp"
	"uiexportroms.cpp"
	"uifileassoc.cpp"
	"uifilebrowser.cpp"
	"uifilefilters.cpp"
	"uifirmware.cpp"
	"uifirmwaremenu.cpp"
	"uifirmwarescan.cpp"
	"uifrontend.cpp"
	"uifullscreenmode.cpp"
	"uihistoryview.cpp"
	"uihostdevice.cpp"
	"uiinput.cpp"
	"uiinputrebind.cpp"
	"uiinputsetup.cpp"
	"uiinstance.cpp"
	"uikeyboard.cpp"
	"uikeyboardcustomize.cpp"
	"uilightpen.cpp"
	"uimainwindow.cpp"
	"uimenu.cpp"
	"uimessagebox.cpp"
	"uimrulist.cpp"
	"uionscreenkeyboard.cpp"
	"uipageddialog.cpp"
	"uipclink.cpp"
	"uiphysdisk.cpp"
	"uiportmenus.cpp"
	"uiprofilemenu.cpp"
	"uiprofiler.cpp"
	"uiprofiles.cpp"
	"uiprogress.cpp"
	"uiqueue.cpp"
	"uirender.cpp"
	"uisavestate.cpp"
	"uiscreeneffects.cpp"
	"uisettingsmain.cpp"
	"uisettingswindow.cpp"
	"uisetupwizard.cpp"
	"uitapecontrol.cpp"
	"uitapeeditor.cpp"
	"uitraceviewer.cpp"
	"uiupdatecheck.cpp"
	"uiverifier.cpp"
	"uivideodisplaywindow.cpp"
	"uivideooutputmenu.cpp"
	"uivideorecording.cpp"

	# stdafx.cpp — we use our own
	"stdafx.cpp"

	# Files that depend heavily on Windows UI/shell APIs
	"about.cpp"
	"browser.cpp"
	"directorywatcher.cpp"
	"updatecheck.cpp"
	"updatefeed.cpp"

	# MSVC-only debug allocator (uses crtdbg.h)
	"allocator.cpp"

	# ARM NEON-only (not relevant on x86 Linux)
	"artifacting_neon.cpp"
	"artifacting_ntsc_neon.cpp"
	"artifacting_pal_neon.cpp"

	# MSVC inline assembly files (intrin versions used instead)
	"artifacting_ntsc_sse2.cpp"
	"artifacting_pal_sse2.cpp"

	# All command handlers — depend on Win32 UI dialogs/native UI
	"cmdaudio.cpp"
	"cmdcart.cpp"
	"cmdcassette.cpp"
	"cmdcpu.cpp"
	"cmddebug.cpp"
	"cmdinput.cpp"
	"cmdoptions.cpp"
	"cmds.cpp"
	"cmdsystem.cpp"
	"cmdtools.cpp"
	"cmdview.cpp"
	"cmdwindow.cpp"

	# Windows-only implementations
	"debuggerautotest.cpp"
	"idephysdisk.cpp"
	"midimate.cpp"
	"modemtcp.cpp"
	"profilerui.cpp"
	"startuplogger.cpp"
	"texteditor.cpp"
	"tracenative.cpp"
)

# Build the filtered source list
set(ALTIRRA_SOURCES "")
foreach(src ${ALTIRRA_ALL_SOURCES})
	get_filename_component(fname ${src} NAME)
	set(excluded FALSE)
	foreach(pattern ${ALTIRRA_EXCLUDE_PATTERNS})
		if(fname STREQUAL pattern)
			set(excluded TRUE)
			break()
		endif()
	endforeach()
	if(NOT excluded)
		list(APPEND ALTIRRA_SOURCES ${src})
	endif()
endforeach()

# Build firmware ROMs with MADS at configure time
find_program(MADS_EXECUTABLE mads)

# Helper macro to assemble a single ROM with MADS
macro(mads_assemble OUTPUT_VAR OUTPUT_NAME)
	set(${OUTPUT_VAR} "")
	if(MADS_EXECUTABLE)
		set(${OUTPUT_VAR} "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}")
		execute_process(
			COMMAND ${MADS_EXECUTABLE} ${ARGN}
					-o:${${OUTPUT_VAR}}
			RESULT_VARIABLE _MADS_RESULT
			OUTPUT_VARIABLE _MADS_OUTPUT
			ERROR_VARIABLE _MADS_ERROR
		)
		if(NOT _MADS_RESULT EQUAL 0)
			message(WARNING "MADS failed to assemble ${OUTPUT_NAME}: ${_MADS_ERROR}")
			set(${OUTPUT_VAR} "")
		else()
			message(STATUS "Assembled ${OUTPUT_NAME} -> ${${OUTPUT_VAR}}")
		endif()
	endif()
endmacro()

set(KERNEL_SRC ${CMAKE_SOURCE_DIR}/src/Kernel)
set(KERNEL_SHARED ${KERNEL_SRC}/source/Shared)

# ATDiskLoader128 (IDR_DISKLOADER128 = 150)
mads_assemble(DISKLOADER128_BIN atdiskloader128.bin
	-d:MEMORY=128 -s -p -b:$0700
	${KERNEL_SRC}/source/ATDiskLoader/main.s)

# Kernel ROMs
mads_assemble(KERNEL_BIN kernel.rom
	-d:_KERNEL_XLXE=0 -s -p -i:${KERNEL_SHARED} -b:$d800
	${KERNEL_SRC}/source/main.xasm)

mads_assemble(KERNELXL_BIN kernelxl.rom
	-d:_KERNEL_XLXE=1 -s -p -i:${KERNEL_SHARED} -b:$c000
	${KERNEL_SRC}/source/main.xasm)

mads_assemble(KERNEL816_BIN kernel816.rom
	-d:_KERNEL_XLXE=1 -d:_KERNEL_816=1 -s -p -i:${KERNEL_SHARED} -b:$c000
	${KERNEL_SRC}/source/main.xasm)

mads_assemble(SUPERKERNEL_BIN superkernel.rom
	-s -p -i:${KERNEL_SHARED} -b:$f800
	${KERNEL_SRC}/source/SuperKernel/main.xasm)

mads_assemble(NOKERNEL_BIN nokernel.rom
	-s -p -i:${KERNEL_SHARED} -b:$c000
	${KERNEL_SRC}/source/NoKernel/main.xasm)

mads_assemble(NOCARTRIDGE_BIN nocartridge.rom
	-s -p -b:$b000
	${KERNEL_SRC}/source/NoCartridge/main.s)

mads_assemble(NOHDBIOS_BIN nohdbios.rom
	-s -p -b:$d800
	${KERNEL_SRC}/source/NoHDBIOS/main.s)

mads_assemble(NOGAME_BIN nogame.rom
	-s -p -b:$a000
	${KERNEL_SRC}/source/NoGame/main.s)

mads_assemble(ATBASIC_BIN atbasic.bin
	-c -s -d:CART=1 -b:$a000
	${CMAKE_SOURCE_DIR}/src/ATBasic/source/atbasic.s)

if(NOT MADS_EXECUTABLE)
	message(STATUS "MADS not found — kernel ROMs and firmware will not be embedded")
endif()

# Embed resource files from Altirra/res at configure time
include(${CMAKE_SOURCE_DIR}/cmake/EmbedResources.cmake)

# Build STUFF resource list
set(STUFF_RESOURCES
	112 changes.txt
	143 dbghelp.txt
	164 about.txt
	166 trackstep.pcm
	167 DiskSpin.pcm
	168 TrackStep2.pcm
	169 menu_default.txt
	201 TrackStep3.pcm
	223 compatdb.atcpengine
	225 romset.html
	235 cmdhelp.txt
	253 speaker-click.pcm
	260 dbghelp-template.html
	261 1030relay.pcm
	264 printer-1029-pin.pcm
	265 printer-1029-platen.pcm
	266 printer-1029-retract.pcm
	267 printer-1029-home.pcm
	268 printer-1025-feed.pcm
)
if(DISKLOADER128_BIN)
	list(APPEND STUFF_RESOURCES 150 ${DISKLOADER128_BIN})
endif()
if(KERNEL_BIN)
	list(APPEND STUFF_RESOURCES 110 ${KERNEL_BIN})
endif()
if(KERNELXL_BIN)
	list(APPEND STUFF_RESOURCES 174 ${KERNELXL_BIN})
endif()
if(KERNEL816_BIN)
	list(APPEND STUFF_RESOURCES 236 ${KERNEL816_BIN})
endif()
if(SUPERKERNEL_BIN)
	list(APPEND STUFF_RESOURCES 136 ${SUPERKERNEL_BIN})
endif()
if(NOKERNEL_BIN)
	list(APPEND STUFF_RESOURCES 124 ${NOKERNEL_BIN})
endif()
if(NOCARTRIDGE_BIN)
	list(APPEND STUFF_RESOURCES 138 ${NOCARTRIDGE_BIN})
endif()
if(NOHDBIOS_BIN)
	list(APPEND STUFF_RESOURCES 154 ${NOHDBIOS_BIN})
endif()
if(NOGAME_BIN)
	list(APPEND STUFF_RESOURCES 162 ${NOGAME_BIN})
endif()
if(ATBASIC_BIN)
	list(APPEND STUFF_RESOURCES 125 ${ATBASIC_BIN})
endif()

embed_resources(
	OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/embedded_resources.cpp
	RESOURCE_DIR ${CMAKE_SOURCE_DIR}/src/Altirra/res
	STUFF ${STUFF_RESOURCES}
	PNG
		231 traceViewerToolbar.png
		233 profilerToolbar.png
		237 warning.png
		272 firmware.png
)

# wxWidgets frontend sources
set(WX_SOURCES
	source/main_wx.cpp
	source/mainframe.cpp
	source/menubar.cpp
	source/display_wx.cpp
	source/input_wx.cpp
	source/dialog_system.cpp
	source/dialog_cpu.cpp
	source/dialog_boot.cpp
	source/dialog_keyboard.cpp
	source/dialog_audio.cpp
	source/dialog_video.cpp
	source/dialog_cassette.cpp
	source/dialog_firmware.cpp
	source/dialog_cartridge.cpp
	source/dialog_devices.cpp
	source/dialog_misc.cpp
	source/debugger_wx.cpp
)

# Linux replacement implementations (shared with AltirraLinux target)
set(LINUX_SOURCES
	${CMAKE_SOURCE_DIR}/src/AltirraLinux/source/oshelper_linux.cpp
	source/console_wx.cpp
	${CMAKE_SOURCE_DIR}/src/AltirraLinux/source/stubs_linux.cpp
	${CMAKE_SOURCE_DIR}/src/AltirraLinux/source/modemtcp_linux.cpp
	${CMAKE_SOURCE_DIR}/src/AltirraLinux/source/uikeyboard_linux.cpp
	${CMAKE_SOURCE_DIR}/src/AltirraLinux/source/worker_linux.cpp
	${CMAKE_CURRENT_BINARY_DIR}/embedded_resources.cpp
)

# Also include joystick_sdl3.cpp and commands_linux.cpp from AltirraShell
# (these are UI-framework-agnostic)
set(SHELL_SHARED_SOURCES
	${CMAKE_SOURCE_DIR}/src/AltirraShell/source/joystick_sdl3.cpp
	${CMAKE_SOURCE_DIR}/src/AltirraShell/source/commands_linux.cpp
	${CMAKE_SOURCE_DIR}/src/AltirraShell/source/enhancedtext_linux.cpp
)

# Build executable
add_executable(altirra-wx
	${ALTIRRA_SOURCES}
	${WX_SOURCES}
	${LINUX_SOURCES}
	${SHELL_SHARED_SOURCES}
)

# Include directories — BEFORE ensures our shim headers override system-wide includes
target_include_directories(altirra-wx BEFORE
	PRIVATE
		# Our wxWidgets-specific headers (stdafx.h override)
		${CMAKE_CURRENT_SOURCE_DIR}/source

		# Our wxWidgets UI headers
		${CMAKE_CURRENT_SOURCE_DIR}/h

		# AltirraLinux shim headers (tchar.h, uienhancedtext.h)
		${CMAKE_SOURCE_DIR}/src/AltirraLinux/h

		# Altirra application headers (simulator.h, console.h, etc.)
		${CMAKE_SOURCE_DIR}/src/Altirra/h

		# Altirra resource header (resource.h)
		${CMAKE_SOURCE_DIR}/src/Altirra/res

		# Altirra autobuild version header
		${CMAKE_SOURCE_DIR}/src/Altirra/autobuild_default

		# Altirra autogen (generated firmware .inl files)
		${CMAKE_SOURCE_DIR}/src/Altirra/autogen

		# AltirraShell headers (for joystick_sdl3.h, commands_linux.h, etc.)
		${CMAKE_SOURCE_DIR}/src/AltirraShell/h

		# Foundation headers
		${CMAKE_SOURCE_DIR}/src/h
)

# Link all libraries
target_link_libraries(altirra-wx
	PRIVATE
		# Core emulation libraries
		atcore
		atcpu
		atemulation
		ataudio
		atio
		atdevices
		atnetwork
		atnetsockets
		atdebugger
		atvm
		atui
		kasumi

		# Foundation libraries
		system
		vdjson

		# wxWidgets
		${wxWidgets_LIBRARIES}

		# External libraries
		SDL3::SDL3
		OpenGL::GL
		ZLIB::ZLIB

		# System libraries
		pthread
		dl
		m
)

# Optional FFmpeg support for H.264 recording
if(AT_HAVE_FFMPEG)
	target_compile_definitions(altirra-wx PRIVATE AT_HAVE_FFMPEG)
	target_include_directories(altirra-wx PRIVATE
		${LIBAVCODEC_INCLUDE_DIRS} ${LIBAVFORMAT_INCLUDE_DIRS}
		${LIBAVUTIL_INCLUDE_DIRS} ${LIBSWSCALE_INCLUDE_DIRS}
		${LIBSWRESAMPLE_INCLUDE_DIRS}
	)
	target_link_libraries(altirra-wx PRIVATE
		${LIBAVCODEC_LIBRARIES} ${LIBAVFORMAT_LIBRARIES}
		${LIBAVUTIL_LIBRARIES} ${LIBSWSCALE_LIBRARIES}
		${LIBSWRESAMPLE_LIBRARIES}
	)
endif()

# Install as "altirra" (drop-in replacement for ImGui build)
set_target_properties(altirra-wx PROPERTIES OUTPUT_NAME altirra)
install(TARGETS altirra-wx RUNTIME DESTINATION bin)
