set(AT21CS_ENABLE_ESP32_BACKEND ON CACHE BOOL
    "Build the external ESP32 GPIO/timing transport")

set(AT21CS_SOURCES
  "src/AT21CS.cpp"
  "src/Bus.cpp"
)

if(AT21CS_ENABLE_ESP32_BACKEND)
  list(APPEND AT21CS_SOURCES "src/platform/esp32/Esp32Transport.cpp")
  idf_component_register(
    SRCS ${AT21CS_SOURCES}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer freertos
  )
else()
  idf_component_register(
    SRCS ${AT21CS_SOURCES}
    INCLUDE_DIRS "include"
  )
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
