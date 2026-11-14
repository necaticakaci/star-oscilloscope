# SPDX-License-Identifier: GPL-3.0-or-later
# Runs before CPack packs the staged files (packaging/CMakeLists.txt). A package takes the files' modes from the
# staging folder, and a Windows drive under WSL (/mnt/c, /mnt/d) shows every file as writable by everyone: such a
# package would install files anyone could change. Stage it on a Linux file system instead.
execute_process(
    COMMAND find "${CPACK_TEMPORARY_INSTALL_DIRECTORY}" -perm -o+w -not -type l
    OUTPUT_VARIABLE writable OUTPUT_STRIP_TRAILING_WHITESPACE)
if(writable)
    message(FATAL_ERROR "The staged files are writable by everyone (a Windows drive under WSL?). Give CPack a folder "
                        "on a Linux file system, for example: cpack --config <build>/CPackConfig.cmake -B ~/packages")
endif()
