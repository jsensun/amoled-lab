# AMOLED-LAB 设计系统文档

> 设备：微雪 ESP32-S3-Touch-AMOLED-1.8 **V2**（CO5300 屏 + CST820 触摸）
> 分辨率：368×448 · AMOLED 真黑 · 文档建立于阶段 0 收官后

## 一、四人格产品线（用户已确认的总体方向）

| 人格 | 风格 | 承载项目 | 状态 |
|---|---|---|---|
| **MONO** | 瑞士极简 | 时钟/信息终端 | **当前阶段（阶段1）** |
| **DOT** | 点阵科技 | 游戏、仪表、宏键盘 | 规划中 |
| **MOCHI** | 温柔拟物 | AI 桌宠、语音互动 | 规划中 |
| **LIT** | 杂志编辑 | 诗词日历、节气卡 | 规划中 |

## 二、阶段 1 选定方案：C · EDITORIAL（修订版）

用户从三稿中选定 C 并提出修订：加电量显示、数字加粗。已落实到视觉稿。

### 设计令牌（Design Tokens）

```
色彩
  背景        #000000   （AMOLED 真黑，不用深灰）
  主文字      #F5F5F0   （暖白）
  次文字      #9A9A94   （中灰）
  弱文字      #3A3A38   （暗灰，仅装饰线与次要标注）
  强调色      #E8503A   （朱砂红，仅用于句号圆点等 ≤5% 面积）

字体
  数字/西文   Segoe UI Light（上机用 LVGL 内嵌等宽变体，tabular-nums）
  大号时间    118px 等效 · 字重 Light(300) · 行高 0.88 · 字距 -0.03em
  小字        11–15px · 宽字距 .1em–.5em（编辑风标志性格）
  金句        衬线斜体（宋体/Georgia 系）15px

布局网格
  左栏      内容区，左右留白 32px
  右栏      64px 竖排栏（writing-mode: vertical-rl），左侧 1px 分隔线 #161618
  结构      kicker行(FRIDAY+电量) / 垂直居中大数字堆叠 / 底部金句

组件规范
  电量计    20×10px 描边外壳 + 填充条 + 右侧凸头，数据源 AXP2101
  竖排日期  「二〇二六年」+ 朱砂圆点分隔 + 「八月廿一日」（汉字数字，非阿拉伯）
```

### 三稿归档（全部保留，未来复用）

| 方案 | 归档位置 | 未来用途 |
|---|---|---|
| A · GRID 瑞士网格 | mono-watchface-concepts.html | MONO 信息密度高的页面（天气卡片页） |
| B · ARC 圆弧仪表 | mono-watchface-concepts.html | DOT 人格的仪表盘/表盘 |
| C · EDITORIAL ★ | mono-watchface-concepts.html | **阶段1实现目标** + LIT 人格基础 |

## 三、工作流程约定（用户明确偏好）

1. **设计先行**：任何界面上机前，先出 368×448 真实比例 HTML 视觉稿，浏览器可预览，
   用户确认后才写固件代码。
2. **分工**：用户负责方向指导与审美决策；所有编码、烧录、调试由助手执行。
3. **烧录纪律**：统一命令行烧录；Arduino IDE 只看代码不点上传（防止覆盖事故）。
4. **每阶段有可见产出**，情绪价值即时兑现。
5. **字库纪律**（2026-09-01 新增，血泪教训）：
   - 改了任何界面文案必须重跑字库流程，**禁止手工维护 `symbols.txt`**
     （手工维护已导致两次"空心方框"事故）。
   - 标准流程：
     ```
     python tools/sync-symbols.py     # 从 .ino 自动提取字符集（UTF-8 安全）
     node   tools/gen-fonts.js        # 重新生成字库
     python tools/check-font.py       # 校验覆盖，退出码 0 才准烧录
     ```
   - `check-font.py` 退出码非 0 = 有字会渲染成空心方框，**禁止烧录**。
   - 中文文件一律用 Python/UTF-8 工具读写，**禁止 PowerShell 的
     `Get-Content` / `Out-File` / `Set-Content`**（会毁掉 UTF-8 编码）。
