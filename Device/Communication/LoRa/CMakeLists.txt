add_library(device_lora STATIC
    "device_lora.c")

target_include_directories(device_lora PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(device_lora PUBLIC
    device_serial
    mspm0_build_options)
