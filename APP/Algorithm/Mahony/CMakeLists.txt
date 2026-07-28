add_library(algorithm_mahony STATIC
    "src/algorithm_mahony.c")

target_include_directories(algorithm_mahony PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(algorithm_mahony PRIVATE
    "${CMAKE_SOURCE_DIR}/config")

target_link_libraries(algorithm_mahony PUBLIC
    mspm0_build_options
    m)
