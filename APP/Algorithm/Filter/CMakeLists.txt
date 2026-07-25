add_library(algorithm_filter STATIC
    "src/algorithm_filter_biquad.c"
    "src/algorithm_filter_kalman.c"
    "src/algorithm_filter_low_pass.c"
    "src/algorithm_filter_median.c"
    "src/algorithm_filter_moving_average.c")

target_include_directories(algorithm_filter PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(algorithm_filter PUBLIC
    mspm0_build_options
    m)
