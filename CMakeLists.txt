cmake_minimum_required(VERSION 3.13)
project(PvZ_Miyoo LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.0")
    add_compile_definitions(constinit=)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(PVZ_ENABLE_GLES=0)
add_compile_definitions(LOW_MEMORY=1)

if(NOT WIN32)
    add_compile_definitions(_POSIX_C_SOURCE=200809L _GNU_SOURCE)
endif()

# Global includes
include_directories(
    /opt/miyoomini-toolchain/arm-linux-gnueabihf/libc/usr/include/libpng16
    ${CMAKE_SYSROOT}/usr/include/libpng16
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SexyAppFramework
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SexyAppFramework/sound/SDL-Mixer-X/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/include/SDL2
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jpeg
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libopenmpt/include
)

# ---------------------------------------------------------------------------
# Vendored SDL2 imported target
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/lib/libSDL2.so")
    add_library(SDL2_Vendored SHARED IMPORTED GLOBAL)
    set_target_properties(SDL2_Vendored PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/lib/libSDL2.so"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/include/SDL2"
    )
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/lib/libneonarmmiyoo.so")
        set_target_properties(SDL2_Vendored PROPERTIES
            INTERFACE_LINK_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/lib/libneonarmmiyoo.so"
        )
    endif()
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS SDL2_Vendored)
    endif()
    set(SDL2_LIB "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/lib/libSDL2.so")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/lib/libneonarmmiyoo.so")
        list(APPEND SDL2_LIB "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/lib/libneonarmmiyoo.so")
    endif()
    set(SDL2_FOUND TRUE)
endif()

# ---------------------------------------------------------------------------
# Module: JPEG library (pvz_jpeg)
# ---------------------------------------------------------------------------
set(JPEG_SOURCES
    3rdparty/jpeg/jcapimin.c 
    3rdparty/jpeg/jcapistd.c
    3rdparty/jpeg/jccoefct.c 
    3rdparty/jpeg/jccolor.c 
    3rdparty/jpeg/jcdctmgr.c 
    3rdparty/jpeg/jchuff.c
    3rdparty/jpeg/jcinit.c 
    3rdparty/jpeg/jcmainct.c 
    3rdparty/jpeg/jcmarker.c 
    3rdparty/jpeg/jcmaster.c 
    3rdparty/jpeg/jcomapi.c 
    3rdparty/jpeg/jcparam.c
    3rdparty/jpeg/jcphuff.c 
    3rdparty/jpeg/jcprepct.c 
    3rdparty/jpeg/jcsample.c
    3rdparty/jpeg/jctrans.c 
    3rdparty/jpeg/jdapimin.c 
    3rdparty/jpeg/jdapistd.c
    3rdparty/jpeg/jdatadst.c 
    3rdparty/jpeg/jdatasrc.c 
    3rdparty/jpeg/jdcoefct.c 
    3rdparty/jpeg/jdcolor.c 
    3rdparty/jpeg/jddctmgr.c 
    3rdparty/jpeg/jdhuff.c
    3rdparty/jpeg/jdinput.c 
    3rdparty/jpeg/jdmainct.c 
    3rdparty/jpeg/jdmarker.c 
    3rdparty/jpeg/jdmaster.c 
    3rdparty/jpeg/jdmerge.c 
    3rdparty/jpeg/jdphuff.c
    3rdparty/jpeg/jdpostct.c 
    3rdparty/jpeg/jdsample.c 
    3rdparty/jpeg/jdtrans.c 
    3rdparty/jpeg/jerror.c 
    3rdparty/jpeg/jfdctflt.c 
    3rdparty/jpeg/jfdctfst.c
    3rdparty/jpeg/jfdctint.c 
    3rdparty/jpeg/jidctflt.c 
    3rdparty/jpeg/jidctfst.c 
    3rdparty/jpeg/jidctint.c 
    3rdparty/jpeg/jidctred.c 
    3rdparty/jpeg/jquant1.c
    3rdparty/jpeg/jquant2.c 
    3rdparty/jpeg/jutils.c 
    3rdparty/jpeg/jmemmgr.c
    3rdparty/jpeg/jmemansi.c
)
add_library(pvz_jpeg STATIC ${JPEG_SOURCES})
target_include_directories(pvz_jpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jpeg)

