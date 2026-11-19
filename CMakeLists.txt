cmake_minimum_required(VERSION 3.16)

project(poniafe
    DESCRIPTION "Cross platform, customizable graphical frontend for launching games"
    HOMEPAGE_URL "https://ponia-frontend.org/"
    LANGUAGES CXX
)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/cmake")


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_SKIP_RPATH ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

cmake_policy(SET CMP0071 NEW)  # Enable processing Qt generated files


option(PEGASUS_USE_SDL2_GAMEPAD "Use SDL2 for gamepad support" ON)
# 安卓上 SDL 的电量查询依赖 SDLActivity 初始化，这里改用自带的 Java 实现（BatteryInfo）
if(ANDROID)
    set(PEGASUS_SDL2_POWER_DEFAULT OFF)
else()
    set(PEGASUS_SDL2_POWER_DEFAULT ON)
endif()
option(PEGASUS_USE_SDL2_POWER "Use SDL2 for battery info support" ${PEGASUS_SDL2_POWER_DEFAULT})
option(PEGASUS_ENABLE_APNG "Enable animated PNG support" OFF)
option(PEGASUS_STATIC_CXX "Link stdc++ statically" OFF)
option(PEGASUS_ENABLE_LTO "Allow link-time optimizations (slow to build)" ON)


include(PoniaVersion)
include(PegasusGitInfo)

# Find every Qt module used by the subprojects here, so the imported targets
# are visible everywhere (Qt 6 resolves the dependencies of linked libraries
# in the directory of the final executable)
include(PegasusQtUtils)
pegasus_require_qt(COMPONENTS
    Core
    Qml
    Quick
    QuickCompiler
    Multimedia
    Network
    Sql
    Svg
    QmlImportScanner
    LinguistTools
)


if(PEGASUS_ENABLE_APNG)
    add_subdirectory(thirdparty/apng)
endif()
add_subdirectory(thirdparty/SortFilterProxyModel)
add_subdirectory(assets)
add_subdirectory(src)


if(NOT ANDROID)
    enable_testing(true)
    add_subdirectory(tests)
endif()
