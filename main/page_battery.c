// main/page_battery.c -- CW2017 电量计: 大数字 SOC + 电池图形 + 电压历史曲线。
#include "ui.h"
#include "app_sensors.h"

#include <stdio.h>

static ui_bignum_t *s_num;
static lv_obj_t *s_fill, *s_volt, *s_soc_lbl, *s_state;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_ser;
static lv_timer_t *s_timer;

static void timer_cb(lv_timer_t *t) {
    (void)t;
    const app_sensors_snap_t *s = app_sensors_snap();
    int soc = s->soc < 0 ? 0 : s->soc;
    int mv = s->mv;

    ui_bignum_set(s_num, soc);
    lv_obj_set_height(s_fill, (60 * soc) / 100);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(soc < 20 ? UI_WARN : UI_INK), 0);

    lv_label_set_text_fmt(s_volt, "%d mV", mv);
    lv_label_set_text(s_soc_lbl, soc < 20 ? "SOC // LOW" : "SOC // OK");
    lv_obj_set_style_text_color(s_soc_lbl,
        lv_color_hex(soc < 20 ? UI_WARN : UI_DIM), 0);
    lv_label_set_text(s_state, s->batt_ok ? "CW2017" : "NO GAUGE");

    // 曲线: 每秒一点
    if (s->batt_hist_n > 0)
        lv_chart_set_next_value(s_chart, s_ser, mv);
}

static void enter(lv_obj_t *root) {
    s_soc_lbl = ui_label_make(root, "SOC");
    lv_obj_set_style_text_color(s_soc_lbl, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(s_soc_lbl, 2, 2);

    // 左侧: 大数字 SOC(3 位, px=5 -> 宽 60 高 25)
    s_num = ui_bignum_create(root, 2, 22, 5);
    lv_obj_t *pct = ui_label_make(root, "%");
    lv_obj_set_style_text_color(pct, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(pct, 64, 34);

    s_volt = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_volt, lv_color_hex(UI_ACC), 0);
    lv_obj_set_pos(s_volt, 2, 58);

    s_state = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_state, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(s_state, 2, 80);

    // 右侧: 电池图形(外壳 + 正极帽 + 电量填充)
    lv_obj_t *shell = lv_obj_create(root);
    lv_obj_set_size(shell, 64, 80);
    lv_obj_set_pos(shell, 140, 8);
    lv_obj_set_style_border_color(shell, lv_color_hex(UI_INK2), 0);
    lv_obj_set_style_border_width(shell, 2, 0);
    lv_obj_set_style_radius(shell, 4, 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_obj_create(root);       // 电池正极帽
    lv_obj_set_size(cap, 12, 6);
    lv_obj_set_pos(cap, 166, 2);
    lv_obj_set_style_bg_color(cap, lv_color_hex(UI_INK2), 0);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_set_style_radius(cap, 2, 0);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);

    s_fill = lv_obj_create(shell);             // 电量填充(底对齐)
    lv_obj_set_size(s_fill, 54, 60);
    lv_obj_align(s_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_bg_opa(s_fill, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_fill, 0, 0);
    lv_obj_set_style_radius(s_fill, 2, 0);
    lv_obj_clear_flag(s_fill, LV_OBJ_FLAG_SCROLLABLE);

    // 电压历史曲线
    lv_obj_t *kt = ui_label_make(root, "VBAT 1S/PT");
    lv_obj_set_style_text_color(kt, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(kt, 2, 102);

    s_chart = lv_chart_create(root);
    lv_obj_set_size(s_chart, 208, 124);
    lv_obj_set_pos(s_chart, 2, 118);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_border_color(s_chart, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_radius(s_chart, 0, 0);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, APP_BATT_HIST_N);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 3400, 4300);
    lv_chart_set_div_line_count(s_chart, 4, 8);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(UI_GRID), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_chart, 0, LV_PART_MAIN);
    s_ser = lv_chart_add_series(s_chart, lv_color_hex(UI_INK), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(s_chart, s_ser, LV_CHART_POINT_NONE);

    timer_cb(NULL);
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_num) { ui_bignum_delete(s_num); s_num = NULL; }
}

const ui_page_t page_battery = { .id = "POWER", .enter = enter, .exit = page_exit };
