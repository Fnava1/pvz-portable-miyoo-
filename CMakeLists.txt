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

# ---------------------------------------------------------------------------
# Target 1: Engine Core (PvZ)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES
    "src/*.cpp"
    "src/*.c"
)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "src/setup/.*")

if(NOT ENGINE_SOURCES)
    # Temporary scaffolding stub until engine modules are populated
    set(ENGINE_STUB "${CMAKE_CURRENT_BINARY_DIR}/stub_engine.cpp")
    file(WRITE "${ENGINE_STUB}" "int main(int argc, char* argv[]) { (void)argc; (void)argv; return 0; }\n")
    set(ENGINE_SOURCES "${ENGINE_STUB}")
endif()

add_executable(PvZ ${ENGINE_SOURCES})

# ---------------------------------------------------------------------------
# Target 2: Setup Utility (setup_miyoo)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SETUP_SOURCES
    "src/setup/*.c"
    "src/setup/*.cpp"
)

if(NOT SETUP_SOURCES)
    # Temporary scaffolding stub until setup modules are populated
    set(SETUP_STUB "${CMAKE_CURRENT_BINARY_DIR}/stub_setup.c")
    file(WRITE "${SETUP_STUB}" "int main(int argc, char* argv[]) { (void)argc; (void)argv; return 0; }\n")
    set(SETUP_SOURCES "${SETUP_STUB}")
endif()

add_executable(setup_miyoo ${SETUP_SOURCES})

# ---------------------------------------------------------------------------
# Target 3: Test Suite / Dummy ARM Target (test_arm)
# ---------------------------------------------------------------------------
enable_testing()

file(GLOB_RECURSE TEST_SOURCES
    "tests/*.cpp"
    "tests/*.c"
)

if(NOT TEST_SOURCES)
    set(TEST_ARM_STUB "${CMAKE_CURRENT_BINARY_DIR}/test_arm.c")
    file(WRITE "${TEST_ARM_STUB}" "/* Miyoo Mini Plus ARM verification target */\nint main(void) { return 0; }\n")
    set(TEST_SOURCES "${TEST_ARM_STUB}")
endif()

add_executable(test_arm ${TEST_SOURCES})
add_test(NAME test_arm COMMAND test_arm)
