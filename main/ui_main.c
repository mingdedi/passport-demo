// main/ui_main.c -- 主界面(开机首屏): 品牌/固件行 + 大号电量 + [MENU] 按钮。
// 本机无触摸、无实体 MENU 键: "点击 MENU 按钮" = 主界面下按 OK;
// 菜单态 OK 长按返回本屏(与页内"长按返回"同手势)。全局按键由本文件统一分发。
#include "ui.h"
#include "app_sensors.h"

#include "esp_app_desc.h"

static lv_obj_t *s_scr;
static ui_bignum_t *s_batt;
static lv_timer_t *s_batt_timer;

// 主屏常驻(仅构建一次, 不随转场删除), 本定时器与状态栏定时器同寿命, 无需清理
static void batt_timer_cb(lv_timer_t *t) {
    (void)t;
    const app_sensors_snap_t *s = app_sensors_snap();
    ui_bignum_set(s_batt, s->soc < 0 ? 0 : s->soc);
}

static void build(void) {
    s_scr = ui_screen_create("PASSPORT");
    lv_obj_t *cont = ui_content_get(s_scr);

    // 品牌 + 硬件/固件行(固件版本取自应用镜像描述符)
    lv_obj_t *brand = ui_label_make(cont, "");
    lv_obj_set_style_text_color(brand, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(brand, 2, 4);
    ui_typewriter_run(brand, "AI PASSPORT", 55);

    lv_obj_t *hw = ui_label_make(cont, "ESP32-C3 8MB");
    lv_obj_set_style_text_color(hw, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(hw, 2, 26);

    lv_obj_t *fw = ui_label_make(cont, "");
    lv_obj_set_style_text_color(fw, lv_color_hex(UI_DARK), 0);
    lv_label_set_text_fmt(fw, "FW:%.5s", esp_app_get_description()->version);
    lv_obj_set_pos(fw, 2, 44);

    ui_hline_make(cont, 2, 66, 212, UI_DARK);

    // 大号电量点阵(px=12: 总宽 132, 恰在 216 内容区居中): 1s 轮询传感器快照
    s_batt = ui_bignum_create(cont, 42, 78, 12);
    lv_obj_t *bcap = ui_label_make(cont, "BATTERY");
    lv_obj_set_style_text_color(bcap, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(bcap, 52, 142);
    batt_timer_cb(NULL);
    s_batt_timer = lv_timer_create(batt_timer_cb, 1000, NULL);

    // MENU 按钮: 整块反色(绿底黑字, 同菜单选中行样式), 全屏最醒目的可"按下"目标
    lv_obj_t *btn = lv_obj_create(cont);
    lv_obj_set_size(btn, 160, 48);
    lv_obj_set_pos(btn, 28, 158);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *btxt = ui_label_make(btn, "[ MENU ]");
    lv_obj_set_style_text_color(btxt, lv_color_hex(UI_BG), 0);
    lv_obj_center(btxt);

    // 按钮左侧闪烁箭头: 终端"待输入"暗示, 与状态栏光标同节奏
    lv_obj_t *arrow = ui_label_make(cont, ">");
    lv_obj_set_style_text_color(arrow, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(arrow, 6, 174);
    ui_anim_blink_start(arrow, 530);

    lv_obj_t *hint = ui_label_make(cont, "PRESS OK");
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(hint, 44, 214);
}

void ui_main_show(void) {
    bool first = !s_scr;
    if (first) build();
    // 仅开机首载需要删掉 LVGL 默认屏; 主界面<->菜单互跳时两屏均常驻
    lv_screen_load_anim(s_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, first);
}

void ui_main_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // 非主界面(菜单/页面态)全部交给菜单导航
    if (lv_screen_active() != s_scr) {
        ui_home_key(btn, ev);
        return;
    }
    // 主界面: OK 单击 = 点击 [MENU] 按钮进入菜单
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK)
        ui_home_show();
}
