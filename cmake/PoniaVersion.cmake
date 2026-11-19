# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# 产品名与版本号只在这里写一次：
#   - 前端「关于」页、日志开头、Android versionName、APK 文件名都引用它
#   - 产品名不翻译（所有语言都显示 PoniaEmu）
# 提交号、编译日期、是否 dirty 每次编译时由 PoniaVersionHeader.cmake 重新取得（见下）。

set(PONIA_PRODUCT_NAME "PoniaEmu")
set(PONIA_VERSION "1.0.0")

# 旧变量名（src/app/CMakeLists.txt 仍在用）
set(PONIAFE_VERSION "${PONIA_VERSION}")

# 每次编译（不只是 cmake 设定时）都重新产生 ponia_version.h：
# （日期、提交号或编译戳变了才会覆盖；只有包含它的少数几个文件会跟着重编。）
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
# 第一次设定时也要有一份，才能让 IDE/配置阶段的检查通过
execute_process(COMMAND ${CMAKE_COMMAND}
    -DPONIA_SRC_DIR=${PROJECT_SOURCE_DIR}
    -DPONIA_OUT_FILE=${PONIA_VERSION_HEADER}
    -DPONIA_PRODUCT_NAME=${PONIA_PRODUCT_NAME}
    -DPONIA_VERSION=${PONIA_VERSION}
    -P ${PROJECT_SOURCE_DIR}/cmake/PoniaVersionHeader.cmake)

# Android versionCode：10000 + 提交总数（设定时取值；比旧的固定值 10000 大，覆盖安装不会被判降级）
execute_process(COMMAND git --git-dir "${PROJECT_SOURCE_DIR}/.git" --work-tree "${PROJECT_SOURCE_DIR}" rev-list --count HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE _PONIA_COMMITS ERROR_QUIET)
if(NOT _PONIA_COMMITS)
    set(_PONIA_COMMITS 0)
endif()
math(EXPR PONIA_ANDROID_VERSION_CODE "10000 + ${_PONIA_COMMITS}")
