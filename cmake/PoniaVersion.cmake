# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# The product name and the version are written here, and only here:
#   - the About page of the frontend, the log header, the Android versionName and the APK file name all use them
#   - the product name is not translated (every language shows PoniaEmu)
# The commit, the build date and the dirty flag are fetched again on every build by PoniaVersionHeader.cmake (see below).

set(PONIA_PRODUCT_NAME "PoniaEmu")
set(PONIA_VERSION "1.0.0")

# Old variable name (still used by src/app/CMakeLists.txt)
set(PONIAFE_VERSION "${PONIA_VERSION}")

# Regenerate ponia_version.h on every build, not only when CMake configures:
# (it is only overwritten when the date, the commit or the build stamp changed, so only the few files that include it are rebuilt.)
set(PONIA_VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/generated")
set(PONIA_VERSION_HEADER "${PONIA_VERSION_HEADER_DIR}/ponia_version.h")
file(MAKE_DIRECTORY "${PONIA_VERSION_HEADER_DIR}")
add_custom_target(ponia_version ALL
    COMMAND ${CMAKE_COMMAND}
        -DPONIA_SRC_DIR=${PROJECT_SOURCE_DIR}
        -DPONIA_OUT_FILE=${PONIA_VERSION_HEADER}
        -DPONIA_PRODUCT_NAME=${PONIA_PRODUCT_NAME}
        -DPONIA_VERSION=${PONIA_VERSION}
        -P ${PROJECT_SOURCE_DIR}/cmake/PoniaVersionHeader.cmake
    BYPRODUCTS "${PONIA_VERSION_HEADER}"
    COMMENT "Generating ponia_version.h"
    VERBATIM
)
# A copy is also needed at the first configure, so that the checks of the IDE and of the configure step pass
execute_process(COMMAND ${CMAKE_COMMAND}
    -DPONIA_SRC_DIR=${PROJECT_SOURCE_DIR}
    -DPONIA_OUT_FILE=${PONIA_VERSION_HEADER}
    -DPONIA_PRODUCT_NAME=${PONIA_PRODUCT_NAME}
    -DPONIA_VERSION=${PONIA_VERSION}
    -P ${PROJECT_SOURCE_DIR}/cmake/PoniaVersionHeader.cmake)

# Android versionCode: 10000 + number of commits (taken at configure time; larger than the old fixed 10000, so installing over an old build is not treated as a downgrade)
execute_process(COMMAND git --git-dir "${PROJECT_SOURCE_DIR}/.git" --work-tree "${PROJECT_SOURCE_DIR}" rev-list --count HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE _PONIA_COMMITS ERROR_QUIET)
if(NOT _PONIA_COMMITS)
    set(_PONIA_COMMITS 0)
endif()
math(EXPR PONIA_ANDROID_VERSION_CODE "10000 + ${_PONIA_COMMITS}")
