add_library(device_xinliu_t8 STATIC
    "device_xinliu_t8.c")

target_include_directories(device_xinliu_t8 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(device_xinliu_t8 PUBLIC
    mspm0_build_options)
