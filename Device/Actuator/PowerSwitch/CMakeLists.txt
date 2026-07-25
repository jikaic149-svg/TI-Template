add_library(device_power_switch STATIC
    "device_power_switch.c")

target_include_directories(device_power_switch PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(device_power_switch PUBLIC
    bsp
    mspm0_build_options)
