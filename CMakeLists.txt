set(AT21CS_ENABLE_ESP32_COMPAT_BACKEND ON CACHE BOOL
    "Build the built-in ESP32 GPIO/timing compatibility backend")

if(AT21CS_ENABLE_ESP32_COMPAT_BACKEND)
  idf_component_register(
    SRCS
      "src/AT21CS.cpp"
      "src/platform/esp32/AT21CSEsp32Backend.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer freertos esp_hw_support
  )
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    AT21CS_ENABLE_ESP32_COMPAT_BACKEND=1
    AT21CS_PLATFORM_IDF=1
  )
else()
  idf_component_register(
    SRCS
      "src/AT21CS.cpp"
      "src/platform/esp32/AT21CSEsp32Backend.cpp"
    INCLUDE_DIRS "include"
  )
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    AT21CS_ENABLE_ESP32_COMPAT_BACKEND=0
  )
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
