cmake_minimum_required(VERSION 3.14)

project(tjs LANGUAGES C CXX)

include(ExternalProject)
include(GNUInstallDirs)

if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, default to Release")
    set(CMAKE_BUILD_TYPE "Release")
endif()
message(STATUS "Building in ${CMAKE_BUILD_TYPE} mode")
message(STATUS "Building with ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} on ${CMAKE_SYSTEM}")

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TJS_SANITIZE "" CACHE STRING "Sanitizer to enable (address,undefined,thread)")

if(MSVC)
    list(APPEND tjs_cflags /experimental:c11atomics)
else()
    list(APPEND tjs_cflags -Wall -g)
    if (CMAKE_BUILD_TYPE MATCHES "Debug")
        list(APPEND tjs_cflags -ggdb -O0 -fno-omit-frame-pointer)
    endif()
endif()

if(TJS_SANITIZE)
    if(MSVC)
        message(WARNING "TJS_SANITIZE is not supported with MSVC")
    else()
        list(APPEND tjs_cflags -fsanitize=${TJS_SANITIZE} -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${TJS_SANITIZE})
        add_compile_options(-fsanitize=${TJS_SANITIZE} -fno-omit-frame-pointer)
    endif()
endif()

# Version defaults. Override at configure time for releases, e.g.
#   TJS_VERSION=1.4.0 make
#   TJS_VERSION=1.4.0-rc1 cmake -B build ...
# TJS_VERSION accepts "MAJOR.MINOR.PATCH" with an optional "-suffix".
set(TJS__VERSION_MAJOR 1)
set(TJS__VERSION_MINOR 3)
set(TJS__VERSION_PATCH 0)
set(TJS__VERSION_SUFFIX "")

set(_tjs_version_override "")
if(DEFINED ENV{TJS_VERSION} AND NOT "$ENV{TJS_VERSION}" STREQUAL "")
    set(_tjs_version_override "$ENV{TJS_VERSION}")
elseif(DEFINED TJS_VERSION AND NOT "${TJS_VERSION}" STREQUAL "")
    set(_tjs_version_override "${TJS_VERSION}")
endif()

if(NOT _tjs_version_override STREQUAL "")
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-(.*))?$" _tjs_version_match "${_tjs_version_override}")
    if(DEFINED CMAKE_MATCH_1 AND NOT CMAKE_MATCH_1 STREQUAL "")
        set(TJS__VERSION_MAJOR ${CMAKE_MATCH_1})
        set(TJS__VERSION_MINOR ${CMAKE_MATCH_2})
        set(TJS__VERSION_PATCH ${CMAKE_MATCH_3})
        set(TJS__VERSION_SUFFIX "${CMAKE_MATCH_4}")
    else()
        message(WARNING "TJS_VERSION='${_tjs_version_override}' is not MAJOR.MINOR.PATCH[-suffix]; using default ${TJS__VERSION_MAJOR}.${TJS__VERSION_MINOR}.${TJS__VERSION_PATCH}")
    endif()
endif()
message(STATUS "xuan version: ${TJS__VERSION_MAJOR}.${TJS__VERSION_MINOR}.${TJS__VERSION_PATCH}${TJS__VERSION_SUFFIX}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h"
)

macro(cpr_option OPTION_NAME OPTION_TEXT OPTION_DEFAULT)
    option(${OPTION_NAME} ${OPTION_TEXT} ${OPTION_DEFAULT})
    if(DEFINED ENV{${OPTION_NAME}})
        # Allow setting the option through an environment variable
        set(${OPTION_NAME} $ENV{${OPTION_NAME}})
    endif()
    if(${OPTION_NAME})
        add_definitions(-D${OPTION_NAME})
    endif()
    message(STATUS "  ${OPTION_NAME}: ${${OPTION_NAME}}")
endmacro()

cpr_option(BUILD_WITH_MIMALLOC "If ON (default), build with mimalloc" ON)
cpr_option(TJS_DISABLE_ADA_URL "If ON, disable ada-based URL runtime module" OFF)
cpr_option(TJS_DISABLE_PROMISE_TRACKER "If ON, disable host promise rejection tracker" OFF)
cpr_option(TJS_DISABLE_SQLITE3 "If ON, disable SQLite3 runtime module" ON)
cpr_option(TJS_SQLITE3_DYNAMIC "If ON, load SQLite3 dynamically at runtime instead of linking static sqlite3" OFF)
cpr_option(TJS_DISABLE_NATIVE_MODULES "If ON, disable loading native shared-library modules" OFF)
cpr_option(TJS_DISABLE_WASM "If ON, disable built-in WebAssembly runtime module (WAMR)" OFF)
cpr_option(TJS_BUILD_WASM_NATIVE_MODULE "If ON, build xuan-wasm native module (requires native modules)" ON)
cpr_option(TJS_ENABLE_LVGL "If ON, enable LVGL GUI library integration" OFF)
option(TJS_LVGL_USE_SDL "Use SDL2 display driver for LVGL (simulator mode)" OFF)
message(STATUS "  TJS_LVGL_USE_SDL: ${TJS_LVGL_USE_SDL}")
option(TJS_USE_SYSROOT_LIBUV "Use sysroot-provided libuv instead of deps/libuv" OFF)
message(STATUS "  TJS_USE_SYSROOT_LIBUV: ${TJS_USE_SYSROOT_LIBUV}")
option(TJS_USE_DEPS_LIBUV_SHARED "Use deps/libuv shared target (libuv.so) instead of static uv_a" OFF)
message(STATUS "  TJS_USE_DEPS_LIBUV_SHARED: ${TJS_USE_DEPS_LIBUV_SHARED}")
option(TJS_DISABLE_QUICKJS_PATCH "If ON, disable quickjs patch" OFF)
message(STATUS "  TJS_DISABLE_QUICKJS_PATCH: ${TJS_DISABLE_QUICKJS_PATCH}")
option(TJS_BUILD_TOOLS_ONLY "If ON, build host tools only (xuanc) without runtime targets" OFF)
message(STATUS "  TJS_BUILD_TOOLS_ONLY: ${TJS_BUILD_TOOLS_ONLY}")
cpr_option(XUAN_ENABLE_HTTP2 "If ON, enable HTTP/2 client support via libwebsockets" ON)

