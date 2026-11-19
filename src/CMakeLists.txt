add_subdirectory(backend)
add_subdirectory(frontend)
add_subdirectory(app)

# Standalone Libretro host (replaces RetroArch). Desktop Linux only for now; Android needs bundled cores.
option(PEGASUS_BUILD_LIBRETRO_HOST "Build poniaplay, the built-in Libretro host" ON)
if(PEGASUS_BUILD_LIBRETRO_HOST)
    add_subdirectory(libretro-host)
endif()
