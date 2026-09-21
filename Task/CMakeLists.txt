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

if(TEMPLATE_ENABLE_EXAMPLE_COMMAND_FLOW)
    target_compile_definitions(task_app PRIVATE TEMPLATE_ENABLE_EXAMPLE_COMMAND_FLOW=1)
    target_link_libraries(task_app PRIVATE example_command_flow)
endif()
