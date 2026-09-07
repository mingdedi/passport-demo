// main/ui_main.c -- 主界面(开机首屏): 系统仪表盘(离线区+在线区)。
// 离线区: 温度/电池/内存/Flash; 在线区: NET 状态(联动标题行 wifi 点阵图标)与
// NTP 同步的上海时间。WiFi 服务快照由 app_wifi 提供, 本屏 1s 轮询。
// 排版沿用分支页 kv 网格; OK 键进菜单(菜单态 OK 长按回本屏)。
#include "ui.h"
#include "app_sensors.h"
#include "app_wifi.h"

#include "esp_partition.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// kv 列基线(同 page_sysinfo: 键列 <=5ch, 值列 <=8ch, 86+128=214)
#define K_X 2
#define V_X 86

static lv_obj_t *s_scr;
static lv_timer_t *s_timer;
static lv_obj_t *l_net, *l_time, *l_temp, *l_batt, *l_heap;
static lv_obj_t *bar_batt, *bar_heap, *bar_flash;

// ---- NET 状态图标: 7x5 点阵 wifi(3px/点), 挂在标题行右端(屏幕坐标) ----
// 标题 "[ PASSPORT ]" 止于 x=200; 图标 206 起 21px 宽, 已避开右上圆角(实测 r=26)
static lv_obj_t *s_net_dot[5][7];
static const uint8_t NET_SHAPE[5] = {        // wifi 轮廓: 外弧+内弧+中心点
    0b0111110, 0b1000001, 0b0011100, 0b0010100, 0b0001000,
};

// level: 0=离线(轮廓幽灵暗绿, 仅中心点亮成暗绿) 1=内弧亮 2=全亮
static void net_icon_set(int level) {
    static const uint8_t MASK[3][5] = {
        {0, 0, 0, 0, 0b0001000},
        {0, 0, 0b0011100, 0b0010100, 0b0001000},
        {0b0111110, 0b1000001, 0b0011100, 0b0010100, 0b0001000},
    };
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 7; c++) {
            lv_obj_t *o = s_net_dot[r][c];
            if (!o) continue;
            bool lit = level > 0 && (MASK[level][r] >> (6 - c)) & 1;
            lv_obj_set_style_bg_color(o, lv_color_hex(
                lit ? UI_INK : (r == 4 ? UI_DIM : UI_DARK)), 0);
        }
}

// NET 行文案/配色 + 图标档位(信号强全亮, 弱亮内弧); 变化时才刷, 避免每秒重设样式
static void net_refresh(const app_wifi_snap_t *w) {
    static int s_state = -1, s_lvl = -1;
    if (w->state != s_state) {
        s_state = w->state;
        static const char *TXT[] = {"OFFLINE", "SCAN..", "JOIN..", "ONLINE"};
        static const uint32_t COL[] = {UI_DIM, UI_WARN, UI_WARN, UI_INK};
        lv_label_set_text(l_net, TXT[w->state]);
        lv_obj_set_style_text_color(l_net, lv_color_hex(COL[w->state]), 0);
        if (w->state != APP_WIFI_ONLINE) { net_icon_set(0); s_lvl = 0; }
    }
    if (w->state == APP_WIFI_ONLINE) {
        int lvl = w->rssi > -60 ? 2 : 1;
        if (lvl != s_lvl) { net_icon_set(lvl); s_lvl = lvl; }
    }
}

// ---- APP 镜像实际长度: Flash 占用的真实值(构建后不变, 只解析一次) ----
// 镜像头在 Flash 上 24 字节(实测段表始于 24, hash 标志在字节 23), 段头=load(4)+len(4)
static uint32_t app_image_len(const esp_partition_t *p) {
    uint8_t hdr[24];
    if (esp_partition_read(p, 0, hdr, sizeof(hdr)) != ESP_OK) return 0;
    if (hdr[0] != 0xE9 || hdr[1] < 1 || hdr[1] > 16) return 0;   // 魔数/段数护栏
    uint32_t off = sizeof(hdr);
    for (int i = 0; i < hdr[1]; i++) {
        uint8_t sh[8];
        if (esp_partition_read(p, off, sh, sizeof(sh)) != ESP_OK) return 0;
        uint32_t dlen;
        memcpy(&dlen, sh + 4, 4);
        off += sizeof(sh) + dlen;
    }
    if (hdr[23]) off += 32;                   // 尾附 SHA256
    return off + 1;                           // + 校验和字节(末尾对齐填充忽略)
}

