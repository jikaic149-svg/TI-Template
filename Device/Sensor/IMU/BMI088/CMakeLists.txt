add_library(device_bmi088 STATIC
    "device_bmi088.c"
    "device_bmi088_spi.c")

target_include_directories(device_bmi088 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(device_bmi088 PUBLIC
    bsp
    device_imu
    mspm0_build_options
    m)
