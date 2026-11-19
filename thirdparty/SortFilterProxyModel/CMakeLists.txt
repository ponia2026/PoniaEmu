# PoniaFE: Qt 6 only. Don't use `find_package(QT NAMES Qt6 Qt5)`, the names
# aren't tried in order, so it may pick a Qt 5 installed next to Qt 6
# QRegExp lives in Core5Compat in Qt 6
find_package(Qt6 REQUIRED COMPONENTS Qml Core5Compat)
set(QT_VERSION_MAJOR 6)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_library(SortFilterProxyModel
    qqmlsortfilterproxymodel.cpp
    filters/filter.cpp
    filters/filtercontainer.cpp
    filters/rolefilter.cpp
    filters/valuefilter.cpp
    filters/indexfilter.cpp
    filters/regexpfilter.cpp
    filters/rangefilter.cpp
    filters/expressionfilter.cpp
    filters/filtercontainerfilter.cpp
    filters/anyoffilter.cpp
    filters/alloffilter.cpp
    filters/filtersqmltypes.cpp
    sorters/sorter.cpp
    sorters/sortercontainer.cpp
    sorters/rolesorter.cpp
    sorters/stringsorter.cpp
    sorters/expressionsorter.cpp
    sorters/sortersqmltypes.cpp
    proxyroles/proxyrole.cpp
    proxyroles/proxyrolecontainer.cpp
    proxyroles/joinrole.cpp
    proxyroles/switchrole.cpp
    proxyroles/expressionrole.cpp
    proxyroles/proxyrolesqmltypes.cpp
    proxyroles/singlerole.cpp
    proxyroles/regexprole.cpp
    sorters/filtersorter.cpp
    proxyroles/filterrole.cpp
)

target_link_libraries(SortFilterProxyModel PUBLIC Qt${QT_VERSION_MAJOR}::Qml)
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    target_link_libraries(SortFilterProxyModel PUBLIC Qt6::Core5Compat)
endif()
target_include_directories(SortFilterProxyModel PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(SortFilterProxyModel PUBLIC cxx_std_17)
set_target_properties(SortFilterProxyModel PROPERTIES CXX_EXTENSIONS OFF)
