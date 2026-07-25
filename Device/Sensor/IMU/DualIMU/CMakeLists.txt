add_library(device_dual_imu STATIC
    "device_dual_imu.c")

target_include_directories(device_dual_imu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(device_dual_imu PUBLIC
    device_bmi088
    device_icm45686
    bsp
    mspm0_build_options)
