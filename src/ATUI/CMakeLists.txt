# ATUI - Altirra UI command manager library

add_library(atui STATIC
	source/uicommandmanager.cpp
)

target_include_directories(atui
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_SOURCE_DIR}/src/AltirraLinux/h
)

target_link_libraries(atui
	PUBLIC
		atcore
		system
)
