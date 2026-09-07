// main/app_glm.c -- GLM 套餐用量服务(独立任务, 不碰 UI, 快照只读供 ui_main 轮询)。
// 联网且 NTP 已同步后, 每 5min 查询智谱额度监控接口(与 shell 状态栏脚本同源):
//   GET https://open.bigmodel.cn/api/monitor/usage/quota/limit
//   Authorization 头直接放 API Key(无 Bearer 前缀); data.limits[] 数组里
//   number==5 为 5 小时窗, unit==6 && number==1 为周窗(字段含义见下方解析处)。
// 等 NTP 是因为 TLS 证书校验依赖系统时间, 未同步就握手必失败。
// 凭据来自 glm_secrets.h(git 不追踪, 模板见 glm_secrets.h.example)。
#include "app_glm.h"
#include "app_wifi.h"
#include "glm_secrets.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "glm";

#define GLM_URL        "https://open.bigmodel.cn/api/monitor/usage/quota/limit"
#define GLM_PERIOD_S   300    // 刷新周期(5min), 与 wifi 扫描节奏一致
#define GLM_POLL_S     5      // 在线检测轮询粒度(联网成功后最长延迟一个周期)
#define GLM_TIMEOUT_MS 8000   // 单次请求超时(shell 版 3s, 设备 TLS 握手放宽)

static app_glm_snap_t s_snap;

// 占位/空 Key 视为未配置, 不发请求(UI 显示 NO KEY, 免无效流量)
static bool key_ok(void) {
    return strlen(GLM_API_KEY) > 0 && strncmp(GLM_API_KEY, "YOUR_", 5) != 0;
}

static int64_t j_num(cJSON *obj, const char *key, int64_t def) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(v) ? (int64_t)v->valuedouble : def;
}

// 请求 + 收 body。返回读到的字节数(>=0), 负数为错误码; *status 出参 HTTP 状态码。
static int glm_fetch(char *buf, size_t bufsz, int *status) {
    esp_http_client_config_t cfg = {
        .url = GLM_URL,
        .timeout_ms = GLM_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,          // 头部缓冲: 响应带 Cookie/CDN 头时 512 默认值偏紧
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return -1;
    esp_http_client_set_method(h, HTTP_METHOD_GET);
    esp_http_client_set_header(h, "Authorization", GLM_API_KEY);
    esp_http_client_set_header(h, "Content-Type", "application/json");

    int nread = -2;
    *status = 0;
    if (esp_http_client_open(h, 0) == ESP_OK) {
        esp_http_client_fetch_headers(h);
        *status = esp_http_client_get_status_code(h);
        // 智谱接口是 chunked 响应(实测无 Content-Length), fetch_headers 返回值
        // 不可作收长依据; 依 esp_http_client_read 的 0/负值(EOF/超时)收尾
        int total = 0;
        while (total < (int)bufsz - 1) {
            int r = esp_http_client_read(h, buf + total, bufsz - 1 - total);
            if (r <= 0) break;
            total += r;
        }
        buf[total] = 0;
        nread = total;
    }
    esp_http_client_cleanup(h);       // 尽快释放 TLS 会话内存(~40K 峰值)
    return nread;
}

// 解析 data.limits[], 按窗口筛选条件填快照。字段: currentValue=已用, usage=总额,
// percentage=百分比, nextResetTime=重置时刻(epoch 毫秒)。
static bool glm_parse(const char *body) {
    cJSON *root = cJSON_Parse(body);
    if (!root) return false;
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *limits = data ? cJSON_GetObjectItem(data, "limits") : NULL;

    bool got5 = false, gotw = false;
    cJSON *it;
    cJSON_ArrayForEach(it, limits) {              // limits 为 NULL 时宏内自保护
        int64_t unit = j_num(it, "unit", 0), number = j_num(it, "number", 0);
        bool is5 = !got5 && number == 5;          // 5 小时窗
        bool isw = !gotw && unit == 6 && number == 1;   // 周窗
        if (!is5 && !isw) continue;

        int64_t used = j_num(it, "currentValue", -1), total = j_num(it, "usage", -1);
        int64_t pct64 = j_num(it, "percentage", -1);
        int pct = pct64 >= 0 ? (int)(pct64 + 0.5)
                             : (total > 0 ? (int)(used * 100 / total) : 0);  // 字段缺失则现算
        if (is5) {
            s_snap.has5 = true; s_snap.used5 = used; s_snap.total5 = total;
            s_snap.pct5 = pct; s_snap.reset5_ms = j_num(it, "nextResetTime", 0);
            got5 = true;
        } else {
            s_snap.hasw = true; s_snap.usedw = used; s_snap.totalw = total;
            s_snap.pctw = pct; s_snap.resetw_ms = j_num(it, "nextResetTime", 0);
            gotw = true;
        }
    }
    cJSON_Delete(root);
    return got5 || gotw;
}

static void glm_task(void *arg) {
    (void)arg;
    if (!key_ok()) {
        s_snap.state = APP_GLM_NOKEY;
        ESP_LOGW(TAG, "GLM_API_KEY 未配置, 用量区显示 NO KEY");
        vTaskDelete(NULL);
        return;
    }

    time_t last_try = 0;
    for (;;) {
        const app_wifi_snap_t *w = app_wifi_snap();
        time_t now = time(NULL);
        if (w->state == APP_WIFI_ONLINE && w->time_valid &&
            now - last_try >= GLM_PERIOD_S) {     // last_try=0 时首查立即触发; 失败也按周期退避
            last_try = now;
            s_snap.state = APP_GLM_FETCH;
            char *buf = malloc(2048);
            if (buf) {
                int status = 0;
                int n = glm_fetch(buf, 2048, &status);
                if (n < 0) {
                    s_snap.state = APP_GLM_ERR; s_snap.http_err = n;
                    ESP_LOGW(TAG, "请求失败 err=%d", n);
                } else if (status != 200) {
                    s_snap.state = APP_GLM_ERR; s_snap.http_err = status;
                    ESP_LOGW(TAG, "HTTP %d: %.120s", status, buf);
                } else if (!glm_parse(buf)) {
                    s_snap.state = APP_GLM_ERR; s_snap.http_err = -10;
                    ESP_LOGW(TAG, "响应无可用量窗口数据");
                } else {
                    s_snap.state = APP_GLM_OK; s_snap.http_err = 0;
                    s_snap.fetched_at = time(NULL);
                    ESP_LOGI(TAG, "5h %lld/%lld(%d%%) 周 %lld/%lld(%d%%)",
                             (long long)s_snap.used5, (long long)s_snap.total5, s_snap.pct5,
                             (long long)s_snap.usedw, (long long)s_snap.totalw, s_snap.pctw);
                }
                free(buf);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(GLM_POLL_S * 1000));
    }
}

void app_glm_start(void) {
    memset(&s_snap, 0, sizeof(s_snap));
    s_snap.state = APP_GLM_WAIT;
    // 8K 栈覆盖 TLS 握手 + cJSON; 任务常驻但绝大多数时间在 vTaskDelay
    xTaskCreate(glm_task, "glm", 8192, NULL, 4, NULL);
}

const app_glm_snap_t *app_glm_snap(void) { return &s_snap; }
