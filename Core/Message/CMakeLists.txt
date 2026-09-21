add_library(core_message STATIC
    "core_message.c"
    "ports/core_message_freertos.c")
target_include_directories(core_message PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(core_message PUBLIC mspm0_build_options)
target_link_libraries(core_message PRIVATE freertos_kernel)
