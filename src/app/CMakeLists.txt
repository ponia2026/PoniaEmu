include(PegasusQtUtils)
pegasus_require_qt(COMPONENTS
    Core
    Qml
    QmlImportScanner
    LinguistTools
)


# Find TS files, compile them to QM and generate a QRC file collecting them

# 只收录 Ponia 指定的语言（英文是源码语言，Locales 会自动补上 en）：zh、zh-TW、es、pt-BR、ru、ja、fr、de。
# 其余天马原有的 .ts 留在 lang/ 但不编进去（主题与新字串没有那些翻译，列在菜单里只会混着英文）。
set(PONIA_LOCALES zh zh-TW es pt-BR ru ja fr de)
set(PEGASUS_TS_FILES)
foreach(LOC IN LISTS PONIA_LOCALES)
    list(APPEND PEGASUS_TS_FILES "${PROJECT_SOURCE_DIR}/lang/pegasus_${LOC}.ts")
endforeach()
qt_add_translation(PEGASUS_QM_FILES "${PEGASUS_TS_FILES}")

set(PEGASUS_LOCALE_QRC "${CMAKE_CURRENT_BINARY_DIR}/locales.qrc")
file(WRITE "${PEGASUS_LOCALE_QRC}" "<!DOCTYPE RCC><RCC version=\"1.0\"><qresource prefix=\"i18n\">\n")

foreach(QM_FILE IN LISTS PEGASUS_QM_FILES)
    get_filename_component(QM_FILENAME "${QM_FILE}" NAME)
    file(APPEND "${PEGASUS_LOCALE_QRC}" "<file>${QM_FILENAME}</file>\n")
endforeach()

file(APPEND "${PEGASUS_LOCALE_QRC}" "</qresource></RCC>")
qt_add_resources(PEGASUS_LOCALE_SRC "${PEGASUS_LOCALE_QRC}")

add_library(pegasus-locales "${PEGASUS_LOCALE_SRC}")


# Main runtime

qt_add_executable(poniafe MANUAL_FINALIZATION WIN32 main.cpp)
target_include_directories(poniafe PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(poniafe PUBLIC
    Qt::Core
    pegasus-assets
    pegasus-backend
    pegasus-locales
    pegasus-qml
)
if(COMMAND qt_import_qml_plugins)
    qt_import_qml_plugins(poniafe)
endif()

include(PegasusCommonProps)
pegasus_add_common_props_optimized(poniafe)

# TODO: Workaround for asset differences for now
target_compile_definitions(poniafe PRIVATE PEGASUS_USING_CMAKE)

if(PEGASUS_ENABLE_APNG)
    target_compile_definitions(poniafe PUBLIC WITH_APNG_SUPPORT)
endif()

if(ANDROID)
    # 生成 APK 的 package 目录：Manifest/Java/资源 + 附带主题（放进 assets/themes，首次启动解包）
    set(PEGASUS_ANDROID_PKG "${CMAKE_CURRENT_BINARY_DIR}/android-package")
    file(REMOVE_RECURSE "${PEGASUS_ANDROID_PKG}")
    # 内建核心放在 libs/<abi>/：只带上这次编译的 ABI（32 位包不要夹带 64 位核心，反之亦然）
    set(PEGASUS_OTHER_ABI_EXCLUDES)
    foreach(_abi arm64-v8a armeabi-v7a x86 x86_64)
        if(NOT _abi STREQUAL "${ANDROID_ABI}")
            list(APPEND PEGASUS_OTHER_ABI_EXCLUDES PATTERN "${_abi}" EXCLUDE)
        endif()
    endforeach()
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/" DESTINATION "${PEGASUS_ANDROID_PKG}" ${PEGASUS_OTHER_ABI_EXCLUDES})
    file(COPY "${PROJECT_SOURCE_DIR}/themes-extra/macaron" DESTINATION "${PEGASUS_ANDROID_PKG}/assets/themes")
    file(GLOB_RECURSE PEGASUS_ANDROID_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PEGASUS_ANDROID_SRC_FILES})
endif()

