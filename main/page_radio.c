// main/page_radio.c -- NimBLE 演示: 广播状态 + ADV 包摘要 + 手机可发现。
#include "ui.h"
#include "app_ble.h"
#include "app_sensors.h"

#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>

static lv_obj_t *s_state_lbl, *s_dot;
static lv_obj_t *s_cnt_lbl, *s_hex_lbl, *s_hint;
static lv_timer_t *s_timer;

static void timer_cb(lv_timer_t *t) {
    (void)t;
    int st = app_ble_state();
    switch (st) {
    case 2:
        lv_label_set_text(s_state_lbl, "CONNECTED");
        lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(UI_ACC), 0);
        lv_obj_set_style_bg_color(s_dot, lv_color_hex(UI_ACC), 0);
        break;
    case 1:
        lv_label_set_text(s_state_lbl, "ADV RUN");
        lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(UI_INK), 0);
        lv_obj_set_style_bg_color(s_dot, lv_color_hex(UI_INK), 0);
        break;
    default:
        lv_label_set_text(s_state_lbl, "BLE OFF");
        lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(UI_DARK), 0);
        lv_obj_set_style_bg_color(s_dot, lv_color_hex(UI_DARK), 0);
    }

    uint32_t n = app_ble_updates();
    const app_sensors_snap_t *s = app_sensors_snap();
    int soc = s->soc < 0 ? 0 : s->soc;
    lv_label_set_text_fmt(s_cnt_lbl, "TX %u", (unsigned)n);
    lv_label_set_text_fmt(s_hex_lbl,
        "ADV 31B:\n"
        "02 01 06\n"
        "FF BEEF %02X%02X\n"
        "SR = NAME",
        (unsigned)(n & 0xFF), (unsigned)soc);

    if (st == 0) lv_label_set_text(s_hint, "BLE OFFLINE");
    else         lv_label_set_text(s_hint, "SCAN:PASSPORT");
}

static void enter(lv_obj_t *root) {
    lv_obj_t *t = ui_label_make(root, "NIMBLE BLE5");
    lv_obj_set_style_text_color(t, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(t, 2, 2);

    // 状态点 + 状态行
    s_dot = lv_obj_create(root);
    lv_obj_set_size(s_dot, 10, 10);
    lv_obj_set_pos(s_dot, 4, 24);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_state_lbl = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(s_state_lbl, 22, 20);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    lv_obj_t *mk = ui_label_make(root, "BDADDR");
    lv_obj_set_style_text_color(mk, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(mk, 2, 44);
    lv_obj_t *m = ui_label_make(root, "");
    lv_label_set_text_fmt(m, "%02X%02X%02X%02X%02X%02X",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    lv_obj_set_style_text_color(m, lv_color_hex(UI_ACC), 0);
    lv_obj_set_pos(m, 2, 62);

    lv_obj_t *iv = ui_label_make(root, "CONN ENABLED");
    lv_obj_set_style_text_color(iv, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(iv, 2, 84);

    s_cnt_lbl = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_cnt_lbl, lv_color_hex(UI_INK2), 0);
    lv_obj_set_pos(s_cnt_lbl, 2, 104);

    lv_obj_t *panel = ui_term_panel(root, 2, 124, 208, 88);
    s_hex_lbl = ui_label_make(panel, "");
    lv_obj_set_style_text_color(s_hex_lbl, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(s_hex_lbl, 0, 0);
    lv_label_set_text(s_hex_lbl, "...");

    s_hint = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(s_hint, 2, 220);

    // 初始化 NimBLE(首次 ~100ms, host 任务起来后 sync 回调自动开广播)
    if (app_ble_start() != ESP_OK) {
        ESP_LOGE("page_radio", "ble start failed");
    }

    timer_cb(NULL);
    s_timer = lv_timer_create(timer_cb, 500, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    app_ble_shutdown();          // 退页全量释放 BLE(~70K), 重进页时自动重建
}

const ui_page_t page_radio = { .id = "RADIO", .enter = enter, .exit = page_exit };
