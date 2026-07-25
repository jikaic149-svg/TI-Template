add_library(algorithm_pid STATIC
    "src/algorithm_pid.c")

target_include_directories(algorithm_pid PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(algorithm_pid PUBLIC
    mspm0_build_options
    m)
