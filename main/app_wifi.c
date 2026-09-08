// main/app_wifi.c -- WiFi 在线服务(事件驱动, 不碰 UI, 快照只读供 ui_main 轮询)。
// 节奏: 开机 STA 启动即扫一轮, 之后每 5min 扫描目标 SSID, 命中即连;
// 瞬断即时重连(上限 3 次, 超过回到周期扫描节奏); 拿到 IP 后 SNTP 同步上海时间。
// 凭据来自 wifi_secrets.h(git 不追踪, 模板见 wifi_secrets.h.example)。
#include "app_wifi.h"
#include "wifi_secrets.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include <time.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "wifi";

#define SCAN_PERIOD_S   300     // 周期扫描间隔(5min)
#define JOIN_RETRY_MAX  3       // 断线即时重连上限

static app_wifi_snap_t s_snap;
static esp_timer_handle_t s_scan_timer;
static int s_join_retries;
static bool s_sntp_started;
static bool s_paused;                   // BLE 页期间射频互斥, 停重连/停扫描
static uint8_t s_filter[33];    // 扫描 SSID 过滤缓冲(扫描期间驱动持有指针, 故用静态)

static void scan_start(void) {
    wifi_scan_config_t sc = { .ssid = s_filter };
    if (esp_wifi_scan_start(&sc, false) == ESP_OK)
        s_snap.state = APP_WIFI_SCAN;              // false=异步, 完成走 SCAN_DONE 事件
}

static void on_time_sync(struct timeval *tv) {
    (void)tv;
    s_snap.time_valid = true;                      // SNTP 任务上下文, 仅置标志
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    ESP_LOGI(TAG, "NTP 同步: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void sntp_begin(void) {
    if (s_sntp_started) return;
    s_sntp_started = true;
    setenv("TZ", "CST-8", 1);                      // 上海 UTC+8, 无夏令时
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
        ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "cn.pool.ntp.org"));
    cfg.sync_cb = on_time_sync;
    cfg.wait_for_sync = false;                     // 非阻塞等待, 靠回调通知
    esp_netif_sntp_init(&cfg);                     // .start=true, init 即启动
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        scan_start();                              // 开机即扫一轮, 不等 5min
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        wifi_ap_record_t recs[4];
        uint16_t n = 4;
        esp_wifi_scan_get_ap_records(&n, recs);    // 已按 SSID 过滤, [0] 信号最强
        if (n > 0) {
            ESP_LOGI(TAG, "扫描命中 %s rssi=%d", WIFI_SSID, recs[0].rssi);
            wifi_config_t wc = { 0 };
            strcpy((char *)wc.sta.ssid, WIFI_SSID);
            strcpy((char *)wc.sta.password, WIFI_PASS);
            wc.sta.pmf_cfg.capable = true;         // 兼容 WPA2/WPA3 过渡模式热点
            esp_wifi_set_config(WIFI_IF_STA, &wc);
            esp_wifi_connect();
            s_snap.state = APP_WIFI_JOIN;
        } else {
            ESP_LOGI(TAG, "扫描未见 %s, %dmin 后重试", WIFI_SSID, SCAN_PERIOD_S / 60);
            s_snap.state = APP_WIFI_OFFLINE;       // 目标不在视野, 等下一轮
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_paused) {                            // 主动停机引发的断连, 不重连
            s_snap.state = APP_WIFI_OFFLINE;
        } else if (s_join_retries < JOIN_RETRY_MAX) {
            s_join_retries++;
            esp_wifi_connect();                    // 瞬断即时重连, 不等下轮扫描
            s_snap.state = APP_WIFI_JOIN;
        } else {
            s_join_retries = 0;
            s_snap.state = APP_WIFI_OFFLINE;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_join_retries = 0;
        s_snap.state = APP_WIFI_ONLINE;
        sntp_begin();
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            s_snap.rssi = ap.rssi;
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "在线 ip=" IPSTR " rssi=%d",
                 IP2STR(&e->ip_info.ip), s_snap.rssi);
    }
}

static void scan_timer_cb(void *arg) {
    (void)arg;
    if (s_paused) return;
    if (s_snap.state == APP_WIFI_ONLINE) {
        wifi_ap_record_t ap;                       // 在线时只刷新信号强度, 不扫(扫会瞬断)
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            s_snap.rssi = ap.rssi;
    } else if (s_snap.state == APP_WIFI_OFFLINE) {
        scan_start();
    }
}

// 立即刷新在线数据(主界面双击 OK 触发): 离线则马上重扫(不清重试计数上限的
// 语义不变, 事件流自动接续连接/NTP/GLM 首查); 已在线不扫(扫描会瞬断),
// 改为重启 SNTP 立即对时, 不等它的周期同步。
void app_wifi_resync(void) {
    if (s_snap.state == APP_WIFI_ONLINE) {
        if (s_sntp_started) {
            esp_netif_sntp_deinit();
            s_sntp_started = false;         // 让 sntp_begin 重新拉起
            sntp_begin();
        }
    } else {
        s_join_retries = 0;
        scan_start();
    }
}

void app_wifi_start(void) {
    strncpy((char *)s_filter, WIFI_SSID, sizeof(s_filter) - 1);
    s_snap.state = APP_WIFI_OFFLINE;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   // 凭据不落 NVS
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());             // STA_START 事件里触发首轮扫描

    const esp_timer_create_args_t args = {
        .name = "wifi_scan", .callback = scan_timer_cb,
    };
    esp_timer_create(&args, &s_scan_timer);
    esp_timer_start_periodic(s_scan_timer, SCAN_PERIOD_S * 1000000ULL);
}

void app_wifi_pause(void) {
    if (s_paused) return;
    s_paused = true;                    // 先置位: stop 派发的 DISCONNECTED 不再触发重连
    esp_wifi_stop();
    s_snap.state = APP_WIFI_OFFLINE;    // UI 显示 NO NET(真实, 射频已让渡给 BLE)
    ESP_LOGI(TAG, "射频让渡给 BLE");
}

void app_wifi_resume(void) {
    if (!s_paused) return;
    s_paused = false;
    s_join_retries = 0;
    esp_wifi_start();                   // STA_START 事件自动触发首轮扫描
    ESP_LOGI(TAG, "WiFi 恢复");
}

const app_wifi_snap_t *app_wifi_snap(void) { return &s_snap; }
