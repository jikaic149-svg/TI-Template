add_library(device_lingke_m0 STATIC
    "device_lingke_m0.c")

target_include_directories(device_lingke_m0 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(device_lingke_m0 PUBLIC
    mspm0_build_options)
