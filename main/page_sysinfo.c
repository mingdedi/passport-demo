// main/page_sysinfo.c -- 芯片/内存/温度/复位原因 实时面板(13ch 网格)。
#include "ui.h"
#include "app_sensors.h"

#include "esp_system.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include <stdio.h>
#include <stdlib.h>

#define K_X 2     // 键列(<=5ch)
#define V_X 86    // 值列(<=8ch): 86+128=214

static lv_obj_t *l_uptime, *l_temp, *l_heap1, *l_heap2, *l_heap3, *heap_bar;
static lv_timer_t *s_timer;

static const char *reset_reason_str(void) {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWER-ON";
    case ESP_RST_SW:       return "SOFTWARE";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT-WDT";
    case ESP_RST_TASK_WDT: return "TASK-WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLP";
    default:               return "UNKNOWN";
    }
}

static lv_obj_t *kv_make(lv_obj_t *root, const char *key, const char *val, int y, uint32_t vcol) {
    lv_obj_t *k = ui_label_make(root, key);
    lv_obj_set_style_text_color(k, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(k, K_X, y);
    lv_obj_t *v = ui_label_make(root, val ? val : "");
    lv_obj_set_style_text_color(v, lv_color_hex(vcol), 0);
    lv_obj_set_pos(v, V_X, y);
    return v;
}

static void timer_cb(lv_timer_t *t) {
    (void)t;
    const app_sensors_snap_t *s = app_sensors_snap();
    uint32_t h = s->uptime_s / 3600, m = (s->uptime_s % 3600) / 60, sec = s->uptime_s % 60;
    lv_label_set_text_fmt(l_uptime, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)sec);

    // LVGL 内置 printf 不支持 %f(LV_USE_FLOAT 未开), 用 0.1C 整数手工拼
    if (s->temp_ok) {
        int t10 = (int)(s->temp_c * 10 + (s->temp_c >= 0 ? 0.5 : -0.5));
        lv_label_set_text_fmt(l_temp, "%d.%d C", t10 / 10, abs(t10 % 10));
    } else {
        lv_label_set_text(l_temp, "N/A");
    }

    lv_label_set_text_fmt(l_heap1, "%3dK", (int)(s->heap_free / 1024));
    lv_label_set_text_fmt(l_heap2, "%3dK", (int)(s->heap_min / 1024));
    lv_label_set_text_fmt(l_heap3, "%3dK", (int)(s->heap_largest / 1024));
    lv_bar_set_value(heap_bar, (int32_t)(s->heap_free * 100 / (400 * 1024)), LV_ANIM_ON);
}

static void enter(lv_obj_t *root) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    const esp_app_desc_t *app = esp_app_get_description();

    int y = 2;
    kv_make(root, "CHIP",  "ESP32-C3",  y, UI_ACC);  y += 17;
    kv_make(root, "REV",   "V1.1 QFN",  y, UI_ACC);  y += 17;
    // MAC 12 字符分两行(键列 + 缩进续行)
    kv_make(root, "MAC",   NULL,        y, UI_ACC);
    lv_obj_t *mac1 = ui_label_make(root, "");
    lv_obj_set_style_text_color(mac1, lv_color_hex(UI_ACC), 0);
    lv_label_set_text_fmt(mac1, "%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3]);
    lv_obj_set_pos(mac1, V_X, y);
    lv_obj_t *mac2 = ui_label_make(root, "");
    lv_obj_set_style_text_color(mac2, lv_color_hex(UI_ACC), 0);
    lv_label_set_text_fmt(mac2, "    %02X%02X", mac[4], mac[5]);
    lv_obj_set_pos(mac2, V_X, y + 17);                 y += 34;
    kv_make(root, "FLASH", "8MB",       y, UI_ACC);    y += 17;
    kv_make(root, "RESET", reset_reason_str(), y, UI_ACC); y += 17;
    char ver[12];
    snprintf(ver, sizeof(ver), "%.5s", app->version);
    kv_make(root, "APP",   ver,         y, UI_ACC);    y += 24;

    l_uptime = kv_make(root, "UP",   "00:00:00", y, UI_ACC);  y += 17;
    l_temp   = kv_make(root, "TEMP",   "--",     y, UI_ACC);  y += 22;

    // HEAP 块: 表头独占一行, FREE/MIN/BIG 走键值行(键列 <=5ch, 值列 "170K" <=8ch)
    lv_obj_t *hk = ui_label_make(root, "HEAP");
    lv_obj_set_style_text_color(hk, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(hk, K_X, y);                       y += 17;
    l_heap1 = kv_make(root, "FREE", "", y, UI_INK2);  y += 17;
    l_heap2 = kv_make(root, "MIN",  "", y, UI_INK2);  y += 17;
    l_heap3 = kv_make(root, "BIG",  "", y, UI_INK2);  y += 20;

    heap_bar = lv_bar_create(root);
    lv_obj_set_size(heap_bar, 208, 10);
    lv_obj_set_pos(heap_bar, 2, y);
    lv_bar_set_range(heap_bar, 0, 100);
    lv_obj_set_style_bg_color(heap_bar, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_border_color(heap_bar, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(heap_bar, 1, 0);
    lv_obj_set_style_radius(heap_bar, 0, 0);
    lv_obj_set_style_bg_color(heap_bar, lv_color_hex(UI_INK2), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(heap_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    timer_cb(NULL);
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
}

const ui_page_t page_sysinfo = { .id = "SYSTEM", .enter = enter, .exit = page_exit };
