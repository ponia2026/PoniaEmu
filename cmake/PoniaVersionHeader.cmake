# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# 以 `cmake -P` 执行：查 git、取编译日期，写出 ponia_version.h（内容没变就不覆盖）。
# 输入：PONIA_SRC_DIR、PONIA_OUT_FILE、PONIA_PRODUCT_NAME、PONIA_VERSION

execute_process(COMMAND git --git-dir "${PONIA_SRC_DIR}/.git" --work-tree "${PONIA_SRC_DIR}"
    rev-parse --short=8 HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE PONIA_GIT_SHORT ERROR_QUIET)
execute_process(COMMAND git --git-dir "${PONIA_SRC_DIR}/.git" --work-tree "${PONIA_SRC_DIR}"
    status --porcelain --untracked-files=no
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE PONIA_GIT_STATUS ERROR_QUIET)
execute_process(COMMAND git --git-dir "${PONIA_SRC_DIR}/.git" --work-tree "${PONIA_SRC_DIR}"
    rev-list --count HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE PONIA_GIT_COUNT ERROR_QUIET)

if(NOT PONIA_GIT_SHORT)
    set(PONIA_GIT_SHORT "unknown")
endif()
if(NOT PONIA_GIT_COUNT)
    set(PONIA_GIT_COUNT 0)
endif()
if(PONIA_GIT_STATUS)
    set(PONIA_GIT_DIRTY "-dirty")
else()
    set(PONIA_GIT_DIRTY "")
endif()

# 编译日期（本机时间）。日期同一天内不变；另给一个到秒的戳，用来让「附带主题」在每次新编译后重新解包
string(TIMESTAMP PONIA_BUILD_DATE "%Y%m%d")
string(TIMESTAMP PONIA_BUILD_STAMP "%Y%m%d%H%M%S")

# 目前 header 里的内容：日期＋提交号＋dirty 决定「显示用」的值；到秒的戳另放，只给主题解包标记用
set(_content "// 自动产生（cmake/PoniaVersionHeader.cmake），请勿手动修改\n")
string(APPEND _content "#pragma once\n")
string(APPEND _content "#define PONIA_PRODUCT_NAME \"${PONIA_PRODUCT_NAME}\"\n")
string(APPEND _content "#define PONIA_VERSION \"${PONIA_VERSION}\"\n")
string(APPEND _content "#define PONIA_BUILD_DATE \"${PONIA_BUILD_DATE}\"\n")
string(APPEND _content "#define PONIA_GIT_SHORT \"${PONIA_GIT_SHORT}${PONIA_GIT_DIRTY}\"\n")
string(APPEND _content "#define PONIA_GIT_COUNT ${PONIA_GIT_COUNT}\n")
string(APPEND _content "// 附带主题的解包标记：版本＋提交号＋编译戳，每次新编译都不同\n")
string(APPEND _content "#define PONIA_ASSET_STAMP \"${PONIA_VERSION}+${PONIA_GIT_SHORT}${PONIA_GIT_DIRTY}.${PONIA_BUILD_STAMP}\"\n")
# 旧名称（theme/QML 可能还在用 Internal.meta.gitRevision / gitDate）
string(APPEND _content "#define GIT_REVISION \"${PONIA_VERSION}+${PONIA_GIT_SHORT}${PONIA_GIT_DIRTY}\"\n")
string(APPEND _content "#define GIT_DATE \"${PONIA_BUILD_DATE}\"\n")

set(_old "")
if(EXISTS "${PONIA_OUT_FILE}")
    file(READ "${PONIA_OUT_FILE}" _old)
endif()
if(NOT _old STREQUAL _content)
    file(WRITE "${PONIA_OUT_FILE}" "${_content}")
    message(STATUS "ponia_version.h: ${PONIA_PRODUCT_NAME} v${PONIA_VERSION} ${PONIA_BUILD_DATE} ${PONIA_GIT_SHORT}${PONIA_GIT_DIRTY}")
endif()
