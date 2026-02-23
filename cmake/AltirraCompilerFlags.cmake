# AltirraCompilerFlags.cmake - Shared compiler settings for Altirra Linux port

# Require C++23 (code uses 'if consteval' and other C++23 features)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_compile_definitions(VD_PLATFORM_LINUX=1)
elseif(WIN32)
	add_compile_definitions(VD_PLATFORM_WINDOWS=1)
endif()

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	add_compile_options(
		-Wall
		-Wextra
		-Wno-unused-parameter
		-Wno-missing-field-initializers
		-Wno-sign-compare
		-Wno-unused-variable
		-Wno-parentheses
		-Wno-switch

		# Warnings disabled to match MSVC W4 profile (4100, 4127, 4245, 4310, 4389, 4456, 4457, 4701, 4702, 4706)
		-Wno-implicit-fallthrough
		-Wno-type-limits
	)

	# Optimization flags for Release/Profile builds
	if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "Profile")
		add_compile_options(-O2)
	endif()

	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_options(-g -O0)
		add_compile_definitions(_DEBUG)
	endif()
endif()
