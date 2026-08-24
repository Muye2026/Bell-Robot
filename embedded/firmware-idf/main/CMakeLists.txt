idf_component_register(
  SRCS
    "main.cpp"
    "app_state.cpp"
    "sedentary_timer.cpp"
    "presence_detector.cpp"
    "buzzer_player.cpp"
    "display_backend.cpp"
    "display_layout.cpp"
    "display_ui.cpp"
    "led_matrix_is31fl3733.cpp"
    "wifi_net.cpp"
    "cloud_client.cpp"
    "web_ui.cpp"
    "ota_update.cpp"
    "sample_store.cpp"
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
    esp_driver_i2c
    esp_driver_ledc
    nvs_flash
    esp32-camera
    spiffs
    app_update
    esp_partition
)

# 显示后端可以在构建时覆盖，不必改 app_config.h：
#   idf.py -DDISPLAY_BACKEND=1 build     # IS31FL3733 LED 点阵
# 不传时用 app_config.h 里的默认值（SSD1306）。
if(DEFINED DISPLAY_BACKEND)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC DISPLAY_BACKEND=${DISPLAY_BACKEND})
endif()
