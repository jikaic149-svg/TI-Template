add_library(device_watchdog STATIC
    "device_watchdog.c")

target_include_directories(device_watchdog PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(device_watchdog PUBLIC
    bsp
    mspm0_build_options)