6. **预览器**：`design/preview/index.html`，坐标 1:1 取自 `mono_watchface.ino`，
   内置溢出/重叠/缺字三防检测与全年内容扫描。改动界面后用它先自检。

## 四、设备使用知识库

- 板子热复位偶尔导致 I2C 总线卡死 → 重启循环。**解法：长按 PWR 6秒彻底断电再开机。**
- 小智固件 v2.4.2（V2 专属版）已验证可用；微雪出厂固件 `V2-FactoryXiaozhi_260601.bin`
  存档于本地缓存备用。
- YUBU CLOCK v2 旧固件完整备份：`amoled-lab\backup\yubu_clock_v2_app0.bin`
- YUBU CLOCK 源码（含实测 V2 引脚配置）：`WorkBuddy\2026-08-20-14-52-45\ESP32_AMOLED_ClockWidget\`

## 五、阶段 1 交付记录（MONO 表盘 · 已验收 ✅）

**最终定稿参数（rev4）**

| 项 | 值 |
|---|---|
| 工程 | `amoled-lab\mono_watchface\` |
| 中文字体 | **思源黑体 Noto Sans SC Medium 20px**（用户裁决：黑体胜出宋体） |
| 西文/数字 | Montserrat Medium：118px 时间 / 46px 温度 / 20px UI小字 |
| 天气 | Open-Meteo API，坐标 29.87N/121.55E，30分钟刷新，ArduinoJson 解析 |
| 金句 | 本地 10 句池按年日轮换（升级方向：联网拉取+按天气配语） |
| 分区 | PartitionScheme=custom（工程内 partitions.csv，app 6.5MB） |

**本阶段关键经验（血泪换来的）**

1. **该面板可读性临界点 = 20px**：≤16px 的文字无论什么字体都发虚（用户感知为"斜体"），
   ≥20px 全部清晰。今后所有界面最小字号 20px。
2. **字库生成管线**：`tools/gen-fonts.js`（npx lv_font_conv@1.5.3），生成后必须把
   `#include "lvgl/lvgl.h"` 替换为 `#include "lvgl.h"`；生成的 .c 必须放工程根目录
   （Arduino 不编译子目录源文件）。
3. **编译命令**（PartitionScheme 键名不是 Partitions！）：
   `arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom"`
4. **编码纪律**：含中文的源文件禁止用 PS `Get-Content/Set-Content` 转存（ANSI 误读会毁掉
   UTF-8 字符串），一律用 write 工具或 .NET ReadAllText/WriteAllText(UTF8)。
5. **诊断方法论**：BOOT 键切换 自定义字体↔系统字体 的 A/B 对比模式是定位渲染问题的利器，
   保留在固件中。

## 六、阶段 1.5 交付记录（电源管理 · v1.1 已验收 ✅）