if(ANDROID)
    # Qt 6 的 androiddeployqt 自带 Gradle 模板；这里只提供 Manifest、Java 源码与资源
    set_target_properties(poniafe PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${PEGASUS_ANDROID_PKG}"
        QT_ANDROID_VERSION_NAME "${PONIAFE_VERSION}"
        QT_ANDROID_VERSION_CODE ${PONIA_ANDROID_VERSION_CODE}
        QT_ANDROID_MIN_SDK_VERSION 23
        QT_ANDROID_TARGET_SDK_VERSION 33
    )
    # 让 androiddeployqt 扫描前端 QML 与附带主题用到的 Qt QML 模块，打进 APK
    # 内置 Libretro 播放器的原生库（PlayerActivity 载入）
    if(PEGASUS_BUILD_LIBRETRO_HOST)
        add_dependencies(poniafe ponia_player)
        set_property(TARGET poniafe APPEND PROPERTY QT_ANDROID_EXTRA_LIBS "${CMAKE_BINARY_DIR}/src/libretro-host/libponia_player.so")
    endif()
    set_property(TARGET poniafe APPEND PROPERTY QT_QML_ROOT_PATH
        "${PROJECT_SOURCE_DIR}/src/frontend"
        "${PROJECT_SOURCE_DIR}/src/themes"
        "${PROJECT_SOURCE_DIR}/themes-extra")
endif()

qt_finalize_executable(poniafe)


# Install

include(PegasusTargetPlatform)
include(GNUInstallDirs)

macro(pegasus_set_installdir var gnu_default)
    set(${var} "${PEGASUS_${var}}")
    if(NOT ${var})
        set(${var} "${PEGASUS_INSTALLDIR}")
    endif()
    if(NOT ${var})
        set(${var} "${gnu_default}")
    endif()
endmacro()

pegasus_set_installdir(INSTALL_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
pegasus_set_installdir(INSTALL_DOCDIR "${CMAKE_INSTALL_FULL_DOCDIR}")
pegasus_set_installdir(INSTALL_ICONDIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/icons/hicolor")
pegasus_set_installdir(INSTALL_DESKTOPDIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/applications")
pegasus_set_installdir(INSTALL_APPSTREAMDIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/metainfo")
pegasus_set_installdir(INSTALL_THEMESDIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/poniafe/themes")

if(INSTALL_BINDIR)
    install(TARGETS poniafe RUNTIME DESTINATION "${INSTALL_BINDIR}" LIBRARY DESTINATION "${INSTALL_BINDIR}")
endif()

if(INSTALL_DOCDIR)
    install(FILES
        "${PROJECT_SOURCE_DIR}/README.md"
        "${PROJECT_SOURCE_DIR}/LICENSE.md"
        DESTINATION "${INSTALL_DOCDIR}"
    )
endif()

if(INSTALL_THEMESDIR AND NOT PEGASUS_INSTALLDIR_NO_THEMES)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/themes-extra/macaron" DESTINATION "${INSTALL_THEMESDIR}")
endif()

if(PEGASUS_ON_X11)
    if(INSTALL_ICONDIR)
        install(FILES platform/linux/icons/16/org.ponia.PoniaFE.png DESTINATION "${INSTALL_ICONDIR}/16x16/apps/")
        install(FILES platform/linux/icons/32/org.ponia.PoniaFE.png DESTINATION "${INSTALL_ICONDIR}/32x32/apps/")
        install(FILES platform/linux/icons/48/org.ponia.PoniaFE.png DESTINATION "${INSTALL_ICONDIR}/48x48/apps/")
        install(FILES platform/linux/icons/64/org.ponia.PoniaFE.png DESTINATION "${INSTALL_ICONDIR}/64x64/apps/")
        install(FILES platform/linux/icons/128/org.ponia.PoniaFE.png DESTINATION "${INSTALL_ICONDIR}/128x128/apps/")
    endif()
    if(INSTALL_DESKTOPDIR AND INSTALL_BINDIR)
        configure_file(
            platform/linux/org.ponia.PoniaFE.desktop.in
            org.ponia.PoniaFE.desktop
        )
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.ponia.PoniaFE.desktop" DESTINATION "${INSTALL_DESKTOPDIR}")
    endif()
    if(INSTALL_APPSTREAMDIR)
        install(FILES platform/linux/org.ponia.PoniaFE.metainfo.xml DESTINATION "${INSTALL_APPSTREAMDIR}")
    endif()
endif()


# Print config

include(PegasusPrettyPrint)
pegasus_print_box(
    TITLE "App Settings"
    CONTENT
        "Build type: ${CMAKE_BUILD_TYPE}"
        "Product: ${PONIA_PRODUCT_NAME} v${PONIA_VERSION} (version header is generated at build time)"
        "Link to static stdc++: ${PEGASUS_STATIC_CXX}"
        "Allow link-time optimizations: ${PEGASUS_ENABLE_LTO}"
        "Deployment paths:"
        "  Binaries:          ${INSTALL_BINDIR}"
        "  License/Readme:    ${INSTALL_DOCDIR}"
        "  X11 icons:         ${INSTALL_ICONDIR}"
        "  X11 desktop file:  ${INSTALL_DESKTOPDIR}"
        "  X11 AppStream:     ${INSTALL_APPSTREAMDIR}"
)
