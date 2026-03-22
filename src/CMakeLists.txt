idf_component_register(
    SRCS
        "main.cpp"
        "app/display_stack.cpp"
        "app/home_screen.cpp"
        "app/light_transport.cpp"
        "app/preset_store.cpp"
        "app/settings_store.cpp"
    INCLUDE_DIRS
        "."
        "../include"
    REQUIRES
        driver
        esp_event
        esp_lcd
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
        esp_lvgl_port
        esp32_display_panel
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