static lv_obj_t *kv_make(lv_obj_t *root, const char *key, const char *val,
                         int32_t y, uint32_t vcol) {
    lv_obj_t *k = ui_label_make(root, key);
    lv_obj_set_style_text_color(k, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(k, K_X, y);
    lv_obj_t *v = ui_label_make(root, val);
    lv_obj_set_style_text_color(v, lv_color_hex(vcol), 0);
    lv_obj_set_pos(v, V_X, y);
    return v;
}

// 用量条(同分支页样式: 208x10, 暗底细框)
static lv_obj_t *bar_make(lv_obj_t *root, int32_t y) {
    lv_obj_t *b = lv_bar_create(root);
    lv_obj_set_size(b, 208, 10);
    lv_obj_set_pos(b, 2, y);
    lv_bar_set_range(b, 0, 100);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_INK2), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(b, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    return b;
}

// 主屏常驻(仅构建一次), 本定时器与状态栏定时器同寿命, 无需清理
static void timer_cb(lv_timer_t *t) {
    (void)t;
    const app_sensors_snap_t *s = app_sensors_snap();
    const app_wifi_snap_t *w = app_wifi_snap();

    // -- 在线区 --
    net_refresh(w);
    if (w->time_valid) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);               // TZ=上海, app_wifi 同步时已设
        lv_label_set_text_fmt(l_time, "%02d:%02d:%02d",
                              tm.tm_hour, tm.tm_min, tm.tm_sec);
        lv_obj_set_style_text_color(l_time, lv_color_hex(UI_ACC), 0);
    }

    // -- 离线区 --
    if (s->temp_ok) {                          // LVGL printf 无 %f, 0.1C 整数拼(同 SYSTEM 页)
        int t10 = (int)(s->temp_c * 10 + (s->temp_c >= 0 ? 0.5 : -0.5));
        lv_label_set_text_fmt(l_temp, "%d.%d C", t10 / 10, abs(t10 % 10));
    } else {
        lv_label_set_text(l_temp, "N/A");
    }

    if (s->soc >= 0) {
        if (s->mv > 0)
            lv_label_set_text_fmt(l_batt, "%d%%%d.%02dV", s->soc, s->mv / 1000, (s->mv % 1000) / 10);
        else
            lv_label_set_text_fmt(l_batt, "%d%%", s->soc);
        lv_bar_set_value(bar_batt, s->soc, LV_ANIM_OFF);
    } else {
        lv_label_set_text(l_batt, "N/A");
        lv_bar_set_value(bar_batt, 0, LV_ANIM_OFF);
    }

    size_t tot = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    if (!tot) tot = 400 * 1024;                // 兜底: 同 SYSTEM 页的 C3 DRAM 近似值
    lv_label_set_text_fmt(l_heap, "%d/%dK", (int)(s->heap_free / 1024), (int)(tot / 1024));
    lv_bar_set_value(bar_heap, (int32_t)(s->heap_free * 100 / tot), LV_ANIM_OFF);
}

static void build(void) {
    s_scr = ui_screen_create("PASSPORT");
    lv_obj_t *cont = ui_content_get(s_scr);
    ui_grid_bg_install(s_scr);                 // 底部留白的终端网格(同菜单屏)

    // wifi 点阵: 只建轮廓上的点(档位配色由 net_refresh 统一上)
    enum { NET_X = 206, NET_Y = 12, NET_PX = 3 };
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 7; c++) {
            s_net_dot[r][c] = NULL;
            if (!((NET_SHAPE[r] >> (6 - c)) & 1)) continue;
            lv_obj_t *o = lv_obj_create(s_scr);
            lv_obj_set_size(o, NET_PX, NET_PX);
            lv_obj_set_pos(o, NET_X + c * NET_PX, NET_Y + r * NET_PX);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(o, 0, 0);
            lv_obj_set_style_radius(o, 0, 0);
            lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
            s_net_dot[r][c] = o;
        }

    // -- 在线区: NET 状态 + NTP 时间(未同步显示占位) --
    l_net  = kv_make(cont, "NET",  "OFFLINE",  2, UI_DIM);
    ui_hline_make(cont, 2, 20, 212, UI_DARK);
    l_time = kv_make(cont, "TIME", "--:--:--", 27, UI_DIM);

    // -- 离线区: kv 网格 + 用量条 --
    l_temp = kv_make(cont, "TEMP", "", 45, UI_ACC);
    l_batt = kv_make(cont, "BATT", "", 63, UI_ACC);
    bar_batt = bar_make(cont, 81);

    l_heap = kv_make(cont, "HEAP", "", 99, UI_INK2);
    bar_heap = bar_make(cont, 117);

    // Flash: APP 镜像长/分区大小(静态, 无需定时器)
    const esp_partition_t *app =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    uint32_t ilen = app ? app_image_len(app) : 0;
    lv_obj_t *l_flash = kv_make(cont, "FLASH", "", 135, UI_INK2);
    if (app && ilen && ilen <= app->size) {
        lv_label_set_text_fmt(l_flash, "%dK/%dM",
                              (int)((ilen + 1023) / 1024),
                              (int)((app->size + 524288) / 1048576));   // MB 四舍五入
        bar_flash = bar_make(cont, 153);
        lv_bar_set_value(bar_flash, (int32_t)(ilen * 100 / app->size), LV_ANIM_OFF);
    } else {
        lv_label_set_text(l_flash, "N/A");
        bar_flash = bar_make(cont, 153);
    }

    ui_hline_make(cont, 2, 171, 212, UI_DARK);

    // 导航提示: 闪烁 ">" 终端待输入暗示(同状态栏光标节奏)
    lv_obj_t *prompt = ui_label_make(cont, ">");
    lv_obj_set_style_text_color(prompt, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(prompt, 2, 179);
    ui_anim_blink_start(prompt, 530);
    lv_obj_t *hint = ui_label_make(cont, "OK:MENU");
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(hint, 20, 179);

    timer_cb(NULL);
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
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
    // 主界面: OK 单击 = 进菜单
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK)
        ui_home_show();
}
