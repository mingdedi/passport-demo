// main/app_glm.c -- GLM 套餐用量服务(esp_timer 调度 + 一次性查询任务)。
// 联网且 NTP 已同步后, 每 5min 查询智谱额度监控接口(与 shell 状态栏脚本同源):
//   GET https://open.bigmodel.cn/api/monitor/usage/quota/limit
//   Authorization 头直接放 API Key(无 Bearer 前缀); data.limits[] 数组里
//   number==5 为 5 小时窗, unit==6 && number==1 为周窗(字段含义见下方解析处)。
// 等 NTP 是因为 TLS 证书校验依赖系统时间, 未同步就握手必失败。
// 按需内存模型(2026-09-08): 查询任务按次创建、查完自删, 8K 栈 + TLS 峰值
// (~40K)只在查询窗口存在; 常驻任务会占住 8K, 令 BLE(需 ~70K)进页时起不来。
// BLE 页活跃期间查询失败置 ERR(LOW MEM), 退页后下个调度点自动恢复。
// 凭据来自 glm_secrets.h(git 不追踪, 模板见 glm_secrets.h.example)。
#include "app_glm.h"
#include "app_wifi.h"
#include "glm_secrets.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "glm";

#define GLM_URL        "https://open.bigmodel.cn/api/monitor/usage/quota/limit"
#define GLM_PERIOD_S   300    // 查询周期(5min)
#define GLM_TICK_S     30     // 调度粒度: 就绪后最长一个 tick 触发(首查/重试)
#define GLM_TIMEOUT_MS 8000   // 单次请求超时(shell 版 3s, 设备 TLS 握手放宽)

static app_glm_snap_t s_snap;
static esp_timer_handle_t s_timer;
static volatile bool s_querying;          // 查询任务存活(防重入)
static volatile bool s_force;             // 双击 OK 的立即查请求(可跨 tick 存活)
static time_t s_last_try;

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

// 一次性查询任务: 查完自删, 内存峰值只在任务存活期出现
static void glm_query_task(void *arg) {
    (void)arg;
    char *buf = malloc(2048);
    if (!buf) {                                // BLE 页活跃挤占时的典型失败
        s_snap.state = APP_GLM_ERR; s_snap.http_err = -21;
        ESP_LOGW(TAG, "响应缓冲分配失败 free=%u", (unsigned)esp_get_free_heap_size());
    } else {
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
    s_querying = false;
    vTaskDelete(NULL);
}

// 调度入口(timer tick / 立即刷新共用): 未就绪静默等下个 tick, 就绪且过了
// 节流窗(或 force)才起任务。tick 上下文(esp_timer 栈小)只做判断+建任务。
static void schedule_query(bool force) {
    if (s_querying) return;
    const app_wifi_snap_t *w = app_wifi_snap();
    if (w->state != APP_WIFI_ONLINE || !w->time_valid) return;
    time_t now = time(NULL);
    if (!force && now - s_last_try < GLM_PERIOD_S) return;
    s_last_try = now;
    s_snap.state = APP_GLM_FETCH;
    s_querying = true;
    if (xTaskCreate(glm_query_task, "glm", 8192, NULL, 4, NULL) != pdPASS) {
        s_querying = false;
        s_snap.state = APP_GLM_ERR; s_snap.http_err = -20;
        ESP_LOGE(TAG, "查询任务创建失败 free=%u", (unsigned)esp_get_free_heap_size());
    }
}

static void timer_cb(void *arg) {
    (void)arg;
    bool force = s_force;
    s_force = false;
    schedule_query(force);
}

void app_glm_start(void) {
    memset(&s_snap, 0, sizeof(s_snap));
    if (!key_ok()) {
        s_snap.state = APP_GLM_NOKEY;
        ESP_LOGW(TAG, "GLM_API_KEY 未配置, 用量区显示 NO KEY");
        return;
    }
    s_snap.state = APP_GLM_WAIT;
    const esp_timer_create_args_t args = {
        .name = "glm_sched", .callback = timer_cb,
    };
    esp_timer_create(&args, &s_timer);
    esp_timer_start_periodic(s_timer, GLM_TICK_S * 1000000ULL);
}

void app_glm_refresh_now(void) {
    // 立即尝试一次; 若恰未就绪(离线/时间未同步), s_force 留给下个 tick 兜底
    s_force = true;
    schedule_query(true);
}

const app_glm_snap_t *app_glm_snap(void) { return &s_snap; }
