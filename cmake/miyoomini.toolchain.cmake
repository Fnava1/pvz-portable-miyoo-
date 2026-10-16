# Miyoo Mini Plus Toolchain Configuration
# Target: SigmaStar SSD202D (Dual ARM Cortex-A7 @ 1.2-1.7 GHz)
# Architecture: armv7-a hard-float with NEON-VFPv4
# OS: OnionOS / Linux

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain base directory
set(TOOLCHAIN_ROOT "/opt/miyoomini-toolchain" CACHE PATH "Miyoo Mini toolchain root")

# Cross-compiler executables
if(EXISTS "${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-gcc")
    set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-gcc")
    set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-g++")
    set(CMAKE_AR "${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-ar" CACHE FILEPATH "Archiver")
    set(CMAKE_RANLIB "${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-ranlib" CACHE FILEPATH "Ranlib")
    set(CMAKE_STRIP "${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-strip" CACHE FILEPATH "Strip")
    set(CMAKE_SYSROOT "${TOOLCHAIN_ROOT}/arm-linux-gnueabihf/sysroot")
else()
    set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
    set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
    find_program(CMAKE_AR NAMES arm-linux-gnueabihf-ar)
    find_program(CMAKE_RANLIB NAMES arm-linux-gnueabihf-ranlib)
    find_program(CMAKE_STRIP NAMES arm-linux-gnueabihf-strip)
endif()

# Miyoo Mini Plus Cortex-A7 hard-float optimization flags
set(MMP_FLAGS "-mcpu=cortex-a7 -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -O3 -fomit-frame-pointer -ffast-math")

set(CMAKE_C_FLAGS_INIT "${MMP_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${MMP_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MMP_FLAGS} -Wl,--as-needed")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${MMP_FLAGS} -Wl,--as-needed")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${MMP_FLAGS} -Wl,--as-needed")

# Search mode settings for cross-compilation
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
