# SPDX-License-Identifier: GPL-3.0-or-later
# Installing the program and making its packages (docs/building.md §1.3):
#
#   cmake -S . -B build/pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAROSC_BUILD_TESTS=OFF
#   cmake --build build/pkg
#   cpack --config build/pkg/CPackConfig.cmake -B build/pkg/packages
#
# Windows (MSYS2 UCRT64): a folder to unzip and run, star-oscilloscope-<version>-windows-x64.zip, with Qt and every DLL
# the program needs. Linux: a Debian package, star-oscilloscope_<version>_amd64.deb, on the system's Qt, with the udev
# rule that lets the logged-in user reach the device. Only the program is installed, not the bench tools.

set(CPACK_PACKAGE_NAME star-oscilloscope)
set(CPACK_PACKAGE_VENDOR "Necati Çakacı")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Oscilloscope software for the Instrustar ISDS205 series")
string(CONCAT CPACK_PACKAGE_DESCRIPTION
    "Star Oscilloscope drives the Instrustar ISDS205 series USB oscilloscopes through its own USB driver: two "
    "channels, software trigger, measurements, cursors, math, reference waveforms, X-Y, a spectrum analyser and the "
    "signal generator of the models that have one.")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)

if(WIN32)
    # The executable at the top of the folder, its DLLs beside it: Windows looks for them there.
    install(TARGETS star-oscilloscope RUNTIME DESTINATION . BUNDLE DESTINATION .)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION . RENAME LICENSE.txt)
    install(FILES "${PROJECT_SOURCE_DIR}/README.md" DESTINATION .)

    # Qt's own translations for the program's languages (app/src/language.cpp looks in translations/ beside it).
    file(GLOB ts_files "${PROJECT_SOURCE_DIR}/app/translations/star-oscilloscope_*.ts")
    set(languages "")
    foreach(ts IN LISTS ts_files)
        string(REGEX REPLACE ".*star-oscilloscope_(.+)\\.ts$" "\\1" language "${ts}")
        list(APPEND languages "${language}")
    endforeach()

    find_package(Qt6 REQUIRED COMPONENTS Core)  # Qt's folders, known in app/ only
    set(qt_bin "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    find_program(STAROSC_WINDEPLOYQT NAMES windeployqt6 windeployqt HINTS "${qt_bin}" NO_DEFAULT_PATH REQUIRED)
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH compiler_bin)
    cmake_path(GET compiler_bin PARENT_PATH msys_prefix)
    # MSYS2's package manager tells which package each DLL comes from, for the licences shipped with them.
    find_program(STAROSC_PACMAN pacman HINTS "${msys_prefix}/../usr/bin" REQUIRED)
    install(CODE "
        set(STAROSC_WINDEPLOYQT \"${STAROSC_WINDEPLOYQT}\")
        set(STAROSC_OBJDUMP \"${CMAKE_OBJDUMP}\")
        set(STAROSC_PACMAN \"${STAROSC_PACMAN}\")
        set(STAROSC_DLL_DIRS \"${qt_bin};${compiler_bin}\")
        set(STAROSC_QT_PLUGINS \"${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}\")
        set(STAROSC_LICENSES \"${msys_prefix}/share/licenses\")
        set(STAROSC_QT_TRANSLATIONS \"${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}\")
        set(STAROSC_LANGUAGES \"${languages}\")")
    install(SCRIPT windows/deploy.cmake)

    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "star-oscilloscope-${PROJECT_VERSION}-windows-x64")
else()
    include(GNUInstallDirs)
    install(TARGETS star-oscilloscope RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" BUNDLE DESTINATION .)
    install(FILES linux/star-oscilloscope.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    foreach(size 16 24 32 48 64 128 256)
        install(FILES "${PROJECT_SOURCE_DIR}/app/resources/icons/star-oscilloscope-${size}.png"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps" RENAME star-oscilloscope.png)
    endforeach()
    # udev reads its rules from /usr/lib/udev/rules.d (the package) and /etc/udev/rules.d (the administrator); an
    # installation under another prefix copies the rule to /etc/udev/rules.d by hand.
    set(STAROSC_UDEV_RULES_DIR "lib/udev/rules.d" CACHE STRING "Where the udev rule is installed, under the prefix")
    install(FILES linux/60-star-oscilloscope.rules DESTINATION "${STAROSC_UDEV_RULES_DIR}")
    set(doc_dir "${CMAKE_INSTALL_DATADIR}/doc/star-oscilloscope")
    install(FILES "${PROJECT_SOURCE_DIR}/README.md" linux/copyright DESTINATION "${doc_dir}")

    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    # Who answers for the package; someone making packages of their own puts their name here.
    set(STAROSC_PACKAGE_CONTACT "Necati Çakacı <necaticakaci@yahoo.com.tr>" CACHE STRING "Maintainer of the Linux packages")
    set(CPACK_PACKAGE_CONTACT "${STAROSC_PACKAGE_CONTACT}")
    # CI makes one package per distribution, each against that distribution's libraries; the name says which.
    set(STAROSC_PACKAGE_DISTRO "" CACHE STRING "Distribution the .deb is built for, put into its file name (e.g. debian13)")
    if(STAROSC_PACKAGE_DISTRO)
        execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE arch OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(CPACK_DEBIAN_FILE_NAME "star-oscilloscope_${PROJECT_VERSION}_${STAROSC_PACKAGE_DISTRO}_${arch}.deb")
    else()
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    endif()
    set(CPACK_DEBIAN_PACKAGE_SECTION electronics)
    # The libraries come from dpkg-shlibdeps (Qt, libusb). Qt's Wayland plugin and its translations are optional:
    # without them the window opens through XWayland, and Qt's own dialogs speak English.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland, qt6-translations-l10n")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/linux/postinst"
                                           "${CMAKE_CURRENT_SOURCE_DIR}/linux/postrm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
    # The package takes the files' modes from the staging folder; on a Windows drive under WSL every file is 0777.
    set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/linux/check-modes.cmake")
endif()

include(CPack)
