// main/ui_boot.c -- 开机动画: 背光渐亮 -> boot log 逐行打字(<=13ch/行) -> 进度条 -> LOGO glitch。
#include "ui.h"
#include "bsp_display.h"
#include "app_audio.h"

#include "esp_random.h"
#include <stdio.h>

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *lines[12];
    lv_obj_t *bar;
    lv_obj_t *logo;
    int step;                 // 状态机步进
    int line;
    uint32_t t0;
    void (*on_done)(void);
    lv_timer_t *timer;
    bool finishing;
} boot_t;

static boot_t s_boot;

// unscii_16 每字符 16px, 安全区内一行最多 13 字符
static const char *BOOT_LINES[] = {
    "BOOT V1.0",
    "ESP32-C3 160M",
    "RAM 400K OK",
    "FLASH 8MB OK",
    "LCD ST7789 OK",
    "I2C 18 63 OK",
    "ES8311 OK",
    "CW2017 OK",
    "KEYS ADC OK",
    "BLE NIMBLE OK",
    "ALL NOMINAL",
    "LOAD SHELL...",
};
#define N_LINES ((int)(sizeof(BOOT_LINES) / sizeof(BOOT_LINES[0])))

static void boot_finish(void) {
    if (s_boot.finishing) return;
    s_boot.finishing = true;
    lv_timer_del(s_boot.timer);
    // 屏幕删除交给 ui_home_show 的 load_anim(auto_del=true), 避免与转场竞争
    s_boot.scr = NULL;
    void (*cb)(void) = s_boot.on_done;

    bsp_display_backlight(100);
    if (cb) cb();
}

// LOGO glitch: 随机抖动 + 透明度闪烁, 结束即收尾
static void glitch_exec_cb(void *var, int32_t v) {
    lv_obj_t *logo = (lv_obj_t *)var;
    if (v & 1) {
        lv_obj_set_pos(logo, 32 + (int32_t)(esp_random() % 24) - 12,
                       242 + (int32_t)(esp_random() % 8) - 4);
        lv_obj_set_style_text_opa(logo, esp_random() % 2 ? LV_OPA_40 : LV_OPA_COVER, 0);
    } else {
        lv_obj_set_pos(logo, 32, 242);
        lv_obj_set_style_text_opa(logo, LV_OPA_COVER, 0);
    }
}

static void boot_timer_cb(lv_timer_t *t) {
    boot_t *b = lv_timer_get_user_data(t);

    switch (b->step) {
    case 0:  // 逐行打字
        if (b->line < N_LINES) {
            lv_obj_t *l = b->lines[b->line];
            lv_label_set_text(l, "");
            ui_typewriter_run(l, BOOT_LINES[b->line], 12);
            b->line++;
        } else {
            b->step = 1;
        }
        break;

    case 1:  // 进度条推满(占位动画, 让打字机先飞一会)
        lv_bar_set_value(b->bar, 100, LV_ANIM_ON);
        b->step = 2;
        break;

    case 2:  // LOGO 出现
        lv_obj_clear_flag(b->logo, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, b->logo);
        lv_anim_set_exec_cb(&a, glitch_exec_cb);
        lv_anim_set_values(&a, 0, 12);
        lv_anim_set_duration(&a, 700);
        lv_anim_set_repeat_count(&a, 1);
        b->step = 3;
        break;

    case 3:  // glitch 结束 -> 收尾
        boot_finish();
        break;
    }
}

void ui_boot_play(void (*on_done)(void)) {
    boot_t *b = &s_boot;
    b->on_done = on_done;
    b->step = 0;
    b->line = 0;
    b->finishing = false;

    b->scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(b->scr, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(b->scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b->scr, 0, 0);
    lv_obj_set_style_radius(b->scr, 0, 0);
    lv_obj_set_style_pad_all(b->scr, 0, 0);

    for (int i = 0; i < N_LINES; i++) {
        lv_obj_t *l = lv_label_create(b->scr);
        lv_obj_set_style_text_font(l, &lv_font_unscii_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(
            i == 0 ? UI_ACC : (i >= N_LINES - 2 ? UI_INK : UI_DIM)), 0);
        lv_obj_set_pos(l, 8, 10 + i * 17);
        b->lines[i] = l;
    }

    // 进度条
    b->bar = lv_bar_create(b->scr);
    lv_obj_set_size(b->bar, 208, 10);
    lv_obj_set_pos(b->bar, 8, 10 + N_LINES * 17 + 6);
    lv_bar_set_range(b->bar, 0, 100);
    lv_bar_set_value(b->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b->bar, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_bg_opa(b->bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b->bar, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(b->bar, 1, 0);
    lv_obj_set_style_radius(b->bar, 0, 0);
    lv_obj_set_style_bg_color(b->bar, lv_color_hex(UI_INK), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(b->bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // LOGO(先藏) "PASSPORT OS" 11ch 居中
    b->logo = lv_label_create(b->scr);
    lv_obj_set_style_text_font(b->logo, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(b->logo, lv_color_hex(UI_INK), 0);
    lv_label_set_text(b->logo, "PASSPORT OS");
    lv_obj_set_pos(b->logo, 32, 242);
    lv_obj_add_flag(b->logo, LV_OBJ_FLAG_HIDDEN);

    // 每行 13ch x 12ms ≈ 156ms; 定时器 110ms 一拍推进状态机
    b->timer = lv_timer_create(boot_timer_cb, 110, b);

    // 背光两档渐亮(0 已由 main 设置, 此处 30->100 近似渐亮)
    bsp_display_backlight(30);
    bsp_display_backlight(100);
}

bool ui_boot_active(void) { return !s_boot.finishing && s_boot.scr != NULL; }

void ui_boot_skip(void) {
    if (!ui_boot_active()) return;
    // 立即补全所有行, 直接进收尾
    for (int i = 0; i < N_LINES; i++)
        lv_label_set_text(s_boot.lines[i], BOOT_LINES[i]);
    lv_bar_set_value(s_boot.bar, 100, LV_ANIM_OFF);
    boot_finish();
}