if(TJS_USE_SYSROOT_LIBUV AND TJS_USE_DEPS_LIBUV_SHARED)
    message(FATAL_ERROR "TJS_USE_SYSROOT_LIBUV and TJS_USE_DEPS_LIBUV_SHARED are mutually exclusive")
endif()
if(TJS_DISABLE_SQLITE3 AND TJS_SQLITE3_DYNAMIC)
    message(FATAL_ERROR "TJS_DISABLE_SQLITE3 and TJS_SQLITE3_DYNAMIC are mutually exclusive")
endif()

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/wamr)

###
### Core library / executables
###

if(NOT TJS_BUILD_TOOLS_ONLY)
set(TJS_CORE_SOURCES
    src/builtins.c
    src/error.c
    src/lws-utils.c
    src/eval.c
    src/mem.c
    src/modules.c
    src/signals.c
    src/text-coding.c
    src/timers.c
    src/utils.c
    src/version.c
    src/vm.c
    src/worker.c
    src/ws.c
    src/httpclient.c
    src/httpserver.c
    $<$<BOOL:${XUAN_ENABLE_HTTP2}>:src/mod_http2.c>
    src/mod_dns.c
    src/mod_buffer.c
    src/mod_crypto.c
    src/mod_engine.c
    src/mod_events.c
    src/mod_ffi.c
    src/mod_fs.c
    src/mod_fswatch.c
    src/mod_mqtt.c
    src/mod_os.c
    src/mod_process.c
    src/mod_streams.c
    src/mod_tls.c
    src/mod_sys.c
    src/mod_udp.c
    src/mod_url.c
    src/mod_util.c
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs/cutils.c")
    list(APPEND TJS_CORE_SOURCES deps/quickjs/cutils.c)
endif()

set(TJS_ENABLE_WAMR OFF)
if(NOT TJS_DISABLE_WASM)
    set(TJS_ENABLE_WAMR ON)
elseif(NOT TJS_DISABLE_NATIVE_MODULES AND TJS_BUILD_WASM_NATIVE_MODULE)
    set(TJS_ENABLE_WAMR ON)
endif()

if(NOT TJS_DISABLE_WASM)
    list(APPEND TJS_CORE_SOURCES src/wasm.c)
endif()

if(TJS_DISABLE_ADA_URL)
    list(APPEND TJS_CORE_SOURCES src/url_stub.c)
else()
    list(APPEND TJS_CORE_SOURCES src/url.c)
endif()

if(NOT TJS_DISABLE_SQLITE3)
    list(APPEND TJS_CORE_SOURCES src/mod_sqlite3.c)
endif()

#
# Bundle C files (generated from JS sources via 'make js')
# These files should exist before CMake configuration.
# If missing, run: make js
#
set(CORE_BUNDLE_FILES
    src/bundles/c/core/core.c
    src/bundles/c/core/polyfills-compat.c
    src/bundles/c/core/polyfills-lite.c
    src/bundles/c/core/polyfills-heavy.c
    src/bundles/c/core/run-main.c
    src/bundles/c/core/run-repl.c
    src/bundles/c/core/worker-bootstrap.c
)

# Add stdlib bundle files
file(GLOB STDLIB_JS_SOURCES "src/js/stdlib/*.js")
foreach(JS_FILE ${STDLIB_JS_SOURCES})
    get_filename_component(MODULE_NAME ${JS_FILE} NAME_WE)
    list(APPEND CORE_BUNDLE_FILES "src/bundles/c/stdlib/${MODULE_NAME}.c")
endforeach()

# Add bundle files to core sources
list(APPEND TJS_CORE_SOURCES ${CORE_BUNDLE_FILES})

add_library(tjs STATIC
    ${TJS_CORE_SOURCES}
)

if(UNIX)
    target_sources(tjs PUBLIC src/mod_posix-socket.c)
endif()

set_target_properties(tjs PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    OUTPUT_NAME tjs_core
)

string(TOLOWER ${CMAKE_SYSTEM_NAME} TJS_PLATFORM)
target_compile_options(tjs PRIVATE ${tjs_cflags})
target_compile_definitions(tjs PRIVATE TJS__PLATFORM="${TJS_PLATFORM}")

if(WIN32)
    target_compile_definitions(tjs PRIVATE WIN32_LEAN_AND_MEAN)
endif()

target_include_directories(tjs PUBLIC src)

add_executable(xuan-cli
    src/cli.c
)
target_link_libraries(xuan-cli tjs)
set_target_properties(xuan-cli
    PROPERTIES OUTPUT_NAME xuan
               ENABLE_EXPORTS TRUE
)
# Ensure QuickJS symbols (compiled with hidden visibility) are exported so
# that native MODULE libraries (xuan-wasm, xuan-sqlite, …) can resolve them
# at runtime via -undefined dynamic_lookup.
if(APPLE)
    target_link_options(xuan-cli PRIVATE "-Wl,-export_dynamic")
elseif(UNIX)
    target_link_options(xuan-cli PRIVATE "-rdynamic")
endif()
# Make the shipped layout relocatable: in the dist tree the binary lives in
# bin/ and bundled shared libraries in lib/ (see scripts/assemble-dist.sh), so
# teach the loader to look next to the binary and in ../lib.
if(UNIX AND NOT APPLE)
    set_target_properties(xuan-cli PROPERTIES
        BUILD_RPATH "$ORIGIN:$ORIGIN/../lib"
        INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib"
    )
endif()
# On Windows with MSVC the stack size if by default 1MB, same as our engine.
# Bump it up to 8MB.
if(WIN32 AND MSVC)
    target_link_options(xuan-cli PRIVATE "/STACK:8388608")
elseif(WIN32 AND NOT MSVC)
    # For MinGW toolchains (including Zig targeting windows-gnu)
    target_link_options(xuan-cli PRIVATE "-Wl,--stack,8388608")
endif()
endif()

add_executable(xuanc EXCLUDE_FROM_ALL
    src/qjsc.c
)

###
### Dependencies
###

# Apply quickjs patches.
if(NOT TJS_DISABLE_QUICKJS_PATCH)
    execute_process(
        COMMAND git apply --reverse --check ${CMAKE_CURRENT_SOURCE_DIR}/patches/quickjs.patch
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs
        RESULT_VARIABLE _quickjs_patch_applied
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_quickjs_patch_applied EQUAL 0)
        message(STATUS "Already applied quickjs.patch")
    else()
        execute_process(
            COMMAND git apply ${CMAKE_CURRENT_SOURCE_DIR}/patches/quickjs.patch
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs
            RESULT_VARIABLE _quickjs_patch_result
        )
        if(_quickjs_patch_result EQUAL 0)
            message(STATUS "Applied quickjs.patch")
        else()
            message(FATAL_ERROR "Failed to apply quickjs.patch")
        endif()
    endif()
endif()

add_subdirectory(deps/quickjs EXCLUDE_FROM_ALL)
# QuickJS only sets JS_EXTERN (visibility("default")) when BUILDING_QJS_SHARED
# is defined, which normally only happens for SHARED library builds.  Since we
# link qjs statically into the xuan executable but still need the symbols to be
# visible to native MODULE libraries (xuan-wasm, xuan-sqlite, …) loaded at
# runtime, force the definition here.
target_compile_definitions(qjs PRIVATE BUILDING_QJS_SHARED)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|mipsel|mips64)")
    target_compile_options(qjs PRIVATE -Wno-error=atomic-alignment -Wno-atomic-alignment)
