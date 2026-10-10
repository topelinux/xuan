set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TJS_ARMHF_TARGET "arm-linux-musleabihf" CACHE STRING "Zig target triple")
set(TJS_ARMHF_CPU "generic+v7a+vfp3+neon" CACHE STRING "ARM CPU tuning")
set(TJS_LINK_MODE "dynamic" CACHE STRING "Link mode: static|dynamic")

# Use wrapper scripts as compilers so try_compile projects inherit target settings
set(_tjs_toolchain_dir "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_C_COMPILER "${_tjs_toolchain_dir}/arm-linux-musleabihf-cc")
set(CMAKE_CXX_COMPILER "${_tjs_toolchain_dir}/arm-linux-musleabihf-cxx")
set(CMAKE_ASM_COMPILER "${_tjs_toolchain_dir}/arm-linux-musleabihf-cc")

set(_tjs_common_flags "-mcpu=${TJS_ARMHF_CPU}")
set(CMAKE_C_FLAGS_INIT "${_tjs_common_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_tjs_common_flags}")
set(CMAKE_ASM_FLAGS_INIT "${_tjs_common_flags}")

if(TJS_LINK_MODE STREQUAL "static")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_tjs_common_flags} -static")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
elseif(TJS_LINK_MODE STREQUAL "dynamic")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_tjs_common_flags} -dynamic")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")
elseif(TJS_LINK_MODE STREQUAL "static-libc")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_tjs_common_flags} -static -Wl,--dynamic-linker,/lib/ld-musl-armhf.so.1")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
else()
    message(FATAL_ERROR "Unsupported TJS_LINK_MODE=${TJS_LINK_MODE}, expected static, dynamic, or static-libc")
endif()

# Minimal sysroot for find_library - prevents cmake from picking up host libraries
set(TJS_ARMHF_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../../armhf_root" CACHE PATH "ARM HF sysroot")
if(EXISTS "${TJS_ARMHF_SYSROOT}")
    list(APPEND CMAKE_FIND_ROOT_PATH "${TJS_ARMHF_SYSROOT}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Cross-compiling from macOS (Mach-O) to Linux (ELF): Ninja cannot relink RPATH,
# so use install RPATH at build time to avoid "not supported with Ninja" errors.
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON CACHE BOOL "" FORCE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib" CACHE STRING "Install RPATH" FORCE)

# Prevent cmake from finding host libraries during cross compile
list(APPEND CMAKE_IGNORE_PATH "/usr/lib32" "/usr/lib" "/usr/lib/x86_64-linux-gnu" "/lib32" "/lib")

# musl includes pthread, rt, dl in libc — prevent cmake from finding host libs
set(CMAKE_C_IMPLICIT_LINK_LIBRARIES "c;gcc;gcc_s;pthread;rt;dl;m")
set(THREADS_PTHREADS "" CACHE INTERNAL "")
set(CMAKE_THREAD_LIBS_INIT "" CACHE INTERNAL "")
set(THREADS_HAVE_PTHREAD_ARG 1 CACHE INTERNAL "")
