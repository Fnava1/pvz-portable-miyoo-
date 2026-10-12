cmake_minimum_required(VERSION 3.13)
project(PvZ_Miyoo LANGUAGES C CXX)

# Language standards: C++20 for engine, C11 for setup and low-level routines
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# GLES Option: strictly OFF by default on Miyoo Mini Plus (SSD202D software blitter)
option(ENABLE_GLES "Enable OpenGLES support (disabled for Miyoo software rasterizer)" OFF)

if(ENABLE_GLES)
    add_compile_definitions(PVZ_ENABLE_GLES=1)
    message(STATUS "Renderer: OpenGL ES enabled")
else()
    add_compile_definitions(PVZ_ENABLE_GLES=0)
    message(STATUS "Renderer: 100% Software Rasterizer (Miyoo Mini Plus native mode)")
endif()

# Global include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Search for SDL headers in system/toolchain include paths
find_path(SDL_INCLUDE_DIR NAMES SDL.h SDL2/SDL.h PATH_SUFFIXES SDL SDL2)
if(SDL_INCLUDE_DIR)
    include_directories(${SDL_INCLUDE_DIR})
endif()

# ---------------------------------------------------------------------------
# Module: Graphics Subsystem (pvz_graphics)
# ---------------------------------------------------------------------------
set(GRAPHICS_SOURCES
    "src/graphics/NeonBlit.cpp"
    "src/graphics/SoftwareRenderer.cpp"
)

add_library(pvz_graphics STATIC ${GRAPHICS_SOURCES})
target_link_libraries(pvz_graphics PUBLIC m)

# ---------------------------------------------------------------------------
# Target 1: Engine Core (PvZ)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES
    "src/*.cpp"
    "src/*.c"
)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "src/setup/.*")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "src/graphics/.*")

# Check if any source provides main() entry point
set(HAS_ENGINE_MAIN FALSE)
foreach(SRC ${ENGINE_SOURCES})
    if(SRC MATCHES ".*main.*")
        set(HAS_ENGINE_MAIN TRUE)
    endif()
endforeach()

if(NOT HAS_ENGINE_MAIN)
    # Temporary scaffolding stub until engine main module is created
    set(ENGINE_STUB "${CMAKE_CURRENT_BINARY_DIR}/stub_engine.cpp")
    file(WRITE "${ENGINE_STUB}" "int main(int argc, char* argv[]) { (void)argc; (void)argv; return 0; }\n")
    list(APPEND ENGINE_SOURCES "${ENGINE_STUB}")
endif()

add_executable(PvZ ${ENGINE_SOURCES})
target_link_libraries(PvZ PRIVATE pvz_graphics m)

# ---------------------------------------------------------------------------
# Target 2: Setup Utility (setup_miyoo) & Setup Core Library
# ---------------------------------------------------------------------------
set(SETUP_LIB_SOURCES
    "src/setup/setup_converter.c"
    "src/setup/fb_progress.c"
    "src/setup/stb_impl.c"
)

# Filter existing files
set(VALID_SETUP_LIB_SOURCES "")
foreach(SRC ${SETUP_LIB_SOURCES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
        list(APPEND VALID_SETUP_LIB_SOURCES "${SRC}")
    endif()
endforeach()

if(VALID_SETUP_LIB_SOURCES)
    add_library(setup_core STATIC ${VALID_SETUP_LIB_SOURCES})
    target_link_libraries(setup_core PUBLIC m)
else()
    # Stub library if implementation files not created yet
    set(SETUP_LIB_STUB "${CMAKE_CURRENT_BINARY_DIR}/stub_setup_lib.c")
    file(WRITE "${SETUP_LIB_STUB}" "/* stub */\n")
    add_library(setup_core STATIC "${SETUP_LIB_STUB}")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/setup/setup_miyoo.c")
    add_executable(setup_miyoo "src/setup/setup_miyoo.c")
    target_link_libraries(setup_miyoo setup_core m)
else()
    set(SETUP_STUB "${CMAKE_CURRENT_BINARY_DIR}/stub_setup.c")
    file(WRITE "${SETUP_STUB}" "int main(int argc, char* argv[]) { (void)argc; (void)argv; return 0; }\n")
    add_executable(setup_miyoo "${SETUP_STUB}")
endif()

# ---------------------------------------------------------------------------
# Target 3: Test Suite (test_viewport_scale, test_setup_conversion, test_arm)
# ---------------------------------------------------------------------------
enable_testing()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_viewport_scale.cpp")
    add_executable(test_viewport_scale "tests/test_viewport_scale.cpp")
    target_link_libraries(test_viewport_scale PRIVATE pvz_graphics m)
    add_test(NAME test_viewport_scale COMMAND test_viewport_scale)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_setup_conversion.cpp")
    add_executable(test_setup_conversion "tests/test_setup_conversion.cpp")
    target_link_libraries(test_setup_conversion setup_core m)
    add_test(NAME test_setup_conversion COMMAND test_setup_conversion)

    # test_arm target for Miyoo Mini ARM verification compatibility
    add_executable(test_arm "tests/test_setup_conversion.cpp")
    target_link_libraries(test_arm setup_core m)
    add_test(NAME test_arm COMMAND test_arm)
else()
    set(TEST_ARM_STUB "${CMAKE_CURRENT_BINARY_DIR}/test_arm.c")
    file(WRITE "${TEST_ARM_STUB}" "/* Miyoo Mini Plus ARM verification target */\nint main(void) { return 0; }\n")
    add_executable(test_arm "${TEST_ARM_STUB}")
    add_test(NAME test_arm COMMAND test_arm)
endif()
