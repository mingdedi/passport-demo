// main/ui.h -- passport-demo UI 公共接口: 调色板 / 页面表 / 主题与动效工具。
#pragma once

#include "lvgl.h"
#include "bsp_button.h"

// ---- 黑绿终端调色板 ----
#define UI_BG    0x050A06   // 底色(近黑带绿)
#define UI_BG2   0x0B1810   // 深面板
#define UI_PANEL 0x0E2114   // 面板
#define UI_GRID  0x14301C   // 网格线
#define UI_INK   0x00FF66   // 荧光绿(主色)
#define UI_INK2  0x00D457   // 中亮绿
#define UI_DIM   0x1FA24E   // 暗绿(次文本)
#define UI_DARK  0x0E5C2C   // 极暗绿(边框)
#define UI_ACC   0xCFFFD9   // 白绿高亮(数值)
#define UI_WARN  0xFFB300   // 琥珀(警告)
#define UI_RED   0xFF5050   // 红(错误)

// ---- 字体度量(unscii_16 实际步进 16px/字符, 见 lv_font_unscii_16.c adv_w=256) ----
#define UI_CH_W  16
#define UI_CH_H  16

// ---- 安全区: 外壳遮挡实测边界(screen-calib 按键校准, 2026-08-23) ----
// 240x320 中可用区 x∈[2,239) y∈[3,319); 壳开孔圆角实测 r=26(边框圆角须不小于它);
// 16px 字体每行最多 14 字符(224px), 现有文案沿用 <=13
#define UI_SAFE_L 2
#define UI_SAFE_R 1
#define UI_SAFE_T 3
#define UI_SAFE_B 1
#define UI_SAFE_W (240 - UI_SAFE_L - UI_SAFE_R)   // 237
#define UI_SAFE_H (320 - UI_SAFE_T - UI_SAFE_B)   // 316

// ---- 页面注册表 ----
typedef struct {
    const char *id;                                   // 菜单项与页标题
    void (*enter)(lv_obj_t *root);                    // 构建页面内容(root=内容容器)
    void (*exit)(void);                               // 清理页面私有资源(可 NULL)
    void (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);      // 页内按键(可 NULL)
} ui_page_t;

extern const ui_page_t *const UI_PAGES[];
#define UI_PAGE_COUNT 8      // 与 main.c 中 UI_PAGES[] 一致

// ---- ui_boot ----
void ui_boot_play(void (*on_done)(void));
bool ui_boot_active(void);
void ui_boot_skip(void);

// ---- ui_home(导航) ----
void ui_home_show(void);                  // 构建并载入主菜单(带入场动画)
void ui_home_key(bsp_btn_t btn, bsp_btn_ev_t ev);   // 全局按键路由入口

// ---- ui_theme ----
lv_obj_t *ui_screen_create(const char *title);       // 边框+标题条+内容区+状态栏
lv_obj_t *ui_content_get(lv_obj_t *scr);
lv_obj_t *ui_term_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
lv_obj_t *ui_label_make(lv_obj_t *parent, const char *text);
void ui_grid_bg_install(lv_obj_t *scr);

// 大数字(3 段码点阵, 0-999)
typedef struct ui_bignum ui_bignum_t;
ui_bignum_t *ui_bignum_create(lv_obj_t *parent, int32_t x, int32_t y, uint8_t px);
void ui_bignum_set(ui_bignum_t *bn, int val);
void ui_bignum_delete(ui_bignum_t *bn);

// ---- ui_anim ----
void ui_anim_slide_in(lv_obj_t *obj, int32_t from_dx, uint32_t dur);
void ui_anim_fade_in(lv_obj_t *obj, uint32_t dur);
lv_timer_t *ui_anim_blink_start(lv_obj_t *obj, uint32_t period_ms);
void ui_typewriter_run(lv_obj_t *label, const char *full, uint16_t ms_per_char);
