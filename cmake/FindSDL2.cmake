set(SDL2_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/3rdparty/SDL2/include;${CMAKE_SOURCE_DIR}/3rdparty/SDL2/include/SDL2")
set(SDL2_LIBRARIES "${CMAKE_SOURCE_DIR}/3rdparty/SDL2/lib/libSDL2.so")
if(EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/SDL2/lib/libneonarmmiyoo.so")
    list(APPEND SDL2_LIBRARIES "${CMAKE_SOURCE_DIR}/3rdparty/SDL2/lib/libneonarmmiyoo.so")
endif()

if(TARGET SDL2_Vendored)
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS SDL2_Vendored)
    endif()
    if(NOT TARGET SDL2::SDL2-static)
        add_library(SDL2::SDL2-static ALIAS SDL2_Vendored)
    endif()
else()
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 SHARED IMPORTED GLOBAL)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_LOCATION "${SDL2_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        )
    endif()
    if(NOT TARGET SDL2::SDL2-static)
        add_library(SDL2::SDL2-static SHARED IMPORTED GLOBAL)
        set_target_properties(SDL2::SDL2-static PROPERTIES
            IMPORTED_LOCATION "${SDL2_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        )
    endif()
endif()

if(NOT TARGET SDL2::SDL2main)
    add_library(SDL2::SDL2main INTERFACE IMPORTED GLOBAL)
endif()

set(SDL2_FOUND TRUE)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2 DEFAULT_MSG SDL2_LIBRARIES SDL2_INCLUDE_DIRS)
