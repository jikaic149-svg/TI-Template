add_library(service_line STATIC "service_line.c")
target_include_directories(service_line PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(service_line PUBLIC mspm0_build_options)
