// main/app_ble_hid.c -- BLE HID 键盘(HOGP): GATT 服务表 + Passkey 配对 + 密码键入任务。
// 状态只经快照函数暴露(page_keys 轮询), host 回调上下文从不直接碰 UI/LVGL。
#include "app_ble_hid.h"
#include "app_ble.h"
#include "app_sensors.h"

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"      // ble_store_util_delete_peer(REPEAT_PAIRING 删旧 bond)
#include "os/os_mbuf.h"          // os_mbuf_append: 读回调标准追加法(自动支持 Long Read)

#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "app_ble_hid";

// ---- HID Report Map: 标准 8 键键盘(modifier + 保留 + 6 键数组 + 5 LED 位)。
// 不带 Report ID: 实测 bluez hog 驱动不剥 Report ID 前缀, 带前缀会整体错位 1 字节
// (前缀 0x01 落进 modifier 位 = 按住左Ctrl, 字符全变快捷键), 单报告键盘用无 ID 形式。
static const uint8_t k_report_map[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0xE0,       //   Usage Min (0xE0)
    0x29, 0xE7,       //   Usage Max (0xE7)
    0x15, 0x00,       //   Log Min (0)
    0x25, 0x01,       //   Log Max (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)      -> modifier 字节
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Const)          -> 保留字节
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Log Min (0)
    0x25, 0x65,       //   Log Max (101)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0x00,       //   Usage Min (0)
    0x29, 0x65,       //   Usage Max (101)
    0x81, 0x00,       //   Input (Data,Array)     -> 6 键位数组
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Min (1)
    0x29, 0x05,       //   Usage Max (5)
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x91, 0x02,       //   Output (Data,Var,Abs)  -> LED 位
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x91, 0x01,       //   Output (Const)         -> 补齐
    0xC0,             // End Collection
};

// HID Information: bcdHID=1.11 LE, 国家码 0, flags=0x02(normally connectable)
static const uint8_t k_hid_info[] = {0x11, 0x01, 0x00, 0x02};
// Report Reference 描述符: Report ID 0(无 ID, 与 Report Map 一致), Input 报告
static const uint8_t k_report_ref[] = {0x00, 0x01};
// PnP ID: 来源=2(USB-IF); VID/PID 取 V-USB 分配给开源项目的公共组合(0x16C0/0x05DF)。
// Windows 枚举 BLE HID 设备必读 PnP ID, 缺了配对后设备列表异常。
static const uint8_t k_pnp_id[] = {0x02, 0xC0, 0x16, 0xDF, 0x05, 0x00, 0x01};

// ---- ASCII(0x20-0x7E) → HID 键码(US 布局): 低 8 位=Usage, 高 8 位=modifier(0x02=左Shift) ----
#define SHIFT 0x0200
static const uint16_t k_ascii2hid[95] = {
    /*  */ 0x2C,
    /* ! */ SHIFT | 0x1E, /* " */ SHIFT | 0x34, /* # */ SHIFT | 0x20,
    /* $ */ SHIFT | 0x21, /* % */ SHIFT | 0x22, /* & */ SHIFT | 0x24,
    /* ' */ 0x34,
    /* ( */ SHIFT | 0x26, /* ) */ SHIFT | 0x27, /* * */ SHIFT | 0x25,
    /* + */ SHIFT | 0x2E,
    /* , */ 0x36, /* - */ 0x2D, /* . */ 0x37, /* / */ 0x38,
    /* 0 */ 0x27, /* 1 */ 0x1E, /* 2 */ 0x1F, /* 3 */ 0x20, /* 4 */ 0x21,
    /* 5 */ 0x22, /* 6 */ 0x23, /* 7 */ 0x24, /* 8 */ 0x25, /* 9 */ 0x26,
    /* : */ SHIFT | 0x33, /* ; */ 0x33,
    /* < */ SHIFT | 0x36, /* = */ 0x2E, /* > */ SHIFT | 0x37, /* ? */ SHIFT | 0x38,
    /* @ */ SHIFT | 0x1F,
    /* A */ SHIFT | 0x04, /* B */ SHIFT | 0x05, /* C */ SHIFT | 0x06,
    /* D */ SHIFT | 0x07, /* E */ SHIFT | 0x08, /* F */ SHIFT | 0x09,
    /* G */ SHIFT | 0x0A, /* H */ SHIFT | 0x0B, /* I */ SHIFT | 0x0C,
    /* J */ SHIFT | 0x0D, /* K */ SHIFT | 0x0E, /* L */ SHIFT | 0x0F,
    /* M */ SHIFT | 0x10, /* N */ SHIFT | 0x11, /* O */ SHIFT | 0x12,
    /* P */ SHIFT | 0x13, /* Q */ SHIFT | 0x14, /* R */ SHIFT | 0x15,
    /* S */ SHIFT | 0x16, /* T */ SHIFT | 0x17, /* U */ SHIFT | 0x18,
    /* V */ SHIFT | 0x19, /* W */ SHIFT | 0x1A, /* X */ SHIFT | 0x1B,
    /* Y */ SHIFT | 0x1C, /* Z */ SHIFT | 0x1D,
    /* [ */ 0x2F, /* \ */ 0x31, /* ] */ 0x30,
    /* ^ */ SHIFT | 0x23, /* _ */ SHIFT | 0x2D, /* ` */ 0x35,
    /* a */ 0x04, /* b */ 0x05, /* c */ 0x06, /* d */ 0x07, /* e */ 0x08,
    /* f */ 0x09, /* g */ 0x0A, /* h */ 0x0B, /* i */ 0x0C, /* j */ 0x0D,
    /* k */ 0x0E, /* l */ 0x0F, /* m */ 0x10, /* n */ 0x11, /* o */ 0x12,
    /* p */ 0x13, /* q */ 0x14, /* r */ 0x15, /* s */ 0x16, /* t */ 0x17,
    /* u */ 0x18, /* v */ 0x19, /* w */ 0x1A, /* x */ 0x1B, /* y */ 0x1C,
    /* z */ 0x1D,
    /* { */ SHIFT | 0x2F, /* | */ SHIFT | 0x31, /* } */ SHIFT | 0x30,
    /* ~ */ SHIFT | 0x35,
};

