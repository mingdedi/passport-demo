// page_matrix.c -- 代码雨: 13 列 x 15 行字符雨, 拖尾 5 级绿色渐隐, 头部白绿高亮。
// 用 15 个行 label + LVGL recolor("#RRGGBB字符#") 实现逐字符颜色, 对象数与重绘面积最小。
#include "ui.h"

#include "esp_random.h"
#include <stdio.h>

#define MX_COLS   13          // 216 / 16
#define MX_ROWS   15          // 240 / 16
#define MX_TICK   80

static const char MX_CHARS[] = "0123456789ABCDEF$%&*+-<>=?@[]{}~/";

// 拖尾颜色梯度(按与头部距离 d): 0=白绿头, 1-2 荧光绿, 3-4 中绿, 5-6 暗绿, 其余极暗绿
static const char *const MX_GRAD[] = { "CFFFD9", "00FF66", "00FF66", "00D457", "00D457", "1FA24E", "1FA24E" };
#define MX_GRAD_N ((int)(sizeof(MX_GRAD) / sizeof(MX_GRAD[0])))

typedef struct {
    int head;               // 头部行号(可为负 = 尚未入场)
    int speed;              // 每行前进占用的 tick 数(1..3)
    int trail;              // 拖尾长度
    int wait;               // 距下次前进的剩余 tick
} mx_col_t;

static lv_obj_t *s_rows[MX_ROWS];
static lv_timer_t *s_timer;
static mx_col_t s_cols[MX_COLS];
static char s_grid[MX_ROWS][MX_COLS];

static void col_respawn(mx_col_t *col) {
    col->head  = -(int)(esp_random() % (MX_ROWS * 2));   // 入场前随机悬停
    col->speed = 1 + (int)(esp_random() % 3);
    col->trail = 5 + (int)(esp_random() % 5);
    col->wait  = (int)(esp_random() % 10);
}

static void rain_cb(lv_timer_t *t) {
    (void)t;

    for (int c = 0; c < MX_COLS; c++) {
        mx_col_t *col = &s_cols[c];

        if (col->wait > 0) { col->wait--; }
        else {
            col->head++;
            col->wait = col->speed - 1;
            if (col->head >= 0 && col->head < MX_ROWS)
                s_grid[col->head][c] = MX_CHARS[esp_random() % (sizeof(MX_CHARS) - 1)];
            // 拖尾中随机变异 1 个字符(经典矩阵雨效果)
            int d = (int)(esp_random() % col->trail);
            int r = col->head - d;
            if (r >= 0 && r < MX_ROWS)
                s_grid[r][c] = MX_CHARS[esp_random() % (sizeof(MX_CHARS) - 1)];
        }
        if (col->head - col->trail > MX_ROWS) col_respawn(col);
    }

    // 逐行渲染: 每字符 "#RRGGBB X#"(recolor 语法要求色码后必须有空格, 该空格不占宽) 或空格
    char buf[MX_COLS * 11 + 1];
    for (int r = 0; r < MX_ROWS; r++) {
        int p = 0;
        for (int c = 0; c < MX_COLS; c++) {
            int d = s_cols[c].head - r;             // 该格与所在列头部的距离
            if (d >= 0 && d <= s_cols[c].trail) {
                const char *g = d < MX_GRAD_N ? MX_GRAD[d] : "0E5C2C";
                p += sprintf(buf + p, "#%s %c#", g, s_grid[r][c]);
            } else {
                buf[p++] = ' ';
            }
        }
        buf[p] = '\0';
        lv_label_set_text(s_rows[r], buf);
    }
}

static void enter(lv_obj_t *root) {
    for (int r = 0; r < MX_ROWS; r++) {
        s_rows[r] = lv_label_create(root);
        lv_obj_set_style_text_font(s_rows[r], &lv_font_unscii_16, 0);
        lv_label_set_recolor(s_rows[r], true);
        lv_label_set_long_mode(s_rows[r], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(s_rows[r], 2, r * 16);
        lv_label_set_text(s_rows[r], "");
    }
    for (int c = 0; c < MX_COLS; c++) {
        col_respawn(&s_cols[c]);
        s_cols[c].wait = (int)(esp_random() % 30);  // 各列错开入场
    }

    rain_cb(NULL);
    s_timer = lv_timer_create(rain_cb, MX_TICK, NULL);
}

static void page_exit(void) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
}

const ui_page_t page_matrix = { .id = "MATRIX", .enter = enter, .exit = page_exit };
