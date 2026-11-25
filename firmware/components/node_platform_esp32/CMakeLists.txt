# src/platform/esp32/（ESP-IDF の API を使えるのはここだけ。CLAUDE.md・NFR-05）
set(P "${NODE_SRC_DIR}/platform/esp32")
idf_component_register(
  SRCS
    "${P}/app_main.cpp"
    "${P}/esp32_hal.cpp"
    "${P}/esp32_infra.cpp"
    "${P}/esp32_eth.cpp"
    "${P}/esp32_mqtt.cpp"
    "${P}/esp32_tasks.cpp"
  INCLUDE_DIRS "${NODE_SRC_DIR}"
  REQUIRES node_core
  PRIV_REQUIRES esp_eth esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_adc esp_timer esp_wifi esp_netif esp_event
                nvs_flash mqtt esp_ringbuf esp_hw_support esp_system log freertos)

include("${CMAKE_CURRENT_LIST_DIR}/../node_flags.cmake")
node_component_flags(${COMPONENT_LIB})
