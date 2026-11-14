# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(StarOscilloscope VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(STAROSC_BUILD_TESTS "Build unit tests (needs Catch2 v3)" ON)
option(STAROSC_BUILD_APP "Build the Qt application (needs Qt 6)" ON)

if(STAROSC_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 REQUIRED)
    include(Catch)
endif()

add_subdirectory(driver)
add_subdirectory(core)
if(STAROSC_BUILD_APP)
    add_subdirectory(app)
    add_subdirectory(packaging)
endif()
