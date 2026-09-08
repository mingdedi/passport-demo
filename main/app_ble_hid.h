// main/app_ble_hid.h -- BLE HID 键盘(密码物理密钥): 状态快照 + 密码键入接口。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "host/ble_gap.h"

// KEYS 页状态机(OFF → ADV → PAIRING → READY; 断连回 ADV)
typedef enum {
    KKEY_ST_OFF = 0,     // 键盘广播未开
    KKEY_ST_ADV,         // 键盘广播中, 等终端连接
    KKEY_ST_PAIRING,     // 配对中(屏显 6 位码, 由终端输入)
    KKEY_ST_READY,       // 已连接 + 已加密(可发射)
} kkey_state_t;

kkey_state_t app_ble_hid_state(void);
uint32_t app_ble_hid_passkey(void);            // PAIRING 态有效, 0-999999
bool app_ble_hid_passkey_valid(void);          // 配对码已生成(屏显用)

// 异步触发键入(text 为 ASCII 明文; 需 READY 态; 正在键入返回 false)
bool app_ble_hid_type(const char *text, bool enter);
bool app_ble_hid_typing(void);
int  app_ble_hid_typed(void);                  // 已键入字符数(粗略进度)
bool app_ble_hid_ready(void);

// ---- 以下仅供 app_ble.c 在 host 生命周期/事件路由时调用, 页面代码勿用 ----
esp_err_t app_ble_hid_register(void);          // 注册 HID GATT 表(须在 host sync 前)
int  app_ble_hid_on_gap(struct ble_gap_event *ev);   // GAP 事件转发; 非 0=事件已裁决
void app_ble_hid_session_reset(void);          // 键盘广播停止时清状态
void app_ble_hid_disconnect(void);             // 断开当前 HID 连接(退页即离线)
void app_ble_hid_task_stop(void);              // 删除 hidtype 任务(shutdown 时, 先于 host 停止)
