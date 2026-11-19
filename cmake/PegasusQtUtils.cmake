# NOTE: This is a macro because `find_package` variables would go
# out of scope at the end of functions
macro(pegasus_require_qt)
    cmake_parse_arguments(QTREQ
        ""
        ""
        COMPONENTS
        ${ARGN})

    # Qt 6 compiles QML through qmlcachegen, there is no separate QuickCompiler package
    list(REMOVE_ITEM QTREQ_COMPONENTS QuickCompiler)
    # QmlImportScanner 只在静态链接时需要（Android 用动态 Qt，安卓套件里也没有它）
    if(ANDROID)
        list(REMOVE_ITEM QTREQ_COMPONENTS QmlImportScanner)
    endif()
    find_package(Qt6 6.4 COMPONENTS ${QTREQ_COMPONENTS} REQUIRED)
endmacro()


# Qt 6 has no QuickCompiler package, keep the old call sites working
# NOTE: Must be defined outside of the macro above, otherwise `ARGN`
# would be substituted with the macro's arguments
function(qtquick_compiler_add_resources outfiles)
    qt_add_resources(resource_files ${ARGN})
    set(${outfiles} "${resource_files}" PARENT_SCOPE)
endfunction()
