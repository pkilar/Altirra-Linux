# ATVM - Virtual machine for custom devices

add_library(atvm STATIC
	source/compiler.cpp
	source/stdafx.cpp
	source/vm.cpp
)

target_include_directories(atvm
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(atvm PUBLIC system atcore)
