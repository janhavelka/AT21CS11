idf_component_register(
  SRCS "src/AT21CS.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_driver_gpio esp_timer freertos esp_hw_support
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC AT21CS_PLATFORM_IDF=1)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
