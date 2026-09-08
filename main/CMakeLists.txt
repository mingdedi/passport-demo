# WiFi 凭据不入开源仓库: 编译期强制要求本地存在 wifi_secrets.h(见 .example 模板)
if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/wifi_secrets.h)
    message(FATAL_ERROR "缺少 main/wifi_secrets.h: 复制 wifi_secrets.h.example 为 wifi_secrets.h 并填入 SSID/密码(该文件已被 .gitignore 排除)")
endif()

# GLM API Key 同理: glm_secrets.h 占位留空可编译(主界面显示 NO KEY)
if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/glm_secrets.h)
    message(FATAL_ERROR "缺少 main/glm_secrets.h: 复制 glm_secrets.h.example 为 glm_secrets.h 并填入智谱 API Key(该文件已被 .gitignore 排除)")
endif()

idf_component_register(
    SRCS "main.c"
         "ui_theme.c"
         "ui_anim.c"
         "ui_boot.c"
         "ui_main.c"
         "ui_home.c"
         "page_sysinfo.c"
         "page_display.c"
         "page_audio.c"
         "page_battery.c"
         "page_input.c"
         "page_radio.c"
         "page_storage.c"
         "page_about.c"
         "page_matrix.c"
         "app_sensors.c"
         "app_power.c"
         "app_wifi.c"
         "app_glm.c"
         "app_audio.c"
         "app_ble.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt nvs_flash esp_driver_tsens spi_flash esp_app_format esp_partition esp_wifi esp_netif esp_http_client mbedtls json
)
