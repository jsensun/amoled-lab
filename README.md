# MONO · EDITORIAL 表盘

> 为 **微雪 Waveshare ESP32-S3-Touch-AMOLED-1.8（V2）** 打造的一块极简编辑排版风格智能表盘。
> **三页卡片**：主页 MONO（时钟）· 天气 DOT（仪表盘）· 日历 LIT（书籍摘抄）。
> 左右滑动切换 · 纯黑 AMOLED · 118px 大字时间 · 实时天气 · 电量仪表 · 每日摘抄。

---

## ✨ 特性

### 主页 MONO（时钟）
- **NTP 自动对时** —— 联网后时间永远准确，断电不断时（RTC 后备）
- **118px 大字时间** + 实时天气（温度 + 天况）
- **电量仪表** —— 直读 AXP2101 电源芯片，充电时显示 `xx%+`
- **每日金句** —— 内置 10 句经典语录按日轮换
- **汉字竖排日期栏** —— 「二〇二六年・八月廿一日」杂志版权页式排版 + 一年进度彩蛋

### 天气 DOT（仪表盘）
- **大温度** 118px + 湿度/风速/气压三列指标
- **24h 温度趋势** —— 圆角渐变柱 / 平滑曲线 两种模式（轻点趋势区切换）
- 电光青强调色，点阵科技风

### 日历 LIT（书籍摘抄）
- **每日书籍摘抄** —— 12 部经典（《论语》《道德经》《庄子》《小窗幽记》《大学》《中庸》《孟子》《周易》《诗经》《楚辞》《世说新语》《浮生六记》）按日轮换
- **农历 + 节气** —— 1900-2100 农历转换表 + 24 节气近似计算
- **宜 / 忌** —— 每日随机建议
- 宋体衬线排版 + 朱砂印章，黑底编辑风（与主页/天气页统一）

### 通用
- **左右滑动切页** + 底部三点指示器
- **自动回主页** —— 子页 60s 无操作自动滑回
- **电源管理** —— 15s 变暗 → 30s 熄屏，触摸/BOOT 唤醒

## 🔧 硬件要求

| 项 | 说明 |
|---|---|
| 主控板 | 微雪 **ESP32-S3-Touch-AMOLED-1.8**，**V2 版**（CO5300 屏 + CST820 触摸） |
| 供电 | USB-C 或 3.7V 锂电池（AXP2101 管理） |
| 网络 | 2.4GHz WiFi |

> V1 板（SH8601 + FT3168）理论上改两处驱动初始化即可适配，欢迎提 PR。

## 🚀 快速开始

### 方式 A：直接刷现成固件（推荐新手）

到 [Releases](../../releases) 下载 `mono_watchface_rev4_merged.bin`，然后：

```bash
esptool --port COM3 --baud 921600 write_flash 0x0 mono_watchface_rev4_merged.bin
```

刷完即用。天气坐标默认为宁波（29.87N, 121.55E），改坐标见下文源码编译部分。

### 方式 B：自己编译（可自定义）

**1. 环境准备**

