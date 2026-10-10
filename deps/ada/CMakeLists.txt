option(ADA_BUILD_SHARED "Build ada as a shared library" OFF)
option(ADA_MIN_SIZE "Optimize ada for binary size" OFF)

if(ADA_BUILD_SHARED)
    set(_ada_lib_type SHARED)
else()
    set(_ada_lib_type STATIC)
endif()

add_library(ada ${_ada_lib_type}
    ada.cpp
)
target_include_directories(ada PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (NOT MSVC)
    target_compile_options(ada PRIVATE -fno-exceptions -fno-rtti)
endif()
if(ADA_MIN_SIZE)
    target_compile_definitions(ada PRIVATE ADA_INCLUDE_URL_PATTERN=0)
    if (NOT MSVC)
        target_compile_options(ada PRIVATE
            -Oz
            -ffunction-sections
            -fdata-sections
            -fno-unwind-tables
            -fno-asynchronous-unwind-tables
        )
        target_link_options(ada PRIVATE -Wl,--gc-sections)
    endif()
endif()
set_target_properties(ada PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
