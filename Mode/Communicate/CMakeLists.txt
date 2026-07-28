add_library(mode_communicate STATIC
    "Bluetooth/mode_communicate_bluetooth_pc.c"
    "Bluetooth/mode_communicate_bluetooth_dual_mcu.c"
    "LoRa/mode_communicate_lora_half_duplex.c")

target_include_directories(mode_communicate PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(mode_communicate PUBLIC
    mspm0_build_options)