endif()
if(TARGET tjs)
    target_link_libraries(tjs PUBLIC qjs)
endif()
target_link_libraries(xuanc qjs)

if(TJS_BUILD_TOOLS_ONLY)
    return()
endif()

if(BUILD_WITH_MIMALLOC)
    option(MI_OVERRIDE "" OFF)
    option(MI_BUILD_SHARED "" OFF)
    option(MI_BUILD_STATIC "" ON)
    option(MI_BUILD_OBJECT "" OFF)
    option(MI_BUILD_TESTS "" OFF)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" AND CMAKE_C_COMPILER MATCHES "zig")
        # Zig cross aarch64 toolchains reject mimalloc's default -march=armv8.1-a.
        set(MI_OPT_ARCH OFF CACHE BOOL "" FORCE)
    endif()
    add_subdirectory(deps/mimalloc EXCLUDE_FROM_ALL)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|mipsel|mips64|aarch64|arm64|arm)$")
        target_compile_options(mimalloc-static PRIVATE -Wno-error=date-time)
    endif()
    target_compile_definitions(tjs PRIVATE TJS__HAS_MIMALLOC)
    target_link_libraries(tjs PUBLIC mimalloc-static)
endif()

set(TJS_LIBUV_LINK uv_a)
set(TJS_LIBUV_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv/include")
set(TJS_HAVE_UV_AVAILABLE_PARALLELISM 1)
set(TJS_HAVE_UV_PIPE_CONNECT2 1)
set(TJS_HAVE_UV_PIPE_BIND2 1)

if(TJS_USE_SYSROOT_LIBUV)
    find_library(TJS_SYSROOT_LIBUV NAMES uv libuv REQUIRED)

    set(TJS_LIBUV_LINK "${TJS_SYSROOT_LIBUV}")
    # Be conservative for cross builds: if symbol probing tools are unavailable,
    # keep compatibility fallbacks enabled to avoid link-time undefined symbols.
    set(TJS_HAVE_UV_AVAILABLE_PARALLELISM 0)
    set(TJS_HAVE_UV_PIPE_CONNECT2 0)
    set(TJS_HAVE_UV_PIPE_BIND2 0)
    execute_process(
        COMMAND llvm-readelf -Ws "${TJS_SYSROOT_LIBUV}"
        RESULT_VARIABLE _tjs_uv_symtab_rc
        OUTPUT_VARIABLE _tjs_uv_symtab
        ERROR_QUIET
    )
    if(NOT _tjs_uv_symtab_rc EQUAL 0)
        execute_process(
            COMMAND readelf -Ws "${TJS_SYSROOT_LIBUV}"
            RESULT_VARIABLE _tjs_uv_symtab_rc
            OUTPUT_VARIABLE _tjs_uv_symtab
            ERROR_QUIET
        )
    endif()
    if(_tjs_uv_symtab_rc EQUAL 0)
        if(_tjs_uv_symtab MATCHES "uv_available_parallelism($|@)")
            set(TJS_HAVE_UV_AVAILABLE_PARALLELISM 1)
        endif()
        if(_tjs_uv_symtab MATCHES "uv_pipe_connect2($|@)")
            set(TJS_HAVE_UV_PIPE_CONNECT2 1)
        endif()
        if(_tjs_uv_symtab MATCHES "uv_pipe_bind2($|@)")
            set(TJS_HAVE_UV_PIPE_BIND2 1)
        endif()
    endif()

    message(STATUS "Using sysroot libuv: ${TJS_SYSROOT_LIBUV}")
    message(STATUS "Using libuv headers from: ${TJS_LIBUV_INCLUDE_DIR}")
    message(STATUS "  uv_available_parallelism: ${TJS_HAVE_UV_AVAILABLE_PARALLELISM}")
    message(STATUS "  uv_pipe_connect2: ${TJS_HAVE_UV_PIPE_CONNECT2}")
    message(STATUS "  uv_pipe_bind2: ${TJS_HAVE_UV_PIPE_BIND2}")
else()
    option(libuv_buildtests "" OFF)
    if(TJS_USE_DEPS_LIBUV_SHARED)
        set(LIBUV_BUILD_SHARED ON CACHE BOOL "" FORCE)
        set(TJS_LIBUV_LINK uv)
    else()
        set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    endif()
    add_subdirectory(deps/libuv EXCLUDE_FROM_ALL)
endif()

target_include_directories(tjs PUBLIC ${TJS_LIBUV_INCLUDE_DIR})
target_link_libraries(tjs PUBLIC ${TJS_LIBUV_LINK})
target_compile_definitions(tjs PUBLIC
    TJS_HAVE_UV_AVAILABLE_PARALLELISM=${TJS_HAVE_UV_AVAILABLE_PARALLELISM}
    TJS_HAVE_UV_PIPE_CONNECT2=${TJS_HAVE_UV_PIPE_CONNECT2}
    TJS_HAVE_UV_PIPE_BIND2=${TJS_HAVE_UV_PIPE_BIND2}
)

if(NOT TJS_DISABLE_SQLITE3)
    target_include_directories(tjs PUBLIC deps/sqlite3)
    if(NOT TJS_SQLITE3_DYNAMIC)
        add_subdirectory(deps/sqlite3 EXCLUDE_FROM_ALL)
        target_link_libraries(tjs PUBLIC sqlite3)
        add_library(sqlite-test SHARED
            tests/fixtures/sqlite-test-ext.c
        )
        target_link_libraries(sqlite-test sqlite3)
    endif()
endif()

if (WIN32 AND NOT CMAKE_CROSSCOMPILING)
    # Native Windows build with vcpkg
    find_package(unofficial-libffi CONFIG REQUIRED)
    target_link_libraries(tjs PUBLIC unofficial::libffi::libffi)
else()
    # Cross-compilation or UNIX-style builds (including Zig cross-compile)
    find_library(FFI_LIB NAMES libffi ffi REQUIRED)
    find_path(FFI_INCLUDE_DIR NAMES ffi.h PATH_SUFFIXES ffi REQUIRED)
    target_include_directories(tjs PRIVATE ${FFI_INCLUDE_DIR})
    target_link_libraries(tjs PUBLIC ${FFI_LIB})
endif()

add_library(ffi-test SHARED
    tests/fixtures/ffi-test-lib.c
)

if(NOT TJS_DISABLE_NATIVE_MODULES)
    if(NOT TARGET sqlite3)
        add_subdirectory(deps/sqlite3 EXCLUDE_FROM_ALL)
    endif()
    if(TARGET sqlite3)
        set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    add_library(native-test MODULE
        tests/fixtures/native-test-module.c
    )
    set_target_properties(native-test PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
    )
    target_link_libraries(native-test PRIVATE qjs_module_lib)
    if(APPLE)
        target_link_options(native-test PRIVATE -undefined dynamic_lookup)
    endif()

    add_library(xuan-sqlite MODULE
        xuan_modules/sqlite/native/xuan-sqlite-module.c
    )
    set_target_properties(xuan-sqlite PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
    )
    target_link_libraries(xuan-sqlite PRIVATE qjs_module_lib sqlite3)
    if(APPLE)
        target_link_options(xuan-sqlite PRIVATE -undefined dynamic_lookup)
    endif()

    if(TJS_BUILD_WASM_NATIVE_MODULE)
        if(NOT TJS_ENABLE_WAMR)
            message(FATAL_ERROR "TJS_BUILD_WASM_NATIVE_MODULE requires WAMR to be enabled")
        endif()
        add_library(xuan-wasm MODULE
            xuan_modules/wasm/native/xuan-wasm-module.c
        )
        set_target_properties(xuan-wasm PROPERTIES
            PREFIX ""
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
        )
        target_link_libraries(xuan-wasm PRIVATE qjs_module_lib)
        if(APPLE)
            target_link_options(xuan-wasm PRIVATE -undefined dynamic_lookup)
        endif()
    endif()

    ###
    ### LVGL Dynamic Module
    ###

    if(TJS_ENABLE_LVGL)
        set(TJS_LVGL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/xuan_modules/lvgl")
        set(TJS_LVGL_DEPS_DIR "${TJS_LVGL_ROOT_DIR}/deps")
        set(TJS_LVGL_CORE_DIR "${TJS_LVGL_DEPS_DIR}/lvgl")
        set(TJS_LVGL_DRIVER_DIR "${TJS_LVGL_DEPS_DIR}/lv_drivers")
        if(NOT EXISTS "${TJS_LVGL_CORE_DIR}/src/lvgl.h")
            message(FATAL_ERROR "LVGL source not found at ${TJS_LVGL_CORE_DIR}. Expected xuan_modules/lvgl/deps/lvgl/src/lvgl.h")
        endif()
        if(NOT EXISTS "${TJS_LVGL_DRIVER_DIR}/display/fbdev.c")
            message(FATAL_ERROR "LVGL drivers source not found at ${TJS_LVGL_DRIVER_DIR}. Expected xuan_modules/lvgl/deps/lv_drivers/*")
        endif()
        if(NOT EXISTS "${TJS_LVGL_DEPS_DIR}/lv_conf.h")
            message(FATAL_ERROR "LVGL config not found at ${TJS_LVGL_DEPS_DIR}/lv_conf.h")
        endif()
        if(NOT EXISTS "${TJS_LVGL_DEPS_DIR}/lv_drv_conf.h")
            message(FATAL_ERROR "LVGL driver config not found at ${TJS_LVGL_DEPS_DIR}/lv_drv_conf.h")
        endif()

        file(GLOB_RECURSE TJS_LVGL_SOURCES CONFIGURE_DEPENDS
            "${TJS_LVGL_CORE_DIR}/src/*.c"
        )
        list(FILTER TJS_LVGL_SOURCES EXCLUDE REGEX ".*/(demos|examples|tests)/.*")
        if(TJS_LVGL_USE_SDL)
            set(TJS_LVGL_DRIVER_SOURCES
                "${TJS_LVGL_DRIVER_DIR}/sdl/sdl.c"
            )
        else()
            set(TJS_LVGL_DRIVER_SOURCES
                "${TJS_LVGL_DRIVER_DIR}/display/fbdev.c"
                "${TJS_LVGL_DRIVER_DIR}/indev/evdev.c"
            )
        endif()

        add_library(xuan-lvgl MODULE
            xuan_modules/lvgl/native/xuan-lvgl-module.c
            ${TJS_LVGL_DEPS_DIR}/tick.c
            ${TJS_LVGL_SOURCES}
            ${TJS_LVGL_DRIVER_SOURCES}
        )
        set_target_properties(xuan-lvgl PROPERTIES
            PREFIX ""
            POSITION_INDEPENDENT_CODE ON
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
        )

        # LVGL includes and sources are compiled in-tree so architecture always matches.
        target_include_directories(xuan-lvgl PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs
            ${TJS_LVGL_CORE_DIR}
            ${TJS_LVGL_CORE_DIR}/src
            ${TJS_LVGL_DRIVER_DIR}
            ${TJS_LVGL_DRIVER_DIR}/display
            ${TJS_LVGL_DRIVER_DIR}/indev
            ${TJS_LVGL_DRIVER_DIR}/sdl
            ${TJS_LIBUV_INCLUDE_DIR}
            ${TJS_LVGL_DEPS_DIR}
        )
        if(TJS_LVGL_USE_SDL)
            find_package(SDL2 REQUIRED)
            target_include_directories(xuan-lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
            target_link_libraries(xuan-lvgl PUBLIC ${SDL2_LIBRARIES})
        endif()

        target_compile_definitions(xuan-lvgl PRIVATE
            LV_CONF_INCLUDE_SIMPLE
            LV_LVGL_H_INCLUDE_SIMPLE
        )
        if(TJS_LVGL_USE_SDL)
            target_compile_definitions(xuan-lvgl PRIVATE
                TJS_LVGL_USE_SDL
                USE_SDL=1
                USE_MONITOR=0
                USE_MOUSE=0
                USE_MOUSEWHEEL=0
                USE_KEYBOARD=0
                USE_FBDEV=0
                USE_EVDEV=0
            )
        else()
            target_compile_definitions(xuan-lvgl PRIVATE
                USE_SDL=0
                USE_MONITOR=0
                USE_FBDEV=1
                USE_EVDEV=1
            )
        endif()

        target_link_libraries(xuan-lvgl PRIVATE
            qjs_module_lib
        )
        if(UNIX AND NOT APPLE)
            target_link_libraries(xuan-lvgl PRIVATE m)
        endif()

        if(APPLE)
            target_link_options(xuan-lvgl PRIVATE -undefined dynamic_lookup)
        endif()

        message(STATUS "LVGL dynamic module enabled (xuan-lvgl.so)")
    endif()
endif()

if(UNIX)
    add_library(serial-shim SHARED
        src/native/serial-shim.c
    )
endif()

# Set consistent output for test libraries across all platforms (including MSVC multi-config)
set_target_properties(ffi-test PROPERTIES
    PREFIX "lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    # For multi-config generators (Visual Studio, Xcode)
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)

if(NOT TJS_DISABLE_SQLITE3 AND NOT TJS_SQLITE3_DYNAMIC)
    set_target_properties(sqlite-test PROPERTIES
        PREFIX "lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        # For multi-config generators (Visual Studio, Xcode)
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
    )
endif()

if(UNIX)
    set_target_properties(serial-shim PROPERTIES
        PREFIX "lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
    )
endif()

# Configure serial-shim for MIPS dynamic build
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|mipsel|mips64)" AND TJS_LINK_MODE STREQUAL "dynamic")
    message(STATUS "Configuring serial-shim for MIPS dynamic build")
    if(TARGET serial-shim)
        set_target_properties(serial-shim PROPERTIES
            POSITION_INDEPENDENT_CODE ON
        )
    endif()
endif()

if(NOT TJS_DISABLE_ADA_URL)
    set(ADA_BUILD_SHARED OFF)
    set(ADA_MIN_SIZE OFF)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|mipsel|mips64)" AND TJS_LINK_MODE STREQUAL "dynamic")
        set(ADA_BUILD_SHARED ON)
        set(ADA_MIN_SIZE ON)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND TJS_LINK_MODE STREQUAL "dynamic")
        set(ADA_BUILD_SHARED ON)
        set(ADA_MIN_SIZE ON)
    endif()
    message(STATUS "  ADA_BUILD_SHARED: ${ADA_BUILD_SHARED}")
    message(STATUS "  ADA_MIN_SIZE: ${ADA_MIN_SIZE}")
    add_subdirectory(deps/ada EXCLUDE_FROM_ALL)
    target_link_libraries(tjs PUBLIC ada)
