// main/app_power.c -- 电源模式推断 + 熄屏管理实现。
// 采样/斜率/熄屏状态机是机制部分; 判定核心 decide() 是设计决策点(见函数注释)。
#include "app_power.h"
#include "bsp_battery.h"
#include "bsp_display.h"

#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "app_pwr";

static app_pwr_snap_t s_snap;
static esp_timer_handle_t s_timer;
static int16_t s_hist[APP_PWR_HIST_N];   // 旧→新的电压轨迹(mV)
static int64_t s_last_key_us;            // 最近一次按键活动(无操作计时的锚点)
static bool s_screen_off;

// ---- 充电判定: 电压轨迹 → 状态(本模块核心设计决策, 留给人工实现) ----
// 输入: hist[0..n-1] 旧→新(mV, 相邻间隔 APP_PWR_SAMPLE_S 秒); slope 为该窗口的
//       最小二乘斜率(mV/min, 已算好); cur_mv/cur_soc 为最新样本; prev 为当前状态
//       (可用于滞回防抖)。
// 物理背景(实测电压曲线后可再校准):
//   插充电器 → 负载转移到 VBUS, 电池电压回稳上升, 恒流充电持续爬升(mV/min 级)
//   满电     → CV 钳压, 电压维持在 ~4200mV 附近不再上行
//   拔线     → 电压下垂, 斜率转负
//   CW2017 电压分辨率 0.3125mV, 单次读数噪声约 ±2mV: 判定需抗抖(阈值+滞回/连续确认)。
static app_pwr_state_t decide(const int16_t *hist, int n, int slope,
                              int cur_mv, int cur_soc, app_pwr_state_t prev) {
    (void)hist; (void)cur_soc;
    if (n < 2) return APP_PWR_UNKNOWN;
    if (cur_mv >= APP_PWR_FULL_MV) return APP_PWR_CHARGING;    // 钳压: CV/满充插着电
    if (slope >= APP_PWR_RISE_MV_MIN) return APP_PWR_CHARGING; // 恒流充电持续爬升
    if (slope <= -APP_PWR_FALL_MV_MIN) return APP_PWR_BATTERY; // 拔线后电压下垂
    return prev;                       // 平稳段维持原状: 天然滞回, 抗读数噪声抖动
}

// 窗口最小二乘斜率(mV/min)。x 用 2i-(n-1) 中心化(×2 保持整数, 免浮点):
// slope_per_sample = Σx'y/Σx'²/2, 再按采样周期折算到分钟。
static int hist_slope(void) {
    int n = s_snap.n;
    if (n < 2) return 0;
    int64_t num = 0, den = 0;
    for (int i = 0; i < n; i++) {
        int xp = 2 * i - (n - 1);
        num += (int64_t)xp * s_hist[i];
        den += (int64_t)xp * xp;
    }
    return (int)(num * 60 / (den * 2 * APP_PWR_SAMPLE_S));
}

static void sample_cb(void *arg) {
    (void)arg;
    int mv = bsp_battery_mv(), soc = bsp_battery_soc();

    if (mv > 0) {                          // 推入轨迹(同 app_sensors 滚动历史的手法)
        if (s_snap.n < APP_PWR_HIST_N) {
            s_hist[s_snap.n++] = (int16_t)mv;
        } else {
            memmove(s_hist, s_hist + 1, sizeof(int16_t) * (APP_PWR_HIST_N - 1));
            s_hist[APP_PWR_HIST_N - 1] = (int16_t)mv;
        }
        s_snap.mv = mv;
    }
    if (soc >= 0) s_snap.soc = soc;

    s_snap.slope_mv_min = hist_slope();
    app_pwr_state_t st = decide(s_hist, s_snap.n, s_snap.slope_mv_min,
                                s_snap.mv, s_snap.soc, s_snap.state);
    if (st != s_snap.state) {
        ESP_LOGI(TAG, "电源模式: %d -> %d (mv=%d slope=%dmV/min n=%d)",
                 s_snap.state, st, s_snap.mv, s_snap.slope_mv_min, s_snap.n);
        s_snap.state = st;
    }

    // 熄屏状态机(挂在 30s 采样粒度上检查, 足够; 背光走 LEDC, 不需 LVGL 锁)
    int64_t now = esp_timer_get_time();
    bool idle = (now - s_last_key_us) >= (int64_t)APP_PWR_IDLE_S * 1000000;
    if (!s_screen_off && idle && s_snap.state == APP_PWR_BATTERY) {
        bsp_display_backlight(0);
        s_screen_off = true;
    } else if (s_screen_off && s_snap.state != APP_PWR_BATTERY) {
        // 熄屏期间检测到充电: 立即点亮兑现"充电常亮"
        bsp_display_backlight(APP_PWR_WAKE_BL);
        s_screen_off = false;
    }
}

void app_power_start(void) {
    memset(&s_snap, 0, sizeof s_snap);
    s_snap.state = APP_PWR_UNKNOWN;
    s_snap.soc = -1;
    s_last_key_us = esp_timer_get_time();

    const esp_timer_create_args_t args = {
        .name = "app_pwr", .callback = sample_cb,
    };
    esp_timer_create(&args, &s_timer);
    esp_timer_start_periodic(s_timer, APP_PWR_SAMPLE_S * 1000000LL);
    ESP_LOGI(TAG, "start: %ds 轨迹采样, 无操作 %ds 熄屏(仅未充电)",
             APP_PWR_SAMPLE_S, APP_PWR_IDLE_S);
}

const app_pwr_snap_t *app_power_snap(void) { return &s_snap; }

bool app_power_key_event(void) {
    s_last_key_us = esp_timer_get_time();
    if (s_screen_off) {
        bsp_display_backlight(APP_PWR_WAKE_BL);
        s_screen_off = false;
        return true;                       // 本次按键只用于唤醒, 由调用方吞掉
    }
    return false;
}

bool app_power_screen_off(void) { return s_screen_off; }
