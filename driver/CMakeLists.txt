# SPDX-License-Identifier: GPL-3.0-or-later
# isds_core: protocol frames, descriptor and calibration. Pure C++, no USB dependency.
add_library(isds_core
    src/protocol.cpp
    src/descriptor.cpp
    src/calibration.cpp
    src/generator.cpp
)
target_include_directories(isds_core PUBLIC include)
if(NOT MSVC)
    target_compile_options(isds_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# isds: device access over libusb (WinUSB on Windows).
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
find_package(Threads REQUIRED)

add_library(isds src/device.cpp)
target_link_libraries(isds PUBLIC isds_core PRIVATE PkgConfig::LIBUSB Threads::Threads)

# RAW_IO for WinUSB came with libusb 1.0.28; on Windows the data endpoint needs it (src/device.cpp).
include(CheckSymbolExists)
set(CMAKE_REQUIRED_INCLUDES ${LIBUSB_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${LIBUSB_LINK_LIBRARIES})
check_symbol_exists(libusb_endpoint_set_raw_io "libusb.h" ISDS_HAVE_RAW_IO)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)
if(ISDS_HAVE_RAW_IO)
    target_compile_definitions(isds PRIVATE ISDS_HAVE_RAW_IO)
endif()
if(NOT MSVC)
    target_compile_options(isds PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(isdsctl tools/isdsctl.cpp)
target_link_libraries(isdsctl PRIVATE isds)

if(STAROSC_BUILD_TESTS)
    add_executable(isds_tests
        tests/test_protocol.cpp
        tests/test_descriptor.cpp
        tests/test_calibration.cpp
        tests/test_generator.cpp
    )
    target_link_libraries(isds_tests PRIVATE isds_core Catch2::Catch2WithMain)
    target_compile_definitions(isds_tests PRIVATE ISDS_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data")
    catch_discover_tests(isds_tests)
endif()
