idf_component_register(
  SRCS
    "main.cpp"
    "seat_model.cpp"
    "ssd1306_spi.cpp"
  INCLUDE_DIRS "."
  REQUIRES
    esp_http_server
    esp_http_client
    esp_wifi
    esp-tls
    esp_timer
    esp_driver_gpio
    esp_driver_ledc
    nvs_flash
    esp32-camera
)
