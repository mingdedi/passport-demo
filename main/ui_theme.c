// main/ui_theme.c -- 黑绿终端主题: 屏幕骨架(安全区边框/标题/内容区/状态栏)、面板、大数字。
// 布局基线: unscii_16 每字符 16x16, 安全区 [6,226)x[6,312), 内容行宽 <=13 字符。
#include "ui.h"
#include "app_sensors.h"

#include <stdio.h>
#include <string.h>

// 骨架纵向布局(屏幕绝对坐标)
#define UI_HDR_SEP_Y 30     // 标题下分隔线
#define UI_CONT_Y    36     // 内容区顶
#define UI_CONT_H    250    // 内容区高 (到 286)
#define UI_SB_SEP_Y  288    // 状态栏上分隔线
#define UI_SB_TXT_Y  292    // 状态栏文字 (16px 到 308)

// ---------------------------------------------------------------------------
// 状态栏: 每屏一份, 全部登记到表里由一个 1s 定时器统一刷新
// (页面屏幕会被转场 auto_del 删除, lv_obj_is_valid 过滤悬空项)
// ---------------------------------------------------------------------------
#define SB_MAX 6
typedef struct { lv_obj_t *left, *right; } sb_pair_t;
static sb_pair_t s_sbs[SB_MAX];
static int s_sb_n;
static lv_timer_t *s_sb_timer;

static void statusbar_timer_cb(lv_timer_t *t) {
    (void)t;
    static uint32_t s_tick;
    s_tick++;

    const app_sensors_snap_t *s = app_sensors_snap();

    char lbuf[12], rbuf[16];
    if (s->soc >= 0) snprintf(lbuf, sizeof(lbuf), "%3d%%", s->soc);
    else             snprintf(lbuf, sizeof(lbuf), "--%%");

    switch ((s_tick / 2) % 3) {            // 右栏 2s 一轮换: 电压/堆/时间
    case 0:
        snprintf(rbuf, sizeof(rbuf), "%d.%02dV", s->mv / 1000, (s->mv % 1000) / 10);
        break;
    case 1:
        snprintf(rbuf, sizeof(rbuf), "H:%3dK", (int)(s->heap_free / 1024));
        break;
    default: {
        uint32_t h = s->uptime_s / 3600, m = (s->uptime_s % 3600) / 60, sec = s->uptime_s % 60;
        if (h > 0) snprintf(rbuf, sizeof(rbuf), "%02u:%02uH", (unsigned)h, (unsigned)m);
        else       snprintf(rbuf, sizeof(rbuf), "%02u:%02u", (unsigned)m, (unsigned)sec);
    }
    }
    lbuf[sizeof(lbuf) - 1] = 0;
    rbuf[sizeof(rbuf) - 1] = 0;

    int32_t rx = UI_SAFE_L + UI_SAFE_W - 4 - (int32_t)strlen(rbuf) * UI_CH_W;
    for (int i = 0; i < s_sb_n; ) {
        if (!lv_obj_is_valid(s_sbs[i].left)) {      // 屏幕已删, 收缩表
            s_sbs[i] = s_sbs[--s_sb_n];
            continue;
        }
        lv_label_set_text(s_sbs[i].left, lbuf);
        lv_label_set_text(s_sbs[i].right, rbuf);
        lv_obj_set_x(s_sbs[i].right, rx);
        i++;
    }
}