// ---- 连接/配对/订阅状态(host 回调写, UI 轮询读) ----
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_encrypted;
static volatile bool s_report_notify;          // Report 特征 CCC 已订阅
static volatile kkey_state_t s_state = KKEY_ST_OFF;
static volatile uint32_t s_passkey;
static volatile bool s_passkey_valid;
static volatile bool s_protocol_mode = true;   // true=report(默认) false=boot

static uint16_t s_report_val_h, s_boot_in_val_h, s_batt_val_h;

// ---- 键入任务 ----
static TaskHandle_t s_type_task;
static const char *s_type_text;                // type() 设好后才 notify, 无需 volatile
static bool s_type_enter;
static volatile bool s_typing;
static volatile int s_typed;
// 任务退出握手: shutdown 置 s_type_quit 并唤醒任务, 任务自删前 give 信号量
static volatile bool s_type_quit;
static SemaphoreHandle_t s_type_done;          // 一次性创建, 随 init 循环复用

static uint8_t batt_level(void) {
    int soc = app_sensors_snap()->soc;
    return (soc >= 0 && soc <= 100) ? (uint8_t)soc : 0;
}

// ---------------------------------------------------------------------------
// GATT access 回调
// ---------------------------------------------------------------------------
// 读回调统一用 os_mbuf_append: 超过 MTU 的值(如 Report Map)会被主机 Long Read 分片,
// 直接替换 ctxt->om 无法表达 offset, 导致 Malformed ATT read response(bluez 实测)。
static int read_flat(struct ble_gatt_access_ctxt *ctxt, const void *data, unsigned len) {
    int rc = os_mbuf_append(ctxt->om, data, len);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int acc_mfg_name(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    static const char name[] = "mingdedi";
    return read_flat(ctxt, name, sizeof name - 1);
}

static int acc_pnp(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    return read_flat(ctxt, k_pnp_id, sizeof k_pnp_id);
}

static int acc_batt(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    uint8_t v = batt_level();
    return read_flat(ctxt, &v, 1);
}

static int acc_hid_info(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    return read_flat(ctxt, k_hid_info, sizeof k_hid_info);
}

static int acc_report_map(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    return read_flat(ctxt, k_report_map, sizeof k_report_map);
}

static int acc_report(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    static uint8_t rpt[8] = {0};                              // 当前键盘报告(无 Report ID)
    return read_flat(ctxt, rpt, sizeof rpt);
}

static int acc_report_ref(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    return read_flat(ctxt, k_report_ref, sizeof k_report_ref);
}

static int acc_boot_in(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    static uint8_t rpt[8] = {0};                              // boot 报告(无 Report ID)
    return read_flat(ctxt, rpt, sizeof rpt);
}

static int acc_ctrl_point(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg; (void)ctxt;  // suspend/唤醒, 忽略
    return 0;
}

static int acc_proto_mode(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t mode = 0x01;                                  // report 协议
        return read_flat(ctxt, &mode, 1);
    }
    uint8_t v = 1;
    ble_hs_mbuf_to_flat(ctxt->om, &v, 1, NULL);               // 主机切 boot(0) 只记录
    s_protocol_mode = v ? true : false;
    return 0;
}

