# Built-in Libretro host (replaces RetroArch): ponia_host lib; Linux exe `poniaplay`; Android libponia_player.so
find_package(SDL2 REQUIRED)
enable_language(C)

# Archive support: zip uses zlib, 7z uses the bundled LZMA SDK decoder (third_party/lzma, public domain, see the README there)
set(PONIA_LZMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzma)
add_library(ponia_lzma STATIC
    ${PONIA_LZMA_DIR}/7zArcIn.c ${PONIA_LZMA_DIR}/7zBuf.c ${PONIA_LZMA_DIR}/7zCrc.c ${PONIA_LZMA_DIR}/7zCrcOpt.c
    ${PONIA_LZMA_DIR}/7zDec.c ${PONIA_LZMA_DIR}/7zFile.c ${PONIA_LZMA_DIR}/7zStream.c ${PONIA_LZMA_DIR}/Bcj2.c
    ${PONIA_LZMA_DIR}/Bra.c ${PONIA_LZMA_DIR}/Bra86.c ${PONIA_LZMA_DIR}/BraIA64.c ${PONIA_LZMA_DIR}/CpuArch.c
    ${PONIA_LZMA_DIR}/Delta.c ${PONIA_LZMA_DIR}/Lzma2Dec.c ${PONIA_LZMA_DIR}/LzmaDec.c)
target_include_directories(ponia_lzma PUBLIC ${PONIA_LZMA_DIR})
target_compile_definitions(ponia_lzma PRIVATE _7ZIP_ST)
set_target_properties(ponia_lzma PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Shared library (used by both the Linux executable and the Android PlayerActivity)
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
    # SDLActivity (PlayerActivity) loads libponia_player.so and calls SDL_main
    add_library(ponia_player SHARED player_main.cpp)
    target_link_libraries(ponia_player PRIVATE ponia_host)

    # Self-test core (not packed into the APK; adb push it to files/cores or give it with --core)
    enable_language(C)
    add_library(testcore_libretro MODULE testcore.c)
    set_target_properties(testcore_libretro PROPERTIES PREFIX "")
else()
    enable_language(C)   # testcore.c (the top-level project only enables CXX)
    add_executable(poniaplay main.cpp)
    target_link_libraries(poniaplay PRIVATE ponia_host)

    # Tiny self-test core (color bars + tone); no ROM needed. Used to verify the host without real cores.
    add_library(testcore_libretro MODULE testcore.c)
    set_target_properties(testcore_libretro PROPERTIES PREFIX "")

    # Put it in the same directory as poniaemu: ProcessLauncher resolves the bare command `poniaplay` to the copy next to the main program
    set_target_properties(poniaplay PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/app")
    install(TARGETS poniaplay RUNTIME DESTINATION bin)
endif()
