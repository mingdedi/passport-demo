# AGENTS.md -- AI 编程助手项目说明

本文件面向 AI coding agent（Claude Code / Qwen Code / Codex 等），是理解与修改本项目的最短路径。人类开发者请先读 `README.md`。

## 项目定位

FoloToy AI Passport（ESP32-C3，8MB flash，无 PSRAM，240x320 圆角屏）的全功能演示固件：LVGL 9 黑绿终端风 UI，9 个交互页面覆盖全部板载外设。**目标芯片固定 esp32c3，工具链固定 ESP-IDF v5.5.x（实测 v5.5.5）**。

## 构建与烧录

```bash
# Linux（本仓库主开发环境）
source ~/.espressif/tools/activate_idf_v5.5.5.sh   # 或任意 ESP-IDF v5.5 导出脚本
idf.py build
idf.py -p /dev/ttyACM0 flash                        # 设备为原生 USB-Serial/JTAG 口

# 首次克隆后需要先选目标（会按 dependencies.lock 解析 managed_components/）
idf.py set-target esp32c3
```

- **`git pull` 后先查 `sdkconfig.defaults` 是否变化**：变了必须 `rm sdkconfig` 再 build（旧缓存不会自动更新，见修改守则 7；跨机器同步开发时尤其注意——曾因 `SNTP_MAX_SERVERS` 残留旧值导致 NTP/GLM 静默失效）。
- **无自动化测试**。验证方式 = `idf.py build` 编译通过 + 烧录后人工看屏。改完代码至少跑 build。
- 烧录报 port busy：设备深睡了（USB CDC 掉线），按键唤醒或重插。
- 本固件不读串口 RX：脚本长时间只写不读会塞满设备 FIFO 使 `write()` 挂起，是预期背压不是故障。
- 抓启动日志的复位序列：`dtr=False; rts=True; sleep 0.1; rts=False`（DTR 不放低时 RTS 复位不生效）。

## 代码结构

```
components/bsp/        板级支持包（源自 FoloToy/ai-passport，MIT）
  include/bsp_pins.h   ★ 硬件参数【单一事实来源】：引脚/ADC 窗口/LEDC/I2S 全在这
  src/bsp_*.c          display / button(ADC 三键) / audio(ES8311) / battery(CW2017) / i2c
main/
  main.c               入口 + 按键全局路由；UI_PAGES[] 页面注册表
  ui_theme.c           屏幕骨架：安全区外框/标题/内容区/状态栏（页面都基于它构建）
  ui_main.c            主界面（开机首屏）：品牌/大号电量 + [MENU] 按钮；OK=点击 MENU 进菜单
  ui_home.c            菜单屏 + 页面导航/转场/按键分发（菜单态 OK 长按回主界面）
  ui_boot.c            开机动画
  ui_anim.c            动效工具（滑入/淡入/闪烁/打字机）
  page_*.c             9 个演示页，一页一文件
  app_sensors.c        1s 周期采样服务（电池/温度/heap），快照只读供 UI 轮询
  app_wifi.c           WiFi 在线服务：5min 周期扫描目标 AP 自动连接 + NTP 上海时间
  wifi_secrets.h       WiFi 凭据（**git 不追踪**，模板 wifi_secrets.h.example；CMake 缺文件报错）
  app_glm.c            GLM 套餐用量服务：联网后 5min 查智谱额度接口，5h/周窗快照供主界面轮询
  glm_secrets.h        智谱 API Key（**git 不追踪**，模板 glm_secrets.h.example；CMake 缺文件报错）
  app_audio.c          ES8311 播放/录音/VU 单任务引擎
  app_ble.c            NimBLE 可连接广播
managed_components/    组件管理器拉取，勿手改（.gitignore 已排除）
partitions.csv         nvs 24K + phy 4K + factory 3MB（勿删，默认 1MB 装不下 ~1MB 镜像）
```

## 运行时架构

