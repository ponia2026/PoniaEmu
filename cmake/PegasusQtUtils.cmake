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
    # QmlImportScanner is only needed for static linking (Android uses a dynamic Qt, and the Android kit does not contain it)
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
