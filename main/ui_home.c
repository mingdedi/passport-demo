// main/ui_home.c -- 菜单屏 + 页面导航(进入/返回/转场动画/按键路由)。
// 开机先进主界面(ui_main), 主界面按 OK 进本菜单; 菜单态 OK 长按回主界面。
// 菜单行 = 单 label(13ch: "NN NAME    ICON"), 选中行整行反色(绿底黑字)。
#include "ui.h"
#include "app_audio.h"

#include <stdio.h>

#define ROW_W 212          // 13ch x 16px + 4px pad
#define ROW_H 18
#define ROW_STEP 19
#define MENU_Y 2           // 内容容器内坐标

static lv_obj_t *s_menu_scr;
static lv_obj_t *s_rows[9];
static int s_sel;
static int s_active = -1;          // -1 = 菜单
static lv_obj_t *s_page_scr;

// 菜单行尾 ASCII 小图标(终端味)
static const char *PAGE_ICONS[9] = {
    "[#]", "[8]", "[o]", "[=]", "[^]", "[~]", "[:]", "[?]", "[R]",
};

static void menu_refresh(void) {
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_t *row = s_rows[i];
        bool sel = (i == s_sel);
        // "01 SYSTEM  [#]" 布局: 2位序号 + 名称补齐7字符 + 图标, 恰好 13 字符
        lv_label_set_text_fmt(row, "%02d %-7s%s", i + 1, UI_PAGES[i]->id, PAGE_ICONS[i]);
        lv_obj_set_style_text_color(row, lv_color_hex(sel ? UI_BG : UI_DIM), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_INK), 0);
        lv_obj_set_style_bg_opa(row, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

static void page_close(void) {
    if (s_active < 0) return;
    if (UI_PAGES[s_active]->exit) UI_PAGES[s_active]->exit();
    s_active = -1;
    lv_screen_load_anim(s_menu_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    s_page_scr = NULL;
}

static void page_open(int idx) {
    if (s_active >= 0) return;
    s_active = idx;

    s_page_scr = ui_screen_create(UI_PAGES[idx]->id);
    if (UI_PAGES[idx]->enter)
        UI_PAGES[idx]->enter(ui_content_get(s_page_scr));

    lv_screen_load_anim(s_page_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void ui_home_show(void) {
    if (!s_menu_scr) {
        s_menu_scr = ui_screen_create("MENU");
        lv_obj_t *cont = ui_content_get(s_menu_scr);
        ui_grid_bg_install(s_menu_scr);

        for (int i = 0; i < UI_PAGE_COUNT; i++) {
            s_rows[i] = ui_label_make(cont, "");
            lv_obj_set_size(s_rows[i], ROW_W, ROW_H);
            lv_obj_set_style_pad_hor(s_rows[i], 2, 0);
            lv_obj_set_pos(s_rows[i], 2, MENU_Y + i * ROW_STEP);
        }

        // 操作提示(两行: 选择/进入 + 长按返回主界面)
        lv_obj_t *hint = ui_label_make(cont, "U/D:SEL OK:GO");
        lv_obj_set_style_text_color(hint, lv_color_hex(UI_DARK), 0);
        lv_obj_set_pos(hint, 4, 176);

        lv_obj_t *hint2 = ui_label_make(cont, "HOLD OK:HOME");
        lv_obj_set_style_text_color(hint2, lv_color_hex(UI_DARK), 0);
        lv_obj_set_pos(hint2, 4, 194);

        menu_refresh();
    }
    // 主界面与菜单屏均常驻互跳, 勿 auto_del(否则会把主界面删掉)
    lv_screen_load_anim(s_menu_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void ui_home_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (s_active >= 0) {
        // 页内: OK 长按统一返回
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            app_audio_stop();
            page_close();
            return;
        }
        if (UI_PAGES[s_active]->key)
            UI_PAGES[s_active]->key(btn, ev);
        return;
    }

    // 菜单态: OK 长按返回主界面(与页内返回同手势)
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        app_audio_stop();
        ui_main_show();
        return;
    }

    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP)   { s_sel = (s_sel + UI_PAGE_COUNT - 1) % UI_PAGE_COUNT; menu_refresh(); }
    if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % UI_PAGE_COUNT; menu_refresh(); }
    if (btn == BSP_BTN_OK)   { page_open(s_sel); }
}
