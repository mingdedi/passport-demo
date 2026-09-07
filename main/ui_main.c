// main/ui_main.c -- 主界面(开机首屏): 系统仪表盘(在线区+离线区+GLM 用量区)。
// 在线区: NET 状态(联动标题行 wifi 点阵图标)与 NTP 上海时间; 离线区: 温度/
// 电池/内存/Flash 纯 kv 行(无用量条, 竖向空间让给 GLM 区); GLM 区: 5h/周套餐
// 用量(app_glm 5min 快照), 每窗三行(数值/百分比条/重置), 值列左移 G_X。
// WiFi/GLM 服务快照由各自服务提供, 本屏 1s 轮询。
// 键位: OK 长按进菜单(菜单态 OK 长按回本屏); 双击 OK 立即刷新
// WiFi 扫描/NTP/GLM 额度(不等各自周期)。
#include "ui.h"
#include "app_sensors.h"
#include "app_wifi.h"
#include "app_glm.h"

#include "esp_partition.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// kv 列基线: 系统区同 page_sysinfo(键 <=5ch, 值 <=8ch, 86+128=214);
// GLM 区键短, 值列左移 G_X 放宽到 10ch(54+160=214), 收在容器宽 216 内
#define K_X 2
#define V_X 86
#define G_X 54

static lv_obj_t *s_scr;
static lv_timer_t *s_timer;
static lv_obj_t *l_net, *l_time, *l_temp, *l_batt, *l_heap;
static lv_obj_t *l_glm, *l_5h, *l_wk, *l_r5, *l_rw;
static lv_obj_t *bar_5h, *bar_wk;

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

// kv 行(列位可指定); kv_make=系统区基线, GLM 区用 kv_row 左移值列
static lv_obj_t *kv_row(lv_obj_t *root, const char *key, int32_t kx,
                        const char *val, int32_t vx, int32_t y, uint32_t vcol) {
    lv_obj_t *k = ui_label_make(root, key);
    lv_obj_set_style_text_color(k, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(k, kx, y);
    lv_obj_t *v = ui_label_make(root, val);
    lv_obj_set_style_text_color(v, lv_color_hex(vcol), 0);
    lv_obj_set_pos(v, vx, y);
    return v;
}

static lv_obj_t *kv_make(lv_obj_t *root, const char *key, const char *val,
                         int32_t y, uint32_t vcol) {
    return kv_row(root, key, K_X, val, V_X, y, vcol);
}

// 用量条(同分支页样式: 208x10, 暗底细框), GLM 两窗百分比
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

// ---- GLM 用量区 ----
// 额度缩写: <1万原样; K/M 档只有一位整数时带一位小数(9.9K), 否则取整(12K/17M)。
// 单值 <=4 字符, "已用/总额" 卡进 GLM 值列 10 字符宽(1074/12K)
static void fmt_q(char *out, int64_t v) {
    if (v < 0) { strcpy(out, "?"); return; }
    if (v < 10000) {
        sprintf(out, "%d", (int)v);
    } else if (v < 1000000) {
        int k = (int)((v + 500) / 1000);
        if (k < 10) sprintf(out, "%d.%dK", k / 10, k % 10);
        else sprintf(out, "%dK", k);
    } else {
        int m = (int)((v + 500000) / 1000000);
        if (m < 10) sprintf(out, "%d.%dM", m / 10, m % 10);
        else sprintf(out, "%dM", m > 999 ? 999 : m);
    }
}

static void win_refresh(const app_glm_snap_t *g, bool is5,
                        lv_obj_t *val, lv_obj_t *bar, lv_obj_t *rst) {
    if (is5 ? g->has5 : g->hasw) {
        char a[8], b[8], v[16];
        fmt_q(a, is5 ? g->used5 : g->usedw);
        fmt_q(b, is5 ? g->total5 : g->totalw);
        sprintf(v, "%s/%s", a, b);
        lv_label_set_text(val, v);
        lv_obj_set_style_text_color(val, lv_color_hex(UI_INK2), 0);
        lv_bar_set_value(bar, is5 ? g->pct5 : g->pctw, LV_ANIM_OFF);
        struct tm tm;
        time_t t = (time_t)((is5 ? g->reset5_ms : g->resetw_ms) / 1000);
        localtime_r(&t, &tm);                    // TZ=上海, app_wifi 同步时已设
        if (is5)  // 5h 窗看重置时刻(今天几点), 周窗看重置日期(几月几号)
            lv_label_set_text_fmt(rst, "%d%%R%02d:%02d", g->pct5, tm.tm_hour, tm.tm_min);
        else
            lv_label_set_text_fmt(rst, "%d%%R%02d-%02d", g->pctw, tm.tm_mon + 1, tm.tm_mday);
    } else {
        lv_label_set_text(val, "N/A");
        lv_obj_set_style_text_color(val, lv_color_hex(UI_DIM), 0);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_label_set_text(rst, "-");
    }
}

// 快照变化才重绘(拼接多, 避免每秒重跑); 离线时把 WAIT 细化为 NO NET
static void glm_refresh(const app_wifi_snap_t *w) {
    static app_glm_snap_t last;
    const app_glm_snap_t *g = app_glm_snap();
    if (memcmp(&last, g, sizeof last) == 0) return;
    last = *g;

    const char *txt = "WAIT";
    uint32_t col = UI_DIM;
    char tbuf[16];
    switch (g->state) {
    case APP_GLM_OK: {
        struct tm tm;
        localtime_r(&g->fetched_at, &tm);
        snprintf(tbuf, sizeof tbuf, "OK %02d:%02d", tm.tm_hour, tm.tm_min);
        txt = tbuf; col = UI_ACC;
        break;
    }
    case APP_GLM_FETCH: txt = "..."; col = UI_INK; break;
    case APP_GLM_NOKEY: txt = "NO KEY"; col = UI_WARN; break;
    case APP_GLM_ERR:
        if (g->http_err == 401) { txt = "KEY ERR"; col = UI_RED; }
        else if (g->http_err > 0) { snprintf(tbuf, sizeof tbuf, "HTTP %d", g->http_err); txt = tbuf; col = UI_WARN; }
        else { txt = "NO RESP"; col = UI_WARN; }
        break;
    default: break;
    }
    if (g->state == APP_GLM_WAIT && w->state != APP_WIFI_ONLINE) txt = "NO NET";
    lv_label_set_text(l_glm, txt);
    lv_obj_set_style_text_color(l_glm, lv_color_hex(col), 0);

    win_refresh(g, true, l_5h, bar_5h, l_r5);
    win_refresh(g, false, l_wk, bar_wk, l_rw);
}

// 主屏常驻(仅构建一次), 本定时器与状态栏定时器同寿命, 无需清理
static void timer_cb(lv_timer_t *t) {
    (void)t;
    const app_sensors_snap_t *s = app_sensors_snap();
    const app_wifi_snap_t *w = app_wifi_snap();

    // -- 在线区 --
    net_refresh(w);
    glm_refresh(w);
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
        // 满电 100% 再带电压会 9 字符溢出值列, 只显示百分比
        if (s->mv > 0 && s->soc < 100)
            lv_label_set_text_fmt(l_batt, "%d%%%d.%02dV", s->soc, s->mv / 1000, (s->mv % 1000) / 10);
        else
            lv_label_set_text_fmt(l_batt, "%d%%", s->soc);
    } else {
        lv_label_set_text(l_batt, "N/A");
    }

    size_t tot = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    if (!tot) tot = 400 * 1024;                // 兜底: 同 SYSTEM 页的 C3 DRAM 近似值
    lv_label_set_text_fmt(l_heap, "%d/%dK", (int)(s->heap_free / 1024), (int)(tot / 1024));
}

