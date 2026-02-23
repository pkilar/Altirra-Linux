# ATDebugger - Debugging infrastructure

add_library(atdebugger STATIC
	source/breakpointsimpl.cpp
	source/debugdevice.cpp
	source/defsymbols.cpp
	source/historytree.cpp
	source/historytreebuilder.cpp
	source/stdafx.cpp
	source/symbolio.cpp
	source/symbols.cpp
	source/target.cpp
)

target_include_directories(atdebugger
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(atdebugger PUBLIC system atcore atcpu)
