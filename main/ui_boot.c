// main/ui_boot.c -- 开机动画: 满屏随机字符闪烁 -> 满屏时底下瞬时载入主界面 -> 字符消散露出主界面。
// 先强制刷屏再开背光, 面板点亮的第一帧即字符画面, 不会露出白底。
#include "ui.h"
#include "bsp_display.h"

#include "esp_random.h"
#include <string.h>

#define BOOT_ROWS 20          // 320 / 16
#define BOOT_COLS 15          // 240 / 16
#define TICK_MS   40
#define SHIMMER   8           // 满屏闪烁 tick 数 (~320ms)
#define STEP      25          // 消散时每 tick 密度 -25% (~160ms)

static const char BOOT_CHARS[] = "0123456789ABCDEF#$%&*+-<>=?@[]{}~";
static const uint32_t BOOT_COLORS[] = { UI_INK, UI_DIM, UI_INK2 };

typedef struct {
    lv_obj_t *mask;               // layer_top 上的全屏字符遮罩(不随屏幕删除)
    lv_obj_t *rows[BOOT_ROWS];
    lv_timer_t *tm;
    void (*on_done)(void);        // 在满屏遮罩底下换出主界面
    int8_t density;               // 0..100
    int8_t phase;                 // 0=闪烁 1=消散
    int8_t shim;
    bool swapped;
} boot_t;

static boot_t s_boot;

static void render(boot_t *b) {
    // 遮罩底色不透明度 = 密度^2: 消散时迅速透出底下的主界面
    lv_obj_set_style_bg_opa(b->mask,
        (lv_opa_t)(b->density * b->density * LV_OPA_COVER / 10000), 0);

    char buf[BOOT_COLS + 1];
    for (int r = 0; r < BOOT_ROWS; r++) {
        for (int c = 0; c < BOOT_COLS; c++) {
            if (b->density > 0 && (int)(esp_random() % 100) < b->density)
                buf[c] = BOOT_CHARS[esp_random() % (sizeof(BOOT_CHARS) - 1)];
            else
                buf[c] = ' ';
        }
        buf[BOOT_COLS] = '\0';
        lv_label_set_text(b->rows[r], buf);
    }
}

static void swap(boot_t *b) {
    if (b->swapped) return;
    b->swapped = true;
    if (b->on_done) b->on_done();       // 主界面在遮罩底下瞬时加载
}

static void finish(boot_t *b) {
    lv_timer_del(b->tm);
    lv_obj_delete(b->mask);
    memset(&s_boot, 0, sizeof(s_boot));
}

static void boot_timer_cb(lv_timer_t *t) {
    boot_t *b = lv_timer_get_user_data(t);

    if (b->phase == 0) {                // 满屏闪烁, 首拍换屏
        swap(b);
        if (--b->shim <= 0) b->phase = 1;
    } else {                            // 消散
        b->density -= STEP;
        if (b->density <= 0) { finish(b); return; }
    }
    render(b);
}

void ui_boot_play(void (*on_done)(void)) {
    boot_t *b = &s_boot;
    memset(b, 0, sizeof(*b));
    b->on_done = on_done;
    b->density = 100;
    b->shim = SHIMMER;

    b->mask = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(b->mask, 0, 0);
    lv_obj_set_size(b->mask, 240, 320);
    lv_obj_set_style_bg_color(b->mask, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(b->mask, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b->mask, 0, 0);
    lv_obj_set_style_pad_all(b->mask, 0, 0);
    lv_obj_clear_flag(b->mask, LV_OBJ_FLAG_SCROLLABLE);

    for (int r = 0; r < BOOT_ROWS; r++) {
        lv_obj_t *row = lv_label_create(b->mask);
        lv_obj_set_style_text_font(row, &lv_font_unscii_16, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(BOOT_COLORS[r % 3]), 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(row, 0, r * 16);
        b->rows[r] = row;
    }

    render(b);                          // 密度 100: 首帧即满屏字符
    lv_refr_now(NULL);                  // 先刷进面板 GRAM, 再开背光, 杜绝白底闪现
    bsp_display_backlight(30);
    bsp_display_backlight(100);

    b->tm = lv_timer_create(boot_timer_cb, TICK_MS, b);
}

bool ui_boot_active(void) { return s_boot.tm != NULL; }

void ui_boot_skip(void) {
    if (!ui_boot_active()) return;
    swap(&s_boot);                      // 底下换出主界面
    finish(&s_boot);                    // 立即撤遮罩
}
