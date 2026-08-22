// main/page_audio.c -- ES8311 实验室: 曲目选择播放 + 麦克风 VU 谱 + 录音回放。
#include "ui.h"
#include "app_audio.h"

#include <stdio.h>

#define VU_N 20

static const char *TRACKS[AUD_TRK_COUNT] = { "BEEP 1KHZ", "SWEEP 2-4K", "ARP MELODY" };

static lv_obj_t *s_track_lbl;
static lv_obj_t *s_status;
static lv_obj_t *s_bar;
static lv_obj_t *s_vu[VU_N];
static uint8_t s_peak[VU_N];
static int s_head;
static lv_timer_t *s_timer;
static int s_track = AUD_TRK_BEEP;

static void timer_cb(lv_timer_t *t) {
    (void)t;

    // VU 谱: 最新电平插到队头, 其余右移, 形成时间频谱
    uint8_t lvl = app_audio_vu_level();
    s_head = (s_head + VU_N - 1) % VU_N;
    s_peak[s_head] = lvl;

    int bh = 78;
    for (int i = 0; i < VU_N; i++) {
        int idx = (s_head + i) % VU_N;
        uint8_t v = s_peak[idx];
        int h = (v * bh) / 100;
        if (h < 2) h = 2;
        lv_obj_set_height(s_vu[i], h);
        lv_obj_align(s_vu[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_bg_color(s_vu[i],
            lv_color_hex(v > 80 ? UI_WARN : (v > 40 ? UI_INK : UI_DIM)), 0);
    }

    // 进度
    int p = app_audio_progress();
    if (p >= 0) lv_bar_set_value(s_bar, p, LV_ANIM_OFF);

    // 状态行(<=12ch)
    if (app_audio_recording())
        lv_label_set_text(s_status, "REC 2S...");
    else if (app_audio_busy())
        lv_label_set_text_fmt(s_status, "PLAY %s", TRACKS[s_track]);
    else if (!app_audio_ok())
        lv_label_set_text(s_status, "NO ES8311");
    else
        lv_label_set_text(s_status, "READY.");
}

static void enter(lv_obj_t *root) {
    lv_obj_t *t = ui_label_make(root, "ES8311 16K16B");
    lv_obj_set_style_text_color(t, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(t, 2, 2);

    s_track_lbl = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_track_lbl, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(s_track_lbl, 2, 20);
    lv_label_set_text_fmt(s_track_lbl, "> %s", TRACKS[s_track]);

    // VU 容器
    lv_obj_t *vu_box = lv_obj_create(root);
    lv_obj_set_size(vu_box, 208, 86);
    lv_obj_set_pos(vu_box, 2, 40);
    lv_obj_set_style_bg_color(vu_box, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_border_color(vu_box, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(vu_box, 1, 0);
    lv_obj_set_style_radius(vu_box, 0, 0);
    lv_obj_set_style_pad_all(vu_box, 2, 0);
    lv_obj_clear_flag(vu_box, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < VU_N; i++) {
        s_vu[i] = lv_obj_create(vu_box);
        lv_obj_set_size(s_vu[i], 8, 2);
        lv_obj_set_pos(s_vu[i], 2 + i * 10, 78);
        lv_obj_set_style_bg_color(s_vu[i], lv_color_hex(UI_DIM), 0);
        lv_obj_set_style_bg_opa(s_vu[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_vu[i], 0, 0);
        lv_obj_set_style_radius(s_vu[i], 0, 0);
        lv_obj_clear_flag(s_vu[i], LV_OBJ_FLAG_SCROLLABLE);
        s_peak[i] = 0;
    }
    s_head = 0;

    lv_obj_t *mk = ui_label_make(root, "MIC RMS");
    lv_obj_set_style_text_color(mk, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(mk, 2, 132);

    // 播放进度条
    s_bar = lv_bar_create(root);
    lv_obj_set_size(s_bar, 208, 10);
    lv_obj_set_pos(s_bar, 2, 150);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_border_color(s_bar, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_INK2), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    s_status = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_ACC), 0);
    lv_obj_set_pos(s_status, 2, 168);
    lv_label_set_text(s_status, "READY.");

    lv_obj_t *h1 = ui_label_make(root, "OK:PLAY");
    lv_obj_set_style_text_color(h1, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(h1, 2, 192);

    lv_obj_t *h2 = ui_label_make(root, "U/D:TRACK");
    lv_obj_set_style_text_color(h2, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(h2, 2, 210);

    lv_obj_t *h3 = ui_label_make(root, "DOWN-L:REC");
    lv_obj_set_style_text_color(h3, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(h3, 2, 228);

    app_audio_vu_start();
    timer_cb(NULL);
    s_timer = lv_timer_create(timer_cb, 80, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    app_audio_stop();
    app_audio_vu_stop();
}

static void key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // DOWN 长按 = 录音回放(OK 长按已被框架拦截为返回)
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_LONG && !app_audio_busy()) {
        app_audio_vu_stop();
        app_audio_record_play();
        app_audio_vu_start();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) {
        app_audio_play((app_audio_track_t)s_track);
    } else if (btn == BSP_BTN_UP && !app_audio_busy()) {
        s_track = (s_track + AUD_TRK_COUNT - 1) % AUD_TRK_COUNT;
        lv_label_set_text_fmt(s_track_lbl, "> %s", TRACKS[s_track]);
    } else if (btn == BSP_BTN_DOWN && !app_audio_busy()) {
        s_track = (s_track + 1) % AUD_TRK_COUNT;
        lv_label_set_text_fmt(s_track_lbl, "> %s", TRACKS[s_track]);
    }
}

const ui_page_t page_audio = { .id = "AUDIO", .enter = enter, .exit = page_exit, .key = key };
