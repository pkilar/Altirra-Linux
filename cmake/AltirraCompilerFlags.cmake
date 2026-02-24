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

		# Upstream code patterns — safe to suppress
		-Wno-multichar              # FourCC constants used throughout for type IDs
		-Wno-ignored-qualifiers     # const return types in VD2 headers
		-Wno-unknown-pragmas        # MSVC #pragma warning directives
		-Wno-reorder                # Member initialization order in upstream headers
		-Wno-attributes             # packed attribute on template types in binary.h
		-Wno-comment                # Multi-line comments in upstream headers
		-Wno-sequence-point         # hash.cpp upstream code
		-Wno-inaccessible-base      # COM-like diamond inheritance in device framework
		-Wno-unused-but-set-variable # Upstream code with conditional compilation
		-Wno-delete-non-virtual-dtor # Controlled deletion through correct type
		-Wno-class-memaccess        # VD2 fast containers use memcpy/memset
		-Wno-cast-function-type     # Function pointer casts in upstream code
		-Wno-conversion-null        # NULL to non-pointer conversions
		-Wno-invalid-offsetof       # offsetof on non-standard-layout types
		-Wno-catch-value            # Catch by value in upstream code
		-Wno-range-loop-construct   # Range loop copy in upstream code
		-Wno-nonnull                # Upstream null argument patterns
		-Wno-format-overflow        # Format overflow in upstream code
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
