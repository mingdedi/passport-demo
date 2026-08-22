// main/ui_anim.c -- 动效工具: 滑入/淡入/闪烁/打字机/扫描线。
#include "ui.h"

#include <string.h>

static void slide_exec_cb(void *var, int32_t v) {
    lv_obj_set_x((lv_obj_t *)var, v);
}

void ui_anim_slide_in(lv_obj_t *obj, int32_t from_dx, uint32_t dur) {
    int32_t x0 = lv_obj_get_x(obj);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, slide_exec_cb);
    lv_anim_set_values(&a, x0 + from_dx, x0);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
}

static void fade_exec_cb(void *var, int32_t v) {
    lv_obj_set_style_bg_opa((lv_obj_t *)var, v, 0);
}

void ui_anim_fade_in(lv_obj_t *obj, uint32_t dur) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, fade_exec_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, dur);
    lv_anim_start(&a);
}

// 闪烁: opa 在 0/cover 间切换(用于光标/状态点)
typedef struct {
    lv_obj_t *obj;
    bool on;
} blink_ctx_t;

static void blink_timer_cb(lv_timer_t *t) {
    blink_ctx_t *ctx = lv_timer_get_user_data(t);
    // 宿主对象随屏幕转场删除后, 定时器自救退出(防 UAF)
    if (!lv_obj_is_valid(ctx->obj)) {
        lv_timer_del(t);
        free(ctx);
        return;
    }
    ctx->on = !ctx->on;
    lv_obj_set_style_text_opa(ctx->obj, ctx->on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

lv_timer_t *ui_anim_blink_start(lv_obj_t *obj, uint32_t period_ms) {
    blink_ctx_t *ctx = malloc(sizeof(blink_ctx_t));
    if (!ctx) return NULL;
    ctx->obj = obj;
    ctx->on = true;
    lv_timer_t *tm = lv_timer_create(blink_timer_cb, period_ms, ctx);
    lv_timer_set_repeat_count(tm, -1);
    return tm;
}

// 打字机: 逐字符显示完整文本
typedef struct {
    lv_obj_t *label;
    const char *full;
    uint32_t n;
} tw_ctx_t;

static void tw_timer_cb(lv_timer_t *t) {
    tw_ctx_t *ctx = lv_timer_get_user_data(t);
    // 宿主 label 随屏幕删除后自救退出(防 UAF)
    if (!lv_obj_is_valid(ctx->label)) {
        lv_timer_del(t);
        free(ctx);
        return;
    }
    ctx->n++;
    if (ctx->n >= strlen(ctx->full)) {
        lv_label_set_text(ctx->label, ctx->full);
        lv_timer_del(t);
        free(ctx);
        return;
    }
    lv_label_set_text_fmt(ctx->label, "%.*s", (int)ctx->n, ctx->full);
}

void ui_typewriter_run(lv_obj_t *label, const char *full, uint16_t ms_per_char) {
    tw_ctx_t *ctx = malloc(sizeof(tw_ctx_t));
    if (!ctx) { lv_label_set_text(label, full); return; }
    ctx->label = label;
    ctx->full = full;
    ctx->n = 0;
    lv_label_set_text(label, "");
    lv_timer_t *tm = lv_timer_create(tw_timer_cb, ms_per_char, ctx);
    lv_timer_set_repeat_count(tm, -1);
}
