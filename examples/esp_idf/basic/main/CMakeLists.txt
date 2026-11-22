idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "." "../../../common"
  REQUIRES AT21CS01_AT21CS11 esp_timer freertos vfs
           esp_usb_cdc_rom_console esp_driver_usb_serial_jtag
)

# Refresh the banner on incremental builds, including when only Git status changes.
set(identity_header "${CMAKE_CURRENT_BINARY_DIR}/AT21CSBuildIdentity.h")
add_custom_target(at21cs_example_identity
  COMMAND "${CMAKE_COMMAND}"
    "-DAT21CS_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../../../.."
    "-DAT21CS_IDENTITY_HEADER=${identity_header}"
    -P "${CMAKE_CURRENT_LIST_DIR}/GenerateBuildIdentity.cmake"
  BYPRODUCTS "${identity_header}"
  VERBATIM
)
add_dependencies(${COMPONENT_LIB} at21cs_example_identity)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