static void hline_make(lv_obj_t *scr, int32_t x, int32_t y, int32_t w, uint32_t color) {
    lv_obj_t *l = lv_obj_create(scr);
    lv_obj_set_size(l, w, 1);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_bg_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_radius(l, 0, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
}

static void statusbar_create(lv_obj_t *scr) {
    hline_make(scr, 10, UI_SB_SEP_Y, UI_SAFE_W - 8, UI_DARK);

    // 闪烁光标(独立 label, blink 定时器带悬空保护)
    lv_obj_t *cur = lv_label_create(scr);
    lv_obj_set_style_text_font(cur, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(cur, lv_color_hex(UI_INK), 0);
    lv_label_set_text(cur, "_");
    lv_obj_set_pos(cur, 10, UI_SB_TXT_Y);
    ui_anim_blink_start(cur, 530);

    lv_obj_t *left = lv_label_create(scr);
    lv_obj_set_style_text_font(left, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(left, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(left, 30, UI_SB_TXT_Y);

    lv_obj_t *right = lv_label_create(scr);
    lv_obj_set_style_text_font(right, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(right, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(right, 120, UI_SB_TXT_Y);

    if (s_sb_n < SB_MAX) {
        s_sbs[s_sb_n].left = left;
        s_sbs[s_sb_n].right = right;
        s_sb_n++;
    }
    statusbar_timer_cb(NULL);
    if (!s_sb_timer) s_sb_timer = lv_timer_create(statusbar_timer_cb, 1000, NULL);
}

// ---------------------------------------------------------------------------
// 屏幕骨架: 安全区外框 + 标题行 + 内容容器 + 状态栏
// ---------------------------------------------------------------------------
lv_obj_t *ui_screen_create(const char *title) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // 安全区外框(避开外壳遮挡)
    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_set_size(frame, UI_SAFE_W, UI_SAFE_H);
    lv_obj_set_pos(frame, UI_SAFE_L, UI_SAFE_T);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    // 标题行(title 需 <=8 字符, "[ XXXX ]" 共 <=12 字符)
    lv_obj_t *t = lv_label_create(scr);
    lv_obj_set_style_text_font(t, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_ACC), 0);
    lv_label_set_text_fmt(t, "[ %s ]", title);
    lv_obj_set_pos(t, 12, 8);

    hline_make(scr, 10, UI_HDR_SEP_Y, UI_SAFE_W - 8, UI_DARK);

    // 内容容器(给页面填充, 页面内坐标以此为原点: 216x250)
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 216, UI_CONT_H);
    lv_obj_set_pos(cont, 8, UI_CONT_Y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);      // ui_content_get 取消隐藏
    lv_obj_set_user_data(scr, cont);

    statusbar_create(scr);
    return scr;
}

lv_obj_t *ui_content_get(lv_obj_t *scr) {
    lv_obj_t *cont = lv_obj_get_user_data(scr);
    if (cont) lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
    return cont;
}

// ---------------------------------------------------------------------------
// 终端面板: 暗底 + 细绿框
// ---------------------------------------------------------------------------
lv_obj_t *ui_term_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_style_bg_color(p, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 6, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t *ui_label_make(lv_obj_t *parent, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_INK2), 0);
    if (text) lv_label_set_text(l, text);
    return l;
}

// 暗网格背景(菜单下方空白区几条 1px 线)
void ui_grid_bg_install(lv_obj_t *scr) {
    for (int i = 0; i < 3; i++)
        hline_make(scr, 10, 248 + i * 14, UI_SAFE_W - 8, UI_GRID);
}

// ---------------------------------------------------------------------------
// 大数字: 3 段码 0-999, 15 个点/字, 只闪 opa 不增删对象
// ---------------------------------------------------------------------------
struct ui_bignum {
    lv_obj_t *dot[3][5][3];    // [digit][row][col]
    uint8_t px;
};

static const uint8_t SEG[10][5] = {   // 每行 3bit 掩码(col0|col1|col2)
    {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
    {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
    {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
    {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
    {0b111, 0b001, 0b001, 0b001, 0b001},  // 7
    {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
    {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
};

ui_bignum_t *ui_bignum_create(lv_obj_t *parent, int32_t x, int32_t y, uint8_t px) {
    ui_bignum_t *bn = malloc(sizeof(ui_bignum_t));
    if (!bn) return NULL;
    bn->px = px;
    for (int d = 0; d < 3; d++) {
        int32_t dx = x + d * (3 * px + px);
        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 3; c++) {
                lv_obj_t *o = lv_obj_create(parent);
                lv_obj_set_size(o, px, px);
                lv_obj_set_pos(o, dx + c * px, y + r * px);
                lv_obj_set_style_bg_color(o, lv_color_hex(UI_INK), 0);
                lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(o, 0, 0);
                lv_obj_set_style_radius(o, 0, 0);
                lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
                bn->dot[d][r][c] = o;
            }
        }
    }
    return bn;
}

void ui_bignum_set(ui_bignum_t *bn, int val) {
    if (!bn) return;
    if (val < 0) val = 0;
    if (val > 999) val = 999;
    int digits[3] = {val / 100, (val / 10) % 10, val % 10};
    bool leading = true;
    for (int d = 0; d < 3; d++) {
        int n = digits[d];
        bool dim = (leading && n == 0 && d < 2);   // 前导 0 熄灭
        if (n != 0) leading = false;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                lv_obj_set_style_bg_opa(bn->dot[d][r][c],
                    (!dim && (SEG[n][r] >> (2 - c)) & 1) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

void ui_bignum_delete(ui_bignum_t *bn) {
    free(bn);   // 点阵对象随屏幕销毁
}