static int acc_boot_out(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg; (void)ctxt;  // LED 状态, 忽略
    return 0;
}

// ---------------------------------------------------------------------------
// GATT 静态表: Device Information + Battery + HID(NimBLE 对 NOTIFY 特征自动加 CCC)
// ---------------------------------------------------------------------------
static const struct ble_gatt_svc_def g_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),                    // Device Information
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A29),              // Manufacturer Name
              .access_cb = acc_mfg_name, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A50),              // PnP ID
              .access_cb = acc_pnp, .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),                    // Battery
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A19),              // Battery Level
              .access_cb = acc_batt,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_batt_val_h },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),                    // HID
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A4A),              // HID Information
              .access_cb = acc_hid_info, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4B),              // Report Map
              .access_cb = acc_report_map, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4C),              // HID Control Point
              .access_cb = acc_ctrl_point, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4E),              // Protocol Mode
              .access_cb = acc_proto_mode,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4D),              // Report (input, 键盘)
              .access_cb = acc_report,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_report_val_h,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908),        // Report Reference
                    .access_cb = acc_report_ref,
                    .att_flags = BLE_ATT_F_READ },
                  { 0 },
              } },
            { .uuid = BLE_UUID16_DECLARE(0x2A22),              // Boot Keyboard Input
              .access_cb = acc_boot_in,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_boot_in_val_h },
            { .uuid = BLE_UUID16_DECLARE(0x2A32),              // Boot Keyboard Output(LED)
              .access_cb = acc_boot_out,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
            { 0 },
        },
    },
    { 0 },
};

// ---------------------------------------------------------------------------
// 键入任务: 每字符 press/release 两报告, 间隔略大于协商连接间隔(15ms)防挤丢
// ---------------------------------------------------------------------------
// notify_custom 收 mbuf 且无论成败都消耗之
static void notify_flat(uint16_t conn, uint16_t handle, const void *data, unsigned len) {
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) ble_gatts_notify_custom(conn, handle, om);
}

static void send_report(uint16_t kc) {
    // 无 Report ID: 报文 = [modifier][保留][6键] 标准 8 字节
    uint8_t rpt[8] = {(uint8_t)(kc >> 8), 0, (uint8_t)(kc & 0xFF), 0, 0, 0, 0, 0};
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_report_notify) {
        s_typing = false;                                      // 断连/失订阅即中止
        return;
    }
    notify_flat(s_conn, s_report_val_h, rpt, sizeof rpt);
}

static void type_task_fn(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_type_quit) break;                                 // shutdown 唤醒: 安全点退出
        const char *p = s_type_text;
        while (s_typing && *p) {
            uint16_t kc = 0;
            if (*p == '\n') kc = 0x28;                         // Return
            else if ((uint8_t)*p >= 0x20 && (uint8_t)*p <= 0x7E)
                kc = k_ascii2hid[(uint8_t)*p - 0x20];
            if (kc) {
                send_report(kc);                               // 按下
                vTaskDelay(pdMS_TO_TICKS(12));
                send_report(0);                                // 抬起
                vTaskDelay(pdMS_TO_TICKS(12));
            }
            s_typed++;
            p++;
        }
        if (s_typing && s_type_enter) {                        // 尾部补回车
            send_report(0x28);
            vTaskDelay(pdMS_TO_TICKS(12));
            send_report(0);
        }
        s_typing = false;
    }
    xSemaphoreGive(s_type_done);                // 告知 shutdown 等待者, 再无 host 调用
    vTaskDelete(NULL);                          // 自删(TCB/栈由 idle 任务回收)
}

// ---------------------------------------------------------------------------
// GAP 事件处理(app_ble.c 转发); 非 0 返回值=事件已裁决(REPEAT_PAIRING)
// ---------------------------------------------------------------------------
static void session_clear(void) {
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_encrypted = false;
    s_report_notify = false;
    s_passkey_valid = false;
    s_typing = false;                                          // 断连即中止发射
    s_state = (app_ble_adv_mode() == APP_BLE_ADV_HID) ? KKEY_ST_ADV : KKEY_ST_OFF;
}

