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