static void build(void) {
    s_scr = ui_screen_create("PASSPORT");
    lv_obj_t *cont = ui_content_get(s_scr);    // 内容区 216x250, 以下均为容器局部坐标

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

    // -- 离线区: 纯 kv 行(电量/内存/Flash 无用量条, 竖向空间让给 GLM 区) --
    l_temp = kv_make(cont, "TEMP", "", 45, UI_ACC);
    l_batt = kv_make(cont, "BATT", "", 63, UI_ACC);
    l_heap = kv_make(cont, "HEAP", "", 81, UI_INK2);

    // Flash: APP 镜像长/分区大小(静态, 无需定时器)
    const esp_partition_t *app =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    uint32_t ilen = app ? app_image_len(app) : 0;
    lv_obj_t *l_flash = kv_make(cont, "FLASH", "", 99, UI_INK2);
    if (app && ilen && ilen <= app->size) {
        lv_label_set_text_fmt(l_flash, "%dK/%dM",
                              (int)((ilen + 1023) / 1024),
                              (int)((app->size + 524288) / 1048576));   // MB 四舍五入
    } else {
        lv_label_set_text(l_flash, "N/A");
    }

    ui_hline_make(cont, 2, 117, 212, UI_DARK);

    // -- GLM 用量区: 每窗三行(已用/总额 + 百分比条 + 百分比&重置), 值列左移 G_X --
    l_glm  = kv_row(cont, "GLM", K_X, "WAIT", G_X, 125, UI_DIM);
    l_5h   = kv_row(cont, "5H",  K_X, "N/A",  G_X, 143, UI_DIM);
    bar_5h = bar_make(cont, 161);
    l_r5   = ui_label_make(cont, "-");          // 子行无键, 与值列对齐缩进
    lv_obj_set_style_text_color(l_r5, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(l_r5, G_X, 173);
    l_wk   = kv_row(cont, "WK",  K_X, "N/A",  G_X, 191, UI_DIM);
    bar_wk = bar_make(cont, 209);
    l_rw   = ui_label_make(cont, "-");
    lv_obj_set_style_text_color(l_rw, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(l_rw, G_X, 221);

    ui_hline_make(cont, 2, 239, 212, UI_DARK);

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
    // 主界面: OK 长按进菜单; 双击立即刷新在线数据(屏幕上 NET/GLM 状态即时反馈)
    if (btn != BSP_BTN_OK) return;
    if (ev == BSP_BTN_LONG) {
        ui_home_show();
    } else if (ev == BSP_BTN_DOUBLE) {
        app_wifi_resync();
        app_glm_refresh_now();
    }
}
