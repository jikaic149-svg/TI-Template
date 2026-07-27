set(TASK_APP_SOURCES
    "task_app.c")

set(TASK_BUILD_LINE_TRACKING_READOUT OFF)
set(TASK_BUILD_ANALOG_TRACKING OFF)

if(TEMPLATE_ENABLE_TASK_ANALOG_TRACKING AND
        TEMPLATE_ENABLE_TASK_LINE_TRACKING_READOUT)
    message(FATAL_ERROR
        "Select only one line-tracking product Task profile")
endif()

if(TEMPLATE_ENABLE_TASK_ANALOG_TRACKING)
    if(NOT MSPM0_MOTOR_DRIVER_NORMALIZED STREQUAL "TB6612" OR
            NOT TARGET mode_car OR NOT TARGET mode_communicate OR
            NOT TARGET device_icm45686 OR
            NOT TARGET device_encoder OR NOT TARGET device_key_input OR
            NOT TARGET device_line_tracking OR
            NOT TARGET device_xinliu_t8 OR
            NOT TARGET device_motor OR NOT TARGET device_rgb OR
            NOT TARGET device_serial)
        message(FATAL_ERROR
            "Analog-tracking Task requires TB6612, Car/PC-communication Modes, ICM45686, encoder, key, XinliuT8, motor, RGB and serial Devices")
    endif()
    set(TASK_BUILD_ANALOG_TRACKING ON)
    list(APPEND TASK_APP_SOURCES
        "LineTracking/task_analog_tracking.c")
elseif(TEMPLATE_ENABLE_TASK_LINE_TRACKING_READOUT)
    if(NOT TARGET mode_car OR NOT TARGET device_line_tracking OR
            NOT TARGET device_xinliu_t8 OR NOT TARGET device_serial)
        message(FATAL_ERROR
            "Line-tracking readout Task requires Mode/Car, LineTracking, XinliuT8 and Serial Devices")
    endif()
    set(TASK_BUILD_LINE_TRACKING_READOUT ON)
    list(APPEND TASK_APP_SOURCES
        "LineTracking/task_line_tracking_readout.c")
endif()

add_library(task_app STATIC
    ${TASK_APP_SOURCES})

target_include_directories(task_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/LineTracking")

target_link_libraries(task_app PUBLIC
    freertos_kernel
    mspm0_build_options)

if(TASK_BUILD_ANALOG_TRACKING)
    target_compile_definitions(task_app PRIVATE
        TASK_ENABLE_ANALOG_TRACKING=1)
    target_link_libraries(task_app PRIVATE
        mode_car
        mode_communicate
        device_icm45686
        device_encoder
        device_key_input
        device_line_tracking
        device_xinliu_t8
        device_motor
        device_rgb
        device_serial
        bsp
        m)
elseif(TASK_BUILD_LINE_TRACKING_READOUT)
    target_compile_definitions(task_app PRIVATE
        TASK_ENABLE_LINE_TRACKING_READOUT=1)
    target_link_libraries(task_app PRIVATE
        mode_car
        device_line_tracking
        device_xinliu_t8
        device_serial
        bsp
        m)
endif()
