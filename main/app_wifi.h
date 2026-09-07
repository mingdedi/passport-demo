// main/app_wifi.h -- WiFi 在线服务: 周期扫描目标 AP 自动连接 + NTP 上海时间。
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    APP_WIFI_OFFLINE = 0,   // 离线(等待下一轮扫描)
    APP_WIFI_SCAN,          // 扫描中
    APP_WIFI_JOIN,          // 连接中(含断线即时重连)
    APP_WIFI_ONLINE,        // 已拿到 IP
} app_wifi_state_t;

typedef struct {
    app_wifi_state_t state;
    int8_t rssi;            // ONLINE 时有效
    bool time_valid;        // NTP 时间已同步(TZ=上海)
} app_wifi_snap_t;

void app_wifi_start(void);
void app_wifi_resync(void);            // 立即刷新: 离线重扫 / 在线重启 NTP 对时
const app_wifi_snap_t *app_wifi_snap(void);
