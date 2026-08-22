// main/page_display.c -- 屏幕能力演示: 彩条/灰阶/彩虹流动/滚动字幕/闪块矩阵 + 背光滑窗。
#include "ui.h"
#include "bsp_display.h"

#include "esp_random.h"
#include <stdio.h>
#include <string.h>

enum { MODE_COLORS = 0, MODE_GRAY, MODE_RAINBOW, MODE_SCROLL, MODE_MATRIX, MODE_COUNT };

static const char *MODE_NAMES[] = { "COLORBAR", "GRAYRAMP", "RAINBOW", "SCROLL", "MATRIX" };

#define STAGE_W 208
#define STAGE_H 150

static lv_obj_t *s_stage;                 // 演示区
static lv_obj_t *s_mode_lbl, *s_bl_lbl;
static lv_timer_t *s_timer;
static int s_mode = MODE_COLORS;
static int s_bl = 100;
static lv_obj_t *s_fx[16];                // 彩虹/矩阵复用的块
static lv_obj_t *s_scroll1, *s_scroll2;

static void stage_clear(void) {
    lv_obj_clean(s_stage);
    memset(s_fx, 0, sizeof(s_fx));
    s_scroll1 = s_scroll2 = NULL;
}

static void block_make(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
}

static void build_colors(void) {
    static const uint32_t c[8] = {
        0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00,
        0x00FFFF, 0x0080FF, 0x8000FF, 0xFFFFFF
    };
    static const char n[8] = { 'R', 'O', 'Y', 'G', 'C', 'B', 'V', 'W' };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *b = lv_obj_create(s_stage);
        lv_obj_set_size(b, STAGE_W / 8, STAGE_H);
        lv_obj_set_pos(b, i * (STAGE_W / 8), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(c[i]), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_unscii_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
        lv_label_set_text_fmt(l, "%c", n[i]);
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}

static void build_gray(void) {
    for (int i = 0; i < 16; i++) {
        uint8_t v = (uint8_t)(i * 255 / 15);
        block_make(s_stage, i * (STAGE_W / 16), 0, STAGE_W / 16 + 1, STAGE_H,
                   (uint32_t)v << 16 | (uint32_t)v << 8 | v);
    }
}

static void build_rainbow(void) {
    for (int i = 0; i < 16; i++) {
        s_fx[i] = lv_obj_create(s_stage);
        lv_obj_set_size(s_fx[i], STAGE_W / 16 + 1, STAGE_H);
        lv_obj_set_pos(s_fx[i], i * (STAGE_W / 16), 0);
        lv_obj_set_style_bg_opa(s_fx[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_fx[i], 0, 0);
        lv_obj_set_style_radius(s_fx[i], 0, 0);
        lv_obj_clear_flag(s_fx[i], LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void build_scroll(void) {
    s_scroll1 = lv_label_create(s_stage);
    lv_obj_set_style_text_font(s_scroll1, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(s_scroll1, lv_color_hex(UI_DARK), 0);
    lv_label_set_text(s_scroll1, "PASSPORT OS");
    lv_obj_set_pos(s_scroll1, 2, 68);

    s_scroll2 = lv_label_create(s_stage);
    lv_obj_set_style_text_font(s_scroll2, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(s_scroll2, lv_color_hex(UI_INK), 0);
    lv_label_set_text(s_scroll2, "PASSPORT OS");
    lv_obj_set_pos(s_scroll2, 0, 66);
}

static void build_matrix(void) {
    // 13x6 随机闪块
    for (int i = 0; i < 16; i++) {
        s_fx[i] = lv_obj_create(s_stage);
        lv_obj_set_size(s_fx[i], STAGE_W / 13 + 1, STAGE_H / 6 + 1);
        lv_obj_set_style_bg_color(s_fx[i], lv_color_hex(UI_INK), 0);
        lv_obj_set_style_border_width(s_fx[i], 0, 0);
        lv_obj_set_style_radius(s_fx[i], 0, 0);
        lv_obj_clear_flag(s_fx[i], LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void rebuild(void) {
    stage_clear();
    switch (s_mode) {
    case MODE_COLORS:  build_colors(); break;
    case MODE_GRAY:    build_gray(); break;
    case MODE_RAINBOW: build_rainbow(); break;
    case MODE_SCROLL:  build_scroll(); break;
    case MODE_MATRIX:  build_matrix(); break;
    }
    lv_label_set_text(s_mode_lbl, MODE_NAMES[s_mode]);
    lv_label_set_text_fmt(s_bl_lbl, "BL %3d%%", s_bl);
}

static uint16_t s_hue = 0;

static void timer_cb(lv_timer_t *t) {
    (void)t;
    if (s_mode == MODE_RAINBOW) {
        s_hue += 6;
        for (int i = 0; i < 16; i++)
            lv_obj_set_style_bg_color(s_fx[i],
                lv_color_hsv_to_rgb((s_hue + i * 22) % 360, 90, 80), 0);
    } else if (s_mode == MODE_MATRIX) {
        // 随机挪动 16 个块 + 随机透明度
        for (int i = 0; i < 16; i++) {
            if (esp_random() % 3) continue;
            lv_obj_set_pos(s_fx[i], (int32_t)(esp_random() % 13) * (STAGE_W / 13),
                           (int32_t)(esp_random() % 6) * (STAGE_H / 6));
            lv_obj_set_style_bg_opa(s_fx[i], LV_OPA_20 + esp_random() % LV_OPA_80, 0);
        }
    } else if (s_mode == MODE_SCROLL) {
        static int32_t x = 0;
        x = (x + 4) % (11 * UI_CH_W + 40);
        int32_t px = STAGE_W - x;
        if (px < -176) px = STAGE_W;
        lv_obj_set_x(s_scroll1, px + 2);
        lv_obj_set_x(s_scroll2, px);
    }
}

static void enter(lv_obj_t *root) {
    s_mode_lbl = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_mode_lbl, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(s_mode_lbl, 2, 2);

    s_bl_lbl = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_bl_lbl, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(s_bl_lbl, 2, 20);

    s_stage = lv_obj_create(root);
    lv_obj_set_size(s_stage, STAGE_W, STAGE_H);
    lv_obj_set_pos(s_stage, 2, 40);
    lv_obj_set_style_bg_color(s_stage, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_border_color(s_stage, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(s_stage, 1, 0);
    lv_obj_set_style_radius(s_stage, 0, 0);
    lv_obj_set_style_pad_all(s_stage, 0, 0);
    lv_obj_clear_flag(s_stage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hint1 = ui_label_make(root, "OK:NEXT");
    lv_obj_set_style_text_color(hint1, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(hint1, 2, 196);

    lv_obj_t *hint2 = ui_label_make(root, "U/D:LIGHT");
    lv_obj_set_style_text_color(hint2, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(hint2, 2, 214);

    rebuild();
    s_timer = lv_timer_create(timer_cb, 60, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    bsp_display_backlight(100);
}

static void key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) { s_mode = (s_mode + 1) % MODE_COUNT; rebuild(); }
    if (btn == BSP_BTN_UP)   { s_bl = s_bl >= 100 ? 100 : s_bl + 10; bsp_display_backlight((uint8_t)s_bl); lv_label_set_text_fmt(s_bl_lbl, "BL %3d%%", s_bl); }
    if (btn == BSP_BTN_DOWN) { s_bl = s_bl <= 10 ? 10 : s_bl - 10;  bsp_display_backlight((uint8_t)s_bl); lv_label_set_text_fmt(s_bl_lbl, "BL %3d%%", s_bl); }
}

const ui_page_t page_display = { .id = "DISPLAY", .enter = enter, .exit = page_exit, .key = key };
