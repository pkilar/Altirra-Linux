# VDJson Library

add_library(vdjson STATIC
	source/jsonoutput.cpp
	source/jsonnametable.cpp
	source/jsonreader.cpp
	source/jsonvalue.cpp
	source/jsonwriter.cpp
)

target_include_directories(vdjson
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(vdjson PUBLIC system)
