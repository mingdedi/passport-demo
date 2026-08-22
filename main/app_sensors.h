// main/app_sensors.h -- 全局采样服务: 电池/温度/堆/运行时间, 1s 快照 + 电池电压历史。
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define APP_BATT_HIST_N 64

typedef struct {
    int soc;                                   // -1 = 未知
    int mv;
    float temp_c;                              // NAN = 无温度传感器
    size_t heap_free, heap_min, heap_largest;
    uint32_t uptime_s;
    int16_t batt_hist_mv[APP_BATT_HIST_N];     // 电压历史(滚动)
    int batt_hist_n;                           // 有效点数
    bool batt_ok;
    bool temp_ok;
} app_sensors_snap_t;

void app_sensors_start(void);
const app_sensors_snap_t *app_sensors_snap(void);