- **启动**：`app_main()` 初始化外设 → `ui_boot_play()` 开机动画 → 回调 `ui_main_show()` 主界面 → OK 单击进菜单屏（`ui_home_show()`）。
- **按键路由**：`bsp_button` 回调（button 组件定时器任务上下文）→ `main.c:on_key()` **先拿 LVGL 锁** → `ui_main_key()`（主界面态就地处理，其余转 `ui_home_key()`）→ 菜单态处理或分发到当前页 `key()`。页面内 OK 长按返回菜单、菜单态 OK 长按回主界面，均由 `ui_home_key` 统一处理。
- **页面模型**：`ui_page_t { id, enter, exit, key }`（ui.h）。`enter(root)` 构建页面（root 是内容容器，坐标相对容器）；`exit()` 清理页面私有资源（删自建 lv_timer 等）；`key()` 收按键事件。
- **线程/LVGL 锁模型**：页面 `enter/key/exit` 与 lv_timer 回调都已在 LVGL 上下文内，可直接调 LVGL API；**只有从其他任务碰 UI 才需要 `bsp_lvgl_lock()`**。服务模块（app_sensors 等）从不直接碰 UI，页面用 `lv_timer` 轮询快照（`app_sensors_snap()` 返回只读指针）。
- **按键枚举即下标**：`bsp_btn_t` 为 UP=0, DOWN=1, OK=2，页面里直接当数组索引用。

## 修改守则（硬约束，均来自实机踩坑）

1. **UI 文案每行 ≤14 字符**。`lv_font_unscii_16` 每字符步进 16px（非 8px），240px 宽只有 ~14 列；超宽会截断或叠印。数值显示须估算宽度（`lv_label_set_text_fmt` 的 %f 在小字宽下极易溢出，SYSTEM 页曾因此乱码）。
2. **安全区与圆角**：内容须收在 `UI_SAFE_*`（实测 L2/R1/T3/B1）内；外框 radius=26 是壳开孔圆角实测值，不得调小。
3. **按键查找表必须按 `bsp_btn_t` 枚举顺序排列**（UP/DOWN/OK）。INPUT 页曾按视觉顺序写表导致 DOWN/OK 显示互换（commit 35f7bf4）。
4. **页面退出函数命名 `page_exit`**，不能叫 `exit`（与 stdlib 冲突）。
5. **自建 `lv_timer` 的回调**操作页面对象前须 `lv_obj_is_valid()` 自查（屏幕转场 `auto_del` 会删掉它们），并在 `page_exit` 里删除 timer。page_input.c 的 `unhold_cb` 是参考写法。
6. **LVGL 9**：`lv_obj_align()` 是持久样式，之后再 `lv_obj_set_pos()` 会被覆盖；屏幕删除统一交给 `lv_screen_load_anim` 的 `auto_del=true`，勿手动删屏。
7. **改 `sdkconfig.defaults` 后必须删 `sdkconfig` 重新生成**；分区表自定义依赖其中的 `CONFIG_PARTITION_TABLE_CUSTOM=y`。
8. **新增页面**：写 `main/page_xxx.c` → `main/CMakeLists.txt` SRCS 加文件 → `main.c` 声明 extern 并加入 `UI_PAGES[]` → `ui.h` 的 `UI_PAGE_COUNT` +1。页面 ID ≤7 字符（菜单行格式 `NN NAME  ICON` 共 13 列）。
9. **硬件参数只改 `bsp_pins.h`**（含 ADC 按键电压窗口；改分压电阻后用 INPUT 页实测 mV 再改表）。
10. IDF 5.5 API 注意：`esp_app_desc.h`（main 需 REQUIRES `esp_app_format`）、`ESP_MAC_BASE`、`spi_flash_mmap`（`spi_flash_read` 已废弃）、NimBLE `adv_fields.flags` 是值不是指针。
11. **凭据卫生（开源项目）**：WiFi SSID/密码只准出现在 `main/wifi_secrets.h`，GLM API Key 只准出现在 `main/glm_secrets.h`（均已被 .gitignore 排除，模板 `*.example` 入库）；被追踪的代码/文档/提交信息里不得出现真实凭据。改凭据只改对应文件后重编译。

## 风格与提交约定

- 注释一律中文，写"为什么"而非"是什么"（现有代码风格如此，保持一致）。
- 提交信息中文，格式 `模块: 一句话描述`（例：`page_input: 修复按键名称表与 bsp_btn_t 枚举顺序错位`），正文可省略或写根因。
- `build/`、`build.win64.bak/`、`sdkconfig`、`managed_components/` 均不入库（.gitignore 已覆盖），提交前 `git status` 确认只暂存源码。
- 派生关系：`components/bsp/` 与工程骨架来自 FoloToy/ai-passport（MIT），保留原署名，勿删除 LICENSE/README 中的声明。
