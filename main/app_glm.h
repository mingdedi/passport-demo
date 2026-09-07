// main/app_glm.h -- GLM 套餐用量服务: 联网后周期查询智谱开放平台额度接口,
// 快照只读供 ui_main 轮询(同 app_wifi 模式, 服务不碰 UI)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    APP_GLM_WAIT = 0,   // 未联网/首查未完成
    APP_GLM_NOKEY,      // glm_secrets.h 未填 Key
    APP_GLM_FETCH,      // 请求进行中
    APP_GLM_OK,         // 最近一次查询成功
    APP_GLM_ERR,        // 失败(http_err 为 HTTP 状态码或负数错误码)
} app_glm_state_t;

typedef struct {
    app_glm_state_t state;
    int http_err;                 // 0=无; >0=HTTP 状态码; <0=传输/解析错误
    bool has5, hasw;              // 5 小时窗/周窗数据是否已拿到
    int64_t used5, total5;        // 5 小时窗 已用/总额(currentValue/usage)
    int64_t usedw, totalw;        // 周窗 已用/总额
    int pct5, pctw;               // 用量百分比(整数, percentage 字段四舍五入)
    int64_t reset5_ms, resetw_ms; // 窗口重置时刻(epoch 毫秒)
    time_t fetched_at;            // 最近成功时刻(秒)
} app_glm_snap_t;

void app_glm_start(void);                       // app_main 里在 app_wifi_start 之后调
void app_glm_refresh_now(void);                 // 立即查一次(双击 OK 触发, 不等 5min)
const app_glm_snap_t *app_glm_snap(void);       // 只读快照, UI 轮询
