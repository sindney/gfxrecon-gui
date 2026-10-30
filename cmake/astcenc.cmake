# ARM astc-encoder 5.7.0 (Apache-2.0) release binaries decode ASTC texture dumps to PNG.
# The two shipped variants are vendored in thirdparty/astcenc/ (from the official
# astcenc-5.7.0-windows-x64.zip release, archive SHA-256
# cbde7c78adcc90d0d90ecf10f32d03f3340fbe1720cb6d6c892147d625e74d2d) and are hash-checked here,
# then copied to <build>/<config>/tools/astcenc/ beside the executables.
set(GFXRECON_ASTCENC_VERSION 5.7.0)
set(GFXRECON_ASTCENC_AVX2_SHA256 2c46c047d3ec53378641e252501b8faa872b6d384f8baec1ff259020a8941b83)
set(GFXRECON_ASTCENC_SSE2_SHA256 ded446f477755af96875c7f4f8e85216a73c06367fdba3fda2744f6bb15b87aa)
set(GFXRECON_ASTCENC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/astcenc" CACHE PATH
    "Directory holding astcenc ${GFXRECON_ASTCENC_VERSION} astcenc-avx2.exe and astcenc-sse2.exe")

if(WIN32)
    set(_astc_binaries "")
    foreach(_variant avx2 sse2)
        string(TOUPPER ${_variant} _upper)
        set(_file "${GFXRECON_ASTCENC_DIR}/astcenc-${_variant}.exe")
        if(NOT EXISTS "${_file}" AND EXISTS "${GFXRECON_ASTCENC_DIR}/bin/astcenc-${_variant}.exe")
            set(_file "${GFXRECON_ASTCENC_DIR}/bin/astcenc-${_variant}.exe") # extracted release layout
        endif()
        if(NOT EXISTS "${_file}")
            message(FATAL_ERROR "astcenc binary missing: ${_file}")
        endif()
        file(SHA256 "${_file}" _hash)
        if(NOT _hash STREQUAL GFXRECON_ASTCENC_${_upper}_SHA256)
            message(FATAL_ERROR "${_file} has SHA-256 ${_hash}; expected the ${GFXRECON_ASTCENC_VERSION} release binary ${GFXRECON_ASTCENC_${_upper}_SHA256}")
        endif()
        list(APPEND _astc_binaries "${_file}")
    endforeach()
    add_custom_target(gfxrecon_astcenc ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>/tools/astcenc"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_astc_binaries} "${CMAKE_BINARY_DIR}/$<CONFIG>/tools/astcenc/"
        COMMENT "Copying astcenc ${GFXRECON_ASTCENC_VERSION} to tools/astcenc")
else()
    add_custom_target(gfxrecon_astcenc)
endif()
