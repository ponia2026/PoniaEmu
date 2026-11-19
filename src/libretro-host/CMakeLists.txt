# Built-in Libretro host (replaces RetroArch): ponia_host lib; Linux exe `poniaplay`; Android libponia_player.so
find_package(SDL2 REQUIRED)
enable_language(C)

# 压缩档支持：zip 用 zlib，7z 用随附的 LZMA SDK 解码端（third_party/lzma，公有领域，见该目录 README）
set(PONIA_LZMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzma)
add_library(ponia_lzma STATIC
    ${PONIA_LZMA_DIR}/7zArcIn.c ${PONIA_LZMA_DIR}/7zBuf.c ${PONIA_LZMA_DIR}/7zCrc.c ${PONIA_LZMA_DIR}/7zCrcOpt.c
    ${PONIA_LZMA_DIR}/7zDec.c ${PONIA_LZMA_DIR}/7zFile.c ${PONIA_LZMA_DIR}/7zStream.c ${PONIA_LZMA_DIR}/Bcj2.c
    ${PONIA_LZMA_DIR}/Bra.c ${PONIA_LZMA_DIR}/Bra86.c ${PONIA_LZMA_DIR}/BraIA64.c ${PONIA_LZMA_DIR}/CpuArch.c
    ${PONIA_LZMA_DIR}/Delta.c ${PONIA_LZMA_DIR}/Lzma2Dec.c ${PONIA_LZMA_DIR}/LzmaDec.c)
target_include_directories(ponia_lzma PUBLIC ${PONIA_LZMA_DIR})
target_compile_definitions(ponia_lzma PRIVATE _7ZIP_ST)
set_target_properties(ponia_lzma PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 共用函式库（Linux 执行档与安卓 PlayerActivity 都用它）
add_library(ponia_host STATIC ponia_play.cpp archive.cpp)
target_include_directories(ponia_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})
target_link_libraries(ponia_host PUBLIC ${SDL2_LIBRARIES} ${CMAKE_DL_LIBS} ponia_lzma)
set_target_properties(ponia_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(ANDROID)
    target_link_libraries(ponia_host PUBLIC log z)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(ponia_host PUBLIC ZLIB::ZLIB)
endif()

if(ANDROID)
    # SDLActivity（PlayerActivity）加载 libponia_player.so 并呼叫 SDL_main
    add_library(ponia_player SHARED player_main.cpp)
    target_link_libraries(ponia_player PRIVATE ponia_host)

    # 自测核心（不打进 APK；adb push 到 files/cores 或用 --core 指定）
    enable_language(C)
    add_library(testcore_libretro MODULE testcore.c)
    set_target_properties(testcore_libretro PROPERTIES PREFIX "")
else()
    enable_language(C)   # testcore.c（顶层项目只启用了 CXX）
    add_executable(poniaplay main.cpp)
    target_link_libraries(poniaplay PRIVATE ponia_host)

    # Tiny self-test core (color bars + tone); no ROM needed. Used to verify the host without real cores.
    add_library(testcore_libretro MODULE testcore.c)
    set_target_properties(testcore_libretro PROPERTIES PREFIX "")

    # 与 poniafe 放同一目录：ProcessLauncher 把裸命令 `poniaplay` 解析成主程序旁边那一份
    set_target_properties(poniaplay PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/app")
    install(TARGETS poniaplay RUNTIME DESTINATION bin)
endif()
