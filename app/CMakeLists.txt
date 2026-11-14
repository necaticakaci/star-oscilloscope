# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(Qt6 QUIET COMPONENTS LinguistTools)
qt_standard_project_setup()

# Everything except main(), so the widgets can be tested.
qt_add_library(starosc_gui STATIC
    src/acquisitionworker.cpp
    src/acquisitionworker.hpp
    src/appicon.cpp
    src/appicon.hpp
    src/calibrationdialog.cpp
    src/calibrationdialog.hpp
    src/calibrationstore.cpp
    src/calibrationstore.hpp
    src/columnsdialog.cpp
    src/columnsdialog.hpp
    src/controlpanel.cpp
    src/controlpanel.hpp
    src/displaystyle.cpp
    src/displaystyle.hpp
    src/frontpanel.cpp
    src/frontpanel.hpp
    src/gainwizard.cpp
    src/gainwizard.hpp
    src/hoverhelp.cpp
    src/hoverhelp.hpp
    src/generatorpanel.cpp
    src/generatorpanel.hpp
    src/generatorstrip.cpp
    src/generatorstrip.hpp
    src/icons.cpp
    src/icons.hpp
    src/knob.cpp
    src/knob.hpp
    src/language.cpp
    src/language.hpp
    src/mainwindow.cpp
    src/mainwindow.hpp
    src/measurementdialog.cpp
    src/measurementdialog.hpp
    src/measurementselection.cpp
    src/measurementselection.hpp
    src/measurementtable.cpp
    src/measurementtable.hpp
    src/overviewstrip.cpp
    src/overviewstrip.hpp
    src/probebox.cpp
    src/probebox.hpp
    src/refreshlimiter.hpp
    src/scopepanel.cpp
    src/scopepanel.hpp
    src/scopeview.cpp
    src/scopeview.hpp
    src/settingsstore.cpp
    src/settingsstore.hpp
    src/spectrumpanel.cpp
    src/spectrumpanel.hpp
    src/spectrumtable.cpp
    src/spectrumtable.hpp
    src/spectrumview.cpp
    src/spectrumview.hpp
    src/sispinbox.cpp
    src/sispinbox.hpp
    src/theme.cpp
    src/theme.hpp
    src/tracecolorsdialog.cpp
    src/tracecolorsdialog.hpp
    src/types.hpp
)
target_include_directories(starosc_gui PUBLIC src)
target_link_libraries(starosc_gui PUBLIC Qt6::Widgets starosc_core starosc_device)

# Translations of the interface (docs/translating.md). `cmake --build <dir> --target update_translations` gathers the texts
# of the sources into the .ts files; the build turns them into .qm files inside the program, under :/i18n. Without
# Qt's Linguist tools the program is built in English only.
set(STAROSC_TS_FILES translations/star-oscilloscope_tr.ts)
if(TARGET Qt6::lrelease)
    qt_add_translations(starosc_gui
        TS_FILES ${STAROSC_TS_FILES}
        RESOURCE_PREFIX /i18n
        LUPDATE_OPTIONS -locations none -no-obsolete)
    target_compile_definitions(starosc_gui PUBLIC STAROSC_TRANSLATED)
else()
    message(WARNING "Qt Linguist tools not found: the interface is built in English only "
                    "(Debian/Ubuntu: qt6-l10n-tools qt6-tools-dev; MSYS2: mingw-w64-ucrt-x86_64-qt6-tools)")
endif()

qt_add_executable(star-oscilloscope src/main.cpp)
target_link_libraries(star-oscilloscope PRIVATE starosc_gui)
target_compile_definitions(star-oscilloscope PRIVATE STAROSC_VERSION="${PROJECT_VERSION}")
set_target_properties(star-oscilloscope PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
if(WIN32)
    # The icon and version of the executable file; the windows get their icon from app_icon::icon().
    set(STAROSC_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/star-oscilloscope.ico")
    configure_file(resources/star-oscilloscope.rc.in star-oscilloscope.rc @ONLY)
    target_sources(star-oscilloscope PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/star-oscilloscope.rc")
endif()

# Writes the icon files in app/resources from the drawing in src/appicon.cpp (tools/make_icon.cpp).
qt_add_executable(star-icon ../tools/make_icon.cpp)
target_link_libraries(star-icon PRIVATE starosc_gui)

# Bench self-test of the generator and the sample rates, DDS OUT on CH2 (tools/loopback.cpp).
qt_add_executable(star-loopback ../tools/loopback.cpp)
target_link_libraries(star-loopback PRIVATE starosc_gui)
# Bench check of the Spectrum tab's measurements on the generator, DDS OUT on CH2, or of its math trace with one
# signal on both inputs (tools/spectrum_bench.cpp).
qt_add_executable(star-spectrum-bench ../tools/spectrum_bench.cpp)
target_link_libraries(star-spectrum-bench PRIVATE starosc_gui)
# Bench check that no command reply goes missing on the USB link (tools/usb_stress.cpp).
add_executable(star-usb-stress ../tools/usb_stress.cpp)
target_link_libraries(star-usb-stress PRIVATE starosc_device isds)

if(NOT MSVC)
    target_compile_options(starosc_gui PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(star-oscilloscope PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(STAROSC_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(starosc_gui_tests tests/test_gui.cpp)
    target_link_libraries(starosc_gui_tests PRIVATE starosc_gui Qt6::Test)
    target_compile_definitions(starosc_gui_tests PRIVATE STAROSC_ICON_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
                                                         STAROSC_TS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/translations")
    add_test(NAME starosc_gui_tests COMMAND starosc_gui_tests)
    set_tests_properties(starosc_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
