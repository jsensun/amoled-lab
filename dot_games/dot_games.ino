/*
 * ============================================================
 *  DOT · 游戏 —— amoled-lab 阶段 3 · 游戏菜单 + 2048
 *  硬件: 微雪 ESP32-S3-Touch-AMOLED-1.8 V2 (CO5300 + CST820)
 *
 *  功能:
 *   - 游戏菜单: 网格卡片式, 5 个游戏(2048 可玩, 其余"即将推出")
 *   - 2048: 完整可玩, 触摸滑动合并, 分数+最高分(NVS), 结束检测
 *   - 电源管理: 15s 变暗 / 30s 熄屏 / 触摸唤醒
 *   - 防误触: 滑动阈值 50px, 大按钮(40px+)
 *
 *  视觉: 苹果手表 Liquid Glass 风 · 深色 + 霓虹发光 · 高端科技感
 *  注意: 本文件必须保持 UTF-8 编码; 禁止用 PS Get-Content 转存
 * ============================================================
 */

#include <WiFi.h>
#include <Wire.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include <Adafruit_XCA9554.h>
#include "XPowersLib.h"
#include "HWCDC.h"
#include <Preferences.h>
#include <ESP_I2S.h>
#include "es8311.h"

#include "pin_config.h"

/* ---------------- 音频 (ES8311 + I2S) ---------------- */
#define AUDIO_MCK_IO 16
#define AUDIO_BCK_IO 9
#define AUDIO_WS_IO  45
#define AUDIO_DO_IO  8
#define AUDIO_PA_IO  46
#define AUDIO_SAMPLE_RATE 16000
I2SClass audio_i2s;

LV_FONT_DECLARE(font_game_40);
LV_FONT_DECLARE(font_ui_20);
LV_FONT_DECLARE(font_cjk_20);

/* ---------------- 设计令牌 ---------------- */
#define COL_BG     lv_color_hex(0x000000)
#define COL_HI     lv_color_hex(0xF5F5F0)
#define COL_MID    lv_color_hex(0x8A8A92)
#define COL_LOW    lv_color_hex(0x4A4A52)
#define COL_DIM    lv_color_hex(0x2A2A32)
#define COL_CYAN   lv_color_hex(0x00E5C8)
#define COL_BLUE   lv_color_hex(0x3B82F6)
#define COL_PURPLE lv_color_hex(0x8B5CF6)
#define COL_GOLD   lv_color_hex(0xFFD700)
#define COL_ORANGE lv_color_hex(0xFF6B4A)
#define COL_RED    lv_color_hex(0xFF3B30)
#define COL_WHITE  lv_color_hex(0xFFFFFF)

HWCDC USBSerial;

/* ================= 主题系统 =================
 * 2 套主题: 彩色(默认, 现配色) / 墨色(黑白灰高级感, AMOLED真黑)
 * 切换入口=菜单右上角按钮(与游戏目录同级); NVS 持久化
 * 所有游戏色一律经 TG_XXX() 查询, 切换后重进游戏即全屏换色 */
enum Theme { THEME_COLOR, THEME_MONO };
static int g_theme = THEME_COLOR;
static Preferences theme_prefs;
static lv_obj_t* g_themeBtn;      /* 菜单主题按钮文案 */

/* 灰阶 (墨色): 5级 — AMOLED 上 #000 底, 灰阶差即层级 */
#define MONO_G1  lv_color_hex(0xF5F5F0)   /* 最高亮: 头/重要 */
#define MONO_G2  lv_color_hex(0xC8C8C2)   /* 亮 */
#define MONO_G3  lv_color_hex(0x9A9A94)   /* 中 */
#define MONO_G4  lv_color_hex(0x5A5A56)   /* 暗 */
#define MONO_G5  lv_color_hex(0x2A2A30)   /* 最暗: 底格 */

/* 通用强调色: 彩色=青, 墨色=亮灰 */
static lv_color_t TG_Accent() { return g_theme ? MONO_G1 : COL_CYAN; }
/* 次强调: 彩色=金, 墨色=G2 */
static lv_color_t TG_Accent2() { return g_theme ? MONO_G2 : COL_GOLD; }
/* 正常文字色: 彩色=暖白, 墨色=同 */
static lv_color_t TG_Text() { return COL_HI; }

/* ---- 各游戏主题色查询 ---- */
/* 2048: v=块数值 2..2048 */
static lv_color_t TG_2048Tile(int v) {
  if (g_theme == THEME_MONO) {
    /* 墨色: 数值越高越亮 (2=G5 → 2048=G1) */
    if (v <= 2)   return MONO_G5;
    if (v <= 8)   return MONO_G4;
    if (v <= 32)  return MONO_G3;
    if (v <= 256) return MONO_G2;
    return MONO_G1;
  }
  switch (v) {
    case 2:    return lv_color_hex(0x3FB8A8);
    case 4:    return lv_color_hex(0x35A0B8);
    case 8:    return lv_color_hex(0x3082C8);
    case 16:   return lv_color_hex(0x6A5AE0);
    case 32:   return lv_color_hex(0x8B5CF6);
    case 64:   return lv_color_hex(0xB04AE0);
    case 128:  return lv_color_hex(0xD4354A);
    case 256:  return lv_color_hex(0xE8503A);
    case 512:  return lv_color_hex(0xFF6B4A);
    case 1024: return lv_color_hex(0xFF9F4A);
    case 2048: return lv_color_hex(0xFFD700);
    default:   return lv_color_hex(0xFFD700);
  }
}
static lv_color_t TG_2048Border(int v) { return TG_2048Tile(v); }
static lv_color_t TG_2048Text(int v) {
  if (g_theme == THEME_MONO) {
    /* 墨色: 底暗配亮字, 底亮配黑字 (字底同色=数字隐身, 2026-09 验收实报) */
    if (v <= 8) return MONO_G2;              /* 底 G5/G4 暗灰 → 亮字 */
    return lv_color_hex(0x0A0A0E);          /* 底 G3 以上 → 黑字 */
  }
  return TG_2048Tile(v);                     /* 彩色: 文字=块色 (原设计) */
}

/* 蛇: tier 0-3 + 头(4) + 食物(5) */
static lv_color_t TG_Snake(int tier) {
  if (g_theme == THEME_MONO) {
    switch (tier) {
      case 4:  return MONO_G1;    /* 头 */
      case 5:  return MONO_G2;    /* 食物 */
      case 0:  return MONO_G3;
      case 1:  return MONO_G4;
      case 2:  return MONO_G5;
      default: return MONO_G5;
    }
  }
  switch (tier) {
    case 4:  return lv_color_hex(0x00FFC8);   /* 头 */
    case 5:  return COL_ORANGE;               /* 食物 */
    case 0:  return lv_color_hex(0x00E5C8);
    case 1:  return lv_color_hex(0x00B89C);
    case 2:  return lv_color_hex(0x00917C);
    default: return lv_color_hex(0x1A5A50);
  }
}

/* 砖块: row 0-5 (高行亮) */
static lv_color_t TG_Brick(int row) {
  if (g_theme == THEME_MONO) {
    static const lv_color_t g[6] = {MONO_G1, MONO_G2, MONO_G3, MONO_G4, MONO_G5, MONO_G5};
    return g[row];
  }
  static const lv_color_t c[6] = {
    lv_color_hex(0x00FFC8), lv_color_hex(0x00E5C8), lv_color_hex(0x00B89C),
    lv_color_hex(0x00917C), lv_color_hex(0x1A9A7A), lv_color_hex(0x1A5A50)
  };
  return c[row];
}
/* 打砖块: 挡板/球 */
static lv_color_t TG_Paddle() { return g_theme ? MONO_G1 : COL_HI; }
static lv_color_t TG_Ball()  { return g_theme ? MONO_G1 : COL_WHITE; }

/* 俄方块: piece 0-6 */
static lv_color_t TG_Tet(int piece) {
  if (g_theme == THEME_MONO) {
    /* 墨色: 7块按重要性分灰 — I最亮, J/L/S/Z 中, O/T 暗一点 */
    static const lv_color_t g[7] = {MONO_G1, MONO_G3, MONO_G2, MONO_G3, MONO_G2, MONO_G4, MONO_G3};
    return g[piece];
  }
  static const lv_color_t c[7] = {
    lv_color_hex(0x00E5C8), lv_color_hex(0xFFD700), lv_color_hex(0x8B5CF6),
    lv_color_hex(0x00B89C), lv_color_hex(0xFF6B4A), lv_color_hex(0x3B82F6),
    lv_color_hex(0xFFB300)
  };
  return c[piece];
}

/* 点阵鸟: 鸟/管 */
static lv_color_t TG_Bird() { return g_theme ? MONO_G1 : COL_HI; }
static lv_color_t TG_Pipe() { return g_theme ? MONO_G4 : lv_color_hex(0x0D3B33); }
static lv_color_t TG_PipeBorder() { return g_theme ? MONO_G2 : lv_color_hex(0x00E5C8); }

/* 菜单: 图标底色 */
static lv_color_t TG_MenuIcon(int i) {
  if (g_theme == THEME_MONO) {
    static const lv_color_t g[5] = {MONO_G2, MONO_G3, MONO_G2, MONO_G3, MONO_G2};
    return g[i];
  }
  static const lv_color_t c[5] = {COL_CYAN, COL_BLUE, COL_PURPLE, COL_GOLD, COL_ORANGE};
  return c[i];
}

/* 墨色下残留彩色的统一灰阶化 (进游戏必经 NewGame 刷新, 2026-09 验收需求) */
static lv_color_t TG_Score() { return g_theme ? MONO_G1 : COL_CYAN; }    /* SCORE 数值 */
static lv_color_t TG_Best()  { return g_theme ? MONO_G2 : COL_GOLD; }     /* BEST 数值 */
static lv_color_t TG_Restart() { return g_theme ? MONO_G1 : COL_CYAN; }   /* 重开按钮底 */
static lv_color_t TG_Lives() { return g_theme ? MONO_G2 : COL_CYAN; }     /* 生命图标文字 */

/* ---------------- 屏幕 ---------------- */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT, 16, 0, 0, 0);
Adafruit_XCA9554 expander;

/* ---------------- 触摸 ---------------- */
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> CST816(new Arduino_CST816x(
    IIC_Bus, CST816T_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));
void Arduino_IIC_Touch_Interrupt(void) { }

/* ---------------- 电源状态机 ---------------- */
enum PwrState { PWR_ACTIVE, PWR_DIM, PWR_OFF };
static PwrState pwr = PWR_ACTIVE;
static uint32_t lastActivity = 0;
#define DIM_AFTER_MS  15000UL
#define OFF_AFTER_MS  30000UL
#define BRIGHT_ON     180   /* 220 仍有 4 条横纹, 回 180 (布局已变, 该值下 SCORE 无斜体; 条纹与亮度无关) */
#define BRIGHT_DIM    45

static void poke() {
  lastActivity = millis();
  if (pwr != PWR_ACTIVE) wakeUp();
}
static void dimNow() {
  pwr = PWR_DIM;
  gfx->setBrightness(BRIGHT_DIM);
}
static void sleepNow() {
  pwr = PWR_OFF;
  gfx->setBrightness(0);
  setCpuFrequencyMhz(80);
  lv_timer_enable(false);
}
static void wakeUp() {
  if (pwr == PWR_OFF) {
    setCpuFrequencyMhz(240);
    lv_timer_enable(true);
    lv_obj_invalidate(lv_scr_act());
  }
  pwr = PWR_ACTIVE;
  lastActivity = millis();
  gfx->setBrightness(BRIGHT_ON);
  CST816->IIC_Interrupt_Flag = false;
}

/* ================= 音频 (ES8311 音效 · 异步队列) =================
 * 教训: 在 LVGL indev/触摸回调里同步播放长音效会阻塞渲染管线,
 * 与覆盖层重绘叠加时屏幕出现彩色横向纹理 + 触摸迟钝。
 * 改为: 回调只置队列标志, loop() 渲染间隙统一播放。 */
static bool audio_ok = false;

/* 待播队列 (单槽轮播: 新事件覆盖旧的, 短音效场景足够) */
enum SfxId { SFX_NONE = 0, SFX_MOVE, SFX_MERGE, SFX_CLICK, SFX_GAMEOVER, SFX_WIN };
static volatile uint8_t sfxQueue = SFX_NONE;

static void audio_init() {
  pinMode(AUDIO_PA_IO, OUTPUT);
  digitalWrite(AUDIO_PA_IO, HIGH);   /* 打开功放 */

  audio_i2s.setPins(AUDIO_BCK_IO, AUDIO_WS_IO, AUDIO_DO_IO, -1, AUDIO_MCK_IO);
  if (!audio_i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                       I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    USBSerial.println("I2S init fail");
    return;
  }

  es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) { USBSerial.println("ES8311 create fail"); return; }
  const es8311_clock_config_t clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = AUDIO_SAMPLE_RATE * 256,
    .sample_frequency = AUDIO_SAMPLE_RATE
  };
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    USBSerial.println("ES8311 init fail");
    return;
  }
  es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
  es8311_microphone_config(es, false);
  es8311_voice_volume_set(es, 60, NULL);   /* 音量 60/100, 柔和 */
  audio_ok = true;
  USBSerial.println("Audio ok");
}

/* 播放一段正弦波音效 (只在 loop 渲染间隙调用, 严禁在 LVGL 回调内直接调用) */
static void playTone(float freq, int durationMs, float vol) {
  if (!audio_ok) return;
  int n = AUDIO_SAMPLE_RATE * durationMs / 1000;
  int16_t buf[128];                    /* 栈缓冲减半: 128采样=256B, 防深递归栈压 */
  int written = 0;
  float phase = 0;
  float step = 2.0f * 3.14159265f * freq / AUDIO_SAMPLE_RATE;
  while (written < n) {
    int chunk = (n - written) < 64 ? (n - written) : 64;
    for (int i = 0; i < chunk; i++) {
      float env = 1.0f;
      /* 淡入淡出避免爆音 */
      int pos = written + i;
      if (pos < 200) env = (float)pos / 200.0f;
      if (n - pos < 200) env = (float)(n - pos) / 200.0f;
      int16_t s = (int16_t)(sinf(phase) * 32767.0f * vol * env);
      buf[i * 2] = s;      /* 左声道 */
      buf[i * 2 + 1] = s;  /* 右声道 */
      phase += step;
    }
    audio_i2s.write((uint8_t*)buf, chunk * 4);
    written += chunk;
  }
}

/* 音效封装: 只置标志, 不播放 (LVGL 回调/触摸处理内安全调用) */
static void sfxQueuePush(uint8_t id) { sfxQueue = id; }
static void sfxMove()     { sfxQueuePush(SFX_MOVE); }
static void sfxMerge()    { sfxQueuePush(SFX_MERGE); }
static void sfxClick()    { sfxQueuePush(SFX_CLICK); }
static void sfxGameOver() { sfxQueuePush(SFX_GAMEOVER); }
static void sfxWin()      { sfxQueuePush(SFX_WIN); }

