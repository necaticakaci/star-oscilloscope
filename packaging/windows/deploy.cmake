# SPDX-License-Identifier: GPL-3.0-or-later
# Runs when the program is installed on Windows (packaging/CMakeLists.txt): puts Qt and every DLL the program needs
# from the toolchain beside star-oscilloscope.exe, so that the folder runs on a computer without MSYS2.
#
# Given by packaging/CMakeLists.txt: STAROSC_WINDEPLOYQT, STAROSC_OBJDUMP, STAROSC_DLL_DIRS (where Qt and the
# toolchain keep their DLLs), STAROSC_QT_TRANSLATIONS (Qt's translations), STAROSC_LANGUAGES (the program's languages
# besides English).

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)  # paths with forward slashes before they are matched (CMake 4.2)
endif()

set(dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
set(exe "${dir}/star-oscilloscope.exe")

# Qt's libraries and plugins. Left out: image formats (screenshots are PNG, which Qt reads and writes itself), touch
# input over the network (it brings Qt Network along), network information and TLS. Qt's translations are copied
# below, only those of the program's languages.
execute_process(
    COMMAND "${STAROSC_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
            --skip-plugin-types generic,imageformats,networkinformation,tls --verbose 0 "${exe}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed on ${exe}")
endif()

foreach(language IN LISTS STAROSC_LANGUAGES)
    set(qm "${STAROSC_QT_TRANSLATIONS}/qtbase_${language}.qm")
    if(EXISTS "${qm}")
        file(COPY "${qm}" DESTINATION "${dir}/translations")
    else()
        message(WARNING "Qt has no translation for '${language}': ${qm}")
    endif()
endforeach()

# Everything those need in turn from the toolchain: libusb, the C++ runtime, ICU, the font and image libraries. The
# system's own DLLs stay out.
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${STAROSC_OBJDUMP}")
file(GLOB_RECURSE libraries "${dir}/*.dll")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${exe}"
    LIBRARIES ${libraries}
    DIRECTORIES ${STAROSC_DLL_DIRS}
    RESOLVED_DEPENDENCIES_VAR found
    UNRESOLVED_DEPENDENCIES_VAR missing
    CONFLICTING_DEPENDENCIES_PREFIX twice
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]")
if(missing)
    message(FATAL_ERROR "DLLs not found: ${missing}")
endif()
# Those windeployqt put here are found both here and in Qt's folder.
foreach(name IN LISTS twice_FILENAMES)
    list(APPEND found ${twice_${name}})
endforeach()

# Folders compared as Windows does, without regard to case.
set(dll_dirs "")
foreach(dll_dir IN LISTS STAROSC_DLL_DIRS)
    cmake_path(NORMAL_PATH dll_dir)
    string(TOLOWER "${dll_dir}" dll_dir)
    list(APPEND dll_dirs "${dll_dir}")
endforeach()
set(here "${dir}")
cmake_path(NORMAL_PATH here)
string(TOLOWER "${here}" here)
set(copied 0)
foreach(file IN LISTS found)
    cmake_path(GET file PARENT_PATH parent)
    cmake_path(NORMAL_PATH parent)
    string(TOLOWER "${parent}" parent)
    cmake_path(GET file FILENAME name)
    if(parent STREQUAL here)
        continue()  # already beside the program
    elseif(NOT parent IN_LIST dll_dirs)
        message(WARNING "${name} comes from ${parent}, outside the toolchain: left out")
    elseif(NOT EXISTS "${dir}/${name}")
        file(COPY "${file}" DESTINATION "${dir}")
        math(EXPR copied "${copied} + 1")
    endif()
endforeach()
message(STATUS "Deployed Qt and ${copied} further DLLs beside star-oscilloscope.exe")

# The licences of everything shipped besides the program, from the MSYS2 packages the DLLs belong to, and a list of
# those packages with their versions: their sources are kept by MSYS2 under the same names and versions. LGPL
# libraries such as Qt and libusb ask for both.
file(GLOB_RECURSE shipped RELATIVE "${dir}" "${dir}/*.dll")
set(packages "")
foreach(dll IN LISTS shipped)
    if(dll MATCHES "/")
        set(origin "${STAROSC_QT_PLUGINS}/${dll}")  # a Qt plugin, in its folder
    else()
        set(origin "")
        foreach(dll_dir IN LISTS STAROSC_DLL_DIRS)
            if(NOT origin AND EXISTS "${dll_dir}/${dll}")
                set(origin "${dll_dir}/${dll}")
            endif()
        endforeach()
    endif()
    execute_process(COMMAND "${STAROSC_PACMAN}" -Qqo "${origin}" OUTPUT_VARIABLE package
                    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT package)
        message(FATAL_ERROR "no MSYS2 package owns ${dll} (${origin}): its licence is unknown")
    endif()
    list(APPEND packages "${package}")
endforeach()
list(REMOVE_DUPLICATES packages)
list(SORT packages)

file(REMOVE_RECURSE "${dir}/licenses")
set(listing "Libraries shipped with Star Oscilloscope, as MSYS2 packages (https://www.msys2.org).\n")
string(APPEND listing "Each folder here holds the licence of one package. The sources of these exact versions are at\n")
string(APPEND listing "https://repo.msys2.org/mingw/sources/ and the build recipes at https://github.com/msys2/MINGW-packages.\n\n")
foreach(package IN LISTS packages)
    execute_process(COMMAND "${STAROSC_PACMAN}" -Q "${package}" OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(APPEND listing "${version}\n")
    string(REGEX REPLACE "^mingw-w64-[a-z0-9]+-x86_64-" "" short "${package}")
    if(IS_DIRECTORY "${STAROSC_LICENSES}/${short}")
        file(COPY "${STAROSC_LICENSES}/${short}/" DESTINATION "${dir}/licenses/${short}")
    else()
        # A few packages keep their licence elsewhere (ICU: share/icu/<version>/LICENSE).
        execute_process(COMMAND "${STAROSC_PACMAN}" -Qlq "${package}" OUTPUT_VARIABLE files)
        string(REPLACE "\n" ";" files "${files}")
        list(FILTER files INCLUDE REGEX "/(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE)[^/]*$")
        if(NOT files)
            message(FATAL_ERROR "no licence of ${package} found")
        endif()
        cmake_path(GET STAROSC_LICENSES PARENT_PATH share)
        cmake_path(GET share PARENT_PATH root)  # the MSYS2 prefix, /ucrt64 in pacman's paths
        cmake_path(GET root PARENT_PATH msys)
        foreach(file IN LISTS files)
            file(COPY "${msys}${file}" DESTINATION "${dir}/licenses/${short}")
        endforeach()
    endif()
endforeach()
file(WRITE "${dir}/licenses/PACKAGES.txt" "${listing}")
list(LENGTH packages count)
message(STATUS "Licences of ${count} packages in licenses/")