endif()

if(TJS_ENABLE_WAMR)
    # WAMR Configuration (interpreter only for maximum portability)
    set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/wamr)

    # Detect platform for WAMR
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(WAMR_BUILD_PLATFORM "darwin")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(WAMR_BUILD_PLATFORM "windows")
    else()
        set(WAMR_BUILD_PLATFORM "linux")
    endif()

    # Configure WAMR build options
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|mipsel|mips64)")
        set(WAMR_BUILD_TARGET "MIPS")
        # use C fallback to avoid MIPS asm requiring hard-float instructions
        set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 1)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(WAMR_BUILD_TARGET "AARCH64")
        set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 0)
        set(WAMR_BUILD_BUILD_AOT 0)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|armv7)")
        set(WAMR_BUILD_TARGET "ARM")
        set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 0)
        set(WAMR_BUILD_BUILD_AOT 0)
    endif()
    set(WAMR_BUILD_INTERP 1)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|mipsel|mips64)")
        # mipsel soft-float toolchains fail on invokeNative_mips.s FPU opcodes.
        set(WAMR_BUILD_FAST_INTERP 0)
    else()
        set(WAMR_BUILD_FAST_INTERP 1)
    endif()
    set(WAMR_BUILD_AOT 0)
    set(WAMR_BUILD_JIT 0)
    set(WAMR_BUILD_LIBC_BUILTIN 1)
    set(WAMR_BUILD_LIBC_WASI 1)
    set(WAMR_BUILD_MULTI_MODULE 1)
    # Default to OFF to avoid network FetchContent(simde) in typical/offline builds.
    set(TJS_WAMR_SIMD "OFF" CACHE STRING "Override WAMR SIMD: AUTO|ON|OFF")
    set_property(CACHE TJS_WAMR_SIMD PROPERTY STRINGS AUTO ON OFF)
    if(TJS_WAMR_SIMD STREQUAL "ON")
        set(WAMR_BUILD_SIMD 1)
    elseif(TJS_WAMR_SIMD STREQUAL "OFF")
        set(WAMR_BUILD_SIMD 0)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|mipsel|mips64)")
        set(WAMR_BUILD_SIMD 0)
    else()
        set(WAMR_BUILD_SIMD 1)
    endif()
    set(WAMR_BUILD_REF_TYPES 1)
    set(WAMR_BUILD_BULK_MEMORY 1)
    set(WAMR_BUILD_LIB_PTHREAD 0)
    set(WAMR_BUILD_MINI_LOADER 0)
    set(WAMR_DISABLE_HW_BOUND_CHECK 1)
    set(WAMR_DISABLE_STACK_HW_BOUND_CHECK 1)

    # Include WAMR build scripts
    include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

    add_library(vmlib STATIC ${WAMR_RUNTIME_LIB_SOURCE})
    target_include_directories(vmlib PUBLIC ${WAMR_ROOT_DIR}/core/iwasm/include)
    if(TARGET xuan-wasm)
        # xuan-wasm is a MODULE target and links vmlib; ensure archive objects are PIC.
        set_target_properties(vmlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    if(WIN32)
        target_link_libraries(vmlib PUBLIC ntdll)
    endif()
    if(NOT TJS_DISABLE_WASM)
        target_include_directories(tjs PRIVATE ${WAMR_ROOT_DIR}/core/iwasm/include)
        target_link_libraries(tjs PUBLIC vmlib)
    endif()
    if(TARGET xuan-wasm)
        target_include_directories(xuan-wasm PRIVATE ${WAMR_ROOT_DIR}/core/iwasm/include)
        target_link_libraries(xuan-wasm PRIVATE vmlib)
    endif()
endif()

add_subdirectory(deps/miniz EXCLUDE_FROM_ALL)
target_link_libraries(tjs PUBLIC miniz)

option(ENABLE_TESTING "" OFF)
option(ENABLE_PROGRAMS "" OFF)
option(MBEDTLS_FATAL_WARNINGS "" OFF)
set(MBEDTLS_AS_SUBPROJECT ON)
set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/mbedtls_config.h" CACHE FILEPATH "" FORCE)
add_subdirectory(deps/mbedtls EXCLUDE_FROM_ALL)
target_link_libraries(tjs PUBLIC mbedtls mbedx509 mbedcrypto)

if (NOT CMAKE_BUILD_TYPE MATCHES "Debug")
    set(LWS_WITH_NO_LOGS ON CACHE BOOL "" FORCE)
endif()
set(LWS_WITH_HTTP2 ${XUAN_ENABLE_HTTP2} CACHE BOOL "HTTP/2 support (controlled by XUAN_ENABLE_HTTP2)" FORCE)
set(LWS_ROLE_RAW_FILE OFF CACHE BOOL "" FORCE)
set(LWS_UNIX_SOCK OFF CACHE BOOL "" FORCE)
set(LWS_WITH_SYS_STATE OFF CACHE BOOL "" FORCE)
set(LWS_WITH_SYS_SMD OFF CACHE BOOL "" FORCE)
set(LWS_WITH_UPNG OFF CACHE BOOL "" FORCE)
set(LWS_WITH_DLO OFF CACHE BOOL "" FORCE)
set(LWS_WITH_SECURE_STREAMS OFF CACHE BOOL "" FORCE)
set(LWS_WITH_LEJP OFF CACHE BOOL "" FORCE)
set(LWS_WITH_LHP OFF CACHE BOOL "" FORCE)
set(LWS_WITH_JSONRPC OFF CACHE BOOL "" FORCE)
set(LWS_WITH_LWSAC OFF CACHE BOOL "" FORCE)
set(LWS_WITH_CONMON OFF CACHE BOOL "" FORCE)
set(LWS_WITH_WOL OFF CACHE BOOL "" FORCE)
set(LWS_WITH_LIBUV ON CACHE BOOL "" FORCE)
set(LWS_WITH_MBEDTLS ON CACHE BOOL "" FORCE)
set(LWS_WITH_SSL ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_EXTENSIONS ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TESTAPPS ON CACHE BOOL "" FORCE)
set(LWS_WITH_SHARED OFF CACHE BOOL "" FORCE)
set(LWS_WITH_STATIC ON CACHE BOOL "" FORCE)
set(LWS_WITH_LIBCAP OFF CACHE BOOL "" FORCE)
set(LWS_WITH_MINIMAL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LWS_ROLE_MQTT ON CACHE BOOL "" FORCE)
set(LWS_WITH_TLS_SESSIONS OFF CACHE BOOL "" FORCE)
set(LWS_WITH_EXPORT_LWSTARGETS OFF CACHE BOOL "" FORCE)
set(LWS_WITH_NETLINK OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|mipsel|mips64)")
    set(LWS_WITH_SYS_ASYNC_DNS OFF CACHE BOOL "" FORCE)
else()
    set(LWS_WITH_SYS_ASYNC_DNS ON CACHE BOOL "" FORCE)
endif()
set(LWS_WITH_CACHE_NSCOOKIEJAR ON CACHE BOOL "" FORCE)
set(LWS_WITH_MINIZ ON CACHE BOOL "" FORCE)
# Pre-set ZLIB variables so lws skips find_package(Miniz)
set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/deps/miniz" CACHE STRING "" FORCE)
set(ZLIB_LIBRARIES miniz CACHE STRING "" FORCE)
set(LWS_HAVE_ZLIB_H 1 CACHE BOOL "" FORCE)
# Tell lws about mbedtls capabilities (CHECK_FUNCTION_EXISTS fails for static libs)
set(LWS_HAVE_mbedtls_md_setup 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_rsa_complete 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_net_init 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_ssl_conf_alpn_protocols 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_ssl_get_alpn_protocol 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_ssl_conf_sni 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_ssl_set_hs_ca_chain 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_ssl_set_hs_own_cert 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_ssl_set_hs_authmode 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_x509_crt_parse_file 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_mbedtls_internal_aes_encrypt 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_MBEDTLS_NET_SOCKETS 1 CACHE BOOL "" FORCE)
set(LWS_HAVE_MBEDTLS_SSL_NEW_SESSION_TICKET 1 CACHE BOOL "" FORCE)
set(LWS_MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto CACHE STRING "" FORCE)
set(LWS_MBEDTLS_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/deps/mbedtls/include" CACHE STRING "" FORCE)
set(LWS_LIBUV_LIBRARIES "${TJS_LIBUV_LINK}" CACHE STRING "" FORCE)
set(LWS_LIBUV_INCLUDE_DIRS "${TJS_LIBUV_INCLUDE_DIR}" CACHE STRING "" FORCE)

# Apply libuv patches.
execute_process(
    COMMAND git apply --reverse --check ${CMAKE_CURRENT_SOURCE_DIR}/patches/libuv.patch
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv
    RESULT_VARIABLE _libuv_patch_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(_libuv_patch_applied EQUAL 0)
    message(STATUS "Already applied libuv.patch")
else()
    execute_process(
        COMMAND git apply ${CMAKE_CURRENT_SOURCE_DIR}/patches/libuv.patch
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/libuv
        RESULT_VARIABLE _libuv_patch_result
    )
    if(_libuv_patch_result EQUAL 0)
        message(STATUS "Applied libuv.patch")
    else()
        message(FATAL_ERROR "Failed to apply libuv.patch")
    endif()
endif()

# Apply mimalloc patches.
execute_process(
    COMMAND git apply --reverse --check ${CMAKE_CURRENT_SOURCE_DIR}/patches/mimalloc.patch
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/mimalloc
    RESULT_VARIABLE _mimalloc_patch_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(_mimalloc_patch_applied EQUAL 0)
    message(STATUS "Already applied mimalloc.patch")
else()
    execute_process(
        COMMAND git apply ${CMAKE_CURRENT_SOURCE_DIR}/patches/mimalloc.patch
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/mimalloc
        RESULT_VARIABLE _mimalloc_patch_result
    )
    if(_mimalloc_patch_result EQUAL 0)
        message(STATUS "Applied mimalloc.patch")
    else()
        message(FATAL_ERROR "Failed to apply mimalloc.patch")
    endif()
endif()

# Apply lws patches.
execute_process(
    COMMAND git apply --reverse --check ${CMAKE_CURRENT_SOURCE_DIR}/patches/lws.patch
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/libwebsockets
    RESULT_VARIABLE _lws_patch_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(_lws_patch_applied EQUAL 0)
    message(STATUS "Already applied lws.patch")
else()
    execute_process(
        COMMAND git apply ${CMAKE_CURRENT_SOURCE_DIR}/patches/lws.patch
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/libwebsockets
        RESULT_VARIABLE _lws_patch_result
    )
    if(_lws_patch_result EQUAL 0)
        message(STATUS "Applied lws.patch")
    else()
        message(FATAL_ERROR "Failed to apply lws.patch")
    endif()
endif()
set(LWS_IPV6 ON CACHE BOOL "Compile with support for ipv6" FORCE)
add_subdirectory(deps/libwebsockets EXCLUDE_FROM_ALL)
# Work around mbedtls ssl.h using #if instead of #ifdef on
# MBEDTLS_SSL_DTLS_CONNECTION_ID_COMPAT (undefined when DTLS is disabled).
if(MSVC)
    target_compile_options(websockets PRIVATE /wd4005)
else()
    target_compile_options(websockets PRIVATE -Wno-undef)
endif()
target_link_libraries(tjs PUBLIC websockets)
target_include_directories(tjs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/deps/libwebsockets/include)
# Generated lws headers (build-time)
target_include_directories(tjs PRIVATE ${CMAKE_BINARY_DIR}/deps/libwebsockets)
target_include_directories(tjs PRIVATE ${CMAKE_BINARY_DIR}/deps/libwebsockets/include)
# lws internal headers (source tree)
target_include_directories(tjs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/deps/libwebsockets/lib)

# HTTP/2 build status
if(XUAN_ENABLE_HTTP2)
    target_compile_definitions(tjs PUBLIC TJS__HAS_HTTP2)
    message(STATUS "  HTTP/2 (libwebsockets): ENABLED")
else()
    message(STATUS "  HTTP/2 (libwebsockets): disabled (default)")
endif()

###
### LVGL Integration (optional GUI library - dynamic module)
###

if(TJS_ENABLE_LVGL)
    message(STATUS "=== LVGL Dynamic Module Configuration ===")
    if(TJS_LVGL_USE_SDL)
        message(STATUS "  Driver: SDL2 (simulator)")
    else()
        message(STATUS "  Driver: fbdev/evdev (embedded device)")
    endif()
    message(STATUS "  Build mode: compile LVGL/lv_drivers from source under xuan_modules/lvgl/deps")
    message(STATUS "  Output: xuan-lvgl.so (dynamic module)")
    message(STATUS "===========================================")
endif()

# Link time64 stub library for ARM targets (old musl compatibility)
if(DEFINED TJS_TIME64_STUB_LIB AND EXISTS "${TJS_TIME64_STUB_LIB}")
    get_filename_component(_stub_name "${TJS_TIME64_STUB_LIB}" NAME)
    get_filename_component(_stub_dir "${TJS_TIME64_STUB_LIB}" DIRECTORY)
    # Copy stub to build dir root for consistent linking
    set(_stub_dest "${CMAKE_BINARY_DIR}/${_stub_name}")
    if(NOT "${TJS_TIME64_STUB_LIB}" STREQUAL "${_stub_dest}")
        file(COPY "${TJS_TIME64_STUB_LIB}" DESTINATION "${CMAKE_BINARY_DIR}")
    endif()
    # Create imported library to link by name (avoids absolute path in DT_NEEDED)
    add_library(time64_stub SHARED IMPORTED)
    set_target_properties(time64_stub PROPERTIES IMPORTED_LOCATION "${_stub_dest}")
    target_link_libraries(tjs PUBLIC time64_stub)
    set_target_properties(xuan-cli PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON
        BUILD_RPATH "\\\$ORIGIN"
    )
    message(STATUS "Linking time64 stub library: ${_stub_dest}")
endif()
