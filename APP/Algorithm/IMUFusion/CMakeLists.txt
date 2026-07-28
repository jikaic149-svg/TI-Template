add_library(algorithm_imu_fusion STATIC
    "src/algorithm_imu_fusion.c")

target_include_directories(algorithm_imu_fusion PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(algorithm_imu_fusion PRIVATE
    "${CMAKE_SOURCE_DIR}/config")

target_link_libraries(algorithm_imu_fusion PUBLIC
    algorithm_mahony
    mspm0_build_options
    m)
