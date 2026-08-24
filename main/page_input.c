// main/page_input.c -- 三键 ADC 演示: 实时按下高亮 + 事件统计 + 原始电压条。
#include "ui.h"
#include "bsp_button.h"

#include <stdio.h>

typedef struct { int click, dbl, lng; } ev_cnt_t;

static lv_obj_t *s_keybox[3];              // 三个键的大方块
static lv_obj_t *s_keylbl[3];
static lv_obj_t *s_cnt_lbl[3];
static lv_obj_t *s_mv_lbl, *s_win_lbl;
static lv_obj_t *s_mv_bar;
static ev_cnt_t s_cnt[3];
static lv_timer_t *s_timer;
static int s_hold_btn = -1;

// 顺序须与 bsp_btn_t 枚举一致(UP=0, DOWN=1, OK=2),btn 回调值直接做下标
static const char KEYNAME[3][3] = { "UP", "DN", "OK" };
static const char KEYSYM[3][2]  = { "^", "v", "O" };

static void keybox_style(int i, bool lit) {
    lv_obj_set_style_bg_color(s_keybox[i], lv_color_hex(lit ? UI_PANEL : UI_BG2), 0);
    lv_obj_set_style_bg_opa(s_keybox[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_keybox[i], lv_color_hex(lit ? UI_INK : UI_DARK), 0);
    lv_obj_set_style_text_color(s_keylbl[i], lv_color_hex(lit ? UI_INK : UI_DIM), 0);
}

static void cnt_refresh(void) {
    for (int i = 0; i < 3; i++)
        lv_label_set_text_fmt(s_cnt_lbl[i], "C%02d D%02d L%02d",
                              s_cnt[i].click > 99 ? 99 : s_cnt[i].click,
                              s_cnt[i].dbl > 99 ? 99 : s_cnt[i].dbl,
                              s_cnt[i].lng > 99 ? 99 : s_cnt[i].lng);
}

// 按住高亮 250ms 后自动熄灭( PRESS 无 RELEASE 事件, 用定时器兜底 )
static void unhold_cb(lv_timer_t *t) {
    (void)t;
    if (s_hold_btn >= 0) { keybox_style(s_hold_btn, false); s_hold_btn = -1; }
    lv_timer_del(t);
}

static void flash_key(int i) {
    if (s_hold_btn >= 0 && s_hold_btn != i) keybox_style(s_hold_btn, false);
    s_hold_btn = i;
    keybox_style(i, true);
    lv_timer_t *t = lv_timer_create(unhold_cb, 250, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void timer_cb(lv_timer_t *t) {
    (void)t;
    int mv = bsp_button_read_mv();
    lv_label_set_text_fmt(s_mv_lbl, "ADC %4d mV", mv);
    if (mv >= 0) lv_bar_set_value(s_mv_bar, mv * 100 / 3300, LV_ANIM_ON);

    if (mv >= 0 && mv < 150)        lv_label_set_text(s_win_lbl, "UP [0,150)");
    else if (mv < 447)              lv_label_set_text(s_win_lbl, "DN [150,447)");
    else if (mv < 1900)             lv_label_set_text(s_win_lbl, "OK [447,2K)");
    else                            lv_label_set_text(s_win_lbl, "IDLE (OPEN)");
}

static void enter(lv_obj_t *root) {
    lv_obj_t *t = ui_label_make(root, "KEYS @ADC0");
    lv_obj_set_style_text_color(t, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(t, 2, 2);

    for (int i = 0; i < 3; i++) {
        s_keybox[i] = lv_obj_create(root);
        lv_obj_set_size(s_keybox[i], 64, 64);
        lv_obj_set_pos(s_keybox[i], 2 + i * 72, 22);
        lv_obj_set_style_radius(s_keybox[i], 0, 0);
        lv_obj_set_style_border_width(s_keybox[i], 1, 0);
        lv_obj_clear_flag(s_keybox[i], LV_OBJ_FLAG_SCROLLABLE);

        s_keylbl[i] = ui_label_make(s_keybox[i], KEYSYM[i]);
        lv_obj_center(s_keylbl[i]);

        lv_obj_t *nm = ui_label_make(root, KEYNAME[i]);
        lv_obj_set_style_text_color(nm, lv_color_hex(UI_DARK), 0);
        lv_obj_set_pos(nm, 2 + i * 72 + 24, 90);

        s_cnt[i].click = s_cnt[i].dbl = s_cnt[i].lng = 0;
        keybox_style(i, false);
    }

    // 事件统计: 键名 + 计数行
    for (int i = 0; i < 3; i++) {
        lv_obj_t *kn = ui_label_make(root, KEYNAME[i]);
        lv_obj_set_style_text_color(kn, lv_color_hex(UI_DARK), 0);
        lv_obj_set_pos(kn, 2, 110 + i * 18);

        s_cnt_lbl[i] = ui_label_make(root, "");
        lv_obj_set_style_text_color(s_cnt_lbl[i], lv_color_hex(UI_INK2), 0);
        lv_obj_set_pos(s_cnt_lbl[i], 44, 110 + i * 18);
    }
    cnt_refresh();

    s_mv_lbl = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_mv_lbl, lv_color_hex(UI_ACC), 0);
    lv_obj_set_pos(s_mv_lbl, 2, 168);

    s_mv_bar = lv_bar_create(root);
    lv_obj_set_size(s_mv_bar, 208, 10);
    lv_obj_set_pos(s_mv_bar, 2, 188);
    lv_bar_set_range(s_mv_bar, 0, 100);
    lv_obj_set_style_bg_color(s_mv_bar, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_border_color(s_mv_bar, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(s_mv_bar, 1, 0);
    lv_obj_set_style_radius(s_mv_bar, 0, 0);
    lv_obj_set_style_bg_color(s_mv_bar, lv_color_hex(UI_INK2), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_mv_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    s_win_lbl = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_win_lbl, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(s_win_lbl, 2, 206);

    lv_obj_t *hint = ui_label_make(root, "CLICK/2X/HOLD");
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(hint, 2, 228);

    timer_cb(NULL);
    s_timer = lv_timer_create(timer_cb, 120, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
}

static void key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn < 0 || btn > 2) return;
    switch (ev) {
    case BSP_BTN_PRESS: flash_key(btn); return;
    case BSP_BTN_CLICK: s_cnt[btn].click++; break;
    case BSP_BTN_DOUBLE: s_cnt[btn].dbl++; break;
    case BSP_BTN_LONG: s_cnt[btn].lng++; break;
    }
    flash_key(btn);
    cnt_refresh();
}

const ui_page_t page_input = { .id = "INPUT", .enter = enter, .exit = page_exit, .key = key };
