# passport-demo

[English](#overview) | 简体中文说明见下

FoloToy AI Passport 黑绿终端风全功能演示固件：一个覆盖全部板载外设的自制 demo 固件，用于硬件验证与开发参考。

## Overview

passport-demo is a full-featured demo firmware for the [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) wearable AI hardware (ESP32-C3, 8MB flash, 240×240 round-corner LCD, ES8311 audio, CW2017 fuel gauge, 3-key ADC button). It renders a black-and-green terminal-style UI with LVGL and exposes nine interactive pages covering every onboard peripheral.

> **Attribution:** This project is derived from the official FoloToy development repository
> **https://github.com/FoloToy/ai-passport** (MIT License, © 2026 FoloToy).
> The `components/bsp/` board support package is taken from that repository, and `main/main.c`, `main/CMakeLists.txt`, `CMakeLists.txt`, `sdkconfig.defaults` are modified from it. All demo pages and UI code in `main/` are original work for this project.

## Pages

| # | Page | What it demonstrates |
|---|------|----------------------|
| 01 | SYSTEM | Chip info, MAC, reset reason, uptime, temperature, heap usage (live) |
| 02 | DISPLAY | Backlight levels and brightness test patterns |
| 03 | AUDIO | ES8311 playback/recording loopback test |
| 04 | BATTERY | CW2017 SOC, voltage, charge status (live) |
| 05 | INPUT | 3-key ADC button events (press/click/long-press) |
| 06 | RADIO | NimBLE connectable advertising as `PASSPORT-DEMO` |
| 07 | STORAGE | Flash partition map and free space |
| 08 | ABOUT | Deep-sleep / reboot with double-click confirm, plus an easter egg |
| 09 | MATRIX | Matrix-style digital rain animation |

## Build & Flash

Requires ESP-IDF v5.5.x (tested with v5.5.5) with the `esp32c3` target:

```bash
idf.py set-target esp32c3   # first time; resolves managed_components from dependencies.lock
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # device exposes a native USB-Serial/JTAG port
```

## Layout

```
main/            demo application (pages, UI, boot animation) — original
components/bsp/  board support package — from FoloToy/ai-passport
partitions.csv   factory-like layout: nvs / phy_init / factory(3MB)
```

## License

[MIT](LICENSE) — © 2026 FoloToy (original BSP and project skeleton), © 2026 mingmingde (demo application).

---

## 项目说明（中文）

本仓库是为 FoloToy AI Passport 智能徽章编写的全功能演示固件，采用黑绿终端风格 UI（LVGL），包含 9 个交互页面，覆盖全部板载外设（系统信息 / 屏幕 / 音频 / 电池 / 按键 / BLE / 存储 / 电源管理 / 矩阵动画）。

**项目来源声明**：本项目派生自 FoloToy 官方开发仓库 **https://github.com/FoloToy/ai-passport**（MIT 协议，© 2026 FoloToy）。其中 `components/bsp/` 板级支持包直接取自官方仓库；`main/main.c`、`main/CMakeLists.txt`、`CMakeLists.txt`、`sdkconfig.defaults` 基于官方版本修改；`main/` 下其余演示页面与 UI 代码为本项目原创。

编译需要 ESP-IDF v5.5.x（实测 v5.5.5），构建与烧录命令同上文英文部分。协议为 [MIT](LICENSE)。
