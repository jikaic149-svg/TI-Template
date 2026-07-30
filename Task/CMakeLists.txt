add_library(task_app STATIC
    "task_app.c")

target_include_directories(task_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(task_app PRIVATE
    "${CMAKE_SOURCE_DIR}/config")

target_link_libraries(task_app PUBLIC
    freertos_kernel
    mspm0_build_options)
target_link_libraries(task_app PRIVATE
    app_system_watchdog
    device_watchdog)

if(TEMPLATE_ENABLE_TASK_BRINGUP_CHASSIS)
    target_sources(task_app PRIVATE "task_bringup_chassis.c")
    target_compile_definitions(task_app PUBLIC
        TEMPLATE_ENABLE_TASK_BRINGUP_CHASSIS=1)
    target_link_libraries(task_app PRIVATE
        device_encoder
        device_icm45686
        device_jy61p
        device_key
        device_key_input
        device_line_tracking
        device_motor
        device_peer_link
        device_rgb
        device_serial
        device_st7735s
        device_yahboom_track
        mode_car
        mode_communicate)
endif()
