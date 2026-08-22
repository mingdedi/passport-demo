// main/app_ble.c -- NimBLE GAP 可连接广播; 手机可搜到 "PASSPORT-DEMO" 并连接。
#include "app_ble.h"
#include "app_sensors.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "app_ble";

#define BLE_NAME "PASSPORT-DEMO"

static bool s_inited;
static volatile int s_state;                 // 0/1/2
static volatile uint32_t s_updates;
static volatile bool s_want_adv;             // 页面在不在
static esp_timer_handle_t s_update_timer;

static int gap_event_cb(struct ble_gap_event *event, void *arg);

// 拼装并(重)启动广播
static int adv_start(void) {
    if (!s_inited || !s_want_adv) return -1;

    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }

    uint8_t mfg[4] = {0xBE, 0xEF, (uint8_t)(s_updates & 0xFF), 0};
    int soc = app_sensors_snap()->soc;
    mfg[3] = (soc >= 0 && soc <= 100) ? (uint8_t)soc : 0xFF;

    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.mfg_data = mfg;
    f.mfg_data_len = 4;
    // 名字放 scan-response, ADV 包更省
    struct ble_hs_adv_fields sr = {0};
    sr.name = (const uint8_t *)BLE_NAME;
    sr.name_len = sizeof(BLE_NAME) - 1;
    sr.name_is_complete = 1;

    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = 0x0040;                      // 40ms
    p.itvl_max = 0x0080;                      // 80ms

    int rc = ble_gap_adv_set_fields(&f);
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&sr);
    if (rc == 0) rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                                        &p, gap_event_cb, NULL);
    s_state = (rc == 0) ? 1 : 0;
    return rc;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_state = 2;
            ESP_LOGI(TAG, "connected, conn_handle=%d", event->connect.conn_handle);
        } else {
            adv_start();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected, restarting adv");
        adv_start();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        adv_start();
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void) {
    adv_start();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "host reset, reason=%d", reason);
    s_state = 0;
}

static void host_task(void *param) {
    (void)param;
    nimble_port_run();                        // 返回即 host 停止
    nimble_port_freertos_deinit();
}

// 1s 刷新厂商数据(计数/电量递增, 手机扫码可见变化)
static void update_timer_cb(void *arg) {
    (void)arg;
    if (!s_inited || !s_want_adv) return;
    s_updates++;
    adv_start();
}

esp_err_t app_ble_start(void) {
    if (s_inited) { adv_start(); return ESP_OK; }

    s_want_adv = true;

    int rc = nimble_port_init();
    if (rc != 0) return ESP_FAIL;

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_device_name_set(BLE_NAME);

    nimble_port_freertos_init(host_task);

    s_inited = true;

    const esp_timer_create_args_t args = {
        .name = "ble_upd", .callback = update_timer_cb,
    };
    esp_timer_create(&args, &s_update_timer);
    esp_timer_start_periodic(s_update_timer, 1000000);
    return ESP_OK;
}

void app_ble_adv_stop(void) {
    s_want_adv = false;
    if (s_inited && ble_gap_adv_active()) {
        ble_gap_adv_stop();
        s_state = 0;
    }
}

void app_ble_adv_resume(void) {
    if (!s_inited) return;
    s_want_adv = true;
    adv_start();
}

bool app_ble_active(void)     { return s_inited; }
int  app_ble_state(void)      { return s_state; }
uint32_t app_ble_updates(void){ return s_updates; }
