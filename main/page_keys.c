// main/page_keys.c -- KEYS 页: BLE 密钥账户列表 + 二次确认 + 触发键入。
// 账户名(选中循环滚动) + 账户 ID(暗色辅助行); 密码永不显示。
// OK 单击=装填(5s 内再按 OK 才发射), 防误触把密码打进当前焦点窗口。
#include "ui.h"
#include "app_ble.h"
#include "app_ble_hid.h"
#include "keys_secrets.h"

#include <stdio.h>
#include <string.h>

#define LIST_Y     22                        // 列表区顶(状态行下)
#define ROW_STEP   34                        // 每账户两行: name 16 + id 16 + 2 间隙
#define VIS_ROWS   5                         // 每屏 5 项, 其余窗口滚动
#define HINT_Y     (LIST_Y + VIS_ROWS * ROW_STEP + 4)
#define ARM_TIMEOUT_MS 5000

static const kkey_acc_t s_accs[] = KKEY_ACCS;
#define ACC_N ((int)(sizeof s_accs / sizeof s_accs[0]))

static lv_obj_t *s_st_lbl;                   // BLE 状态行
static lv_obj_t *s_names[ACC_N];
static lv_obj_t *s_ids[ACC_N];
static lv_obj_t *s_hint;
static lv_timer_t *s_timer;
static int s_sel;
static int s_top;                            // 显示窗口首项
static bool s_armed;                         // 二次确认装填态
static uint32_t s_arm_at;
static uint32_t s_warn_until;                // "NOT PAIRED" 红显截止

static void list_refresh(void) {
    if (s_sel < s_top) s_top = s_sel;                        // 窗口跟随选中项
    if (s_sel >= s_top + VIS_ROWS) s_top = s_sel - VIS_ROWS + 1;
    if (s_top > ACC_N - VIS_ROWS) s_top = ACC_N - VIS_ROWS;
    if (s_top < 0) s_top = 0;

    for (int i = 0; i < ACC_N; i++) {
        bool vis = (i >= s_top && i < s_top + VIS_ROWS);
        if (!vis) {
            lv_obj_add_flag(s_names[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_ids[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_names[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ids[i], LV_OBJ_FLAG_HIDDEN);
        int y = LIST_Y + (i - s_top) * ROW_STEP;
        lv_obj_set_pos(s_names[i], 4, y);
        lv_obj_set_pos(s_ids[i], 20, y + 17);

        bool sel = (i == s_sel);
        // 选中项长名循环滚动(marquee), 失选回退截断
        lv_label_set_long_mode(s_names[i],
            sel ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
        lv_label_set_text_fmt(s_names[i], "%c%s", sel ? (s_armed ? '!' : '>') : ' ',
                              s_accs[i].name);
        lv_obj_set_style_text_color(s_names[i],
            lv_color_hex(!sel ? UI_DIM : (s_armed ? UI_WARN : UI_INK)), 0);
        lv_label_set_text(s_ids[i], s_accs[i].acct_id);
    }
}

static void timer_cb(lv_timer_t *t) {
    (void)t;
    if (s_armed && lv_tick_get() - s_arm_at > ARM_TIMEOUT_MS) {
        s_armed = false;                                     // 装填超时自动解除
        list_refresh();
    }

    char buf[24];
    uint32_t color = UI_INK;
    if (app_ble_hid_typing()) {
        const kkey_acc_t *a = &s_accs[s_sel];
        int total = (int)strlen(a->pass) + (a->enter ? 1 : 0);
        snprintf(buf, sizeof buf, "TYPING %d/%d", app_ble_hid_typed(), total);
        color = UI_ACC;
    } else if (s_warn_until && lv_tick_get() < s_warn_until) {
        snprintf(buf, sizeof buf, "NOT PAIRED");
        color = UI_RED;
    } else {
        s_warn_until = 0;
        switch (app_ble_hid_state()) {
        case KKEY_ST_ADV:
            snprintf(buf, sizeof buf, "SCAN:KEYBOARD");
            break;
        case KKEY_ST_PAIRING:
            if (app_ble_hid_passkey_valid()) {
                snprintf(buf, sizeof buf, "KEY:%06u", (unsigned)app_ble_hid_passkey());
                color = UI_WARN;
            } else {
                snprintf(buf, sizeof buf, "PAIRING...");
            }
            break;
        case KKEY_ST_READY:
            snprintf(buf, sizeof buf, "LINK READY");
            color = UI_ACC;
            break;
        default:
            snprintf(buf, sizeof buf, "BLE OFF");
            color = UI_DARK;
        }
    }
    lv_label_set_text(s_st_lbl, buf);
    lv_obj_set_style_text_color(s_st_lbl, lv_color_hex(color), 0);

    if (app_ble_hid_typing())      lv_label_set_text(s_hint, "TYPING...");
    else if (s_armed)              lv_label_set_text(s_hint, "OK:SEND\nUD:ESC");
    else if (app_ble_hid_state() == KKEY_ST_READY)
                                    lv_label_set_text(s_hint, "UD:SEL OK:SET");
    else                           lv_label_set_text(s_hint, "PAIR TO USE");
}

static void enter(lv_obj_t *root) {
    s_st_lbl = ui_label_make(root, "");
    lv_obj_set_pos(s_st_lbl, 2, 0);

    for (int i = 0; i < ACC_N; i++) {
        s_names[i] = ui_label_make(root, "");
        lv_obj_set_size(s_names[i], 208, 16);                // 固宽才有滚动/截断
        lv_label_set_long_mode(s_names[i], LV_LABEL_LONG_CLIP);
        s_ids[i] = ui_label_make(root, "");
        lv_obj_set_size(s_ids[i], 188, 16);
        lv_label_set_long_mode(s_ids[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(s_ids[i], lv_color_hex(UI_DIM), 0);
    }

    s_hint = ui_label_make(root, "");
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(s_hint, 2, HINT_Y);

    s_sel = 0;
    s_top = 0;
    s_armed = false;
    s_warn_until = 0;
    list_refresh();

    app_ble_hid_adv_start();         // 进页即键盘广播, 等终端配对

    timer_cb(NULL);
    s_timer = lv_timer_create(timer_cb, 300, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    app_ble_shutdown();             // 退页即离线: 停广播断连 + 全量释放 BLE(~70K)
}

static void key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;
    if (app_ble_hid_typing()) return;                        // 发射中锁操作

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (s_armed) { s_armed = false; }                    // 方向键取消装填
        s_sel = (s_sel + (btn == BSP_BTN_UP ? ACC_N - 1 : 1)) % ACC_N;
        list_refresh();
        return;
    }
    if (btn != BSP_BTN_OK) return;

    if (!app_ble_hid_ready()) {
        s_warn_until = lv_tick_get() + 3000;                 // 未配对红显提醒
        return;
    }
    if (!s_armed) {                                           // 第一次 OK: 装填
        s_armed = true;
        s_arm_at = lv_tick_get();
        list_refresh();
        return;
    }
    app_ble_hid_type(s_accs[s_sel].pass, s_accs[s_sel].enter);  // 第二次 OK: 发射
    s_armed = false;
    list_refresh();
}

const ui_page_t page_keys = { .id = "KEYS", .enter = enter, .exit = page_exit, .key = key };
