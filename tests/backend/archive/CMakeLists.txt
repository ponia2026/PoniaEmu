pegasus_cxx_test(test_Archive)
target_include_directories(test_Archive PRIVATE "${PROJECT_SOURCE_DIR}/src/libretro-host")
target_link_libraries(test_Archive PRIVATE ponia_host)
