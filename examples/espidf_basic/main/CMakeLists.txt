idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "." "../../common" "../../.."
  REQUIRES AT21CS11 esp_driver_gpio esp_timer esp_hw_support freertos vfs
)
