if(NOT TARGET algorithm_mahony)
    message(FATAL_ERROR "Device/Sensor/IMU requires algorithm_mahony")
endif()

add_library(device_imu STATIC
    "device_imu.c")

target_include_directories(device_imu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(device_imu PUBLIC
    algorithm_mahony
    mspm0_build_options)
