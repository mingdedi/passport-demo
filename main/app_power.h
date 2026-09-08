// main/app_power.h -- 电源模式推断(CW2017 电压轨迹) + 熄屏管理服务。
// 背景: 本板无充电检测硬件通路(无 VBUS 分压进 ADC、无充电 IC 状态脚、CW2017
// 是纯电量计), 唯一软件途径是电压轨迹启发式: 插充电器后系统负载转移到 VBUS,
// 电池电压回稳并随恒流充电爬升, 满电后钳在 ~4.2V(CV); 拔线后电压下垂。
// 据此推断 充电/未充电 两态, 驱动熄屏策略(未充电无操作熄屏, 充电常亮)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---- 策略参数(实测电压曲线后可调) ----
#define APP_PWR_SAMPLE_S   30    // 轨迹采样周期(秒); I2C 读取是微秒级操作, 取小只求响应快
#define APP_PWR_HIST_N     16    // 轨迹长度: 16*30s = 8min 窗口, 覆盖插拔特征时段
#define APP_PWR_IDLE_S     300   // 未充电模式无操作熄屏时限(5min)
#define APP_PWR_WAKE_BL    100   // 唤醒背光(主界面全程 100%, 与现状一致)
#define APP_PWR_FULL_MV    4150  // 钳压阈值: CV/满充插着电时电压维持在此之上(静置满电
                                 // 也会短暂停留, 拔线后跌破即自愈回 BATTERY)
#define APP_PWR_RISE_MV_MIN  2   // 进入充电的爬升斜率阈值(mV/min), 恒流充电远高于此
#define APP_PWR_FALL_MV_MIN  1   // 退出充电的下垂斜率阈值(mV/min), 拔线放电转负

typedef enum {
    APP_PWR_BATTERY = 0,   // 未充电(电池供电): 无操作熄屏
    APP_PWR_CHARGING,      // 检测到充电特征: 屏幕常亮
    APP_PWR_UNKNOWN,       // 轨迹样本不足, 尚无法判定
} app_pwr_state_t;

typedef struct {
    app_pwr_state_t state;
    int mv, soc;           // 最新样本(CW2017 直读)
    int slope_mv_min;      // 近窗口最小二乘斜率(mV/min), decide 的现成输入兼调试显示
    int n;                 // 有效轨迹样本数 0..APP_PWR_HIST_N
} app_pwr_snap_t;

void app_power_start(void);              // 依赖 bsp_battery_init 已完成(app_sensors 先启动)
const app_pwr_snap_t *app_power_snap(void);

// ---- 熄屏管理 ----
// 按键事件统一入口(on_key 最前端调用): 记录活动时间; 熄屏态点亮屏幕并返回 true
// —— 该次按键被"唤醒"消费掉, 不透传 UI, 避免唤醒键误触发页面操作。
bool app_power_key_event(void);
bool app_power_screen_off(void);         // UI 定时器用它跳过熄屏期间的重绘
