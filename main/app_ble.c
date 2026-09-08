// main/app_ble.c -- NimBLE host 生命周期 + 广播模式互斥管理。
// RADIO(page_radio 演示广播) / HID(page_keys 键盘广播); GATT/配对/键入见 app_ble_hid.c。
#include "app_ble.h"
#include "app_ble_hid.h"
#include "app_sensors.h"
#include "app_wifi.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "app_ble";

#define BLE_NAME_RADIO "PASSPORT-DEMO"
#define BLE_NAME_HID   "PASSPORT-KEYS"

static bool s_inited;
static volatile app_ble_adv_mode_t s_mode = APP_BLE_ADV_OFF;
static volatile int s_state;                 // 0/1/2
static volatile uint32_t s_updates;
static esp_timer_handle_t s_update_timer;
static SemaphoreHandle_t s_host_done;        // host 任务退出信号(shutdown 同步用)

static int gap_event_cb(struct ble_gap_event *event, void *arg);
static int adv_radio_start(void);
static int adv_hid_start(void);

static void adv_start_by_mode(void) {
    switch (s_mode) {
    case APP_BLE_ADV_RADIO: adv_radio_start(); break;
    case APP_BLE_ADV_HID:   adv_hid_start();   break;
    default: break;
    }
}

// 演示广播: 厂商数据(计数+电量), 手机扫码可见变化
static int adv_radio_start(void) {
    if (!s_inited || s_mode != APP_BLE_ADV_RADIO) return -1;
    if (ble_gap_adv_active()) ble_gap_adv_stop();

    uint8_t mfg[4] = {0xBE, 0xEF, (uint8_t)(s_updates & 0xFF), 0};
    int soc = app_sensors_snap()->soc;
    mfg[3] = (soc >= 0 && soc <= 100) ? (uint8_t)soc : 0xFF;

    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.mfg_data = mfg;
    f.mfg_data_len = 4;
    // 名字放 scan-response, ADV 包更省
    struct ble_hs_adv_fields sr = {0};
    sr.name = (const uint8_t *)BLE_NAME_RADIO;
    sr.name_len = sizeof(BLE_NAME_RADIO) - 1;
    sr.name_is_complete = 1;

    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = 0x0040;                      // 40ms
    p.itvl_max = 0x0080;                      // 80ms

    int rc = ble_svc_gap_device_name_set(BLE_NAME_RADIO);
    if (rc == 0) rc = ble_gap_adv_set_fields(&f);
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&sr);
    if (rc == 0) rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                                        &p, gap_event_cb, NULL);
    if (rc != 0) ESP_LOGW(TAG, "radio adv 失败 rc=%d", rc);
    s_state = (rc == 0) ? 1 : 0;
    return rc;
}

// 键盘广播: appearance=键盘 + HID 服务 UUID, 终端会把它当蓝牙键盘配对
static int adv_hid_start(void) {
    if (!s_inited || s_mode != APP_BLE_ADV_HID) return -1;
    if (ble_gap_adv_active()) ble_gap_adv_stop();

    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids16 = (ble_uuid16_t *)&hid_uuid;
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    f.appearance = 0x03C1;                    // HID Keyboard
    f.appearance_is_present = 1;

    struct ble_hs_adv_fields sr = {0};
    sr.name = (const uint8_t *)BLE_NAME_HID;
    sr.name_len = sizeof(BLE_NAME_HID) - 1;
    sr.name_is_complete = 1;

    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = 0x0030;                      // 30ms(快配对)
    p.itvl_max = 0x0060;                      // 60ms

    int rc = ble_svc_gap_device_name_set(BLE_NAME_HID);
    if (rc == 0) rc = ble_gap_adv_set_fields(&f);
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&sr);
    if (rc == 0) rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                                        &p, gap_event_cb, NULL);
    if (rc != 0) ESP_LOGW(TAG, "hid adv 失败 rc=%d", rc);
    s_state = (rc == 0) ? 1 : 0;
    return rc;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    // HID 相关(配对/加密/订阅/重配对)先交 app_ble_hid 裁决
    int rc = app_ble_hid_on_gap(event);
    if (rc) return rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_state = 2;
            ESP_LOGI(TAG, "connected, conn_handle=%d", event->connect.conn_handle);
        } else if (s_mode != APP_BLE_ADV_OFF) {
            adv_start_by_mode();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected, reason=%d", event->disconnect.reason);
        s_state = 0;
        if (s_mode != APP_BLE_ADV_OFF) adv_start_by_mode();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        adv_start_by_mode();
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void) {
    // host 与 controller 同步完成, 把当前模式的广播拉起
    ESP_LOGI(TAG, "on_sync, mode=%d", s_mode);
    adv_start_by_mode();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "host reset, reason=%d", reason);
    s_state = 0;
}

