# passport-demo

[English](#overview) | 简体中文说明见下

FoloToy AI Passport 黑绿终端风全功能演示固件：一个覆盖全部板载外设的自制 demo 固件，用于硬件验证与开发参考。

## Overview

passport-demo is a full-featured demo firmware for the [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) wearable AI hardware (ESP32-C3, 8MB flash, 240x320 round-corner LCD, ES8311 audio, CW2017 fuel gauge, 3-key ADC button). It renders a black-and-green terminal-style UI with LVGL and exposes nine interactive pages covering every onboard peripheral.

> **Attribution:** This project is derived from the official FoloToy development repository
> **https://github.com/FoloToy/ai-passport** (MIT License, © 2026 FoloToy).
> The `components/bsp/` board support package is taken from that repository, and `main/main.c`, `main/CMakeLists.txt`, `CMakeLists.txt`, `sdkconfig.defaults` are modified from it. All demo pages and UI code in `main/` are original work for this project.

## Pages

| # | Page | What it demonstrates |
|---|------|----------------------|
| 01 | SYSTEM | Chip info, MAC, reset reason, uptime, temperature, heap usage (live) |
| 02 | DISPLAY | 5 test patterns (color bars / grayscale / rainbow / marquee / blink blocks); UP/DOWN adjusts backlight (LEDC PWM) |
| 03 | AUDIO | Square-wave synth (beep / sweep / arpeggio) + live mic VU bars + 2s record & playback (hold DOWN) |
| 04 | POWER | CW2017 fuel gauge: big 7-seg SOC, battery icon, voltage history chart |
| 05 | INPUT | 3-key ADC button events: live highlight, CLICK/DOUBLE/LONG counters, raw ADC mV bar + window verdict |
| 06 | RADIO | NimBLE connectable advertising as `PASSPORT-DEMO`, ADV payload refreshed every second |
| 07 | STORAGE | Runtime partition table walk + app descriptor + `spi_flash_mmap` peek at the factory imgava partition |
| 08 | ABOUT | Open-source statement + easter egg; double-click OK for 10s deep sleep, double-click DOWN to reboot |
| 09 | MATRIX | Matrix-style digital rain animation |

## Interaction

Three buttons share one ADC pin (voltage-divider: UP≈0mV, DOWN≈300mV, OK≈595mV, release≈3300mV):

- **Menu**: UP/DOWN move the selection (row inverts green-on-black), OK opens the page.
- **In-page**: hold OK to go back (handled globally); pages layer their own keys on top.
- The status bar is always present (battery + voltage/heap/uptime rotating every 2s).
- Boot animation: random character shower that dissolves into the menu, followed by a boot arpeggio (OK skips).

## Firmware internals

- **UI**: LVGL 9.5 with the `lv_font_unscii_16` pixel monospace font, black-and-green terminal theme (`#050A06` background / `#00FF66` ink). Everything is drawn with labels/vector styles -- zero bitmap assets.
- **Character grid**: the font advances **16 px per glyph** (not 8 -- it is a 2x version of unscii_8), so the 240x320 panel is effectively a 14-column terminal. Keep every line of text within ~14 characters or it will clip/overlap.
- **Safe area**: the enclosure aperture eats into the panel edge. Measured on-device with a calibration firmware: L2/R1/T3/B1 px, corner radius r=26. `ui.h` exposes `UI_SAFE_*`; the frame in `ui_theme.c` is `(2,5) 237x314 r26` -- the radius must stay ≥26 or the frame corners get bitten by the shell.
- **Partitions**: custom `partitions.csv` (nvs 24K + phy 4K + factory 3MB) matching the factory layout. The default 1MB app partition overflows once BLE+LVGL push the image to ~1MB.
- **LVGL memory pool**: 48KB (`CONFIG_LV_MEM_SIZE_KILOBYTES=48`, one step above the 24KB BSP baseline) to fit the charts and animations.
- **Managed components** (pinned via `dependencies.lock`): lvgl 9.5.0, esp_lvgl_port 2.9.0, button 4.2.0, esp_codec_dev 1.6.2; NimBLE comes from IDF itself.

## Build & Flash

Requires ESP-IDF v5.5.x (tested with v5.5.5) with the `esp32c3` target:

```bash
idf.py set-target esp32c3   # first time; resolves managed_components from dependencies.lock
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # device exposes a native USB-Serial/JTAG port
```

## Layout

```
main/
  main.c          app entry, peripheral init, global key routing
  ui_boot.c       boot animation (character shower -> menu reveal)
  ui_home.c       main menu + navigation (enter/back/transitions/key routing)
  ui_theme.c      screen scaffold: safe-area frame, title, status bar, panels
  ui_anim.c       shared animation helpers (slide/fade/blink/typewriter)
  page_*.c        the nine demo pages
  app_sensors.c   1Hz sampler (CW2017 / tsens / heap) feeding status bar & pages
  app_audio.c     ES8311 engine: play / record / VU in one audio task
  app_ble.c       NimBLE GAP connectable advertising
components/bsp/   board support package - from FoloToy/ai-passport
                 (single source of truth for hardware params: bsp_pins.h)
partitions.csv    factory-like layout: nvs / phy_init / factory(3MB)
```

## Engineering notes

Pitfalls hit while building this firmware, all verified on hardware:

1. `sdkconfig.defaults` must explicitly set `CONFIG_PARTITION_TABLE_CUSTOM=y` (otherwise the default 1MB layout is flashed), and after editing defaults you must delete `sdkconfig` so it regenerates.
2. ESP-IDF 5.5 renames/breaks: header `esp_app_desc.h` (main needs `REQUIRES esp_app_format`), `ESP_MAC_BASE` (not the old EFUSE API), `spi_flash_read` deprecated (use `spi_flash_mmap` to read unmapped partitions); NimBLE `adv_fields.flags` is a value, not a pointer.
3. LVGL 9 traps: `lv_obj_align()` is a persistent style, so a later `lv_obj_set_pos()` on the same object gets overridden; self-created `lv_timer` callbacks must `lv_obj_is_valid()`-check their labels before touching them, because screens (and their children) are deleted by transitions with `auto_del`.
4. Don't name a page exit function `static void exit(void)` (clashes with stdlib `exit(int)`); the convention here is `page_exit`.
5. A boot-finish animation racing the screen transition once caused a use-after-free crash loop (two animations on one screen + ready_cb deleting objects). Fixed by letting the transition's `auto_del=true` own the lifetime -- don't manually delete screens around `lv_screen_load_anim`.
6. Deep sleep drops the USB-Serial/JTAG CDC port, and flashing then reports "port busy"; press any key to wake the device or replug.
7. This firmware never reads its console RX line. Writing to the serial port for a long time without reading fills the device-side RX FIFO and host `write()` blocks -- that is expected backpressure, not a fault.
8. `bsp_btn_t` enum values (UP=0, DOWN=1, OK=2) are used directly as array indices in pages; any per-key lookup table must follow this exact order (the INPUT page once shipped with a visually ordered table and showed OK/DN swapped).

## License

[MIT](LICENSE) — © 2026 FoloToy (original BSP and project skeleton), © 2026 mingmingde (demo application).

---

## 项目说明（中文）

本仓库是为 FoloToy AI Passport 智能徽章编写的全功能演示固件，采用黑绿终端风格 UI（LVGL），包含 9 个交互页面，覆盖全部板载外设（系统信息 / 屏幕 / 音频 / 电池 / 按键 / BLE / 存储 / 电源管理 / 矩阵动画）。

**项目来源声明**：本项目派生自 FoloToy 官方开发仓库 **https://github.com/FoloToy/ai-passport**（MIT 协议，© 2026 FoloToy）。其中 `components/bsp/` 板级支持包直接取自官方仓库；`main/main.c`、`main/CMakeLists.txt`、`CMakeLists.txt`、`sdkconfig.defaults` 基于官方版本修改；`main/` 下其余演示页面与 UI 代码为本项目原创。

编译需要 ESP-IDF v5.5.x（实测 v5.5.5），构建与烧录命令同上文英文部分。协议为 [MIT](LICENSE)。

### 固件细节

- **UI**：LVGL 9.5 + `lv_font_unscii_16` 像素等宽字体，黑底荧光绿（`#050A06`/`#00FF66`），全部矢量/文本绘制，零位图资源。
- **字符网格**：该字体每字符步进 **16px**（非 8px，本质是 unscii_8 的 2x 版），240x320 屏实际是一台 14 列的"大字终端"，每行文案请控制在 14 字符内，否则截断/叠印。
- **安全区**：外壳开孔会吃掉屏幕边缘，实机校准值为 L2/R1/T3/B1 px、开孔圆角 r=26（`ui.h` 的 `UI_SAFE_*` 与 `ui_theme.c` 外框 `(2,5) 237x314 r26`；圆角不得小于 26，否则框角被壳咬掉）。
- **分区表**：自定义 `partitions.csv`（nvs 24K + phy 4K + factory 3MB，对齐出厂布局；默认 1MB 分区装不下 BLE+LVGL 的 ~1MB 镜像）。
- **LVGL 内存池**：48KB（官方 BSP 基线 24KB 加大一档，容纳折线图与动画）。
- **交互**：三键共用一个 ADC 引脚分压识别（UP≈0mV / DOWN≈300mV / OK≈595mV / 松开≈3300mV）；菜单 UP/DOWN 选位（选中行整行反色）、OK 进页、**OK 长按返回**；状态栏常驻（电量 + 电压/heap/uptime 每 2s 轮换）；开机动画为随机字符雨消散露出菜单 + 开机琶音（OK 跳过）。

### 关键工程注记

二次开发前建议先读英文部分的 Engineering notes，要点：改 `sdkconfig.defaults` 后必须删 `sdkconfig` 重新生成；IDF 5.5 的 API 更名（`esp_app_desc.h`、`ESP_MAC_BASE`、`spi_flash_mmap`、NimBLE `adv_fields.flags` 值语义）；LVGL 9 的 `lv_obj_align()` 持久对齐会覆盖后续 `lv_obj_set_pos()`、自建 `lv_timer` 回调须 `lv_obj_is_valid()` 自查；页面退出函数统一命名 `page_exit`；深睡会掉 USB CDC 口（烧录报 port busy 时按键唤醒）；本固件不读串口 RX，长时间只写不读会塞满设备侧 FIFO 使 `write()` 阻塞（属预期背压非故障）；页面把 `bsp_btn_t` 枚举值（UP=0/DOWN=1/OK=2）直接当数组下标用，任何按键查找表必须按此顺序排列。