int app_ble_hid_on_gap(struct ble_gap_event *ev) {
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) return 0;                 // 连接失败由广播层处理
        if (app_ble_adv_mode() != APP_BLE_ADV_HID) return 0;   // RADIO 演示连接不归 HID 管
        s_conn = ev->connect.conn_handle;
        s_encrypted = false;
        s_report_notify = false;
        s_state = KKEY_ST_PAIRING;                             // 连上即待配对(已 bond 的直接走加密)
        {
            // 键盘连接参数: 15-30ms(低打字延迟); timeout 须 > (1+latency)*max_itvl*2
            struct ble_gap_upd_params up = {
                .itvl_min = 12, .itvl_max = 24, .latency = 2,
                .supervision_timeout = 200,
                .min_ce_len = 0, .max_ce_len = 0,
            };
            ble_gap_update_params(s_conn, &up);
            // HID 惯例: 主动发起配对; 已 bond 终端会直接恢复加密(ENC_CHANGE)
            ble_gap_security_initiate(s_conn);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        if (ev->disconnect.conn.conn_handle != s_conn) return 0;
        session_clear();                                       // 广播重启由 app_ble.c 负责
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.conn_handle != s_conn) return 0;
        if (ev->enc_change.status == 0) {
            s_encrypted = true;
            s_state = KKEY_ST_READY;
            ESP_LOGI(TAG, "encrypted, ready");
        } else {
            ESP_LOGW(TAG, "encrypt fail, status=%d", ev->enc_change.status);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (ev->passkey.conn_handle != s_conn) return 0;
        if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io = { .action = BLE_SM_IOACT_DISP };
            io.passkey = esp_random() % 1000000;
            s_passkey = io.passkey;
            s_passkey_valid = true;
            s_state = KKEY_ST_PAIRING;
            ble_sm_inject_io(s_conn, &io);
            ESP_LOGI(TAG, "passkey: %06u", (unsigned)io.passkey);
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.conn_handle != s_conn) return 0;
        if (ev->subscribe.attr_handle == s_report_val_h)
            s_report_notify = ev->subscribe.cur_notify;
        if (ev->subscribe.attr_handle == s_batt_val_h && ev->subscribe.cur_notify) {
            uint8_t b = batt_level();                          // 订阅电量即推一次
            notify_flat(s_conn, s_batt_val_h, &b, 1);
        }
        if (s_encrypted && s_report_notify) s_state = KKEY_ST_READY;
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (ev->repeat_pairing.conn_handle != s_conn) return 0;
        {
            // 换终端重配对(密钥设备常客): 删旧 bond 后让新配对继续
            struct ble_gap_conn_desc cd;
            if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &cd) == 0)
                ble_store_util_delete_peer(&cd.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
esp_err_t app_ble_hid_register(void) {
    int rc = ble_gatts_count_cfg(g_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(g_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt register rc=%d", rc);
        return ESP_FAIL;
    }
    if (!s_type_done) s_type_done = xSemaphoreCreateBinary();
    s_type_quit = false;                       // 新任务生命周期开始, 复位退出标志
    if (xTaskCreate(type_task_fn, "hidtype", 3072, NULL, 4, &s_type_task) != pdPASS) {
        ESP_LOGE(TAG, "hidtype task create failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_ble_hid_task_stop(void) {
    if (!s_type_task) return;
    s_type_quit = true;
    xTaskNotify(s_type_task, 0, eIncrement);    // 唤醒挂在 ulTaskNotifyTake 的任务
    if (xSemaphoreTake(s_type_done, pdMS_TO_TICKS(1000)) != pdTRUE)
        ESP_LOGW(TAG, "hidtype 1s 内未退出, 继续拆 host");
    s_type_task = NULL;                         // 句柄交还, register 下圈重建
}

kkey_state_t app_ble_hid_state(void)  { return s_state; }
uint32_t app_ble_hid_passkey(void)    { return s_passkey; }
bool app_ble_hid_passkey_valid(void)  { return s_passkey_valid; }
bool app_ble_hid_typing(void)         { return s_typing; }
int app_ble_hid_typed(void)           { return s_typed; }

bool app_ble_hid_ready(void) {
    return s_state == KKEY_ST_READY && s_encrypted && s_report_notify &&
           s_conn != BLE_HS_CONN_HANDLE_NONE;
}

bool app_ble_hid_type(const char *text, bool enter) {
    if (!text || !app_ble_hid_ready() || s_typing) return false;
    s_type_text = text;
    s_type_enter = enter;
    s_typed = 0;
    s_typing = true;
    xTaskNotifyGive(s_type_task);
    return true;
}

void app_ble_hid_session_reset(void) { session_clear(); }

void app_ble_hid_disconnect(void) {
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn, 0x13);                       // 0x13=远端用户主动断开
    }
    session_clear();
}
