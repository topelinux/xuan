set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TJS_ARM_TARGET "arm-linux-musleabihf" CACHE STRING "Zig target triple")
set(TJS_ARM_CPU "generic+vfp4+v7a" CACHE STRING "ARM CPU tuning")
set(TJS_LINK_MODE "dynamic" CACHE STRING "Link mode: static|dynamic")

set(_tjs_default_sysroot "${CMAKE_CURRENT_LIST_DIR}/../../armv7_root/target-arm-linux-musleabihf")
if(NOT DEFINED TJS_ARM_SYSROOT)
    set(TJS_ARM_SYSROOT "${_tjs_default_sysroot}" CACHE PATH "ARM target sysroot root")
endif()

# Version script to hide time64 symbols for old musl (< 1.2.0) compatibility
set(_tjs_version_script "${CMAKE_CURRENT_LIST_DIR}/hide_time64.map")

set(CMAKE_C_COMPILER zig)
set(CMAKE_C_COMPILER_ARG1 cc)
set(CMAKE_CXX_COMPILER zig)
set(CMAKE_CXX_COMPILER_ARG1 c++)
set(CMAKE_ASM_COMPILER zig)
set(CMAKE_ASM_COMPILER_ARG1 cc)

set(_tjs_common_flags "-target ${TJS_ARM_TARGET} -mcpu=${TJS_ARM_CPU} -D_TIME_BITS=32 -D__UAPI_DEF_TIME64=0")
set(CMAKE_C_FLAGS_INIT "${_tjs_common_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_tjs_common_flags}")
set(CMAKE_ASM_FLAGS_INIT "${_tjs_common_flags}")
if(TJS_LINK_MODE STREQUAL "static")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_tjs_common_flags} -static")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
elseif(TJS_LINK_MODE STREQUAL "dynamic")
    # Dynamic linking with time64 symbol hiding for old musl compatibility
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_tjs_common_flags} -dynamic -Wl,--version-script=${_tjs_version_script}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")
elseif(TJS_LINK_MODE STREQUAL "static-libc")
    # Fully static linking except we keep the dynamic linker for dlopen
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_tjs_common_flags} -static -Wl,--dynamic-linker,/lib/ld-musl-armhf.so.1")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
else()
    message(FATAL_ERROR "Unsupported TJS_LINK_MODE=${TJS_LINK_MODE}, expected static, dynamic, or static-libc")
endif()

if(DEFINED TJS_ARM_SYSROOT AND EXISTS "${TJS_ARM_SYSROOT}")
    list(APPEND CMAKE_FIND_ROOT_PATH "${TJS_ARM_SYSROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)