// main/page_about.c -- 关于 + 数字雨彩蛋 + 深睡/重启演示(双击确认)。
#include "ui.h"
#include "bsp_display.h"

#include "esp_random.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include <stdio.h>

#define RAIN_COLS 12
#define RAIN_ROWS 4

static lv_obj_t *s_rain[RAIN_COLS][RAIN_ROWS];
static lv_obj_t *s_confirm;
static lv_timer_t *s_timer;
static int s_pending;                 // 0 无 1 sleep 2 reboot
static int s_confirm_ticks;

static const char RAIN_CHARS[] = "0123456789ABCDEF";

static void rain_cb(lv_timer_t *t) {
    (void)t;
    for (int c = 0; c < RAIN_COLS; c++) {
        int drop = (int)(esp_random() % RAIN_ROWS);
        for (int r = 0; r < RAIN_ROWS; r++) {
            if (r == drop)
                lv_obj_set_style_text_color(s_rain[c][r], lv_color_hex(UI_INK), 0);
            else if (r == (drop + 1) % RAIN_ROWS)
                lv_obj_set_style_text_color(s_rain[c][r], lv_color_hex(UI_DIM), 0);
            else
                lv_obj_set_style_text_color(s_rain[c][r], lv_color_hex(UI_DARK), 0);

            if (esp_random() % 4 == 0)
                lv_label_set_text_fmt(s_rain[c][r], "%c",
                                      RAIN_CHARS[esp_random() % 16]);
        }
    }

    if (s_pending) {
        s_confirm_ticks++;
        if (s_confirm_ticks > 3) {        // ~2.4s 未二次确认 -> 取消
            s_pending = 0;
            lv_label_set_text(s_confirm, "CANCELED");
        }
    }
}

static void do_sleep(void) {
    bsp_display_backlight(0);
    esp_deep_sleep(10 * 1000000LL);       // 定时器 10s 唤醒(重启固件)
}

static void do_reboot(void) {
    esp_restart();
}

static void key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;

    if (s_pending == 0) {
        if (btn == BSP_BTN_OK)   { s_pending = 1; s_confirm_ticks = 0; lv_label_set_text(s_confirm, "SLEEP? OK 2X"); }
        if (btn == BSP_BTN_DOWN) { s_pending = 2; s_confirm_ticks = 0; lv_label_set_text(s_confirm, "REBOOT? DN 2X"); }
        return;
    }
    // 二次确认
    if (s_pending == 1 && btn == BSP_BTN_OK)   { lv_label_set_text(s_confirm, "GOOD NIGHT.."); do_sleep(); }
    if (s_pending == 2 && btn == BSP_BTN_DOWN) { lv_label_set_text(s_confirm, "BYE...");       do_reboot(); }
}

static void enter(lv_obj_t *root) {
    lv_obj_t *logo = ui_label_make(root, "PASSPORT_OS");
    lv_obj_set_style_text_color(logo, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(logo, 2, 2);

    lv_obj_t *ver = ui_label_make(root, "ESP32-C3 TERM");
    lv_obj_set_style_text_color(ver, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(ver, 2, 20);

    const char *rows[] = {
        "UI LVGL 9.5",
        "RADIO NIMBLE",
        "SDK IDF 5.5.5",
        "BSP FOLOTOY",
        "LICENSE MIT",
    };
    int y = 44;
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        lv_obj_t *l = ui_label_make(root, rows[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(UI_INK2), 0);
        lv_obj_set_pos(l, 2, y);
        y += 17;
    }

    // 操作区
    lv_obj_t *h1 = ui_label_make(root, "OK SLEEP10S");
    lv_obj_set_style_text_color(h1, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(h1, 2, 136);

    lv_obj_t *h2 = ui_label_make(root, "DN REBOOT");
    lv_obj_set_style_text_color(h2, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(h2, 2, 153);

    s_confirm = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_confirm, lv_color_hex(UI_WARN), 0);
    lv_obj_set_pos(s_confirm, 2, 172);

    // 底部数字雨彩蛋(12 列 x 4 行)
    for (int c = 0; c < RAIN_COLS; c++)
        for (int r = 0; r < RAIN_ROWS; r++) {
            s_rain[c][r] = lv_label_create(root);
            lv_obj_set_style_text_font(s_rain[c][r], &lv_font_unscii_16, 0);
            lv_obj_set_style_text_color(s_rain[c][r], lv_color_hex(UI_DARK), 0);
            lv_label_set_text_fmt(s_rain[c][r], "%c", RAIN_CHARS[esp_random() % 16]);
            lv_obj_set_pos(s_rain[c][r], 2 + c * 16, 186 + r * 16);
        }

    s_pending = 0;
    rain_cb(NULL);
    s_timer = lv_timer_create(rain_cb, 600, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_pending = 0;
}

const ui_page_t page_about = { .id = "ABOUT", .enter = enter, .exit = page_exit, .key = key };
