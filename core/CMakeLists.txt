# SPDX-License-Identifier: GPL-3.0-or-later
# starosc_core: acquisition logic, triggering, measurements and the simulator. No USB, no Qt.
add_library(starosc_core
    src/autoset.cpp
    src/calibration.cpp
    src/cursor.cpp
    src/engine.cpp
    src/fft.cpp
    src/gain_transfer.cpp
    src/generator.cpp
    src/interpolate.cpp
    src/math.cpp
    src/reference.cpp
    src/measure.cpp
    src/scale.cpp
    src/simulator.cpp
    src/spectrum.cpp
    src/timebase.cpp
    src/trigger.cpp
    src/window.cpp
    src/zoom.cpp
)
target_include_directories(starosc_core PUBLIC include)
target_link_libraries(starosc_core PUBLIC isds_core)

# starosc_device: the ISDS205X as a Source.
add_library(starosc_device src/device_source.cpp)
target_link_libraries(starosc_device PUBLIC starosc_core PRIVATE isds)

if(NOT MSVC)
    target_compile_options(starosc_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(starosc_device PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(STAROSC_BUILD_TESTS)
    add_executable(starosc_tests
        tests/test_autoset.cpp
        tests/test_calibration.cpp
        tests/test_cursor.cpp
        tests/test_engine.cpp
        tests/test_gain_transfer.cpp
        tests/test_generator.cpp
        tests/test_interpolate.cpp
        tests/test_math.cpp
        tests/test_measure.cpp
        tests/test_scale.cpp
        tests/test_settings.cpp
        tests/test_spectrum.cpp
        tests/test_timebase.cpp
        tests/test_trigger.cpp
        tests/test_zoom.cpp
    )
    target_link_libraries(starosc_tests PRIVATE starosc_core Catch2::Catch2WithMain)
    catch_discover_tests(starosc_tests)
endif()