/* loop() 里消费队列: 真正发声 (渲染间隙, 不与 flush 竞争) */
static void sfxPump() {
  if (sfxQueue == SFX_NONE) return;
  uint8_t id = sfxQueue;
  sfxQueue = SFX_NONE;
  switch (id) {
    case SFX_MOVE:     playTone(880, 25, 0.25f); break;                       /* 移动: 柔和短音 */
    case SFX_MERGE:   playTone(1320, 30, 0.30f); playTone(1760, 40, 0.25f); break; /* 合并: 清脆双音 */
    case SFX_CLICK:   playTone(660, 20, 0.20f); break;                        /* 点击/返回 */
    case SFX_GAMEOVER:{ float f[4] = {392, 330, 262, 196};                    /* 结束: 缩短的下滑音 */
                        for (int i = 0; i < 4; i++) playTone(f[i], 55, 0.28f);
                      } break;
    case SFX_WIN:     { float f[4] = {523, 659, 784, 1046};                   /* 胜利: 琶音 */
                        for (int i = 0; i < 4; i++) playTone(f[i], 80, 0.28f);
                      } break;
  }
}

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];

/* ---------------- 电源 ---------------- */
static XPowersAXP2101 pmu;
static bool pmu_ok = false;

/* ================= LVGL 底层 ================= */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(disp);
}

/* 失效区→全宽行条带 (dot_games 花屏根治第二步)
 * 现象: 蛇横向移动时, 同行零星小格各自小窗口 flush, CS 高频翻转的
 * 短突发序列触发面板错乱(变形/残影); 竖向合并成窄条反而安全。
 * 对策: 强制每次 flush 都是全宽 368px 行条带 → 同 tick 变化格合并,
 * 每 tick 仅 1-3 次规整大 flush, 与 mono 表盘/2048 全盘重绘同模式
 * (该模式 40MHz 下已长期验证无恙)。副作用: 每 flush 多画些无用像素,
 * 368x40px ≈ 15k ≈ 1.5ms, 10Hz 游戏刷新完全无感。 */
void my_disp_rounder(lv_disp_drv_t *disp_drv, lv_area_t *area) {
  area->x1 = 0;
  area->x2 = LCD_WIDTH - 1;
}

void example_increase_lvgl_tick(void *arg) {
  lv_tick_inc(2);
}

/* ================= 屏幕管理 ================= */
enum Screen { SCR_MENU, SCR_GAME, SCR_SNAKE, SCR_BREAKOUT, SCR_TETRIS, SCR_FLAPPY };
static int currentScreen = SCR_MENU;
static lv_obj_t *scr_menu, *scr_game, *scr_snake, *scr_breakout, *scr_tetris, *scr_flappy;

/* ================= 文本工具 ================= */
static lv_obj_t* new_label(lv_obj_t* parent, const lv_font_t* font,
                           lv_color_t color, lv_coord_t x, lv_coord_t y,
                           const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_pos(l, x, y);
  if (text) lv_label_set_text(l, text);
  return l;
}
static lv_obj_t* new_rect(lv_obj_t* parent, lv_coord_t w, lv_coord_t h,
                          lv_coord_t x, lv_coord_t y, lv_color_t color) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, w, h);
  lv_obj_set_pos(r, x, y);
  lv_obj_set_style_bg_color(r, color, 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  return r;
}

/* ================= 游戏菜单 ================= */
static lv_obj_t* menu_cards[5];
static const char* MENU_NAMES[5] = {"2048", "贪吃蛇", "打砖块", "俄罗斯方块", "点阵鸟"};
static const char* MENU_DESC[5] = {"滑动合并数字", "触摸方向控制", "挡板接球", "经典下落", "点击穿越"};
static const lv_color_t MENU_COL[5] = {COL_CYAN, COL_BLUE, COL_PURPLE, COL_GOLD, COL_ORANGE};
static const char* MENU_ICO[5] = {"2", "S", "B", "T", "F"};

/* 主题切换: NVS 持久化 + 按钮文案刷新 (游戏色在进游戏时经 TG 查询自然生效) */
static void themeToggle() {
  g_theme = g_theme ? THEME_COLOR : THEME_MONO;
  theme_prefs.putInt("theme", g_theme);
  lv_label_set_text(g_themeBtn, g_theme ? "墨色主题" : "彩色主题");
  /* 菜单卡片图标即时刷新 */
  for (int i = 0; i < 5; i++) {
    lv_obj_t* card = menu_cards[i];
    /* 卡片结构: ico(0) 内含 ico_t, name(1), desc(2), soon?(3) */
    lv_obj_t* ico = lv_obj_get_child(card, 0);
    lv_obj_t* ico_t = lv_obj_get_child(ico, 0);
    lv_obj_set_style_bg_color(ico, TG_MenuIcon(i), 0);
    lv_obj_set_style_text_color(ico_t, g_theme ? MONO_G1 : MENU_COL[i], 0);
  }
  sfxClick();
}

static void build_menu() {
  scr_menu = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_menu, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_menu, LV_OPA_COVER, LV_PART_MAIN);

  /* 标题 + 主题切换按钮 (与游戏目录同级) */
  new_label(scr_menu, &font_cjk_20, COL_HI, 22, 24, "DOT·游戏");
  lv_obj_t* themeBtn = lv_btn_create(scr_menu);
  lv_obj_remove_style_all(themeBtn);
  lv_obj_set_size(themeBtn, 108, 36);
  lv_obj_set_pos(themeBtn, 238, 16);
  lv_obj_set_style_bg_color(themeBtn, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(themeBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(themeBtn, 12, 0);
  lv_obj_set_style_border_color(themeBtn, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(themeBtn, 1, 0);
  lv_obj_add_flag(themeBtn, LV_OBJ_FLAG_CLICKABLE);
  g_themeBtn = lv_label_create(themeBtn);
  lv_obj_set_style_text_font(g_themeBtn, &font_cjk_20, 0);
  lv_obj_set_style_text_color(g_themeBtn, COL_MID, 0);
  lv_obj_center(g_themeBtn);
  lv_label_set_text(g_themeBtn, g_theme ? "墨色主题" : "彩色主题");

  /* 2 列网格卡片: 5 个卡片排 3 行, 第 3 行 1 个居中 */
  int cardW = 156, cardH = 120, gap = 12;
  int startX = 22, startY = 60;
  for (int i = 0; i < 5; i++) {
    int col = i % 2, row = i / 2;
    int x = startX + col * (cardW + gap);
    int y = startY + row * (cardH + gap);
    /* 第 3 行只有 1 个卡片, 居中 */
    if (i == 4) x = startX + (cardW + gap) / 2;

    lv_obj_t* card = lv_obj_create(scr_menu);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, cardW, cardH);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0E), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1A1A22), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    menu_cards[i] = card;

    /* 图标块 */
    lv_obj_t* ico = lv_obj_create(card);
    lv_obj_remove_style_all(ico);
    lv_obj_set_size(ico, 40, 40);
    lv_obj_set_pos(ico, 12, 12);
    lv_obj_set_style_bg_color(ico, TG_MenuIcon(i), 0);
    lv_obj_set_style_bg_opa(ico, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ico, 12, 0);
    lv_obj_set_style_bg_grad_color(ico, lv_color_hex(0x0D0D12), 0);
    lv_obj_set_style_bg_grad_dir(ico, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(ico, 0, 0);
    lv_obj_set_style_bg_grad_stop(ico, 255, 0);

    lv_obj_t* ico_t = lv_label_create(ico);
    lv_obj_set_style_text_font(ico_t, &font_ui_20, 0);
    lv_obj_set_style_text_color(ico_t, g_theme ? MONO_G1 : MENU_COL[i], 0);
    lv_obj_center(ico_t);
    lv_label_set_text(ico_t, MENU_ICO[i]);

    /* 名称 + 描述 (中文用 font_cjk_20, 避免缺字方块) */
    lv_obj_t* name = new_label(card, &font_cjk_20, COL_HI, 12, 62, MENU_NAMES[i]);
    lv_obj_t* desc = new_label(card, &font_cjk_20, COL_MID, 12, 92, MENU_DESC[i]);

    /* 未开放标记 (y=100 时底边 122 溢出 120px 卡片, 上移到 94) */
    if (i > 4) {
      lv_obj_t* soon = new_label(card, &font_ui_20, COL_LOW, 12, 94, "SOON");
    }
  }
}

/* ================= 2048 游戏 ================= */
static int board[4][4];
static int score = 0;
static int best = 0;
static bool gameOver = false;
static bool won = false;
static bool mergedThisMove = false;
static Preferences prefs;

static lv_obj_t* g_score_val;
static lv_obj_t* g_best_val;
static lv_obj_t* g_cells[4][4];
static lv_obj_t* g_cell_nums[4][4];
static lv_obj_t* g_overlay;
static lv_obj_t* g_overlay_text;

/* 方块数值 -> 颜色 */
static lv_color_t tileColor(int v) { return TG_2048Border(v); }   /* 边框/文字色 → 主题 */
static lv_color_t tileBg(int v) {   /* 块底色 → 主题 (彩色=深色底, 墨色=灰阶即块色) */
  if (g_theme == THEME_MONO) return TG_2048Tile(v);
  switch (v) {
    case 2:    return lv_color_hex(0x0E1F1C);
    case 4:    return lv_color_hex(0x101A26);
    case 8:    return lv_color_hex(0x1A1626);
    case 16:   return lv_color_hex(0x26200E);
    case 32:   return lv_color_hex(0x261A14);
    default:   return lv_color_hex(0x202020);
  }
}

/* 方块数值 -> 背景色(暗) */


/* 生成新方块 */
static void spawnTile() {
  int empty[16][2], n = 0;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (board[r][c] == 0) { empty[n][0] = r; empty[n][1] = c; n++; }
  if (n == 0) return;
  int idx = random(0, n);
  board[empty[idx][0]][empty[idx][1]] = (random(0, 10) < 9) ? 2 : 4;
}

/* 移动一行并合并, 返回是否有变化 */
static bool moveLine(int line[4]) {
  int out[4] = {0,0,0,0};
  int oi = 0;
  /* 压缩 */
  for (int i = 0; i < 4; i++) if (line[i] != 0) out[oi++] = line[i];
  /* 合并 */
  for (int i = 0; i < 3; i++) {
    if (out[i] != 0 && out[i] == out[i+1]) {
      out[i] *= 2;
      score += out[i];
      mergedThisMove = true;
      if (out[i] == 2048) won = true;
      for (int j = i+1; j < 3; j++) out[j] = out[j+1];
      out[3] = 0;
    }
  }
  bool changed = false;
  for (int i = 0; i < 4; i++) if (out[i] != line[i]) changed = true;
  for (int i = 0; i < 4; i++) line[i] = out[i];
  return changed;
}

/* 移动整个棋盘 */
static bool moveBoard(int dir) {
  bool changed = false;
  int line[4];
  if (dir == 0) {          /* UP */
    for (int c = 0; c < 4; c++) {
      for (int r = 0; r < 4; r++) line[r] = board[r][c];
      if (moveLine(line)) changed = true;
      for (int r = 0; r < 4; r++) board[r][c] = line[r];
    }
  } else if (dir == 1) {   /* DOWN */
    for (int c = 0; c < 4; c++) {
      for (int r = 0; r < 4; r++) line[3-r] = board[r][c];
      if (moveLine(line)) changed = true;
      for (int r = 0; r < 4; r++) board[r][c] = line[3-r];
    }
  } else if (dir == 2) {   /* LEFT */
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) line[c] = board[r][c];
      if (moveLine(line)) changed = true;
      for (int c = 0; c < 4; c++) board[r][c] = line[c];
    }
  } else {                 /* RIGHT */
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) line[3-c] = board[r][c];
      if (moveLine(line)) changed = true;
      for (int c = 0; c < 4; c++) board[r][c] = line[3-c];
    }
  }
  return changed;
}

/* 检查游戏是否结束 */
static bool isGameOver() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      if (board[r][c] == 0) return false;
      if (c < 3 && board[r][c] == board[r][c+1]) return false;
      if (r < 3 && board[r][c] == board[r+1][c]) return false;
    }
  return true;
}

/* 刷新棋盘显示 */
static void refreshBoard() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      int v = board[r][c];
      lv_obj_t* cell = g_cells[r][c];
      lv_obj_t* num = g_cell_nums[r][c];
      if (v == 0) {
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x0A0A0E), 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x14141A), 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_label_set_text(num, "");
      } else {
        lv_obj_set_style_bg_color(cell, tileBg(v), 0);
        lv_obj_set_style_border_color(cell, tileColor(v), 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_text_color(num, TG_2048Text(v), 0);
        char buf[8];
        itoa(v, buf, 10);
        lv_label_set_text(num, buf);
      }
    }
  char sb[8];
  itoa(score, sb, 10);
  lv_label_set_text(g_score_val, sb);
  itoa(best, sb, 10);
  lv_label_set_text(g_best_val, sb);
}

/* 显示结束/胜利覆盖层 */
static void showOverlay(const char* title) {
  lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_overlay_text, title);
  poke();
}

/* 隐藏覆盖层 */
static void hideOverlay() {
  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* 新游戏 */
static void newGame() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) board[r][c] = 0;
  score = 0;
  gameOver = false;
  won = false;
  spawnTile();
  spawnTile();
  hideOverlay();
  refreshBoard();
}

/* 处理一次滑动 */
static void handleSwipe(int dir) {
  if (gameOver) return;
  mergedThisMove = false;
  if (moveBoard(dir)) {
    if (mergedThisMove) sfxMerge(); else sfxMove();
    spawnTile();
    refreshBoard();
    if (isGameOver()) {
      gameOver = true;
      if (score > best) {
        best = score;
        prefs.putInt("best", best);
      }
      showOverlay("游戏结束");
      sfxGameOver();
    } else if (won) {
      gameOver = true;
      if (score > best) {
        best = score;
        prefs.putInt("best", best);
      }
      showOverlay("你赢了!");
      sfxWin();
    }
  }
}

