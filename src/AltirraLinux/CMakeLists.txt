# AltirraLinux - Linux port of Altirra emulator (Phases 6-8)
#
# Strategy: GLOB all .cpp from Altirra/source, exclude Windows-only UI files,
# add our Linux replacement implementations, link against all Phase 0-5 libraries.

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

# Collect all Altirra source files
file(GLOB ALTIRRA_ALL_SOURCES
	${CMAKE_SOURCE_DIR}/src/Altirra/source/*.cpp
)

# Exclude Windows-only files
set(ALTIRRA_EXCLUDE_PATTERNS
	# Windows entry point
	"main.cpp"

	# Windows implementations (we have Linux replacements)
	"console.cpp"
	"oshelper.cpp"
	"joystick.cpp"

	# Win32-specific implementations
	"asyncdownloader_win32.cpp"
	# customdevice_win32.cpp — uses platform-agnostic ATNetConnect, works on Linux
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

# Linux replacement implementations
set(LINUX_SOURCES
	source/main_linux.cpp
	source/oshelper_linux.cpp
	source/console_linux.cpp
	source/stubs_linux.cpp
	source/modemtcp_linux.cpp
	source/uikeyboard_linux.cpp
	source/worker_linux.cpp
)

# Build executable
add_executable(altirra
	${ALTIRRA_SOURCES}
	${LINUX_SOURCES}
)

# Include directories — BEFORE ensures our shim headers override system-wide includes
target_include_directories(altirra BEFORE
	PRIVATE
		# Our Linux headers (stdafx.h, tchar.h, uiframe.h shim) — must be FIRST to override
		${CMAKE_CURRENT_SOURCE_DIR}/h

		# Altirra application headers (simulator.h, console.h, etc.)
		${CMAKE_SOURCE_DIR}/src/Altirra/h

		# Altirra resource header (resource.h)
		${CMAKE_SOURCE_DIR}/src/Altirra/res

		# Altirra autobuild version header
		${CMAKE_SOURCE_DIR}/src/Altirra/autobuild_default

		# Altirra autogen (generated firmware .inl files)
		${CMAKE_SOURCE_DIR}/src/Altirra/autogen

		# AltirraShell headers (display_sdl2.h, input_sdl2.h)
		${CMAKE_SOURCE_DIR}/src/AltirraShell/h

		# Foundation headers
		${CMAKE_SOURCE_DIR}/src/h

		# SDL2
		${SDL2_INCLUDE_DIRS}
)

# Link all libraries
target_link_libraries(altirra
	PRIVATE
		# Linux frontend backends (Phase 3-5)
		altirrashell

		# Core emulation libraries (Phase 2)
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
		kasumi

		# Foundation libraries (Phase 0-1)
		system
		vdjson

		# External libraries
		SDL2::SDL2
		OpenGL::GL
		ZLIB::ZLIB

		# System libraries
		pthread
		dl
		m
)

# Install targets
install(TARGETS altirra RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/data/altirra.desktop DESTINATION share/applications)
install(DIRECTORY DESTINATION share/altirra/firmware)
