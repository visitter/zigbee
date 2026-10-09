if(CONFIG_BUTTON_MQTT_CUSTOM_CA)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/broker_ca.pem")
        message(FATAL_ERROR "Copy your broker CA certificate to main/broker_ca.pem")
    endif()
    set(broker_ca_file "broker_ca.pem")
endif()
idf_component_register(SRCS "mqtt_buttons.c" "button_debounce.c" "ha_discovery.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio esp_timer nvs_flash mqtt json
                                     esp_wifi esp_event esp_netif lwip mbedtls
                       EMBED_TXTFILES ${broker_ca_file})