/* 构建 2048 界面 */
static void build_game() {
  scr_game = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_game, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_game, LV_OPA_COVER, LV_PART_MAIN);

  /* 顶部: 返回 + 分数 */
  lv_obj_t* back = lv_obj_create(scr_game);
  lv_obj_remove_style_all(back);
  lv_obj_set_size(back, 40, 40);
  lv_obj_set_pos(back, 20, 20);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(back, 12, 0);
  lv_obj_set_style_border_color(back, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(back, 1, 0);
  lv_obj_t* back_t = lv_label_create(back);
  lv_obj_set_style_text_font(back_t, &font_ui_20, 0);
  lv_obj_set_style_text_color(back_t, COL_MID, 0);
  lv_obj_center(back_t);
  lv_label_set_text(back_t, "<");

  /* 分数卡片 (54px 高: 标题 22 + 数值 22 + 边距 10, 不再互相挤压重叠) */
  lv_obj_t* sc = lv_obj_create(scr_game);
  lv_obj_remove_style_all(sc);
  lv_obj_set_size(sc, 90, 54);
  lv_obj_set_pos(sc, 74, 16);
  lv_obj_set_style_bg_color(sc, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(sc, 12, 0);
  lv_obj_set_style_border_color(sc, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(sc, 1, 0);
  new_label(sc, &font_cjk_20, COL_MID, 0, 5, "SCORE");
  lv_obj_align(lv_obj_get_child(sc, 0), LV_ALIGN_TOP_MID, 0, 5);
  g_score_val = new_label(sc, &font_cjk_20, TG_Score(), 0, 0, "0");
  lv_obj_align(g_score_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  lv_obj_t* bc = lv_obj_create(scr_game);
  lv_obj_remove_style_all(bc);
  lv_obj_set_size(bc, 90, 54);
  lv_obj_set_pos(bc, 174, 16);
  lv_obj_set_style_bg_color(bc, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(bc, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bc, 12, 0);
  lv_obj_set_style_border_color(bc, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(bc, 1, 0);
  new_label(bc, &font_cjk_20, COL_MID, 0, 5, "BEST");
  lv_obj_align(lv_obj_get_child(bc, 0), LV_ALIGN_TOP_MID, 0, 5);
  g_best_val = new_label(bc, &font_cjk_20, TG_Best(), 0, 0, "0");
  lv_obj_align(g_best_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  /* 4x4 棋盘 */
  int boardX = 20, boardY = 84, boardW = 328, gap = 8;
  int pad = 8;                          /* 棋盘内边距 */
  int cellW = (boardW - 2 * pad - 3 * gap) / 4;   /* 两侧内边距也要扣除: (328-16-24)/4=72 */
  lv_obj_t* boardBg = lv_obj_create(scr_game);
  lv_obj_remove_style_all(boardBg);
  lv_obj_set_size(boardBg, boardW, boardW);
  lv_obj_set_pos(boardBg, boardX, boardY);
  lv_obj_set_style_bg_color(boardBg, lv_color_hex(0x050507), 0);
  lv_obj_set_style_bg_opa(boardBg, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(boardBg, 18, 0);
  lv_obj_set_style_border_color(boardBg, lv_color_hex(0x14141A), 0);
  lv_obj_set_style_border_width(boardBg, 1, 0);

  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      int x = boardX + pad + c * (cellW + gap);
      int y = boardY + pad + r * (cellW + gap);
      lv_obj_t* cell = lv_obj_create(scr_game);
      lv_obj_remove_style_all(cell);
      lv_obj_set_size(cell, cellW, cellW);
      lv_obj_set_pos(cell, x, y);
      lv_obj_set_style_bg_color(cell, lv_color_hex(0x0A0A0E), 0);
      lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(cell, 12, 0);
      lv_obj_set_style_border_color(cell, lv_color_hex(0x14141A), 0);
      lv_obj_set_style_border_width(cell, 1, 0);
      g_cells[r][c] = cell;

      lv_obj_t* num = lv_label_create(cell);
      lv_obj_set_style_text_font(num, &font_game_40, 0);
      lv_obj_set_style_text_color(num, COL_CYAN, 0);
      lv_obj_center(num);
      g_cell_nums[r][c] = num;
    }

  /* 底部提示 (中文用 font_cjk_20, 避免缺字方块) */
  new_label(scr_game, &font_cjk_20, COL_LOW, 0, 0, "滑动屏幕 · 合并数字 · 目标 2048");
  lv_obj_align(lv_obj_get_child(scr_game, lv_obj_get_child_cnt(scr_game) - 1), LV_ALIGN_BOTTOM_MID, 0, -8);

  /* 覆盖层(游戏结束/胜利) */
  g_overlay = lv_obj_create(scr_game);
  lv_obj_remove_style_all(g_overlay);
  lv_obj_set_size(g_overlay, 328, 328);
  lv_obj_set_pos(g_overlay, boardX, boardY);
  lv_obj_set_style_bg_color(g_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_overlay, LV_OPA_80, 0);
  lv_obj_set_style_radius(g_overlay, 18, 0);
  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);

  g_overlay_text = new_label(g_overlay, &font_cjk_20, COL_HI, 0, 0, "");
  lv_obj_align(g_overlay_text, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t* restart = lv_obj_create(g_overlay);
  lv_obj_remove_style_all(restart);
  lv_obj_set_size(restart, 120, 44);
  lv_obj_set_style_bg_color(restart, TG_Restart(), 0);
  lv_obj_set_style_bg_opa(restart, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(restart, 12, 0);
  lv_obj_align(restart, LV_ALIGN_CENTER, 0, 40);
  lv_obj_t* rt = lv_label_create(restart);
  lv_obj_set_style_text_font(rt, &font_cjk_20, 0);
  lv_obj_set_style_text_color(rt, COL_BG, 0);
  lv_obj_center(rt);
  lv_label_set_text(rt, "重新开始");
}

/* ================= 贪吃蛇 =================
 * 16x16 网格, 环形缓冲蛇身 (O(1) 增删), 每帧只刷头/尾/食物 3 格
 * 蛇身渐进色: 头亮尾暗; 滑动手势转向 (带 2 步方向队列防反向自杀) */
#define SN_GRID   16
#define SN_CELL   20
#define SN_GAP    1
#define SN_PAD    1
#define SN_STEP   (SN_CELL + SN_GAP)               /* 21 */
#define SN_FRAME  (2*SN_PAD + SN_GRID*SN_CELL + (SN_GRID-1)*SN_GAP)  /* 337 */
#define SN_BX     ((368 - SN_FRAME) / 2)           /* 15 */
#define SN_BY     84
#define SN_MAXLEN (SN_GRID * SN_GRID)              /* 256 */

/* 方向: 0上 1下 2左 3右 (与2048滑动手势编码一致) */
static uint8_t sn_body[SN_MAXLEN + 8][2];  /* 环形缓冲: [x,y] */
static int sn_head, sn_tail, sn_len;       /* head=最新段, tail=最旧段 */
static int sn_dir, sn_dirQ[2], sn_dirQLen;/* 当前方向 + 输入队列(防反向) */
static int sn_food[2];                    /* 食物 [x,y] */
static int sn_score = 0, sn_best = 0;
static bool sn_alive = false;
static uint32_t sn_lastTick = 0;
static uint32_t sn_tickMs = 260;          /* 速度: 吃食加速 260→90ms */
static uint32_t sn_spawnedAt = 0;         /* 开局静默期(防误触立即转向) */
static bool sn_paused = false;

static lv_obj_t* sn_cells[SN_GRID][SN_GRID];   /* 预创建格子, 只换样式 */
static lv_obj_t* sn_score_val, *sn_best_val;
static lv_obj_t* sn_overlay, *sn_overlay_text;
static Preferences sn_prefs;

/* 渲染档位 + 脏检查 (防大面积flush花屏的关键)
 * 教训链: 全蛇256格每帧重画 → LVGL合并成337x337巨型脏区 → 持续大流量QSPI突发
 *   → 圆角mask被斜切成"等腰三角形"、行错位成"平行四边形" (与2048横纹同源)
 * 修复: 档位量化(距头0=头色, 1-5=0档, 6-10=1档, 11-15=2档, 16+=3档)
 *   + sn_shownTier 缓存 → 每tick只有档位变化的格子(~6格)真正触发样式/失效 */
#define SN_T_EMPTY (-1)
#define SN_T_HEAD    4
#define SN_T_FOOD     5
static int8_t sn_shownTier[SN_GRID][SN_GRID];      /* 每格当前显示的档位 */

static lv_color_t snakeTierColor(int tier) { return TG_Snake(tier); }

/* 段距头 idx → 档位 */
static int snakeTierOf(int idxFromHead) {
  if (idxFromHead == 0) return SN_T_HEAD;
  int t = idxFromHead * 3 / 16;
  return t > 3 ? 3 : t;
}

/* 脏检查画格: 档位没变就完全不碰 lv_obj (零失效区) */
static void snakeDrawCell(int x, int y, int tier) {
  if (sn_shownTier[y][x] == tier) return;
  sn_shownTier[y][x] = (int8_t)tier;
  lv_obj_t* c = sn_cells[y][x];
  lv_color_t col;
  lv_coord_t r;
  switch (tier) {
    case SN_T_EMPTY: col = lv_color_hex(0x0A0A0E); r = 8;  break;   /* 空地 */
    case SN_T_FOOD:  col = COL_ORANGE;           r = 10; break;     /* 食物=圆点 */
    default:         col = snakeTierColor(tier); r = 6;  break;    /* 蛇头/身 */
  }
  lv_obj_set_style_bg_color(c, col, 0);
  lv_obj_set_style_radius(c, r, 0);
}

static void snakePlaceFood() {
  /* 先收集全部空格, 再随机挑一个: 蛇很长时 O(N²) 随机重试会卡帧, 收集法稳定 O(N²) 一次性 */
  static int8_t freeX[SN_MAXLEN], freeY[SN_MAXLEN];
  int nFree = 0;
  bool occupied[SN_GRID][SN_GRID] = { false };
  for (int i = 0; i < sn_len; i++) {
    int k = (sn_head - i + SN_MAXLEN) % SN_MAXLEN;
    occupied[sn_body[k][0]][sn_body[k][1]] = true;
  }
  for (int y = 0; y < SN_GRID; y++)
    for (int x = 0; x < SN_GRID; x++)
      if (!occupied[y][x]) { freeX[nFree] = x; freeY[nFree] = y; nFree++; }
  if (nFree == 0) return;                 /* 满盘 (由通关逻辑接管) */
  int pick = random(nFree);
  sn_food[0] = freeX[pick]; sn_food[1] = freeY[pick];
  snakeDrawCell(sn_food[0], sn_food[1], SN_T_FOOD);
}

static void snakeNewGame() {
  /* 清场: 全格档位清空 (snakeDrawCell 自带脏检查) */
  for (int y = 0; y < SN_GRID; y++)
    for (int x = 0; x < SN_GRID; x++)
      snakeDrawCell(x, y, SN_T_EMPTY);
  /* 初始蛇: 3 段, 屏幕中间横向, 头朝右
   * 环形缓冲一致性(本条曾出大bug): 全代码遍历约定 = 从 head 往回走 (head-i)
   *   → 身体必须放在 head, head-1, head-2; tail = head-len+1
   *   → head+1 前进方向永远背向 tail (余量 253 格), 不会自擦/自吃
   * 错误做法(已废): 头放下标0身体放下标1/2 → head-i 踩到未初始化(0,0)
   *   画出杂点; 且 head+1 迎面撞 tail, 吃食后新头秒被擦 → 蛇冻结+撞墙死 */
  sn_len = 3;
  sn_head = 2;                                        /* 头在下标 2 */
  sn_tail = 0;                                        /* 最旧段在下标 0 */
  sn_body[0][0] = 6; sn_body[0][1] = 8;               /* 尾 (6,8) */
  sn_body[1][0] = 7; sn_body[1][1] = 8;
  sn_body[2][0] = 8; sn_body[2][1] = 8;               /* 头 (8,8) */
  sn_dir = 3;                                        /* 右 */
  sn_dirQLen = 0;
  sn_score = 0; sn_alive = true; sn_paused = false;
  sn_tickMs = 260;
  sn_spawnedAt = millis();
  snakePlaceFood();
  /* 画初始 3 段 (脏检查) */
  for (int i = 0; i < sn_len; i++) {
    int k = (sn_head - i + SN_MAXLEN) % SN_MAXLEN;
    snakeDrawCell(sn_body[k][0], sn_body[k][1], snakeTierOf(i));
  }
  char sb[8]; itoa(sn_score, sb, 10); lv_label_set_text(sn_score_val, sb);
  itoa(sn_best, sb, 10); lv_label_set_text(sn_best_val, sb);
  lv_obj_add_flag(sn_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void snakeDirPush(uint8_t d) {
  if (sn_dirQLen >= 2) return;
  /* 相同方向或反向(相对队尾方向)不入队 */
  uint8_t cur = sn_dirQLen ? sn_dirQ[sn_dirQLen - 1] : sn_dir;
  if (d == cur || (d ^ 1) == cur) return;   /* 0^1=1 上下互反, 2^1=3 左右互反 */
  sn_dirQ[sn_dirQLen++] = d;
}

static void snakeTick() {
  if (!sn_alive || sn_paused) return;
  /* 环形保险: 头尾指针决不允许重合 (重合=环形满, head-i 遍历会绕环两圈画出鬼影) */
  if (sn_len >= SN_MAXLEN) { sn_alive = false; return; }

  /* 开局 400ms 内忽略方向输入(静默期), 但仍然前进 */
  bool silent = (millis() - sn_spawnedAt) < 400;

  /* 消费方向队列 */
  if (!silent && sn_dirQLen) {
    sn_dir = sn_dirQ[0];
    sn_dirQ[0] = sn_dirQ[1];
    sn_dirQLen--;
  }

  /* 计算新头 + 穿墙 wrap (降低难度: 撞四周从对侧穿出, 撞自己仍死) */
  int nx = sn_body[sn_head][0], ny = sn_body[sn_head][1];
  switch (sn_dir) {
    case 0: ny--; break;
    case 1: ny++; break;
    case 2: nx--; break;
    case 3: nx++; break;
  }
  if (nx < 0) nx = SN_GRID - 1; else if (nx >= SN_GRID) nx = 0;
  if (ny < 0) ny = SN_GRID - 1; else if (ny >= SN_GRID) ny = 0;

  /* 撞身检测 (穿墙版不再判撞墙; 尾格豁免: 不吃时尾将移走) */
  bool hitSelf = false;
  {
    bool willEat = (nx == sn_food[0] && ny == sn_food[1]);
    int checkLen = willEat ? sn_len : sn_len - 1;
    for (int i = 0; i < checkLen; i++) {
      int k = (sn_head - i + SN_MAXLEN) % SN_MAXLEN;
      if (sn_body[k][0] == nx && sn_body[k][1] == ny) { hitSelf = true; break; }
    }
  }
  if (hitSelf) {
    sn_alive = false;
    char sb[8];
    if (sn_score > sn_best) {
      sn_best = sn_score;
      sn_prefs.putInt("best", sn_best);
      itoa(sn_best, sb, 10); lv_label_set_text(sn_best_val, sb);
    }
    lv_label_set_text(sn_overlay_text, "游戏结束");
    lv_obj_clear_flag(sn_overlay, LV_OBJ_FLAG_HIDDEN);
    sfxGameOver();
    return;
  }

  /* 新头入环形缓冲 */
  sn_head = (sn_head + 1) % SN_MAXLEN;
  sn_body[sn_head][0] = nx; sn_body[sn_head][1] = ny;

  bool ate = (nx == sn_food[0] && ny == sn_food[1]);
  if (ate) {
    sn_len++;
    sn_score++;
    char sb[8]; itoa(sn_score, sb, 10); lv_label_set_text(sn_score_val, sb);
    if (sn_len >= 40) sn_tickMs = 90;
    else if (sn_len >= 24) sn_tickMs = 120;
    else if (sn_len >= 14) sn_tickMs = 150;
    else if (sn_len >= 8)  sn_tickMs = 190;
    else if (sn_len >= 5)  sn_tickMs = 220;
    sfxMerge();                    /* 吃食: 复用双音上行 */
    snakePlaceFood();
    if (sn_len >= SN_MAXLEN) {     /* 满盘通关 */
      sn_alive = false;
      lv_label_set_text(sn_overlay_text, "你赢了!");
      lv_obj_clear_flag(sn_overlay, LV_OBJ_FLAG_HIDDEN);
      sfxWin();
      return;
    }
  } else {
    /* 尾部移除: 先取老尾坐标, tail 再 +1
     * (顺序曾写反: 吃食后下一tick用 tail-1 倒推会擦到垃圾格/蛇身格 → "光点散开") */
    int ex = sn_body[sn_tail][0], ey = sn_body[sn_tail][1];
    sn_tail = (sn_tail + 1) % SN_MAXLEN;
    snakeDrawCell(ex, ey, SN_T_EMPTY);
  }

  /* 增量渲染 (脏检查: 每tick只有档位变化的~6格触发失效)
   * 非-吃: 全段距头+1 → 只有跨档边界的段变档 (24段蛇≈3格)
   * 吃: 距头不变 → 蛇身0格变档 (尾不动距头也不变) */
  for (int i = 0; i < sn_len; i++) {
    int k = (sn_head - i + SN_MAXLEN) % SN_MAXLEN;
    snakeDrawCell(sn_body[k][0], sn_body[k][1], snakeTierOf(i));
  }
}

static void build_snake() {
  scr_snake = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_snake, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_snake, LV_OPA_COVER, LV_PART_MAIN);

  /* 顶栏: 返回键 (与2048同位 20,20,40x40) */
  lv_obj_t* back = lv_obj_create(scr_snake);
  lv_obj_remove_style_all(back);
  lv_obj_set_size(back, 40, 40);
  lv_obj_set_pos(back, 20, 20);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(back, 12, 0);
  lv_obj_set_style_border_color(back, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(back, 1, 0);
  lv_obj_t* back_t = lv_label_create(back);
  lv_obj_set_style_text_font(back_t, &font_ui_20, 0);
  lv_obj_set_style_text_color(back_t, COL_MID, 0);
  lv_obj_center(back_t);
  lv_label_set_text(back_t, "<");

  /* 分数卡 (SCORE + BEST, 样式与2048一致) */
  lv_obj_t* sc = lv_obj_create(scr_snake);
  lv_obj_remove_style_all(sc);
  lv_obj_set_size(sc, 90, 54);
  lv_obj_set_pos(sc, 74, 16);
  lv_obj_set_style_bg_color(sc, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(sc, 12, 0);
  lv_obj_set_style_border_color(sc, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(sc, 1, 0);
  new_label(sc, &font_cjk_20, COL_MID, 0, 5, "SCORE");
  lv_obj_align(lv_obj_get_child(sc, 0), LV_ALIGN_TOP_MID, 0, 5);
  sn_score_val = new_label(sc, &font_cjk_20, TG_Score(), 0, 0, "0");
  lv_obj_align(sn_score_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  lv_obj_t* bc = lv_obj_create(scr_snake);
  lv_obj_remove_style_all(bc);
  lv_obj_set_size(bc, 90, 54);
  lv_obj_set_pos(bc, 174, 16);
  lv_obj_set_style_bg_color(bc, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(bc, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bc, 12, 0);
  lv_obj_set_style_border_color(bc, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(bc, 1, 0);
  new_label(bc, &font_cjk_20, COL_MID, 0, 5, "BEST");
  lv_obj_align(lv_obj_get_child(bc, 0), LV_ALIGN_TOP_MID, 0, 5);
  sn_best_val = new_label(bc, &font_cjk_20, TG_Best(), 0, 0, "0");
  lv_obj_align(sn_best_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  /* 棋盘外框 */
  lv_obj_t* frame = lv_obj_create(scr_snake);
  lv_obj_remove_style_all(frame);
  lv_obj_set_size(frame, SN_FRAME, SN_FRAME);
  lv_obj_set_pos(frame, SN_BX, SN_BY);
  lv_obj_set_style_bg_color(frame, lv_color_hex(0x050507), 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(frame, 18, 0);
  lv_obj_set_style_border_color(frame, lv_color_hex(0x14141A), 0);
  lv_obj_set_style_border_width(frame, 1, 0);

  /* 16x16 预创建格子 (337px 框内: pad1 + 20格 + 1缝) */
  for (int y = 0; y < SN_GRID; y++)
    for (int x = 0; x < SN_GRID; x++) {
      lv_obj_t* c = lv_obj_create(scr_snake);
      lv_obj_remove_style_all(c);
      lv_obj_set_size(c, SN_CELL, SN_CELL);
      lv_obj_set_pos(c, SN_BX + SN_PAD + x * SN_STEP,
                          SN_BY + SN_PAD + y * SN_STEP);
      lv_obj_set_style_bg_color(c, lv_color_hex(0x0A0A0E), 0);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(c, 8, 0);
      sn_cells[y][x] = c;
    }

  /* 底部提示 */
  new_label(scr_snake, &font_cjk_20, COL_LOW, 0, 0, "滑动转向 · 可穿墙 · 吃光点");
  lv_obj_align(lv_obj_get_child(scr_snake, lv_obj_get_child_cnt(scr_snake) - 1),
               LV_ALIGN_BOTTOM_MID, 0, -4);   /* 棋盘底421, 提示顶421 相接不重叠(-8曾压4px) */

  /* 覆盖层 (结束/胜利) */
  sn_overlay = lv_obj_create(scr_snake);
  lv_obj_remove_style_all(sn_overlay);
  lv_obj_set_size(sn_overlay, SN_FRAME, SN_FRAME);
  lv_obj_set_pos(sn_overlay, SN_BX, SN_BY);
  lv_obj_set_style_bg_color(sn_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(sn_overlay, LV_OPA_80, 0);
  lv_obj_set_style_radius(sn_overlay, 18, 0);
  lv_obj_add_flag(sn_overlay, LV_OBJ_FLAG_HIDDEN);

  sn_overlay_text = new_label(sn_overlay, &font_cjk_20, COL_HI, 0, 0, "");
  lv_obj_align(sn_overlay_text, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t* restart = lv_obj_create(sn_overlay);
  lv_obj_remove_style_all(restart);
  lv_obj_set_size(restart, 120, 44);
  lv_obj_set_style_bg_color(restart, g_theme ? MONO_G1 : COL_ORANGE, 0);
  lv_obj_set_style_bg_opa(restart, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(restart, 12, 0);
  lv_obj_align(restart, LV_ALIGN_CENTER, 0, 40);
  lv_obj_t* rt = lv_label_create(restart);
  lv_obj_set_style_text_font(rt, &font_cjk_20, 0);
  lv_obj_set_style_text_color(rt, COL_BG, 0);
  lv_obj_center(rt);
  lv_label_set_text(rt, "重新开始");
}

/* ================= 打砖块 =================
 * 7x6 砖墙 + 70px 跟手挡板 + 浮点物理球(30Hz tick)
 * 渲染严格套「增量+脏检查」范式(skill 6.3C): 每 tick 只有真实变化的
 * ~3 格(球旧/新, 挡板旧/新)触发样式, 砖只在被击碎时变一次
 * 物理已 140 局仿真验证: 零穿模/零卡死/零超时 */
#define BK_COLS   7
#define BK_ROWS   6
#define BK_BW     42
#define BK_BH     18
#define BK_GAP    3
#define BK_WX     28
#define BK_WY     84
#define BK_WW     (BK_COLS*BK_BW + (BK_COLS-1)*BK_GAP)   /* 312 */
#define BK_PD_W   70
#define BK_PD_H   10
#define BK_PD_Y   384
#define BK_R      5
#define BK_SPEED  5.15f
#define BK_SPEED2 6.6f         /* 剩砖<12 提速 (×1.28) */
#define BK_TICK   25           /* ms, 40Hz — 高速球用更细步进保碰撞精度 */

static uint8_t bk_bricks[BK_ROWS][BK_COLS];   /* 1=在 0=碎 */
static int bk_left;
static float bk_bx, bk_by, bk_vx, bk_vy;
static int bk_pdX;                             /* 挡板左缘 */
static int bk_lives, bk_score, bk_best;
static bool bk_alive, bk_serving;              /* serving: 球粘板上待发 */
static uint32_t bk_lastTick;
static uint32_t bk_serveAt;
static Preferences bk_prefs;

static lv_obj_t* bk_brickObj[BK_ROWS][BK_COLS];
static lv_obj_t* bk_paddle;
static lv_obj_t* bk_ball;
static lv_obj_t* bk_livesLabel;
static lv_obj_t* bk_score_val, *bk_best_val;
static lv_obj_t* bk_overlay, *bk_overlay_text;
static bool bk_paddleDirty;                    /* 挡板需重画 */
static int bk_ballOldX, bk_ballOldY;            /* 球上一渲染位置 */
static bool bk_ballShown;

/* 砖行颜色: 高行亮低行暗 (青色系, 与蛇呼应) */
static lv_color_t bkBrickColor(int row) { return TG_Brick(row); }

static void bkNewGame() {
  for (int r = 0; r < BK_ROWS; r++)
    for (int c = 0; c < BK_COLS; c++) {
      /* 无条件重画砖色 (主题切换后进游戏也要正确换色, 不能只画碎过的) */
      lv_obj_set_style_bg_color(bk_brickObj[r][c], bkBrickColor(r), 0);
      lv_obj_set_style_bg_opa(bk_brickObj[r][c], LV_OPA_COVER, 0);
      bk_bricks[r][c] = 1;
    }
  lv_obj_set_style_bg_color(bk_paddle, TG_Paddle(), 0);
  lv_obj_set_style_bg_color(bk_ball, TG_Ball(), 0);
  bk_left = BK_ROWS * BK_COLS;
  bk_lives = 3; bk_score = 0;
  bk_alive = true; bk_serving = true;
  bk_pdX = (368 - BK_PD_W) / 2;
  bk_paddleDirty = true;
  bk_ballShown = false;                       /* 球隐藏待发 */
  lv_obj_add_flag(bk_ball, LV_OBJ_FLAG_HIDDEN);
  bk_serveAt = millis();
  char sb[8];
  itoa(bk_score, sb, 10); lv_label_set_text(bk_score_val, sb);
  itoa(bk_best, sb, 10); lv_label_set_text(bk_best_val, sb);
  lv_label_set_text(bk_livesLabel, "生命 ●●●");
  lv_obj_add_flag(bk_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* 发球: 随机角度 ±50°, 球贴板顶 */
static void bkServe() {
  float a = radians(random(-50, 50));
  bk_bx = 368 / 2.0f; bk_by = BK_PD_Y - 30.0f;
  bk_vx = sinf(a) * BK_SPEED; bk_vy = -cosf(a) * BK_SPEED;
  bk_serving = false;
  bk_ballShown = false;   /* 首帧画球时再显示 */
}

static void bkTick() {
  if (!bk_alive) return;
  if (bk_serving) return;   /* 球粘板: 等轻点 */

  /* 提速档 */
  float spd = (bk_left < 12) ? BK_SPEED2 : BK_SPEED;

  /* 步进 */
  bk_bx += bk_vx * (spd / BK_SPEED);   /* 简化: 速度档直接缩放向量 */
  bk_by += bk_vy * (spd / BK_SPEED);

  /* 三面墙反弹 */
  if (bk_bx - BK_R < BK_WX) { bk_bx = BK_WX + BK_R; bk_vx = -bk_vx; sfxClick(); }
  if (bk_bx + BK_R > BK_WX + BK_WW) { bk_bx = BK_WX + BK_WW - BK_R; bk_vx = -bk_vx; sfxClick(); }
  if (bk_by - BK_R < BK_WY) { bk_by = BK_WY + BK_R; bk_vy = -bk_vy; sfxClick(); }

  /* 挡板: 只接下落球(vy>0); 物理镜面反射 (平面法线竖直: vx保持, vy翻转)
   * 判定窗口 PD_H+8: 40Hz 高速球(6.6px/tick)需更宽容的接触判定防穿透 */
  if (bk_vy > 0 &&
      bk_by + BK_R >= BK_PD_Y && bk_by + BK_R <= BK_PD_Y + BK_PD_H + 8 &&
      bk_bx >= bk_pdX - BK_R && bk_bx <= bk_pdX + BK_PD_W + BK_R) {
    bk_vy = -bk_vy;
    bk_by = BK_PD_Y - BK_R;
    /* 防垂直死循环: 水平分量过小时轻推 (物理上等效轻微旋转挡板) */
    if (fabsf(bk_vx) < 0.4f) {
      bk_vx = (bk_vx >= 0) ? 0.4f : -0.4f;
      float s = spd / sqrtf(bk_vx * bk_vx + bk_vy * bk_vy);
      bk_vx *= s; bk_vy *= s;        /* 保持速度幅值不变 */
    }
    sfxClick();
  }

  /* 砖 AABB 碰撞 (单砖/tick, 小步进不穿模) */
  {
    float bl = bk_bx - BK_R, br = bk_bx + BK_R;
    float bt = bk_by - BK_R, bb = bk_by + BK_R;
    for (int r = 0; r < BK_ROWS; r++) {
      for (int c = 0; c < BK_COLS; c++) {
        if (!bk_bricks[r][c]) continue;
        float xl = BK_WX + c * (BK_BW + BK_GAP);
        float yt = BK_WY + r * (BK_BH + BK_GAP);
        if (br > xl && bl < xl + BK_BW && bb > yt && bt < yt + BK_BH) {
          bk_bricks[r][c] = 0; bk_left--; bk_score += (BK_ROWS - r);  /* 高行分高 */
          lv_obj_set_style_bg_color(bk_brickObj[r][c], lv_color_hex(0x0A0A0E), 0);
          lv_obj_set_style_bg_opa(bk_brickObj[r][c], LV_OPA_20, 0);   /* 碎砖留淡痕 */
          char sb[8]; itoa(bk_score, sb, 10); lv_label_set_text(bk_score_val, sb);
          sfxMerge();
          /* 反射轴: 重叠量小者翻转 */
          float ox = (br - xl < xl + BK_BW - bl) ? (br - xl) : (xl + BK_BW - bl);
          float oy = (bb - yt < yt + BK_BH - bt) ? (bb - yt) : (yt + BK_BH - bt);
          if (ox < oy) bk_vx = -bk_vx; else bk_vy = -bk_vy;
          if (bk_left == 0) {                 /* 通关 */
            bk_alive = false;
            if (bk_score > bk_best) { bk_best = bk_score; bk_prefs.putInt("best", bk_best);
              char sb[8]; itoa(bk_best, sb, 10); lv_label_set_text(bk_best_val, sb); }
            lv_label_set_text(bk_overlay_text, "你赢了!");
            lv_obj_clear_flag(bk_overlay, LV_OBJ_FLAG_HIDDEN);
            sfxWin();
            return;
          }
          goto brick_done;   /* 单砖/tick */
        }
      }
    }
  }
brick_done:

  /* 丢球 */
  if (bk_by - BK_R > 448) {
    bk_lives--;
    if (bk_lives == 2) lv_label_set_text(bk_livesLabel, "生命 ●●○");
    else if (bk_lives == 1) lv_label_set_text(bk_livesLabel, "生命 ●○○");
    else lv_label_set_text(bk_livesLabel, "生命 ○○○");
    if (bk_lives <= 0) {
      bk_alive = false;
      if (bk_score > bk_best) { bk_best = bk_score; bk_prefs.putInt("best", bk_best);
        char sb[8]; itoa(bk_best, sb, 10); lv_label_set_text(bk_best_val, sb); }
      lv_label_set_text(bk_overlay_text, "游戏结束");
      lv_obj_clear_flag(bk_overlay, LV_OBJ_FLAG_HIDDEN);
      sfxGameOver();
      return;
    }
    bk_serving = true;
    bk_ballShown = false;
    lv_obj_add_flag(bk_ball, LV_OBJ_FLAG_HIDDEN);
    bk_serveAt = millis();
  }
}

static void build_breakout() {
  scr_breakout = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_breakout, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_breakout, LV_OPA_COVER, LV_PART_MAIN);

  /* 顶栏: 返回 (与2048/蛇同位) */
  lv_obj_t* back = lv_obj_create(scr_breakout);
  lv_obj_remove_style_all(back);
  lv_obj_set_size(back, 40, 40);
  lv_obj_set_pos(back, 20, 20);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(back, 12, 0);
  lv_obj_set_style_border_color(back, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(back, 1, 0);
  lv_obj_t* back_t = lv_label_create(back);
  lv_obj_set_style_text_font(back_t, &font_ui_20, 0);
  lv_obj_set_style_text_color(back_t, COL_MID, 0);
  lv_obj_center(back_t);
  lv_label_set_text(back_t, "<");

  /* 分数卡 (SCORE + BEST) */
  lv_obj_t* sc = lv_obj_create(scr_breakout);
  lv_obj_remove_style_all(sc);
  lv_obj_set_size(sc, 90, 54);
  lv_obj_set_pos(sc, 74, 16);
  lv_obj_set_style_bg_color(sc, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(sc, 12, 0);
  lv_obj_set_style_border_color(sc, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(sc, 1, 0);
  new_label(sc, &font_cjk_20, COL_MID, 0, 5, "SCORE");
  lv_obj_align(lv_obj_get_child(sc, 0), LV_ALIGN_TOP_MID, 0, 5);
  bk_score_val = new_label(sc, &font_cjk_20, TG_Score(), 0, 0, "0");
  lv_obj_align(bk_score_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  lv_obj_t* bcard = lv_obj_create(scr_breakout);
  lv_obj_remove_style_all(bcard);
  lv_obj_set_size(bcard, 90, 54);
  lv_obj_set_pos(bcard, 174, 16);
  lv_obj_set_style_bg_color(bcard, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(bcard, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bcard, 12, 0);
  lv_obj_set_style_border_color(bcard, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(bcard, 1, 0);
  new_label(bcard, &font_cjk_20, COL_MID, 0, 5, "BEST");
  lv_obj_align(lv_obj_get_child(bcard, 0), LV_ALIGN_TOP_MID, 0, 5);
  bk_best_val = new_label(bcard, &font_cjk_20, TG_Best(), 0, 0, "0");
  lv_obj_align(bk_best_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  /* 砖墙 7x6 (312 宽居中) */
  for (int r = 0; r < BK_ROWS; r++)
    for (int c = 0; c < BK_COLS; c++) {
      lv_obj_t* b = lv_obj_create(scr_breakout);
      lv_obj_remove_style_all(b);
      lv_obj_set_size(b, BK_BW, BK_BH);
      lv_obj_set_pos(b, BK_WX + c * (BK_BW + BK_GAP), BK_WY + r * (BK_BH + BK_GAP));
      lv_obj_set_style_bg_color(b, bkBrickColor(r), 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(b, 4, 0);
      bk_brickObj[r][c] = b;
    }

  /* 挡板 */
  bk_paddle = lv_obj_create(scr_breakout);
  lv_obj_remove_style_all(bk_paddle);
  lv_obj_set_size(bk_paddle, BK_PD_W, BK_PD_H);
  lv_obj_set_pos(bk_paddle, (368 - BK_PD_W) / 2, BK_PD_Y);
  lv_obj_set_style_bg_color(bk_paddle, TG_Paddle(), 0);
  lv_obj_set_style_bg_opa(bk_paddle, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bk_paddle, 5, 0);

  /* 球 (10px 圆) */
  bk_ball = lv_obj_create(scr_breakout);
  lv_obj_remove_style_all(bk_ball);
  lv_obj_set_size(bk_ball, BK_R * 2, BK_R * 2);
  lv_obj_set_style_bg_color(bk_ball, TG_Ball(), 0);
  lv_obj_set_style_bg_opa(bk_ball, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bk_ball, 5, 0);
  lv_obj_add_flag(bk_ball, LV_OBJ_FLAG_HIDDEN);

  /* 底部: 生命 + 提示 */
  bk_livesLabel = new_label(scr_breakout, &font_cjk_20, TG_Lives(), 0, 0, "生命 ●●●");
  lv_obj_align(bk_livesLabel, LV_ALIGN_BOTTOM_LEFT, 24, -12);
  new_label(scr_breakout, &font_cjk_20, COL_LOW, 0, 0, "拖动挡板 · 轻点发球");
  lv_obj_align(lv_obj_get_child(scr_breakout, lv_obj_get_child_cnt(scr_breakout) - 1),
               LV_ALIGN_BOTTOM_RIGHT, -24, -12);

  /* 覆盖层 */
  bk_overlay = lv_obj_create(scr_breakout);
  lv_obj_remove_style_all(bk_overlay);
  lv_obj_set_size(bk_overlay, 312, 320);
  lv_obj_set_pos(bk_overlay, BK_WX, BK_WY);
  lv_obj_set_style_bg_color(bk_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(bk_overlay, LV_OPA_80, 0);
  lv_obj_set_style_radius(bk_overlay, 18, 0);
  lv_obj_add_flag(bk_overlay, LV_OBJ_FLAG_HIDDEN);

  bk_overlay_text = new_label(bk_overlay, &font_cjk_20, COL_HI, 0, 0, "");
  lv_obj_align(bk_overlay_text, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t* restart = lv_obj_create(bk_overlay);
  lv_obj_remove_style_all(restart);
  lv_obj_set_size(restart, 120, 44);
  lv_obj_set_style_bg_color(restart, TG_Restart(), 0);
  lv_obj_set_style_bg_opa(restart, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(restart, 12, 0);
  lv_obj_align(restart, LV_ALIGN_CENTER, 0, 40);
  lv_obj_t* rt = lv_label_create(restart);
  lv_obj_set_style_text_font(rt, &font_cjk_20, 0);
  lv_obj_set_style_text_color(rt, COL_BG, 0);
  lv_obj_center(rt);
  lv_label_set_text(rt, "重新开始");
}

/* ================= 俄罗斯方块 =================
 * 10x18 井 + 7-bag + 旋转(O免转/I用公式/T SZ J L用3x3) + 简化踢墙
 * 渲染: 180井格预创建 + 脏检查(每tick只有活动块覆盖的~8格变化) */
#define TT_W  10
#define TT_H  18
#define TT_PX 18
#define TT_X  30
#define TT_Y  76
#define TT_INFO_X 238

/* 7 形状 (4x4/3x3 盒, 原点左上; 已仿真验证旋转闭合) */
static const int8_t TT_SHAPES[7][4][2] = {
  /* I */ {{0,1},{1,1},{2,1},{3,1}},
  /* O */ {{1,1},{2,1},{1,2},{2,2}},
  /* T */ {{1,0},{0,1},{1,1},{2,1}},
  /* S */ {{1,0},{2,0},{0,1},{1,1}},
  /* Z */ {{0,0},{1,0},{1,1},{2,1}},
  /* J */ {{0,0},{0,1},{1,1},{2,1}},
  /* L */ {{2,0},{0,1},{1,1},{2,1}},
};
static const lv_color_t TT_COLORS[7] = {
  lv_color_hex(0x00E5C8),   /* I 青 */
  lv_color_hex(0xFFD700),   /* O 金 */
  lv_color_hex(0x8B5CF6),   /* T 紫 */
  lv_color_hex(0x00B89C),   /* S 绿 */
  lv_color_hex(0xFF6B4A),   /* Z 橙红 */
  lv_color_hex(0x3B82F6),   /* J 蓝 */
  lv_color_hex(0xFFD700),   /* L 金 — 改橙黄区分 */
};
/* 7 形颜色修正: L 用橙黄区分 O 金 */
/* (直接在数组里给 0xFFB300) */

static uint8_t tt_board[TT_H][TT_W];          /* 0空 1-7=块色号+1 */
static int tt_cur, tt_rot, tt_px, tt_py;      /* 当前方: 形/旋转/位置 */
static int tt_next;                            /* NEXT 预览 */
static int tt_bag[7], tt_bagN;                /* 7-bag */
static int tt_score, tt_lines, tt_level, tt_best;
static bool tt_alive, tt_paused;
static uint32_t tt_lastFall, tt_fallMs;
static Preferences tt_prefs;

static lv_obj_t* tt_cells[TT_H][TT_W];       /* 井格 180 */
static lv_obj_t* tt_nextCells[4][4];          /* NEXT 4x4 */
static lv_obj_t* tt_score_val, *tt_best_val, *tt_lines_val, *tt_level_val;
static lv_obj_t* tt_overlay, *tt_overlay_text;
static uint8_t tt_shown[TT_H][TT_W];          /* 脏检查: 当前显示色号 */
static uint8_t tt_nextShown[4][4];

/* 旋转: O 免转; I 用4x4公式 (x,y)→(3-y,x); 其余 3x3 (x,y)→(2-y,x) */
static void ttRotateCells(int piece, int rot, int8_t out[4][2]) {
  memcpy(out, TT_SHAPES[piece], 8);
  if (piece == 1) return;                    /* O */
  int n = (piece == 0) ? 4 : 3;              /* I 盒4 其余盒3 */
  for (int r = 0; r < rot; r++)
    for (int i = 0; i < 4; i++) {
      int x = out[i][0], y = out[i][1];
      out[i][0] = (int8_t)(n - 1 - y);
      out[i][1] = (int8_t)x;
    }
}

static bool ttCollides(int piece, int rot, int px, int py) {
  int8_t c[4][2];
  ttRotateCells(piece, rot, c);
  for (int i = 0; i < 4; i++) {
    int x = px + c[i][0], y = py + c[i][1];
    if (x < 0 || x >= TT_W || y >= TT_H) return true;
    if (y >= 0 && tt_board[y][x]) return true;
  }
  return false;
}

/* 画格 (脏检查) */
static void ttDrawCell(int x, int y, uint8_t colorIdx) {   /* 0=空 1-7=色 */
  if (tt_shown[y][x] == colorIdx) return;
  tt_shown[y][x] = colorIdx;
  lv_obj_t* c = tt_cells[y][x];
  if (colorIdx == 0) {
    lv_obj_set_style_bg_color(c, lv_color_hex(0x0A0A0E), 0);
  } else {
    lv_obj_set_style_bg_color(c, TG_Tet(colorIdx - 1), 0);
  }
}

/* 画活动块 (覆盖位置, 脏检查) */
static void ttDrawPieceAt(int piece, int rot, int px, int py, bool show) {
  int8_t c[4][2];
  ttRotateCells(piece, rot, c);
  for (int i = 0; i < 4; i++) {
    int x = px + c[i][0], y = py + c[i][1];
    if (y >= 0 && x >= 0 && x < TT_W && y < TT_H)
      ttDrawCell(x, y, show ? (uint8_t)(piece + 1) : (tt_board[y][x] ? (uint8_t)(tt_board[y][x]) : 0));
  }
}

/* NEXT 预览渲染 (4x4, 脏检查) */
static void ttDrawNext() {
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) {
      uint8_t want = 0;
      for (int i = 0; i < 4; i++)
        if (TT_SHAPES[tt_next][i][0] == x && TT_SHAPES[tt_next][i][1] == y)
          want = (uint8_t)(tt_next + 1);
      if (tt_nextShown[y][x] != want) {
        tt_nextShown[y][x] = want;
        lv_obj_set_style_bg_color(tt_nextCells[y][x],
          want ? TG_Tet(want - 1) : lv_color_hex(0x0A0A0E), 0);
      }
    }
}

/* 7-bag */
static int ttNextPiece() {
  if (tt_bagN <= 0) {
    for (int i = 0; i < 7; i++) tt_bag[i] = i;
    for (int i = 6; i > 0; i--) {
      int j = random(i + 1);
      int t = tt_bag[i]; tt_bag[i] = tt_bag[j]; tt_bag[j] = t;
    }
    tt_bagN = 7;
  }
  return tt_bag[--tt_bagN];
}

static void ttSpawn() {
  tt_cur = tt_next;
  tt_next = ttNextPiece();
  tt_rot = 0;
  tt_px = (TT_W - 4) / 2;   /* 3 */
  tt_py = -2;                /* 顶上方潜入 */
  ttDrawNext();
  if (ttCollides(tt_cur, tt_rot, tt_px, tt_py)) {
    /* 出生即堵 = 顶出, 游戏结束 */
    tt_alive = false;
    if (tt_score > tt_best) {
      tt_best = tt_score;
      tt_prefs.putInt("best", tt_best);
      char sb[8]; itoa(tt_best, sb, 10); lv_label_set_text(tt_best_val, sb);
    }
    lv_label_set_text(tt_overlay_text, "游戏结束");
    lv_obj_clear_flag(tt_overlay, LV_OBJ_FLAG_HIDDEN);
    sfxGameOver();
  }
}

/* 锁定 → 消行 → 计分 */
static void ttLock() {
  int8_t c[4][2];
  ttRotateCells(tt_cur, tt_rot, c);
  for (int i = 0; i < 4; i++) {
    int x = tt_px + c[i][0], y = tt_py + c[i][1];
    if (y >= 0) tt_board[y][x] = (uint8_t)(tt_cur + 1);
  }
  /* 消行 */
  int cleared = 0;
  for (int y = TT_H - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < TT_W; x++)
      if (!tt_board[y][x]) { full = false; break; }
    if (full) {
      cleared++;
      for (int yy = y; yy > 0; yy--)
        for (int x = 0; x < TT_W; x++)
          tt_board[yy][x] = tt_board[yy - 1][x];
      for (int x = 0; x < TT_W; x++) tt_board[0][x] = 0;
      y++;   /* 当前行重新检查(已上移) */
    }
  }
  if (cleared > 0) {
    static const int LINE_SCORE[5] = {0, 100, 300, 500, 800};
    tt_score += LINE_SCORE[cleared] * (tt_level + 1);
    tt_lines += cleared;
    tt_level = tt_lines / 10;
    tt_fallMs = 800 - tt_level * 70; if (tt_fallMs < 120) tt_fallMs = 120;
    char sb[8];
    itoa(tt_score, sb, 10); lv_label_set_text(tt_score_val, sb);
    itoa(tt_lines, sb, 10); lv_label_set_text(tt_lines_val, sb);
    itoa(tt_level, sb, 10); lv_label_set_text(tt_level_val, sb);
    if (cleared >= 2) sfxWin(); else sfxMerge();
  } else {
    sfxClick();
  }
  /* 全盘重画 (消行后整列下移, 逐格脏检查自然只画差异) */
  for (int y = 0; y < TT_H; y++)
    for (int x = 0; x < TT_W; x++)
      ttDrawCell(x, y, tt_board[y][x]);
  ttSpawn();
}

static void ttNewGame() {
  memset(tt_board, 0, sizeof(tt_board));
  for (int y = 0; y < TT_H; y++)
    for (int x = 0; x < TT_W; x++) ttDrawCell(x, y, 0);
  memset(tt_nextShown, 0xFF, sizeof(tt_nextShown));   /* 强制 NEXT 重画 */
  tt_score = 0; tt_lines = 0; tt_level = 0;
  tt_fallMs = 800;
  tt_alive = true; tt_paused = false;
  tt_bagN = 0;
  tt_next = ttNextPiece();
  char sb[8];
  itoa(tt_score, sb, 10); lv_label_set_text(tt_score_val, sb);
  itoa(tt_lines, sb, 10); lv_label_set_text(tt_lines_val, sb);
  itoa(tt_level, sb, 10); lv_label_set_text(tt_level_val, sb);
  itoa(tt_best, sb, 10); lv_label_set_text(tt_best_val, sb);
  lv_obj_add_flag(tt_overlay, LV_OBJ_FLAG_HIDDEN);
  tt_lastFall = millis();
  ttSpawn();
}

/* 重力 tick (loop 驱动) */
static void ttFallStep() {
  if (!tt_alive || tt_paused) return;
  uint32_t now = millis();
  if (now - tt_lastFall < tt_fallMs) return;
  tt_lastFall = now;
  if (!ttCollides(tt_cur, tt_rot, tt_px, tt_py + 1)) {
    /* 下移: 擦旧画新 */
    ttDrawPieceAt(tt_cur, tt_rot, tt_px, tt_py, false);
    tt_py++;
    ttDrawPieceAt(tt_cur, tt_rot, tt_px, tt_py, true);
  } else {
    ttLock();
  }
}

/* 用户操作: 左右移/旋转/软降 — 全部带踢墙(简单: 逐格左右试) */
static bool ttMove(int dx) {
  if (ttCollides(tt_cur, tt_rot, tt_px + dx, tt_py)) return false;
  ttDrawPieceAt(tt_cur, tt_rot, tt_px, tt_py, false);
  tt_px += dx;
  ttDrawPieceAt(tt_cur, tt_rot, tt_px, tt_py, true);
  return true;
}

static bool ttRotate() {
  int newRot = (tt_rot + 1) & 3;
  /* 踢墙: 原位 → 左1 → 右1 → 左2 → 右2 */
  static const int8_t kicks[5] = {0, -1, 1, -2, 2};
  for (int k = 0; k < 5; k++) {
    if (!ttCollides(tt_cur, newRot, tt_px + kicks[k], tt_py)) {
      ttDrawPieceAt(tt_cur, tt_rot, tt_px, tt_py, false);
      tt_rot = newRot;
      tt_px += kicks[k];
      ttDrawPieceAt(tt_cur, tt_rot, tt_px, tt_py, true);
      return true;
    }
  }
  return false;
}

/* 硬降: 直落到底+锁定 (下滑手势; 一次一格格降用户无感, 2026-09 验收反馈) */
static void ttHardDrop() {
  while (!ttCollides(tt_cur, tt_rot, tt_px, tt_py + 1)) tt_py++;
  ttLock();
}

static void build_tetris() {
  scr_tetris = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_tetris, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_tetris, LV_OPA_COVER, LV_PART_MAIN);

  /* 顶栏: 返回 (三游戏同位) */
  lv_obj_t* back = lv_obj_create(scr_tetris);
  lv_obj_remove_style_all(back);
  lv_obj_set_size(back, 40, 40);
  lv_obj_set_pos(back, 20, 20);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(back, 12, 0);
  lv_obj_set_style_border_color(back, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(back, 1, 0);
  lv_obj_t* back_t = lv_label_create(back);
  lv_obj_set_style_text_font(back_t, &font_ui_20, 0);
  lv_obj_set_style_text_color(back_t, COL_MID, 0);
  lv_obj_center(back_t);
  lv_label_set_text(back_t, "<");

  /* 分数卡 */
  lv_obj_t* sc = lv_obj_create(scr_tetris);
  lv_obj_remove_style_all(sc);
  lv_obj_set_size(sc, 90, 54);
  lv_obj_set_pos(sc, 74, 16);
  lv_obj_set_style_bg_color(sc, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(sc, 12, 0);
  lv_obj_set_style_border_color(sc, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(sc, 1, 0);
  new_label(sc, &font_cjk_20, COL_MID, 0, 5, "SCORE");
  lv_obj_align(lv_obj_get_child(sc, 0), LV_ALIGN_TOP_MID, 0, 5);
  tt_score_val = new_label(sc, &font_cjk_20, TG_Score(), 0, 0, "0");
  lv_obj_align(tt_score_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  lv_obj_t* bcard = lv_obj_create(scr_tetris);
  lv_obj_remove_style_all(bcard);
  lv_obj_set_size(bcard, 90, 54);
  lv_obj_set_pos(bcard, 174, 16);
  lv_obj_set_style_bg_color(bcard, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(bcard, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bcard, 12, 0);
  lv_obj_set_style_border_color(bcard, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(bcard, 1, 0);
  new_label(bcard, &font_cjk_20, COL_MID, 0, 5, "BEST");
  lv_obj_align(lv_obj_get_child(bcard, 0), LV_ALIGN_TOP_MID, 0, 5);
  tt_best_val = new_label(bcard, &font_cjk_20, TG_Best(), 0, 0, "0");
  lv_obj_align(tt_best_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  /* 井框 + 井格 180 */
  lv_obj_t* frame = lv_obj_create(scr_tetris);
  lv_obj_remove_style_all(frame);
  lv_obj_set_size(frame, TT_W * TT_PX + 8, TT_H * TT_PX + 8);
  lv_obj_set_pos(frame, TT_X - 4, TT_Y - 4);
  lv_obj_set_style_bg_color(frame, lv_color_hex(0x050507), 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(frame, 18, 0);
  lv_obj_set_style_border_color(frame, lv_color_hex(0x14141A), 0);
  lv_obj_set_style_border_width(frame, 1, 0);

  for (int y = 0; y < TT_H; y++)
    for (int x = 0; x < TT_W; x++) {
      lv_obj_t* c = lv_obj_create(scr_tetris);
      lv_obj_remove_style_all(c);
      lv_obj_set_size(c, TT_PX - 2, TT_PX - 2);
      lv_obj_set_pos(c, TT_X + x * TT_PX, TT_Y + y * TT_PX);
      lv_obj_set_style_bg_color(c, lv_color_hex(0x0A0A0E), 0);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(c, 4, 0);
      tt_cells[y][x] = c;
      tt_shown[y][x] = 0;
    }

  /* NEXT 预览 */
  new_label(scr_tetris, &font_cjk_20, COL_MID, TT_INFO_X, 84, "NEXT");
  lv_obj_t* nframe = lv_obj_create(scr_tetris);
  lv_obj_remove_style_all(nframe);
  lv_obj_set_size(nframe, 4 * TT_PX + 8, 4 * TT_PX + 8);
  lv_obj_set_pos(nframe, TT_INFO_X - 4, 84 + 24 - 4);
  lv_obj_set_style_bg_color(nframe, lv_color_hex(0x050507), 0);
  lv_obj_set_style_bg_opa(nframe, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(nframe, 12, 0);
  lv_obj_set_style_border_color(nframe, lv_color_hex(0x14141A), 0);
  lv_obj_set_style_border_width(nframe, 1, 0);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) {
      lv_obj_t* c = lv_obj_create(scr_tetris);
      lv_obj_remove_style_all(c);
      lv_obj_set_size(c, TT_PX - 2, TT_PX - 2);
      lv_obj_set_pos(c, TT_INFO_X + x * TT_PX, 84 + 24 + y * TT_PX);
      lv_obj_set_style_bg_color(c, lv_color_hex(0x0A0A0E), 0);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(c, 4, 0);
      tt_nextCells[y][x] = c;
    }

  /* 右侧统计: LINES / LEVEL */
  new_label(scr_tetris, &font_cjk_20, COL_MID, TT_INFO_X, 220, "行数");
  tt_lines_val = new_label(scr_tetris, &font_cjk_20, COL_HI, TT_INFO_X, 246, "0");
  new_label(scr_tetris, &font_cjk_20, COL_MID, TT_INFO_X, 292, "等级");
  tt_level_val = new_label(scr_tetris, &font_cjk_20, COL_HI, TT_INFO_X, 318, "0");

  /* 底部提示 */
  new_label(scr_tetris, &font_cjk_20, COL_LOW, 0, 0, "左右滑移动 · 上滑旋转 · 下滑直落");
  lv_obj_align(lv_obj_get_child(scr_tetris, lv_obj_get_child_cnt(scr_tetris) - 1),
               LV_ALIGN_BOTTOM_MID, 0, -8);

  /* 覆盖层 (井区域) */
  tt_overlay = lv_obj_create(scr_tetris);
  lv_obj_remove_style_all(tt_overlay);
  lv_obj_set_size(tt_overlay, TT_W * TT_PX + 8, TT_H * TT_PX + 8);
  lv_obj_set_pos(tt_overlay, TT_X - 4, TT_Y - 4);
  lv_obj_set_style_bg_color(tt_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(tt_overlay, LV_OPA_80, 0);
  lv_obj_set_style_radius(tt_overlay, 18, 0);
  lv_obj_add_flag(tt_overlay, LV_OBJ_FLAG_HIDDEN);

  tt_overlay_text = new_label(tt_overlay, &font_cjk_20, COL_HI, 0, 0, "");
  lv_obj_align(tt_overlay_text, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t* restart = lv_obj_create(tt_overlay);
  lv_obj_remove_style_all(restart);
  lv_obj_set_size(restart, 120, 44);
  lv_obj_set_style_bg_color(restart, g_theme ? MONO_G1 : COL_PURPLE, 0);
  lv_obj_set_style_bg_opa(restart, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(restart, 12, 0);
  lv_obj_align(restart, LV_ALIGN_CENTER, 0, 40);
  lv_obj_t* rt = lv_label_create(restart);
  lv_obj_set_style_text_font(rt, &font_cjk_20, 0);
  lv_obj_set_style_text_color(rt, COL_BG, 0);
  lv_obj_center(rt);
  lv_label_set_text(rt, "重新开始");
}

/* ================= 点阵鸟 =================
 * 重力跳跃 + 滚动管道; 40Hz tick 与打砖块一致
 * 渲染: 鸟/管全 set_pos 增量 (对象总数极少 ~15)
 * 物理 30 局仿真定稿: 缺口130..300, AI均分2/最高14 (Flappy 正常曲线) */
#define FB_BIRD_X   100
#define FB_FLOOR_Y  400
#define FB_TOP_Y    84
#define FB_PIPE_W   52
#define FB_GAP      76
#define FB_TICK     25          /* ms, 40Hz */
#define FB_GRAV     0.28f
#define FB_JUMP     (-4.6f)
#define FB_TERMINAL 7.0f
#define FB_SPEED    2.2f
#define FB_SPACING  150         /* 管水平间隔 */
#define FB_PIPES    3           /* 循环管组数 */

static float fb_by, fb_bvy;
static float fb_pipeX[FB_PIPES];          /* 管组左缘 */
static int   fb_gapY[FB_PIPES];           /* 缺口中心 */
static int   fb_score, fb_best;
static bool fb_alive, fb_serving;         /* serving: 待开始(轻点起飞) */
static uint32_t fb_lastTick, fb_serveAt;
static Preferences fb_prefs;

static lv_obj_t* fb_bird;
static lv_obj_t* fb_pipeTop[FB_PIPES];    /* 上管 (y=84 到 缺口上) */
static lv_obj_t* fb_pipeBot[FB_PIPES];    /* 下管 (缺口下 到 地面) */
static lv_obj_t* fb_score_val, *fb_best_val;
static lv_obj_t* fb_scoreBig;             /* 中央大分数 (待开始/进行中) */
static lv_obj_t* fb_overlay, *fb_overlay_text;

static void fbNewGame() {
  fb_by = 240.0f; fb_bvy = 0;
  for (int i = 0; i < FB_PIPES; i++) {
    fb_pipeX[i] = 368 + 60 + i * FB_SPACING;   /* 428, 578, 728 */
    fb_gapY[i] = random(130, 300);
  }
  fb_score = 0;
  fb_alive = true; fb_serving = true;
  fb_serveAt = millis();
  char sb[8];
  itoa(fb_best, sb, 10); lv_label_set_text(fb_best_val, sb);
  itoa(fb_score, sb, 10); lv_label_set_text(fb_score_val, sb);
  lv_label_set_text(fb_scoreBig, "0");
  lv_obj_add_flag(fb_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(fb_scoreBig, LV_OBJ_FLAG_HIDDEN);
  /* 管道初始摆位 (含颜色刷新: 主题切换后正确换色) */
  for (int i = 0; i < FB_PIPES; i++) {
    int gy = fb_gapY[i];
    int topH = gy - FB_GAP / 2 - FB_TOP_Y;
    lv_obj_set_pos(fb_pipeTop[i], (int)fb_pipeX[i], FB_TOP_Y);
    lv_obj_set_size(fb_pipeTop[i], FB_PIPE_W, topH);
    lv_obj_set_pos(fb_pipeBot[i], (int)fb_pipeX[i], gy + FB_GAP / 2);
    lv_obj_set_size(fb_pipeBot[i], FB_PIPE_W, FB_FLOOR_Y - (gy + FB_GAP / 2));
    lv_obj_set_style_bg_color(fb_pipeTop[i], TG_Pipe(), 0);
    lv_obj_set_style_bg_color(fb_pipeBot[i], TG_Pipe(), 0);
    lv_obj_set_style_border_color(fb_pipeTop[i], TG_PipeBorder(), 0);
    lv_obj_set_style_border_color(fb_pipeBot[i], TG_PipeBorder(), 0);
    lv_obj_clear_flag(fb_pipeTop[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(fb_pipeBot[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_pos(fb_bird, FB_BIRD_X - 10, (int)fb_by - 10);
  lv_obj_set_style_bg_color(fb_bird, TG_Bird(), 0);   /* 主题色刷新 */
}

static void fbDie() {
  fb_alive = false;
  if (fb_score > fb_best) {
    fb_best = fb_score;
    fb_prefs.putInt("best", fb_best);
    char sb[8]; itoa(fb_best, sb, 10); lv_label_set_text(fb_best_val, sb);
  }
  lv_label_set_text(fb_overlay_text, "游戏结束");
  lv_obj_clear_flag(fb_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(fb_scoreBig, LV_OBJ_FLAG_HIDDEN);
  sfxGameOver();
}

static void fbTick() {
  if (!fb_alive) return;

  if (fb_serving) {
    /* 待开始: 鸟悬浮呼吸 (小正弦) */
    float t = (millis() - fb_serveAt) / 1000.0f;
    float hover = 240.0f + sinf(t * 3.0f) * 6.0f;
    fb_by = hover;
    lv_obj_set_pos(fb_bird, FB_BIRD_X - 10, (int)fb_by - 10);
    return;   /* 管不动 */
  }

  /* 物理 */
  fb_bvy = fb_bvy + FB_GRAV;
  if (fb_bvy > FB_TERMINAL) fb_bvy = FB_TERMINAL;
  fb_by += fb_bvy;
  if (fb_by < FB_TOP_Y + 10) { fb_by = FB_TOP_Y + 10; fb_bvy = 0; }   /* 顶不死 */

  /* 管滚动 */
  for (int i = 0; i < FB_PIPES; i++) {
    fb_pipeX[i] -= FB_SPEED;
    if (fb_pipeX[i] + FB_PIPE_W < -4) {        /* 出左界 → 回收到右界重生 */
      fb_pipeX[i] += FB_PIPES * FB_SPACING;
      fb_gapY[i] = random(130, 300);
      int gy = fb_gapY[i];
      int topH = gy - FB_GAP / 2 - FB_TOP_Y;
      lv_obj_set_pos(fb_pipeTop[i], (int)fb_pipeX[i], FB_TOP_Y);
      lv_obj_set_size(fb_pipeTop[i], FB_PIPE_W, topH);
      lv_obj_set_pos(fb_pipeBot[i], (int)fb_pipeX[i], gy + FB_GAP / 2);
      lv_obj_set_size(fb_pipeBot[i], FB_PIPE_W, FB_FLOOR_Y - (gy + FB_GAP / 2));
    } else {
      lv_obj_set_pos(fb_pipeTop[i], (int)fb_pipeX[i], FB_TOP_Y);
      lv_obj_set_pos(fb_pipeBot[i], (int)fb_pipeX[i], FB_TOP_Y);   /* 占位, 下面统一设 */
    }
  }
  /* 下管 y 每帧统一 (上管只动x; 重生时才变尺寸) */
  for (int i = 0; i < FB_PIPES; i++)
    lv_obj_set_pos(fb_pipeBot[i], (int)fb_pipeX[i], fb_gapY[i] + FB_GAP / 2);

  /* 计分: 管中心过鸟 */
  static int fb_passed[FB_PIPES];
  for (int i = 0; i < FB_PIPES; i++) {
    if (!fb_passed[i] && fb_pipeX[i] + FB_PIPE_W / 2 < FB_BIRD_X) {
      fb_passed[i] = 1;
      fb_score++;
      char sb[8];
      itoa(fb_score, sb, 10);
      lv_label_set_text(fb_score_val, sb);
      lv_label_set_text(fb_scoreBig, sb);
      sfxMerge();
    }
    if (fb_pipeX[i] > 368) fb_passed[i] = 0;   /* 重置回收标志 */
  }

  /* 碰撞: 地面 */
  if (fb_by + 10 >= FB_FLOOR_Y) { fb_by = FB_FLOOR_Y - 10; fbDie(); return; }
  /* 碰撞: 管 (鸟20px: x 90..110, y by±10; 缺口 gy±38) */
  for (int i = 0; i < FB_PIPES; i++) {
    if (FB_BIRD_X + 10 > fb_pipeX[i] && FB_BIRD_X - 10 < fb_pipeX[i] + FB_PIPE_W) {
      if (fb_by - 10 < fb_gapY[i] - FB_GAP / 2 || fb_by + 10 > fb_gapY[i] + FB_GAP / 2) {
        fbDie();
        return;
      }
    }
  }

  /* 鸟渲染 */
  lv_obj_set_pos(fb_bird, FB_BIRD_X - 10, (int)fb_by - 10);
}

static void build_flappy() {
  scr_flappy = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_flappy, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_flappy, LV_OPA_COVER, LV_PART_MAIN);

  /* 顶栏: 返回 */
  lv_obj_t* back = lv_obj_create(scr_flappy);
  lv_obj_remove_style_all(back);
  lv_obj_set_size(back, 40, 40);
  lv_obj_set_pos(back, 20, 20);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(back, 12, 0);
  lv_obj_set_style_border_color(back, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(back, 1, 0);
  lv_obj_t* back_t = lv_label_create(back);
  lv_obj_set_style_text_font(back_t, &font_ui_20, 0);
  lv_obj_set_style_text_color(back_t, COL_MID, 0);
  lv_obj_center(back_t);
  lv_label_set_text(back_t, "<");

  /* 分数卡 (SCORE + BEST) */
  lv_obj_t* sc = lv_obj_create(scr_flappy);
  lv_obj_remove_style_all(sc);
  lv_obj_set_size(sc, 90, 54);
  lv_obj_set_pos(sc, 74, 16);
  lv_obj_set_style_bg_color(sc, lv_color_hex(0x0A0A0E), 0);
  lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(sc, 12, 0);
  lv_obj_set_style_border_color(sc, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(sc, 1, 0);
  new_label(sc, &font_cjk_20, COL_MID, 0, 5, "SCORE");
  lv_obj_align(lv_obj_get_child(sc, 0), LV_ALIGN_TOP_MID, 0, 5);
  fb_score_val = new_label(sc, &font_cjk_20, TG_Score(), 0, 0, "0");
  lv_obj_align(fb_score_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  lv_obj_t* bcard = lv_obj_create(scr_flappy);
  lv_obj_remove_style_all(bcard);
  lv_obj_set_size(bcard, 90, 54);
  lv_obj_set_pos(bcard, 174, 16);
  lv_obj_set_style_bg_color(bcard, lv_color_hex(0x0A0E), 0);
  lv_obj_set_style_bg_opa(bcard, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bcard, 12, 0);
  lv_obj_set_style_border_color(bcard, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_border_width(bcard, 1, 0);
  new_label(bcard, &font_cjk_20, COL_MID, 0, 5, "BEST");
  lv_obj_align(lv_obj_get_child(bcard, 0), LV_ALIGN_TOP_MID, 0, 5);
  fb_best_val = new_label(bcard, &font_cjk_20, TG_Best(), 0, 0, "0");
  lv_obj_align(fb_best_val, LV_ALIGN_BOTTOM_MID, 0, -5);

  /* 中央大分数 (游戏中) */
  fb_scoreBig = lv_label_create(scr_flappy);
  lv_obj_set_style_text_font(fb_scoreBig, &font_game_40, 0);
  lv_obj_set_style_text_color(fb_scoreBig, COL_HI, 0);
  lv_obj_align(fb_scoreBig, LV_ALIGN_TOP_MID, 0, 100);
  lv_label_set_text(fb_scoreBig, "0");

  /* 管道 (3组, 上下管各1对象; 颜色深青绿) */
  for (int i = 0; i < FB_PIPES; i++) {
    fb_pipeTop[i] = lv_obj_create(scr_flappy);
    lv_obj_remove_style_all(fb_pipeTop[i]);
    lv_obj_set_size(fb_pipeTop[i], FB_PIPE_W, 100);
    lv_obj_set_pos(fb_pipeTop[i], 368 + i * FB_SPACING, FB_TOP_Y);
    lv_obj_set_style_bg_color(fb_pipeTop[i], TG_Pipe(), 0);
    lv_obj_set_style_bg_opa(fb_pipeTop[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fb_pipeTop[i], 6, 0);
    lv_obj_set_style_border_color(fb_pipeTop[i], TG_PipeBorder(), 0);
    lv_obj_set_style_border_width(fb_pipeTop[i], 1, 0);
    fb_pipeBot[i] = lv_obj_create(scr_flappy);
    lv_obj_remove_style_all(fb_pipeBot[i]);
    lv_obj_set_size(fb_pipeBot[i], FB_PIPE_W, 100);
    lv_obj_set_pos(fb_pipeBot[i], 368 + i * FB_SPACING, 300);
    lv_obj_set_style_bg_color(fb_pipeBot[i], TG_Pipe(), 0);
    lv_obj_set_style_bg_opa(fb_pipeBot[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fb_pipeBot[i], 6, 0);
    lv_obj_set_style_border_color(fb_pipeBot[i], TG_PipeBorder(), 0);
    lv_obj_set_style_border_width(fb_pipeBot[i], 1, 0);
  }

  /* 地面装饰条 */
  lv_obj_t* floor = lv_obj_create(scr_flappy);
  lv_obj_remove_style_all(floor);
  lv_obj_set_size(floor, 368, 48);
  lv_obj_set_pos(floor, 0, FB_FLOOR_Y);
  lv_obj_set_style_bg_color(floor, lv_color_hex(0x050507), 0);
  lv_obj_set_style_bg_opa(floor, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(floor, lv_color_hex(0x14141A), 0);
  lv_obj_set_style_border_width(floor, 1, 0);

  /* 鸟 (20px, 暖白) */
  fb_bird = lv_obj_create(scr_flappy);
  lv_obj_remove_style_all(fb_bird);
  lv_obj_set_size(fb_bird, 20, 20);
  lv_obj_set_style_bg_color(fb_bird, TG_Bird(), 0);
  lv_obj_set_style_bg_opa(fb_bird, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(fb_bird, 6, 0);
  lv_obj_set_style_border_color(fb_bird, COL_CYAN, 0);
  lv_obj_set_style_border_width(fb_bird, 1, 0);

  /* 底部提示 (地面条上) */
  new_label(scr_flappy, &font_cjk_20, COL_LOW, 0, 0, "轻点起飞 · 穿过缺口");
  lv_obj_align(lv_obj_get_child(scr_flappy, lv_obj_get_child_cnt(scr_flappy) - 1),
               LV_ALIGN_BOTTOM_MID, 0, -12);

  /* 覆盖层 (全飞行区) */
  fb_overlay = lv_obj_create(scr_flappy);
  lv_obj_remove_style_all(fb_overlay);
  lv_obj_set_size(fb_overlay, 368, 316);
  lv_obj_set_pos(fb_overlay, 0, FB_TOP_Y);
  lv_obj_set_style_bg_color(fb_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(fb_overlay, LV_OPA_80, 0);
  lv_obj_add_flag(fb_overlay, LV_OBJ_FLAG_HIDDEN);

  fb_overlay_text = new_label(fb_overlay, &font_cjk_20, COL_HI, 0, 0, "");
  lv_obj_align(fb_overlay_text, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t* restart = lv_obj_create(fb_overlay);
  lv_obj_remove_style_all(restart);
  lv_obj_set_size(restart, 120, 44);
  lv_obj_set_style_bg_color(restart, g_theme ? MONO_G1 : COL_GOLD, 0);
  lv_obj_set_style_bg_opa(restart, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(restart, 12, 0);
  lv_obj_align(restart, LV_ALIGN_CENTER, 0, 40);
  lv_obj_t* rt = lv_label_create(restart);
  lv_obj_set_style_text_font(rt, &font_cjk_20, 0);
  lv_obj_set_style_text_color(rt, COL_BG, 0);
  lv_obj_center(rt);
  lv_label_set_text(rt, "重新开始");
}

/* ================= 触摸处理 ================= */
static lv_coord_t pressX = 0, pressY = 0;
static lv_coord_t lastX = 0, lastY = 0;
static bool pressed = false;
static bool tapHandled = false;

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  int32_t fingers = CST816->IIC_Read_Device_Value(
    CST816->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
  int32_t tx = CST816->IIC_Read_Device_Value(
    CST816->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t ty = CST816->IIC_Read_Device_Value(
    CST816->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

  if (fingers > 0) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = tx;
    data->point.y = ty;
    /* 打砖块: 按住期间实时拖动挡板 (跟手, 不等抬手) */
    if (currentScreen == SCR_BREAKOUT && bk_alive &&
        ty > 100) {   /* 避开顶栏误触 */
      int target = tx - BK_PD_W / 2;              /* 手指x=挡板中心 */
      if (target < BK_WX) target = BK_WX;
      if (target > BK_WX + BK_WW - BK_PD_W) target = BK_WX + BK_WW - BK_PD_W;
      if (target != bk_pdX) {
        bk_pdX = target;
        bk_paddleDirty = true;                    /* loop 渲染时 set_pos */
      }
    }
    if (!pressed) {
      pressX = tx; pressY = ty; pressed = true; tapHandled = false;
    }
    lastX = tx; lastY = ty;
    poke();
  } else {
    data->state = LV_INDEV_STATE_REL;
    if (pressed) {
      pressed = false;
      int dx = lastX - pressX, dy = lastY - pressY;

      if (currentScreen == SCR_MENU) {
        /* 菜单: 轻点卡片进入; 主题按钮(238,16,108x36)切换 */
        if (abs(dx) < 20 && abs(dy) < 20) {
          int px = lastX, py = lastY;
          /* 主题切换按钮 */
          if (px >= 238 && px <= 346 && py >= 16 && py <= 52) {
            themeToggle();
            return;
          }
          /* 卡片区域: 2列 x 3行, 起点(22,60), 卡片156x120, gap12 */
          int cardW = 156, cardH = 120, gap = 12;
          int startX = 22, startY = 60;
          for (int i = 0; i < 5; i++) {
            int col = i % 2, row = i / 2;
            int x = startX + col * (cardW + gap);
            int y = startY + row * (cardH + gap);
            if (i == 4) x = startX + (cardW + gap) / 2;   /* 第3行居中 */
            if (px >= x && px <= x + cardW && py >= y && py <= y + cardH) {
              if (i == 0) {   /* 2048 */
                currentScreen = SCR_GAME;
                lv_scr_load_anim(scr_game, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
                sfxClick();
                newGame();
              } else if (i == 1) {   /* 贪吃蛇 */
                currentScreen = SCR_SNAKE;
                lv_scr_load_anim(scr_snake, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
                sfxClick();
                snakeNewGame();
              } else if (i == 2) {   /* 打砖块 */
                currentScreen = SCR_BREAKOUT;
                lv_scr_load_anim(scr_breakout, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
                sfxClick();
                bkNewGame();
              } else if (i == 3) {   /* 俄罗斯方块 */
                currentScreen = SCR_TETRIS;
                lv_scr_load_anim(scr_tetris, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
                sfxClick();
                ttNewGame();
              } else if (i == 4) {   /* 点阵鸟 */
                currentScreen = SCR_FLAPPY;
                lv_scr_load_anim(scr_flappy, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
                sfxClick();
                fbNewGame();
              }
              break;
            }
          }
        }
      } else if (currentScreen == SCR_GAME) {
        /* 2048: 返回按钮(20,20,40x40) */
        if (abs(dx) < 20 && abs(dy) < 20) {
          int px = lastX, py = lastY;
          if (px >= 10 && px <= 70 && py >= 10 && py <= 70) {   /* ??2.25x: ??(20,20,40x40)??10px */
            currentScreen = SCR_MENU;
            lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
            sfxClick();
            return;
          }
          /* 覆盖层上的"重新开始"按钮 */
          if (!lv_obj_has_flag(g_overlay, LV_OBJ_FLAG_HIDDEN)) {
            int bx = 20 + (328 - 120) / 2, by = 84 + 328/2 + 40 - 22;
            if (px >= bx && px <= bx + 120 && py >= by && py <= by + 44) {
              sfxClick();
              newGame();
              return;
            }
          }
        }
        /* 滑动: 移动方块 */
        if (abs(dx) > 50 || abs(dy) > 50) {
          if (abs(dx) > abs(dy)) {
            handleSwipe(dx < 0 ? 2 : 3);   /* LEFT / RIGHT */
          } else {
            handleSwipe(dy < 0 ? 0 : 1);   /* UP / DOWN */
          }
        }
      } else if (currentScreen == SCR_SNAKE) {
        /* 贪吃蛇 */
        if (abs(dx) < 20 && abs(dy) < 20) {
          int px = lastX, py = lastY;
          /* 返回按钮 (20,20,40x40) */
          if (px >= 10 && px <= 70 && py >= 10 && py <= 70) {   /* ??2.25x: ??(20,20,40x40)??10px */
            currentScreen = SCR_MENU;
            lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
            sfxClick();
            sn_alive = false;      /* 停止 tick, 返回时不残留游戏状态 */
            return;
          }
          /* 覆盖层: 重新开始按钮 (框内居中, 与2048同布局逻辑) */
          if (!lv_obj_has_flag(sn_overlay, LV_OBJ_FLAG_HIDDEN)) {
            int bx = SN_BX + (SN_FRAME - 120) / 2;
            int by = SN_BY + SN_FRAME / 2 + 40 - 22;
            if (px >= bx && px <= bx + 120 && py >= by && py <= by + 44) {
              sfxClick();
              snakeNewGame();
              return;
            }
          }
        }
        /* 滑动: 转向 (阈值低一点, 蛇要灵敏; 2048是50) */
        if (abs(dx) > 30 || abs(dy) > 30) {
          uint8_t d;
          if (abs(dx) > abs(dy)) d = (dx < 0 ? 2 : 3);
          else                   d = (dy < 0 ? 0 : 1);
          if (sn_alive) { snakeDirPush(d); poke(); }
        }
      } else if (currentScreen == SCR_BREAKOUT) {
        /* 打砖块 */
        if (abs(dx) < 20 && abs(dy) < 20) {
          int px = lastX, py = lastY;
          /* 返回按钮 (20,20,40x40) */
          if (px >= 10 && px <= 70 && py >= 10 && py <= 70) {   /* ??2.25x: ??(20,20,40x40)??10px */
            currentScreen = SCR_MENU;
            lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
            sfxClick();
            bk_alive = false;      /* 停 tick */
            return;
          }
          /* 覆盖层: 重新开始 */
          if (!lv_obj_has_flag(bk_overlay, LV_OBJ_FLAG_HIDDEN)) {
            int bx = BK_WX + (312 - 120) / 2;
            int by = BK_WY + 320 / 2 + 40 - 22;
            if (px >= bx && px <= bx + 120 && py >= by && py <= by + 44) {
              sfxClick();
              bkNewGame();
              return;
            }
          }
          /* serving 态: 任意轻点发球 (避开返回键区) */
          if (bk_alive && bk_serving &&
              (millis() - bk_serveAt > 300) &&      /* 防进屏误触立刻发球 */
              !(px >= 20 && px <= 60 && py >= 20 && py <= 60)) {
            bkServe();
            sfxClick();
          }
        }
      } else if (currentScreen == SCR_TETRIS) {
        /* 俄罗斯方块 */
        if (abs(dx) < 20 && abs(dy) < 20) {
          int px = lastX, py = lastY;
          /* 返回 */
          if (px >= 10 && px <= 70 && py >= 10 && py <= 70) {   /* 热区2.25x */
            currentScreen = SCR_MENU;
            lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
            sfxClick();
            tt_alive = false;
            return;
          }
          /* 重新开始 */
          if (!lv_obj_has_flag(tt_overlay, LV_OBJ_FLAG_HIDDEN)) {
            int bx = TT_X + (TT_W * TT_PX - 120) / 2;
            int by = TT_Y + TT_H * TT_PX / 2 + 40 - 22;
            if (px >= bx && px <= bx + 120 && py >= by && py <= by + 44) {
              sfxClick();
              ttNewGame();
              return;
            }
          }
        }
        /* 手势: 左右=移动, 上=旋转, 下=硬降直落到底 (阈值20px, 俄方块要灵敏) */
        if (abs(dx) > 20 || abs(dy) > 20) {
          if (abs(dx) > abs(dy)) {
            if (tt_alive) { if (ttMove(dx < 0 ? -1 : 1)) sfxClick(); }
          } else {
            if (tt_alive) {
              if (dy < 0) { if (ttRotate()) sfxMerge(); }
              else ttHardDrop();
            }
          }
          poke();
        }
      } else if (currentScreen == SCR_FLAPPY) {
        /* 点阵鸟: 轻点即跳 (Flappy 核心交互, 无手势判定) */
        if (abs(dx) < 20 && abs(dy) < 20) {
          int px = lastX, py = lastY;
          /* 返回 */
          if (px >= 10 && px <= 70 && py >= 10 && py <= 70) {   /* 热区2.25x */
            currentScreen = SCR_MENU;
            lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
            sfxClick();
            fb_alive = false;
            return;
          }
          /* 重新开始 */
          if (!lv_obj_has_flag(fb_overlay, LV_OBJ_FLAG_HIDDEN)) {
            int bx = (368 - 120) / 2;
            int by = FB_TOP_Y + 316 / 2 + 40 - 22;
            if (px >= bx && px <= bx + 120 && py >= by && py <= by + 44) {
              sfxClick();
              fbNewGame();
              return;
            }
          }
          /* 起飞/跳 (避开返回键区; 进屏400ms防误触) */
          if (fb_alive && (millis() - fb_serveAt > 400) &&
              !(px >= 10 && px <= 70 && py >= 10 && py <= 70)) {
            if (fb_serving) { fb_serving = false; sfxClick(); }
            fb_bvy = FB_JUMP;
            sfxMove();
            poke();
          }
        }
      }
    }
  }
}

/* ================= 初始化 ================= */
void setup() {
  USBSerial.begin(115200);
  USBSerial.setTxTimeoutMs(0);
  USBSerial.println("\nDOT games boot (menu + 2048)");

  pinMode(0, INPUT_PULLUP);
  pinMode(TP_INT, INPUT_PULLUP);

  Wire.begin(IIC_SDA, IIC_SCL);
  if (!expander.begin(0x20)) {
    USBSerial.println("XCA9554 not found (continuing)");
  } else {
    expander.pinMode(0, OUTPUT);
    expander.pinMode(1, OUTPUT);
    expander.pinMode(2, OUTPUT);
    expander.pinMode(6, OUTPUT);
    expander.digitalWrite(0, LOW);
    expander.digitalWrite(1, LOW);
    expander.digitalWrite(2, LOW);
    expander.digitalWrite(6, LOW);
    delay(20);
    expander.digitalWrite(0, HIGH);
    expander.digitalWrite(1, HIGH);
    expander.digitalWrite(2, HIGH);
    expander.digitalWrite(6, HIGH);
    delay(100);
    expander.digitalWrite(0, LOW);
    expander.digitalWrite(1, LOW);
    expander.digitalWrite(2, LOW);
    delay(300);
    expander.digitalWrite(0, HIGH);
    expander.digitalWrite(1, HIGH);
    expander.digitalWrite(2, HIGH);
    expander.digitalWrite(6, HIGH);
    delay(200);
  }

  bool touch_ok = false;
  for (int i = 0; i < 3 && !touch_ok; i++) {
    touch_ok = CST816->begin();
    if (!touch_ok) { USBSerial.printf("CST816 retry %d...\n", i + 1); delay(200); }
  }
  if (!touch_ok) {
    USBSerial.println("CST816 init fail (touch-wake disabled)");
  } else {
    CST816->IIC_Write_Device_State(
      CST816->Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
      CST816->Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
    USBSerial.println("CST816 ok");
  }

  pmu_ok = pmu.begin(Wire, 0x34, IIC_SDA, IIC_SCL);
  USBSerial.println(pmu_ok ? "AXP2101 ok" : "AXP2101 not found");

  gfx->begin();   /* 40MHz 默认 (20MHz 试验无收益, 条纹与 QSPI 速度无关) */
  gfx->setBrightness(BRIGHT_ON);

  audio_init();

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_WIDTH * LCD_HEIGHT / 10);
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = gfx->width();
  disp_drv.ver_res = gfx->height();
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.rounder_cb = my_disp_rounder;   /* 失效区→全宽条带, 根治小窗口突发花屏 */
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  /* 读取最高分 + 主题 */
  theme_prefs.begin("dotgames", false);
  g_theme = theme_prefs.getInt("theme", THEME_COLOR);
  prefs.begin("dot2048", false);
  best = prefs.getInt("best", 0);
  sn_prefs.begin("dotsnake", false);
  sn_best = sn_prefs.getInt("best", 0);
  bk_prefs.begin("dotbrk", false);
  bk_best = bk_prefs.getInt("best", 0);
  tt_prefs.begin("dottet", false);
  tt_best = tt_prefs.getInt("best", 0);
  fb_prefs.begin("dotfb", false);
  fb_best = fb_prefs.getInt("best", 0);

  build_menu();
  build_game();
  build_snake();
  build_breakout();
  build_tetris();
  build_flappy();
  lv_scr_load(scr_menu);

  const esp_timer_create_args_t tick_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t tick_timer = NULL;
  esp_timer_create(&tick_args, &tick_timer);
  esp_timer_start_periodic(tick_timer, 2000);

  USBSerial.println("Setup done");
}

void loop() {
  static int lastBoot = HIGH;
  int b = digitalRead(0);
  if (b == LOW && lastBoot == HIGH) {
    delay(30);
    if (digitalRead(0) == LOW) {
      if (pwr == PWR_OFF) poke();
      else sleepNow();
    }
  }
  lastBoot = b;

  uint32_t idle = millis() - lastActivity;
  if (pwr == PWR_ACTIVE && idle > DIM_AFTER_MS) dimNow();
  else if (pwr == PWR_DIM && idle > OFF_AFTER_MS) sleepNow();

  if (pwr == PWR_OFF) {
    static uint32_t lastTp = 0;
    if (millis() - lastTp >= 120) {
      lastTp = millis();
      int32_t fingers = CST816->IIC_Read_Device_Value(
        CST816->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
      if (fingers > 0) poke();
    }
    sfxQueue = SFX_NONE;   /* 熄屏时清队列, 不发声 */
    delay(30);
    return;
  }

  lv_timer_handler();
  sfxPump();              /* 渲染完成后消费音效队列 (异步音频) */

  /* 贪吃蛇: 非阻塞 tick (仅在蛇屏活跃时) */
  if (currentScreen == SCR_SNAKE && sn_alive && !sn_paused) {
    uint32_t now = millis();
    if (now - sn_lastTick >= sn_tickMs) {
      sn_lastTick = now;
      snakeTick();
    }
  }

  /* 打砖块: 30Hz tick + 增量渲染 (set_pos 只在坐标变化时调用) */
  if (currentScreen == SCR_BREAKOUT && bk_alive) {
    uint32_t now = millis();
    if (now - bk_lastTick >= BK_TICK) {
      bk_lastTick = now;
      bkTick();

      /* 挡板渲染: 只在移动时 set_pos (LVGL 失效旧+新区域, 天然增量) */
      if (bk_paddleDirty) {
        lv_obj_set_pos(bk_paddle, bk_pdX, BK_PD_Y);
        bk_paddleDirty = false;
      }
      /* 球渲染: 坐标取整, 只在变化时 set_pos (覆盖层隐藏=游戏中才画) */
      if (!bk_serving && lv_obj_has_flag(bk_overlay, LV_OBJ_FLAG_HIDDEN)) {
        int nbx = (int)(bk_bx - BK_R);
        int nby = (int)(bk_by - BK_R);
        if (!bk_ballShown) {
          lv_obj_clear_flag(bk_ball, LV_OBJ_FLAG_HIDDEN);
          bk_ballShown = true;
        }
        if (nbx != bk_ballOldX || nby != bk_ballOldY) {
          lv_obj_set_pos(bk_ball, nbx, nby);
          bk_ballOldX = nbx; bk_ballOldY = nby;
        }
      }
    }
  }

  /* 俄罗斯方块: 重力 tick */
  if (currentScreen == SCR_TETRIS) ttFallStep();

  /* 点阵鸟: 40Hz 物理tick */
  if (currentScreen == SCR_FLAPPY) {
    if (millis() - fb_lastTick >= FB_TICK) {
      fb_lastTick = millis();
      fbTick();
    }
  }

  delay(5);
}
