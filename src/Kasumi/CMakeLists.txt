# Kasumi - Pixel/image manipulation library

set(KASUMI_SOURCES
	source/alphablt.cpp
	source/blitter.cpp
	source/blt.cpp
	source/blt_reference.cpp
	source/blt_reference_pal.cpp
	source/blt_reference_rgb.cpp
	source/blt_reference_yuv.cpp
	source/blt_reference_yuv2yuv.cpp
	source/blt_reference_yuvrev.cpp
	source/blt_setup.cpp
	source/blt_spanutils.cpp
	source/blt_uberblit.cpp
	source/pixel.cpp
	source/pixmaputils.cpp
	source/region.cpp
	source/resample.cpp
	source/resample_kernels.cpp
	source/resample_stages.cpp
	source/resample_stages_reference.cpp
	source/stdafx.cpp
	source/stretchblt_reference.cpp
	source/uberblit.cpp
	source/uberblit_16f.cpp
	source/uberblit_gen.cpp
	source/uberblit_resample.cpp
	source/uberblit_resample_special.cpp
	source/uberblit_swizzle.cpp
	source/uberblit_ycbcr_generic.cpp
)

# Platform-specific SIMD sources
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
	# Intrinsics-only files (portable across MSVC/GCC/Clang)
	list(APPEND KASUMI_SOURCES
		source/region_sse2.cpp
		source/uberblit_resample_special_x86.cpp
		source/uberblit_ycbcr_sse2_intrin.cpp
	)

	# MASM assembly-dependent files (MSVC/Windows only)
	if(WIN32)
		list(APPEND KASUMI_SOURCES
			source/blt_spanutils_x86.cpp
			source/blt_x86.cpp
			source/resample_stages_x64.cpp
			source/resample_stages_x86.cpp
			source/uberblit_swizzle_x86.cpp
			source/uberblit_ycbcr_x86.cpp
		)
	endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
	list(APPEND KASUMI_SOURCES
		source/blt_spanutils_arm64.cpp
		source/region_neon.cpp
		source/resample_stages_arm64.cpp
	)
endif()

add_library(kasumi STATIC ${KASUMI_SOURCES})

target_include_directories(kasumi
	PUBLIC
		${CMAKE_SOURCE_DIR}/src/h
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(kasumi PUBLIC system)