- [Arduino IDE 2.x](https://www.arduino.cc/en/software) 或 arduino-cli
- ESP32 核心包 **3.3.11**（Boards Manager 安装 esp32 by Espressif）
- 以下库（Library Manager 可装）：

| 库 | 用途 |
|---|---|
| lvgl **8.4.0** | 图形框架（注意是 8.x 不是 9.x） |
| GFX_Library_for_Arduino 1.6.x | CO5300 屏驱动（建议用微雪官方 v2 例程包内附版本） |
| Adafruit XCA9554 + Adafruit BusIO | IO 扩展器 |
| XPowersLib | AXP2101 电源管理 |
| ArduinoJson 7.x | 天气解析 |

**2. 配置 WiFi**

```bash
cp mono_watchface/secrets.h.example mono_watchface/secrets.h
# 编辑 secrets.h 填入你的 WiFi 名称和密码
```

> `secrets.h` 已被 `.gitignore` 排除，永远不会被提交。没有它也能编译通过，
> 会以离线占位模式运行并给出警告。

**3. 改天气城市**：编辑 `mono_watchface.ino` 顶部的 `CITY_LAT / CITY_LON`

**4. 编译烧录**

IDE 路径：Board 选 `ESP32S3 Dev Module`，关键选项：
`USB CDC On Boot=Enabled`、`PSRAM=OPI`、`Flash Size=16MB`、
`Partition Scheme=Custom`（使用工程内 partitions.csv）、其余默认。

命令行路径（一条命令）：

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom" --build-path ./build ./mono_watchface
arduino-cli upload  -p COM3 --fqbn "esp32:esp32:esp32s3:PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom" --input-dir ./build ./mono_watchface
```

## 🎨 设计系统

这块屏的可读性临界点是 **20px**——低于它的文字无论什么字体都发虚。
最终字体方案（全部 Medium 字重）：

| 元素 | 字体 | 字号 |
|---|---|---|
| 时间 | Montserrat Medium | 118px |
| 温度 | Montserrat Medium | 46px |
| 指标数值 | Montserrat Medium | 30px |
| 星期/电量/日期行 | Montserrat Medium | 20px |
| 中文（金句/天况/农历） | 思源黑体 Noto Sans SC Medium | 20px |
| 摘抄正文（LIT 页） | 思源宋体 Noto Serif SC Medium | 28px |
| 摘抄出处/印章（LIT 页） | 思源宋体 Noto Serif SC Medium | 20px |

三页统一**黑底编辑风**（AMOLED 真黑 + 暖白文字 + 朱砂/电光青点缀），
LIT 页保留宋体衬线以承载文化韵味。设计令牌与视觉稿方法详见 [DESIGN.md](DESIGN.md)，
三页视觉稿在 `design/stage2-cards-concepts.html`。

字库由 `tools/gen-fonts.js` 一键生成（基于 lv_font_conv），中文子集只含
实际用到的汉字（当前 449 个字形），全部字库仅约 1.5MB。

## 📖 开发历程

这个项目从一个"新板子到手想找点好玩的事"开始，一路走来：

1. **考古开局** —— 板子到手时里面躺着一份前作固件（YUBU CLOCK 像素时钟），
   通过 esptool 读闪存做"字符串 DNA 鉴定"确认了身份，并把它的 V2 引脚配置继承了下来。
2. **小智试金石** —— 先刷了一版小智 AI 固件做全硬件验收，顺便踩了第一个坑：
   热复位会让 I2C 总线卡死陷入重启循环（长按 PWR 彻底断电即解）。
3. **设计先行** —— 用 HTML 按真实分辨率 368×448 出了三版视觉稿（瑞士网格/圆弧仪表/
   编辑排版），选定 C 方案后才动手写代码。事实证明这一步省掉了无数次返工。
4. **字库攻坚** —— LVGL 自带字体最大 48px 且无中文，于是搭了一条
   lv_font_conv 字库生成管线：Montserrat 出数字、思源黑体出 113 字中文子集。
5. **斜体悬案** —— 上屏后发现小字全部"变斜体"。先后排查了字库损坏、变量字体、
   颜色位交换、编码损坏……最后用 BOOT 键 A/B 对比法锁定真相：
   不是软件问题，是这块 AMOLED 菱形像素排列对 ≤16px 文字的物理可读性极限。
   小字提到 20px 后豁然开朗。
6. **三行代码的陷阱** —— 一次用 PowerShell 转存源码，ANSI 误读 UTF-8 把所有中文字符串
   变成乱码；一次 `Partitions=` 写错键名（正确是 `PartitionScheme=`）导致分区始终不生效。
   都已写进下面的踩坑清单。

## ⚠️ 踩坑经验（后来者请收好）

1. **该面板最小可读字号 = 20px**。≤16px 必糊，别浪费时间调字体。
2. **热复位 I2C 卡死**：V2 板偶发启动崩溃循环（AXP2101/CST816 报
   `ESP_ERR_INVALID_RESPONSE`）。长按 PWR 6 秒彻底断电再开机即可，不是硬件坏。
3. **Arduino 只编译工程根目录的源文件**，子目录里的 `.c` 会被静默忽略（链接期才报错）。
4. **lv_font_conv 生成的头文件**写的是 `#include "lvgl/lvgl.h"`，Arduino 平铺布局下要改成
   `"lvgl.h"`。
5. **FQBN 分区键名是 `PartitionScheme`** 不是 `Partitions`；且 `--build-property` 会被
   菜单默认值覆盖，必须走 FQBN 选项。
6. **含中文的源文件严禁用 PowerShell `Get-Content/Set-Content` 转存**——Windows PowerShell
   5.1 默认 ANSI 编码会把 UTF-8 中文读成乱码。用 `.NET ReadAllText/WriteAllText(UTF8)`。
7. **LVGL 自带字体需在 lv_conf.h 里开启**（本项目用到 MONTSERRAT_14/16/20/28/48）。
8. **16MB Flash 别浪费**：默认分区只有 1.25MB 应用空间，工程自带 `partitions.csv`
   （6.5MB APP + 9.7MB SPIFFS），配合 `PartitionScheme=custom` 使用。
9. **BOOT 键 A/B 对比法**：把可疑元素做成"自定义 vs 系统"双态切换，一眼定位问题层。

## 📁 项目结构

```
amoled-lab/
├── DESIGN.md                    # 设计系统文档 + 全部经验记录
├── design/                      # HTML 视觉稿(368×448 真实比例)
│   ├── stage2-cards-concepts.html   # 阶段2 三页设计稿(用户确认稿)
│   ├── mono-watchface-concepts.html # 阶段1 三方案备选稿(A/B/C)
│   └── preview/                     # 1:1 预览器(溢出/重叠/缺字检测)
├── mono_watchface/              # ★ 表盘固件工程
│   ├── mono_watchface.ino       # 主程序(三页架构 + 滑动导航 + 天气 + 农历)
│   ├── pin_config.h             # V2 引脚定义(实测验证)
│   ├── secrets.h.example        # WiFi 配置模板(复制为 secrets.h)
│   ├── partitions.csv           # 16MB 自定义分区
│   ├── font_*.c                 # 7 个预生成的 LVGL 字库
│   └── tools/                   # 字库生成与校验工具
│       ├── gen-fonts.js         # 字库生成管线(node gen-fonts.js)
│       ├── sync-symbols.py      # 从 .ino 自动提取字符集
│       └── check-font.py        # 校验字库覆盖(退出码0才准烧录)
└── releases/                    # 本地固件存档(不入库)
```

## 🙏 致谢与许可

- 屏幕初始化序列基于 [微雪官方例程](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)（Apache-2.0）
- [LVGL](https://lvgl.io/)（MIT）、[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)、
  [XPowersLib](https://github.com/lewisxhe/XPowersLib)（MIT）、[ArduinoJson](https://arduinojson.org/)
- 字体：[Montserrat](https://fonts.google.com/specimen/Montserrat)（OFL）、
  [Noto Sans SC](https://fonts.google.com/noto)（OFL）
- 天气数据：[Open-Meteo](https://open-meteo.com/)（免费无密钥，CC-BY 4.0）

本项目代码以 MIT 协议开源。如果它帮到了你，欢迎点个 Star ⭐

---

*Made with ❤️ and a lot of trial-and-error on a tiny beautiful AMOLED.*
