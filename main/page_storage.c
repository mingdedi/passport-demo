// main/page_storage.c -- Flash 地图: 分区表枚举 + APP 描述 + 出厂 imgava 区魔数。
#include "ui.h"

#include "esp_partition.h"
#include "esp_app_desc.h"
#include "spi_flash_mmap.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// 出厂固件 imgava 分区位置(当前 demo 分区表未映射, 但数据仍在物理 Flash 上)
#define FACT_IMGAVA_OFF 0x3FA000

static const char *short_name(const char *label) {
    if (!strcmp(label, "phy_init")) return "phy";
    if (!strcmp(label, "factory"))  return "app";
    return label;
}

static void enter(lv_obj_t *root) {
    int y = 2;

    // --- 分区表 ---
    lv_obj_t *h1 = ui_label_make(root, "PARTITIONS");
    lv_obj_set_style_text_color(h1, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(h1, 2, y);
    y += 18;

    lv_obj_t *hdr = ui_label_make(root, "NAME OFF SZ");
    lv_obj_set_style_text_color(hdr, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(hdr, 2, y);
    y += 18;

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    int nrows = 0;
    while (it != NULL && nrows < 5) {
        const esp_partition_t *p = esp_partition_get(it);
        int kb = (int)(p->size / 1024);
        char sz[8];
        if (kb < 1024) snprintf(sz, sizeof(sz), "%dK", kb);
        else           snprintf(sz, sizeof(sz), "%dM", kb / 1024);
        lv_obj_t *l = ui_label_make(root, "");
        lv_label_set_text_fmt(l, "%-4s %04X %s",
                              short_name(p->label), (unsigned)p->address, sz);
        lv_obj_set_style_text_color(l, lv_color_hex(UI_INK2), 0);
        lv_obj_set_pos(l, 2, y);
        y += 17;
        nrows++;
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    y += 6;

    // --- 运行 APP 描述 ---
    const esp_app_desc_t *app = esp_app_get_description();
    lv_obj_t *al = ui_label_make(root, app->project_name);
    lv_obj_set_style_text_color(al, lv_color_hex(UI_ACC), 0);
    lv_obj_set_pos(al, 2, y);
    y += 17;

    char vbuf[16];
    const char *md = app->date;      // "Aug 22 2026"
    char mon[4] = {0};
    if (strlen(md) >= 6) { mon[0] = md[0]; mon[1] = md[1]; mon[2] = md[2]; }
    char up[4] = { toupper((unsigned char)mon[0]), toupper((unsigned char)mon[1]), 0 };
    snprintf(vbuf, sizeof(vbuf), "V%.5s %.3s%.2s", app->version, up, md + 4);
    lv_obj_t *bl = ui_label_make(root, vbuf);
    lv_obj_set_style_text_color(bl, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(bl, 2, y);
    y += 22;

    // --- 出厂 imgava 区魔数(数据残留在物理 Flash, 未被本分区表映射) ---
    lv_obj_t *h2 = ui_label_make(root, "RAW @3FA000");
    lv_obj_set_style_text_color(h2, lv_color_hex(UI_DIM), 0);
    lv_obj_set_pos(h2, 2, y);
    y += 18;

    uint8_t buf[8];
    spi_flash_mmap_handle_t mmap_h;
    const uint8_t *mmap_p = NULL;
    esp_err_t err = spi_flash_mmap(FACT_IMGAVA_OFF & ~(SPI_FLASH_MMU_PAGE_SIZE - 1),
                                   SPI_FLASH_MMU_PAGE_SIZE, SPI_FLASH_MMAP_DATA,
                                   (const void **)&mmap_p, &mmap_h);
    if (err == ESP_OK) {
        uint32_t off_in_page = FACT_IMGAVA_OFF & (SPI_FLASH_MMU_PAGE_SIZE - 1);
        memcpy(buf, mmap_p + off_in_page, sizeof(buf));
        spi_flash_munmap(mmap_h);
    }
    if (err == ESP_OK) {
        char line[20];
        snprintf(line, sizeof(line), "%02X %02X %02X %02X",
                 buf[0], buf[1], buf[2], buf[3]);
        bool ava = (buf[0] == 'A' && buf[1] == 'V' && buf[2] == 'A' && buf[3] == '1');
        lv_obj_t *hl = ui_label_make(root, line);
        lv_obj_set_style_text_color(hl, lv_color_hex(UI_INK), 0);
        lv_obj_set_pos(hl, 2, y);
        y += 17;
        lv_obj_t *ml = ui_label_make(root, ava ? "AVA1 FOUND" : "AREA ERASED");
        lv_obj_set_style_text_color(ml, lv_color_hex(ava ? UI_ACC : UI_DARK), 0);
        lv_obj_set_pos(ml, 2, y);
        y += 22;
    } else {
        lv_obj_t *el = ui_label_make(root, "READ FAIL");
        lv_obj_set_style_text_color(el, lv_color_hex(UI_RED), 0);
        lv_obj_set_pos(el, 2, y);
        y += 22;
    }

    // --- Flash 用量条 ---
    lv_obj_t *ul = ui_label_make(root, "DEMO ~1MB/8MB");
    lv_obj_set_style_text_color(ul, lv_color_hex(UI_DARK), 0);
    lv_obj_set_pos(ul, 2, y);

    lv_obj_t *bar = lv_bar_create(root);
    lv_obj_set_size(bar, 208, 10);
    lv_obj_set_pos(bar, 2, y + 18);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 15, LV_ANIM_OFF);      // ~0x9E000/0x310000
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_BG2), 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(UI_DARK), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_INK2), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

const ui_page_t page_storage = { .id = "STORAGE", .enter = enter };
