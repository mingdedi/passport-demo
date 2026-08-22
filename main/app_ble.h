// main/app_ble.h -- NimBLE 演示广播: 可连接广播 + 1s 刷新厂商数据(计数+电量)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// 初始化 NimBLE host 并开始广播(幂等; 首次调用较慢, ~100ms)
esp_err_t app_ble_start(void);
void app_ble_adv_stop(void);
void app_ble_adv_resume(void);

bool app_ble_active(void);          // host 已初始化
int  app_ble_state(void);           // 0=off 1=advertising 2=connected
uint32_t app_ble_updates(void);     // 厂商数据刷新次数(约=广播秒数)