**功能**：无操作 15s 变暗 → 30s 熄屏（CPU 240→80MHz，UI 冻结）；唤醒 = 触摸 / BOOT 键。
**发布**：GitHub [v1.1.0](https://github.com/jsensun/amoled-lab/releases/tag/v1.1.0)（bin+zip 双格式固件）。
**开源仓库**：https://github.com/jsensun/amoled-lab

**新增踩坑经验（6-9 号）**

6. **IMU 轮询陷阱**：以 100ms 周期轮询 QMI8658 做"拿起唤醒"会引发每秒数千次虚假活动
   （驱动层副作用）。**教训：体感唤醒必须用 IMU 硬件中断引脚，不要软件轮询。** 阶段 2 重做。
7. **CST820 触摸 INT 行为**：INT 每个扫描帧后拉低直到主机读数据清除。正确用法 =
   LVGL indev 持续轮询读取（顺带清 INT）；熄屏态轻量轮询 FINGER_NUMBER 寄存器实现触摸唤醒。
   用 INT 电平/边沿判定"活动"会被扫描帧噪声淹没（本次息屏失灵的真正元凶）。
8. **触摸芯片复位时序**：IO 扩展器复位脉冲需 高100ms→低300ms→高（对齐小智官方），
   20ms 快脉冲导致 CST820 init 失败；加 3 次重试后成功率 100%。
9. **PowerShell 编码陷阱实证**：`Get-Content`/`Set-Content` 默认 ANSI 会毁掉 UTF-8 中文源码
   （已实际发生一次，全文件重写恢复）。含中文文件一律 write 工具或 .NET UTF8 读写。

**知识库补充**

- **开源仓库**：https://github.com/jsensun/amoled-lab （README 含完整刷机指南与踩坑清单）
- 固件存档：`releases\mono_watchface_v1.1_20260822\`；历史 rev4 存档于
  `releases\mono_watchface_rev4_20260822\`
- ⚠️ `mono_watchface\secrets.h` 含真实 WiFi 密码：本地保留供编译，已被 .gitignore 排除；
  改 WiFi 时同步更新该文件

## 七、阶段 2 交付记录（三页卡片 · v1.2 已验收 ✅）

**功能**：三页架构（主页 MONO / 天气 DOT / 日历 LIT）+ 左右滑动导航 + 底部三点指示器。
主页保留原 MONO 编辑排版；天气页新增湿度/风速/气压 + 24h 温度趋势（圆角柱/平滑曲线双模式）；
日历页从"每日一诗"改为"每日书籍摘抄"（12 部经典按日轮换）+ 农历节气 + 宜忌。

**发布**：GitHub [v1.2.0](https://github.com/jsensun/amoled-lab/releases/tag/v1.2.0)（bin+zip 双格式固件）。
**本地存档**：`D:\ESP32 固件\mono_watchface_v1.2\`（含源码/字库/设计稿/工具/完整文档）。

**新增踩坑经验（10-16 号）**

10. **Arduino 构建缓存陷阱**：用固定 `--build-path` 时，改字库后可能复用旧对象文件导致
    "空心方框"。**改字库必须删 build 目录强制全新编译**。
11. **汉字编码陷阱**：`唐` 的 Unicode 是 **U+5510**，不是 U+674E（那是 `李`）。曾因混淆导致
    "唐"一直显示方框。**务必用 `[int][char]'字'` 验证真实码点**。
12. **28px 宋体每行最多 13 字**：368px 屏宽 ÷ 28px = 13 字/行，超长摘抄需拆行或换版本。
13. **LIT 页风格统一**：最初 LIT 页用米黄纸张风（仿古书），与黑底编辑风格格不入。
    用户要求统一后改为黑底 + 宋体 + 暗朱红（#B03A2E），既保留文化韵味又风格统一。
14. **红字亮度**：LIT 页红字用亮朱红 #E8503A 过亮，改用暗朱红 #B03A2E 更沉稳。
15. **顶栏对齐**：`lv_obj_align(..., LV_ALIGN_TOP_RIGHT, x, y)` 的 y 是相对屏幕顶的偏移，
    与左侧 `new_label(x, y)` 的绝对 y 需一致才能水平对齐（曾差 26px）。
16. **字库范围检查不可靠**：`font_time_118` 用 format0_tiny（unicode_list=NULL），直接查码点
    会误报 `glyphs=0`。用头文件 `--range` 注释判断覆盖。

**知识库补充**

- 三页视觉稿：`design\stage2-cards-concepts.html`（用户确认稿）
- 1:1 预览器：`design\preview\index.html`（含溢出/重叠/缺字三防检测）
- 字库纪律升级：改文案必须 `sync-symbols.py → gen-fonts.js → check-font.py` 三步走，
  禁止手工维护 symbols.txt（已导致多次空心方框事故）。

