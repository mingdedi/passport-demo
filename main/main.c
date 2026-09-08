// main/main.c -- passport-demo 入口: 外设初始化 -> 开机动画 -> 主界面; 按键全局路由。
#include "ui.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "app_audio.h"
#include "app_sensors.h"
#include "app_wifi.h"
#include "app_glm.h"
#include "app_power.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

static const char *TAG = "main";

// ---- 页面注册表(顺序即菜单顺序) ----
extern const ui_page_t page_sysinfo;
extern const ui_page_t page_display;
extern const ui_page_t page_audio;
extern const ui_page_t page_battery;
extern const ui_page_t page_input;
extern const ui_page_t page_radio;
extern const ui_page_t page_keys;
extern const ui_page_t page_storage;
extern const ui_page_t page_about;
extern const ui_page_t page_matrix;

const ui_page_t *const UI_PAGES[] = {
    &page_sysinfo, &page_display, &page_audio, &page_battery,
    &page_input,   &page_radio,  &page_keys,  &page_storage,
    &page_about,   &page_matrix,
};

static void boot_done(void) {
    ui_main_show();               // 开机首屏 = 主界面, OK 进菜单
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // 熄屏唤醒键在最前端消费掉(背光走 LEDC 不需要 LVGL 锁), 不透传 UI
    if (app_power_key_event()) return;

    if (!bsp_lvgl_lock(500)) return;

    if (ui_boot_active()) {
        if (btn == BSP_BTN_OK && (ev == BSP_BTN_PRESS || ev == BSP_BTN_CLICK))
            ui_boot_skip();
    } else {
        ui_main_key(btn, ev);     // 主界面/菜单/页面统一入口
    }
    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "passport-demo boot");

    // NVS(NimBLE 与后续配置依赖)
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) ESP_LOGW(TAG, "nvs init: %s", esp_err_to_name(nvs_err));

    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    bsp_display_backlight(0);          // 开机动画再点亮

    bsp_button_init(on_key, NULL);
    app_audio_start();
    ESP_LOGI(TAG, "heap post-audio %u", (unsigned)esp_get_free_heap_size());
    app_sensors_start();
    app_power_start();        // 电源模式推断 + 熄屏管理(依赖 CW2017 已初始化)
    app_wifi_start();          // 在线服务: 周期扫描目标 AP + NTP 上海时间
    ESP_LOGI(TAG, "heap post-wifi %u", (unsigned)esp_get_free_heap_size());
    // BLE 不常驻: controller+host 约 70K, 开机即起会挤死 GLM 的 TLS 请求
    // (实测 2026-09-08 剩 3.1K, glm 任务创建失败)。改为进 RADIO/KEYS 页
    // 才 init, 退页 shutdown 全量释放(app_ble.c 管理生命周期)。
    app_glm_start();           // GLM 套餐用量: 联网后 5min 查额度(依赖 wifi/NTP 就绪)
    ESP_LOGI(TAG, "heap post-glm %u", (unsigned)esp_get_free_heap_size());

    if (bsp_lvgl_lock(1000)) {
        ui_boot_play(boot_done);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "ready: audio=%d", app_audio_ok());
}
