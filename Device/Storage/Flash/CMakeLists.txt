add_library(device_flash STATIC
    "device_flash.c")

target_include_directories(device_flash PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(device_flash
    PRIVATE bsp
    PUBLIC mspm0_build_options)
