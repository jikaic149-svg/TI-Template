add_library(app_system_watchdog STATIC
    "app_watchdog.c")

target_include_directories(app_system_watchdog PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(app_system_watchdog PUBLIC
    mspm0_build_options)
