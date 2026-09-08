// main/app_ble.h -- NimBLE host 生命周期 + 两种广播模式互斥(RADIO 演示 / HID 键盘)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    APP_BLE_ADV_OFF = 0,
    APP_BLE_ADV_RADIO,      // page_radio: 演示格式广播(厂商数据+计数)
    APP_BLE_ADV_HID,        // page_keys: 键盘格式广播(appearance+HID UUID)
} app_ble_adv_mode_t;

// 懒初始化 host(幂等): 含 HID GATT 表注册与配对策略, 初始化后不广播。
// 进 RADIO/KEYS 页时经 start/hid_adv_start 触发; 开机不再常驻(约 70K 内存)。
esp_err_t app_ble_host_init(void);
void app_ble_shutdown(void);        // 全量释放 host+controller(退页调用, 可重入)
app_ble_adv_mode_t app_ble_adv_mode(void);

// ---- RADIO 模式(page_radio 用) ----
esp_err_t app_ble_start(void);      // 确保 host 在 + 切 RADIO 广播(幂等)

// ---- HID 模式(page_keys 用) ----
void app_ble_hid_adv_start(void);   // 切键盘广播(自动顶掉 RADIO 广播)

bool app_ble_active(void);          // host 已初始化
int  app_ble_state(void);           // 0=off 1=advertising 2=connected
uint32_t app_ble_updates(void);     // 厂商数据刷新次数(约=RADIO 广播秒数)