static void host_task(void *param) {
    (void)param;
    nimble_port_run();                        // 返回即 host 停止
    if (s_host_done) xSemaphoreGive(s_host_done);
    nimble_port_freertos_deinit();            // 自删任务
}

// 1s 刷新 RADIO 演示厂商数据(计数/电量递增); HID 模式不打扰
static void update_timer_cb(void *arg) {
    (void)arg;
    if (!s_inited || s_mode != APP_BLE_ADV_RADIO) return;
    s_updates++;
    adv_radio_start();
}

esp_err_t app_ble_host_init(void) {
    if (s_inited) return ESP_OK;

    // 射频互斥: WiFi 连接态(beacon 监听窗持续仲裁射频)下 BLE controller 的
    // HCI sync 序列无法完成(实测 2026-09-08, on_sync 永不回调)。BLE 页期间
    // WiFi 全停, 退页 shutdown 时恢复自动重连。
    app_wifi_pause();

    if (!s_host_done) s_host_done = xSemaphoreCreateBinary();
    int rc = nimble_port_init();
    if (rc != 0) { app_wifi_resume(); return ESP_FAIL; }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    // 密钥配对策略: 设备屏显 6 位码(终端输入, 防 MITM) + LE 安全连接 + bonding
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_svc_gap_device_name_set(BLE_NAME_RADIO);

    // HID GATT 表必须在 host sync 完成前注册
    rc = app_ble_hid_register();
    if (rc != ESP_OK) { app_wifi_resume(); return ESP_FAIL; }

    nimble_port_freertos_init(host_task);
    s_inited = true;

    const esp_timer_create_args_t args = {
        .name = "ble_upd", .callback = update_timer_cb,
    };
    esp_timer_create(&args, &s_update_timer);
    esp_timer_start_periodic(s_update_timer, 1000000);
    ESP_LOGI(TAG, "host init done, free=%u", (unsigned)esp_get_free_heap_size());
    return ESP_OK;
}

// 全量释放 BLE(host+controller 约 70K)。退页调用; 页面重进时 host_init 重建。
// 同步等 host 任务退出(最多 1s), 在页面 exit(LVGL 任务)里调用可接受。
void app_ble_shutdown(void) {
    if (!s_inited) return;
    s_mode = APP_BLE_ADV_OFF;
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    app_ble_hid_disconnect();                 // 幂等: 无连接时 no-op
    if (s_update_timer) {
        esp_timer_stop(s_update_timer);
        esp_timer_delete(s_update_timer);
        s_update_timer = NULL;
    }
    nimble_port_stop();                       // 令 nimble_port_run() 返回
    if (xSemaphoreTake(s_host_done, pdMS_TO_TICKS(1000)) != pdTRUE)
        ESP_LOGW(TAG, "host 任务 1s 内未退出, 强行 deinit");
    nimble_port_deinit();                     // 释放 host + controller 全部内存
    app_ble_hid_session_reset();
    s_state = 0;
    s_inited = false;
    app_wifi_resume();                        // 射频归还, 自动重扫重连
    ESP_LOGI(TAG, "shutdown done, free=%u", (unsigned)esp_get_free_heap_size());
}

app_ble_adv_mode_t app_ble_adv_mode(void) { return s_mode; }

esp_err_t app_ble_start(void) {
    esp_err_t err = app_ble_host_init();
    if (err != ESP_OK) return err;
    s_mode = APP_BLE_ADV_RADIO;
    // host 未 sync 时此处失败无妨, on_sync 会按模式拉起
    return adv_radio_start() == 0 ? ESP_OK : ESP_FAIL;
}

void app_ble_hid_adv_start(void) {
    if (app_ble_host_init() != ESP_OK) return;
    if (ble_gap_adv_active()) ble_gap_adv_stop();   // 顶掉 RADIO 广播(两页互斥, 保险)
    s_mode = APP_BLE_ADV_HID;
    adv_hid_start();
}

bool app_ble_active(void)      { return s_inited; }
int  app_ble_state(void)       { return s_state; }
uint32_t app_ble_updates(void) { return s_updates; }
