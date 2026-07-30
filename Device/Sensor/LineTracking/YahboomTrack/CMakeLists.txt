add_library(device_yahboom_track STATIC
    "device_yahboom_track.c")

target_include_directories(device_yahboom_track PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(device_yahboom_track PUBLIC
    mspm0_build_options)
