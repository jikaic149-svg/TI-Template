add_library(service_packet STATIC "service_packet.c")
target_include_directories(service_packet PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(service_packet PUBLIC core_message algorithm_packet)