# ---------------------------------------------------------------------------
# Module: SDL-Mixer-X
# ---------------------------------------------------------------------------
set(SDL_MIXER_X_STATIC ON CACHE BOOL "Build static library of SDL Mixer X")
set(SDL_MIXER_X_SHARED OFF CACHE BOOL "Build shared library of SDL Mixer X")
set(USE_MIDI OFF CACHE BOOL "SDL Mixer X midi")
set(USE_MODPLUG OFF CACHE BOOL "SDL Mixer X modplug")
set(USE_XMP OFF CACHE BOOL "SDL Mixer X xmp")
set(USE_OPUS OFF CACHE BOOL "SDL Mixer X opus")
set(USE_DRFLAC OFF CACHE BOOL "SDL Mixer X flac")
set(USE_PXTONE OFF CACHE BOOL "SDL Mixer X pxtone")
set(USE_SYSTEM_SDL2 ON CACHE BOOL "Use system SDL2")
set(USE_SYSTEM_AUDIO_LIBRARIES ON CACHE BOOL "Use system audio libraries" FORCE)
set(USE_OPENMPT ON CACHE BOOL "SDL Mixer X openmpt" FORCE)
set(USE_OPENMPT_STATIC ON CACHE BOOL "SDL Mixer X openmpt static" FORCE)
set(USE_OPENMPT_DYNAMIC OFF CACHE BOOL "SDL Mixer X openmpt dynamic" FORCE)
set(OpenMPT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libopenmpt/include" CACHE PATH "libopenmpt include dir" FORCE)
set(OpenMPT_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libopenmpt/lib/libopenmpt.a" CACHE FILEPATH "libopenmpt library" FORCE)
set(OpenMPT_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libopenmpt/include" CACHE PATH "libopenmpt include dirs" FORCE)
set(OpenMPT_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libopenmpt/lib/libopenmpt.a" CACHE FILEPATH "libopenmpt libraries" FORCE)
set(OpenMPT_FOUND TRUE CACHE BOOL "libopenmpt found" FORCE)
add_subdirectory(src/SexyAppFramework/sound/SDL-Mixer-X EXCLUDE_FROM_ALL)

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
# Module: Audio Subsystem (pvz_audio)
# ---------------------------------------------------------------------------
set(AUDIO_SOURCES
    "src/audio/OpenMptMusicInterface.cpp"
)
add_library(pvz_audio STATIC ${AUDIO_SOURCES})
target_link_libraries(pvz_audio PUBLIC m SDL2_mixer_ext_Static ${OpenMPT_LIBRARIES} z ${SDL2_LIB})
if(NOT WIN32)
    target_link_libraries(pvz_audio PUBLIC dl)
endif()

# ---------------------------------------------------------------------------
# Module: Input Subsystem (pvz_input)
# ---------------------------------------------------------------------------
set(INPUT_SOURCES
    "src/input/HandheldInput.cpp"
)
add_library(pvz_input STATIC ${INPUT_SOURCES})
target_link_libraries(pvz_input PUBLIC m)

# ---------------------------------------------------------------------------
# Target 1: Engine Core (PvZ)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_CPP_SOURCES CONFIGURE_DEPENDS
    src/LawnApp.cpp
    src/Resources.cpp
    src/Lawn/*.cpp
    src/PvzpLib/*.cpp
    src/SexyAppFramework/*.cpp
    src/SexyAppFramework/graphics/*.cpp
    src/SexyAppFramework/imagelib/*.cpp
    src/SexyAppFramework/misc/*.cpp
    src/SexyAppFramework/paklib/*.cpp
    src/SexyAppFramework/sound/*.cpp
    src/SexyAppFramework/widget/*.cpp
    src/SexyAppFramework/platform/default/Window.cpp
    src/SexyAppFramework/platform/default/Input.cpp
)
list(FILTER ENGINE_CPP_SOURCES EXCLUDE REGEX "src/SexyAppFramework/platform/(switch|emscripten)/")
list(FILTER ENGINE_CPP_SOURCES EXCLUDE REGEX "src/SexyAppFramework/sound/SDL-Mixer-X/")

set(ENGINE_SOURCES
    ${ENGINE_CPP_SOURCES}
    src/SexyAppFramework/fcaseopen/fcaseopen.c
    src/main_miyoo.cpp
)

add_executable(PvZ ${ENGINE_SOURCES})
target_link_libraries(PvZ PRIVATE
    pvz_graphics
    pvz_audio
    pvz_input
    pvz_jpeg
    SDL2_mixer_ext_Static
    ${OpenMPT_LIBRARIES}
    ${SDL2_LIB}
    png16
    z
    m
    pthread
    dl
    stdc++fs
)

# ---------------------------------------------------------------------------
# Target 2: Setup Utility (setup_miyoo) & Setup Core Library
# ---------------------------------------------------------------------------
set(SETUP_LIB_SOURCES
    "src/setup/setup_converter.c"
    "src/setup/fb_progress.c"
    "src/setup/stb_impl.c"
)

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
# Target 3: Test Suite
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

    add_executable(test_arm "tests/test_setup_conversion.cpp")
    target_link_libraries(test_arm setup_core m)
    add_test(NAME test_arm COMMAND test_arm)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_openmpt_playback.cpp")
    add_executable(test_openmpt_playback "tests/test_openmpt_playback.cpp")
    target_link_libraries(test_openmpt_playback PRIVATE pvz_audio SDL2_mixer_ext_Static ${OpenMPT_LIBRARIES} z ${SDL2_LIB} m)
    if(NOT WIN32)
        target_link_libraries(test_openmpt_playback PRIVATE dl pthread)
    endif()
    add_test(NAME test_openmpt_playback COMMAND test_openmpt_playback)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_input_mapping.cpp")
    add_executable(test_input_mapping "tests/test_input_mapping.cpp")
    target_link_libraries(test_input_mapping PRIVATE pvz_input m)
    add_test(NAME test_input_mapping COMMAND test_input_mapping)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main_teardown.cpp")
    add_executable(test_main_teardown "tests/test_main_teardown.cpp" "src/main_miyoo.cpp")
    target_compile_definitions(test_main_teardown PRIVATE PVZ_UNIT_TEST=1)
    target_link_libraries(test_main_teardown PRIVATE pvz_graphics pvz_audio pvz_input m)
    if(SDL2_LIB)
        target_link_libraries(test_main_teardown PRIVATE ${SDL2_LIB})
    endif()
    if(NOT WIN32)
        target_link_libraries(test_main_teardown PRIVATE dl pthread)
    endif()
    add_test(NAME test_main_teardown COMMAND test_main_teardown)
endif()
