# Standalone Libretro host (replaces RetroArch). Declared before the subdirectories below:
# src/app reads it to bundle libponia_player.so into the Android APK, so it must already exist
# on a fresh configure (a stale CMakeCache used to hide this).
option(PEGASUS_BUILD_LIBRETRO_HOST "Build poniaplay, the built-in Libretro host" ON)

add_subdirectory(backend)
add_subdirectory(frontend)
add_subdirectory(app)

if(PEGASUS_BUILD_LIBRETRO_HOST)
    add_subdirectory(libretro-host)
endif()
