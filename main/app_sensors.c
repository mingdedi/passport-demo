// main/app_sensors.c -- 1s 周期采样(CW2017/温度传感器/堆), 快照供状态栏与页面使用。
#include "app_sensors.h"
#include "bsp_battery.h"

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/temperature_sensor.h"
#include <math.h>
#include <string.h>

static app_sensors_snap_t s_snap;
static esp_timer_handle_t s_timer;
static temperature_sensor_handle_t s_tsens;

static void sample_once(void) {
    s_snap.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000LL);
    s_snap.heap_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    s_snap.heap_min = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    s_snap.heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    int soc = bsp_battery_soc(), mv = bsp_battery_mv();
    if (soc >= 0) s_snap.soc = soc;
    if (mv > 0) s_snap.mv = mv;

    if (s_tsens) temperature_sensor_get_celsius(s_tsens, &s_snap.temp_c);

    // 电池电压滚动历史
    if (s_snap.mv > 0) {
        if (s_snap.batt_hist_n < APP_BATT_HIST_N) {
            s_snap.batt_hist_mv[s_snap.batt_hist_n++] = (int16_t)s_snap.mv;
        } else {
            memmove(s_snap.batt_hist_mv, s_snap.batt_hist_mv + 1,
                    sizeof(int16_t) * (APP_BATT_HIST_N - 1));
            s_snap.batt_hist_mv[APP_BATT_HIST_N - 1] = (int16_t)s_snap.mv;
        }
    }
}

static void timer_cb(void *arg) { (void)arg; sample_once(); }

void app_sensors_start(void) {
    s_snap.soc = -1;
    s_snap.mv = 0;
    s_snap.temp_c = NAN;

    s_snap.batt_ok = (bsp_battery_init() == ESP_OK);
    if (s_snap.batt_ok) {
        int soc = bsp_battery_soc(), mv = bsp_battery_mv();
        if (soc >= 0) s_snap.soc = soc;
        if (mv > 0) s_snap.mv = mv;
    }

    // C3 片内温度传感器
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 60);
    if (temperature_sensor_install(&cfg, &s_tsens) == ESP_OK &&
        temperature_sensor_enable(s_tsens) == ESP_OK) {
        s_snap.temp_ok = true;
    } else {
        s_tsens = NULL;
        s_snap.temp_ok = false;
    }

    sample_once();

    const esp_timer_create_args_t args = {
        .name = "sensors", .callback = timer_cb,
    };
    esp_timer_create(&args, &s_timer);
    esp_timer_start_periodic(s_timer, 1000000);
}

const app_sensors_snap_t *app_sensors_snap(void) { return &s_snap; }
