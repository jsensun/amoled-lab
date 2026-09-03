/*
 * ============================================================
 *  MONO · EDITORIAL 表盘 —— amoled-lab 阶段 2 · 三页卡片
 *  硬件: 微雪 ESP32-S3-Touch-AMOLED-1.8 V2 (CO5300 + CST820)
 *
 *  v1.2 变更: 三页架构(主页MONO / 天气DOT / 日历LIT) + 左右滑动导航
 *   - 主页: 原 MONO 编辑排版表盘
 *   - 天气页 DOT: 点阵仪表盘(大温度+湿度/风速/气压+24h趋势), 电光青强调色
 *   - 日历页 LIT: 纸面杂志风(宋体诗文+农历节气+宜忌), 朱砂印章
 *   - 触摸左右滑动切页, 底部三点指示器, 子页60s无操作自动回主页
 *   - 天气扩展: 湿度/风速/气压/体感 + 24h逐时温度
 *   - 农历转换(1900-2100表) + 节气近似计算
 *   注意: 本文件必须保持 UTF-8 编码; 禁止用 PS Get-Content 转存
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include <SensorQMI8658.hpp>
#include <Adafruit_XCA9554.h>
#include "XPowersLib.h"
#include "HWCDC.h"

#include "pin_config.h"
#if !__has_include("secrets.h")
#warning "secrets.h not found -> copy secrets.h.example to secrets.h and fill in your WiFi. Building with OFFLINE placeholders."
#define WIFI_SSID     "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"
#else
#include "secrets.h"
#endif

LV_FONT_DECLARE(font_time_118);
LV_FONT_DECLARE(font_temp_46);
LV_FONT_DECLARE(font_temp_30);
LV_FONT_DECLARE(font_ui_20);
LV_FONT_DECLARE(font_cjk_20);
LV_FONT_DECLARE(font_serif_20);
LV_FONT_DECLARE(font_serif_28);

/* ---------------- 设计令牌 ---------------- */
#define COL_BG     lv_color_hex(0x000000)
#define COL_HI     lv_color_hex(0xF5F5F0)
#define COL_MM     lv_color_hex(0xC4C4BE)
#define COL_MID    lv_color_hex(0x9A9A94)
#define COL_LOW    lv_color_hex(0x3A3A38)
#define COL_ACCENT lv_color_hex(0xE8503A)
/* LIT 页红字: 更沉稳的暗朱红, 避免过亮 */
#define COL_LITRED lv_color_hex(0xB03A2E)
/* DOT 页 */
#define COL_DOT    lv_color_hex(0x00E5C8)
#define COL_DOTDIM lv_color_hex(0x0D3D38)
#define COL_DOTMID lv_color_hex(0x2A8F84)
/* LIT 页 */
#define COL_PAPER  lv_color_hex(0xF6F1E6)
#define COL_INK    lv_color_hex(0x201D18)
#define COL_INK2   lv_color_hex(0x6B6560)
#define COL_RED    lv_color_hex(0xB03A2E)

#define TIMEZONE_OFFSET (8 * 3600)
#define CITY_LAT  29.87f
#define CITY_LON  121.55f

HWCDC USBSerial;

/* ---------------- 屏幕 ---------------- */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT, 16, 0, 0, 0);
Adafruit_XCA9554 expander;

/* ---------------- 触摸 + IMU ---------------- */
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> CST816(new Arduino_CST816x(
    IIC_Bus, CST816T_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));
SensorQMI8658 qmi;
IMUdata acc;
static bool qmi_ok = false;
void Arduino_IIC_Touch_Interrupt(void) { /* INT脉冲噪声, 弃用中断法, 改用电平检测 */ }

/* ---------------- 电源状态机 ---------------- */
enum PwrState { PWR_ACTIVE, PWR_DIM, PWR_OFF };
static PwrState pwr = PWR_ACTIVE;
static uint32_t lastActivity = 0;
#define DIM_AFTER_MS  15000UL
#define OFF_AFTER_MS  30000UL
#define BRIGHT_ON     220
#define BRIGHT_DIM    60

static void updateDateTime(bool force);
static void updateTrend(int curHour);
static void wakeUp();
static void poke() {
  lastActivity = millis();
  if (pwr != PWR_ACTIVE) wakeUp();
}
static void dimNow() {
  pwr = PWR_DIM;
  gfx->setBrightness(BRIGHT_DIM);
  USBSerial.println("[pwr] dim");
}
static void sleepNow() {
  pwr = PWR_OFF;
  gfx->setBrightness(0);
  setCpuFrequencyMhz(80);
  lv_timer_enable(false);
  USBSerial.println("[pwr] screen off (touch / BOOT to wake)");
}
static void wakeUp() {
  if (pwr == PWR_OFF) {
    setCpuFrequencyMhz(240);
    lv_timer_enable(true);
    lv_obj_invalidate(lv_scr_act());
    updateDateTime(true);
  }
  pwr = PWR_ACTIVE;
  lastActivity = millis();
  gfx->setBrightness(BRIGHT_ON);
  CST816->IIC_Interrupt_Flag = false;
  USBSerial.println("[pwr] wake");
}

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];

/* ---------------- 电源 ---------------- */
static XPowersAXP2101 pmu;
static bool pmu_ok = false;

/* ---------------- 天气 (扩展) ---------------- */
struct Weather {
  bool valid;
  float temp; int code;
  int humidity; float wind; float pressure;
  float hourly[24]; int hourlyCount;
};
static Weather wx = { false, 0, -1, 0, 0, 0, {0}, 0 };

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

void example_increase_lvgl_tick(void *arg) {
  lv_tick_inc(2);
}

/* ================= 页面管理 ================= */
enum Page { PAGE_HOME, PAGE_DOT, PAGE_LIT };
static int currentPage = PAGE_HOME;
static lv_obj_t *scr_home, *scr_dot, *scr_lit;
static lv_obj_t *dot_home[3], *dot_dot[3], *dot_lit[3];

static void updateDots() {
  for (int i = 0; i < 3; i++) {
    bool act = (i == currentPage);
    lv_obj_set_style_bg_color(dot_home[i], act ? COL_HI : COL_LOW, 0);
    lv_obj_set_style_bg_color(dot_dot[i],  act ? COL_DOT : COL_DOTDIM, 0);
    lv_obj_set_style_bg_color(dot_lit[i],  act ? COL_HI : COL_LOW, 0);
  }
}

static void switchPage(int target) {
  if (target < 0) target = 0;
  if (target > 2) target = 2;
  if (target == currentPage) return;
  bool goLeft = (target > currentPage);
  currentPage = target;
  lv_obj_t* scr = (target == PAGE_HOME) ? scr_home : (target == PAGE_DOT) ? scr_dot : scr_lit;
  lv_scr_load_anim(scr, goLeft ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
  updateDots();
  poke();
}

/* ================= 触摸输入 (轮询读取 + 滑动检测) ================= */
static int trendMode = 0;             /* 0=圆角渐变柱, 1=平滑曲线 (DOT 页趋势) */
static lv_coord_t pressX = 0, pressY = 0;
static lv_coord_t lastX = 0, lastY = 0;
static bool pressed = false;
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
    if (!pressed) { pressX = tx; pressY = ty; pressed = true; }
    lastX = tx; lastY = ty;   /* 记录按压期间最新坐标 */
    poke();
  } else {
    data->state = LV_INDEV_STATE_REL;
    if (pressed) {
      pressed = false;
      /* 用按压期间最后坐标计算位移(松手时坐标可能被清零/残留) */
      int dx = lastX - pressX, dy = lastY - pressY;
      if (abs(dx) > 50 && abs(dx) > abs(dy)) {
        if (dx < 0) switchPage(currentPage + 1);
        else        switchPage(currentPage - 1);
      } else if (abs(dx) < 20 && abs(dy) < 20) {
        /* 轻点: 在 DOT 页趋势区切换 柱状图/曲线 */
        if (currentPage == PAGE_DOT && lastY > 300 && lastY < 432) {
          trendMode = 1 - trendMode;
          updateTrend(0);
          poke();
        }
      }
    }
  }
}

/* ================= 文本工具 ================= */
static const char* CN_D[] = {"〇","一","二","三","四","五","六","七","八","九"};

static String cnYear(int y) {
  String digits(y);
  String s;
  for (unsigned i = 0; i < digits.length(); i++) s += CN_D[digits[i] - '0'];
  return s;
}
static String cnMonth(int m) {
  if (m == 10) return "十";
  if (m == 11) return "十一";
  if (m == 12) return "十二";
  return CN_D[m];
}
static String cnDay(int d) {
  if (d <= 9)  return CN_D[d];
  if (d == 10) return "十";
  if (d <= 19) return String("十") + CN_D[d - 10];
  if (d == 20) return "二十";
  if (d <= 29) return String("廿") + CN_D[d - 20];
  if (d == 30) return "三十";
  return "卅一";
}
static String verticalize(const String& in) {
  String out;
  int i = 0, n = in.length();
  while (i < n) {
    uint8_t b = in[i];
    int cl = ((b & 0xE0) == 0xE0) ? 3 : (((b & 0xC0) == 0xC0) ? 2 : 1);
    out += in.substring(i, i + cl);
    i += cl;
    if (i < n) out += "\n";
  }
  return out;
}
static const char* daySuffix(int d) {
  if (d % 10 == 1 && d != 11) return "ST";
  if (d % 10 == 2 && d != 12) return "ND";
  if (d % 10 == 3 && d != 13) return "RD";
  return "TH";
}
static void set_kicker(lv_obj_t* lbl, int wday) {
  static const char* WD[] = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY",
                             "THURSDAY","FRIDAY","SATURDAY"};
  String s;
  for (const char* p = WD[wday]; *p; ++p) {
    s += *p;
    if (*(p + 1)) s += ' ';
  }
  lv_label_set_text(lbl, s.c_str());
}

/* ================= 农历转换 (1900-2100) ================= */
static const uint32_t lunarInfo[] = {
0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2,
0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977,
0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970,
0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950,
0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557,
0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0,
0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0,
0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b5a0,0x195a6,
0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570,
0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x055c0,0x0ab60,0x096d5,0x092e0,
0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5,
0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930,
0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530,
0x05aa0,0x076a3,0x096d0,0x04afb,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45,
0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0,
0x14b63,0x09370,0x049f8,0x04970,0x064b0,0x168a6,0x0ea50,0x06b20,0x1a6c4,0x0aae0,
0x092e0,0x0d2e3,0x0c960,0x0d557,0x0d4a0,0x0da50,0x05d55,0x056a0,0x0a6d0,0x055d4,
0x052d0,0x0a9b8,0x0a950,0x0b4a0,0x0b6a6,0x0ad50,0x055a0,0x0aba4,0x0a5b0,0x052b0,
0x0b273,0x06930,0x07337,0x06aa0,0x0ad50,0x14b55,0x04b60,0x0a570,0x054e4,0x0d160,
0x0e968,0x0d520,0x0daa0,0x16aa6,0x056d0,0x04ae0,0x0a9d4,0x0a2d0,0x0d150,0x0f252,
0x0d520
};
static int lLeapMonth(int y) { return lunarInfo[y - 1900] & 0xF; }
static int lLeapDays(int y)  { return lLeapMonth(y) ? ((lunarInfo[y - 1900] & 0x10000) ? 30 : 29) : 0; }
static int lMonthDays(int y, int m) { return (lunarInfo[y - 1900] & (0x10000 >> m)) ? 30 : 29; }
static int lYearDays(int y) {
  int sum = 348;
  for (int i = 0x8000; i > 0x8; i >>= 1) sum += (lunarInfo[y - 1900] & i) ? 1 : 0;
  return sum + lLeapDays(y);
}
static bool solarLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
static int solarMonthDays(int y, int m) {
  static const int md[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2 && solarLeap(y)) return 29;
  return md[m - 1];
}
static long daysSince1900Jan31(int y, int m, int d) {
  long days = 0;
  for (int yy = 1900; yy < y; yy++) days += solarLeap(yy) ? 366 : 365;
  for (int mm = 1; mm < m; mm++) days += solarMonthDays(y, mm);
  days += d - 1;
  return days - 30;   /* 1900-01-31 距 1900-01-01 为 30 天 */
}
static void solarToLunar(int sy, int sm, int sd, int* ly, int* lm, int* ld, int* leap) {
  long offset = daysSince1900Jan31(sy, sm, sd);
  int i, temp = 0;
  for (i = 1900; i < 2101 && offset > 0; i++) { temp = lYearDays(i); offset -= temp; }
  if (offset < 0) { offset += temp; i--; }
  *ly = i;
  *leap = 0;
  int m;
  for (m = 1; m < 13 && offset > 0; m++) {
    if (lLeapMonth(*ly) > 0 && m == lLeapMonth(*ly) + 1 && *leap == 0) {
      --m; *leap = 1; temp = lLeapDays(*ly);
    } else {
      temp = lMonthDays(*ly, m);
    }
    if (*leap == 1 && m == lLeapMonth(*ly) + 1) *leap = 0;
    offset -= temp;
  }
  if (offset == 0 && lLeapMonth(*ly) > 0 && m == lLeapMonth(*ly) + 1) { *leap = 1; --m; }
  if (offset < 0) { offset += temp; --m; }
  *lm = m;
  *ld = offset + 1;
}

/* ================= 节气 (近似) ================= */
static const char* TERM_NAMES[] = {"小寒","大寒","立春","雨水","惊蛰","春分","清明","谷雨",
  "立夏","小满","芒种","夏至","小暑","大暑","立秋","处暑","白露","秋分","寒露","霜降",
  "立冬","小雪","大雪","冬至"};
static const int TERM_MONTH[] = {1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12};
static const double TERM_C[] = {5.4055,20.12,3.87,18.73,5.63,20.646,4.81,20.1,5.52,21.04,
  5.678,21.37,7.108,22.83,7.5,23.13,7.646,23.042,8.318,23.438,7.438,22.36,7.18,21.94};
static int termDay(int year, int idx) {
  int Y = year % 100;
  return (int)(Y * 0.2422 + TERM_C[idx]) - (int)(Y / 4);
}
static int currentTerm(int y, int m, int d) {
  int best = -1;
  for (int i = 0; i < 24; i++) {
    int tm = TERM_MONTH[i], td = termDay(y, i);
    if (tm < m || (tm == m && td <= d)) best = i;
  }
  return best;
}
/* 仅当「今天恰好是节气日」时返回该节气索引, 否则 -1。
   注意: 不要用 currentTerm 判断要不要显示节气名 —— 它返回的是最近一个
   已过去的节气, 会让非节气日也挂出上一个节气名
   (例: 9/1 会显示"· 处暑", 而真正的白露在 9/7)。 */
static int termOnDay(int y, int m, int d) {
  for (int i = 0; i < 24; i++)
    if (TERM_MONTH[i] == m && termDay(y, i) == d) return i;
  return -1;
}

/* ================= 书籍摘抄池 ================= */
struct Excerpt { const char* src; const char* l1; const char* l2; const char* l3; const char* l4; };
static const Excerpt EXCERPTS[] = {
  {"《论语》","学而时习之，不亦说乎？","有朋自远方来，不亦乐乎？","",""},
  {"《道德经》","上善若水。","水善利万物而不争，","处众人之所恶，故几于道。",""},
  {"《庄子》","天地有大美而不言，","四时有明法而不议，","万物有成理而不说。",""},
  {"《小窗幽记》","宠辱不惊，看庭前花开花落；","去留无意，望天上云卷云舒。","",""},
  {"《大学》","苟日新，日日新，又日新。","","",""},
  {"《中庸》","博学之，审问之，","慎思之，明辨之，笃行之。","",""},
  {"《孟子》","穷则独善其身，","达则兼济天下。","",""},
  {"《周易》","天行健，君子以自强不息；","地势坤，君子以厚德载物。","",""},
  {"《诗经》","蒹葭苍苍，白露为霜。","所谓伊人，在水一方。","",""},
  {"《楚辞》","路漫漫其修远兮，","吾将上下而求索。","",""},
  {"《世说新语》","我与我周旋久，宁作我。","","",""},
  {"《浮生六记》","布衣饭菜，可乐终身。","","",""},
};
static const char* YI[] = {"静坐","读书","散步","运动","冥想","早睡","专注","断舍离","感恩","记录","发呆","整理","喝茶","远眺","深呼吸"};
static const char* JI[] = {"熬夜","内耗","甜食","忌口","拖延","焦虑","久坐","刷屏","暴食","熬夜"};

/* ================= 天气 ================= */
static const char* wxText(int c) {
  if (c == 0) return "晴";
  if (c <= 2) return "多云";
  if (c == 3) return "阴";
  if (c == 45 || c == 48) return "雾";
  if (c >= 51 && c <= 57) return "小雨";
  if (c == 61) return "小雨";
  if (c == 63) return "中雨";
  if (c == 65) return "大雨";
  if (c == 66 || c == 67) return "冻雨";
  if (c >= 71 && c <= 77) return "雪";
  if (c >= 80 && c <= 82) return "阵雨";
  if (c == 85 || c == 86) return "阵雪";
  if (c == 95) return "雷阵雨";
  if (c >= 96) return "冰雹";
  return "-";
}
static const char* wxEn(int c) {
  if (c == 0) return "CLEAR";
  if (c <= 2) return "PARTLY";
  if (c == 3) return "OVERCAST";
  if (c == 45 || c == 48) return "FOG";
  if (c >= 51 && c <= 57) return "DRIZZLE";
  if (c == 61) return "RAIN";
  if (c == 63) return "RAIN";
  if (c == 65) return "HEAVY";
  if (c == 66 || c == 67) return "SLEET";
  if (c >= 71 && c <= 77) return "SNOW";
  if (c >= 80 && c <= 82) return "SHOWER";
  if (c == 85 || c == 86) return "SNOW";
  if (c == 95) return "STORM";
  if (c >= 96) return "HAIL";
  return "--";
}

static void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) { wx.valid = false; return; }
  HTTPClient http;
  char url[256];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
    "&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m,surface_pressure"
    "&hourly=temperature_2m&forecast_hours=24&timezone=auto",
    (double)CITY_LAT, (double)CITY_LON);
  http.begin(url);
  http.setTimeout(5000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (!err) {
      wx.temp = doc["current"]["temperature_2m"] | NAN;
      wx.code = doc["current"]["weather_code"] | -1;
      wx.humidity = doc["current"]["relative_humidity_2m"] | -1;
      wx.wind = doc["current"]["wind_speed_10m"] | NAN;
      wx.pressure = doc["current"]["surface_pressure"] | NAN;
      wx.hourlyCount = 0;
      JsonArray h = doc["hourly"]["temperature_2m"].as<JsonArray>();
      for (JsonVariant v : h) {
        if (wx.hourlyCount >= 24) break;
        wx.hourly[wx.hourlyCount++] = v.as<float>();
      }
      wx.valid = !isnan(wx.temp);
      USBSerial.printf("[wx] ok t=%.1f c=%d h=%d w=%.1f p=%.0f n=%d\n",
        wx.temp, wx.code, wx.humidity, wx.wind, wx.pressure, wx.hourlyCount);
    } else {
      USBSerial.printf("[wx] json err: %s\n", err.c_str());
      wx.valid = false;
    }
  } else {
    USBSerial.printf("[wx] HTTP %d\n", code);
    wx.valid = false;
  }
  http.end();
}

/* ================= UI 对象 (主页) ================= */
static lv_obj_t *lbl_kicker, *lbl_batt, *batt_fill;
static lv_obj_t *lbl_hh, *lbl_mm, *lbl_temp, *lbl_cond;
static lv_obj_t *lbl_meta, *lbl_quote;
static lv_obj_t *lbl_rail_year, *lbl_rail_date;

/* ================= UI 对象 (DOT 页) ================= */
static lv_obj_t *dot_temp, *dot_upd;
static lv_obj_t *dot_hum, *dot_wind, *dot_pres;
static lv_obj_t *dot_bars[24];
static lv_obj_t *trend_line = NULL;   /* 平滑曲线模式的线对象 */
static lv_point_t trend_pts[97];      /* 曲线插值点 (96 段) */

/* ================= UI 对象 (LIT 页) ================= */
static lv_obj_t *lit_solar, *lit_title;
static lv_obj_t *lit_l1, *lit_l2, *lit_l3, *lit_l4;
static lv_obj_t *lit_lunar, *lit_yi, *lit_ji;

/* ================= 工具 ================= */
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
static void make_dots(lv_obj_t* scr, lv_obj_t* dots[3], lv_color_t on, lv_color_t off) {
  for (int i = 0; i < 3; i++) {
    dots[i] = new_rect(scr, 6, 6, 0, 0, off);
    lv_obj_set_style_radius(dots[i], 3, 0);
    lv_obj_align(dots[i], LV_ALIGN_BOTTOM_MID, (i - 1) * 16, -14);
  }
}

/* ================= 主页构建 ================= */
static void build_home() {
  scr_home = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_home, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_home, LV_OPA_COVER, LV_PART_MAIN);

  lbl_kicker = new_label(scr_home, &font_ui_20, COL_MID, 32, 26, "");
  lbl_batt   = new_label(scr_home, &font_ui_20, COL_MID, 226, 26, "--");

  lv_obj_t* shell = lv_obj_create(scr_home);
  lv_obj_remove_style_all(shell);
  lv_obj_set_size(shell, 20, 10);
  lv_obj_set_pos(shell, 196, 31);
  lv_obj_set_style_border_color(shell, COL_LOW, 0);
  lv_obj_set_style_border_width(shell, 1, 0);
  lv_obj_set_style_radius(shell, 2, 0);
  batt_fill = lv_obj_create(shell);
  lv_obj_remove_style_all(batt_fill);
  lv_obj_set_size(batt_fill, 0, 6);
  lv_obj_align(batt_fill, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(batt_fill, COL_MID, 0);
  lv_obj_set_style_bg_opa(batt_fill, LV_OPA_COVER, 0);
  new_rect(scr_home, 2, 4, 217, 34, COL_LOW);

  lbl_hh = new_label(scr_home, &font_time_118, COL_HI, 26, 76, "--");
  lbl_mm = new_label(scr_home, &font_time_118, COL_MM, 26, 192, "--");

  lbl_temp = new_label(scr_home, &font_temp_46, COL_HI, 0, 0, "--");
  lv_obj_align(lbl_temp, LV_ALIGN_TOP_RIGHT, -88, 136);
  lbl_cond = new_label(scr_home, &font_cjk_20, COL_MID, 0, 0, "-");
  lv_obj_align(lbl_cond, LV_ALIGN_TOP_RIGHT, -88, 196);

  lbl_meta = new_label(scr_home, &font_ui_20, COL_MID, 32, 344, "");
  lbl_quote = new_label(scr_home, &font_cjk_20, COL_HI, 0, 0, "");
  lv_obj_set_width(lbl_quote, 272);
  /* 底部对齐而非固定 y: 长金句折成两行时向上生长, 不会压到底部指示点(y=428)。
     1 行时顶边 396, 2 行时顶边 372, 都高于 lbl_meta 底边 368, 不会重叠。 */
  lv_obj_align(lbl_quote, LV_ALIGN_BOTTOM_LEFT, 32, -28);

  new_rect(scr_home, 1, 392, 304, 28, lv_color_hex(0x161618));
  lbl_rail_year = new_label(scr_home, &font_cjk_20, COL_MID, 304, 64, "");
  lv_obj_set_width(lbl_rail_year, 64);
  lv_obj_set_style_text_align(lbl_rail_year, LV_TEXT_ALIGN_CENTER, 0);
  new_rect(scr_home, 7, 7, 333, 205, COL_ACCENT);
  lbl_rail_date = new_label(scr_home, &font_cjk_20, COL_MID, 304, 240, "");
  lv_obj_set_width(lbl_rail_date, 64);
  lv_obj_set_style_text_align(lbl_rail_date, LV_TEXT_ALIGN_CENTER, 0);

  make_dots(scr_home, dot_home, COL_HI, COL_LOW);
}

/* ================= DOT 页构建 ================= */
static void build_dot() {
  scr_dot = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_dot, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_dot, LV_OPA_COVER, LV_PART_MAIN);

  /* 顶栏: 左城市, 右时间 */
  new_label(scr_dot, &font_ui_20, COL_MID, 28, 26, "NINGBO");
  dot_upd = new_label(scr_dot, &font_ui_20, COL_MID, 0, 26, "");
  lv_obj_align(dot_upd, LV_ALIGN_TOP_RIGHT, -28, 26);
  new_rect(scr_dot, 312, 1, 28, 52, COL_DOTDIM);

  /* 大温度(英雄元素) + 天况 */
  dot_temp = new_label(scr_dot, &font_time_118, COL_HI, 28, 70, "--");
  new_rect(scr_dot, 312, 1, 28, 196, COL_DOTDIM);

  /* 指标 3 列: 标签在上, 数值在下, 间距充足 */
  new_label(scr_dot, &font_ui_20, COL_DOTMID, 28, 206, "HUMIDITY");
  new_label(scr_dot, &font_ui_20, COL_DOTMID, 140, 206, "WIND");
  new_label(scr_dot, &font_ui_20, COL_DOTMID, 252, 206, "PRESSURE");
  dot_hum  = new_label(scr_dot, &font_temp_30, COL_HI, 28, 240, "--");
  dot_wind = new_label(scr_dot, &font_temp_30, COL_HI, 140, 240, "--");
  dot_pres = new_label(scr_dot, &font_temp_30, COL_HI, 252, 240, "--");
  new_rect(scr_dot, 312, 1, 28, 286, COL_DOTDIM);

  /* 24h 趋势 */
  new_label(scr_dot, &font_ui_20, COL_DOTMID, 28, 294, "24H TEMPERATURE");
  for (int i = 0; i < 24; i++) {
    dot_bars[i] = new_rect(scr_dot, 8, 10, 28 + i * 13, 430, COL_DOTDIM);
    lv_obj_set_style_radius(dot_bars[i], 4, 0);                       /* 圆角柱 */
    lv_obj_set_style_bg_grad_color(dot_bars[i], COL_DOTDIM, 0);       /* 渐变底 */
    lv_obj_set_style_bg_grad_dir(dot_bars[i], LV_GRAD_DIR_VER, 0);    /* 纵向渐变 */
  }
  /* 平滑曲线模式的对象 */
  trend_line = lv_line_create(scr_dot);
  lv_obj_set_style_line_width(trend_line, 3, 0);
  lv_obj_set_style_line_color(trend_line, COL_DOT, 0);
  lv_obj_set_style_line_rounded(trend_line, true, 0);
  lv_obj_add_flag(trend_line, LV_OBJ_FLAG_HIDDEN);

  make_dots(scr_dot, dot_dot, COL_DOT, COL_DOTDIM);
}

/* ================= LIT 页构建 ================= */
static void build_lit() {
  scr_lit = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_lit, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_lit, LV_OPA_COVER, LV_PART_MAIN);

  /* 顶栏: 左标题, 右日期, 细分割线 (与首页/天气页一致) */
  new_label(scr_lit, &font_cjk_20, COL_MID, 32, 26, "墨 · 日历");
  lit_solar = new_label(scr_lit, &font_cjk_20, COL_MID, 0, 26, "");
  lv_obj_align(lit_solar, LV_ALIGN_TOP_RIGHT, -32, 26);
  new_rect(scr_lit, 304, 1, 32, 52, COL_LOW);

  /* 摘抄: 出处 28px 宋体(朱红点缀), 正文 28px 宋体(暖白), 行距 46px */
  lit_title = new_label(scr_lit, &font_serif_28, COL_LITRED, 0, 0, "");
  lv_obj_align(lit_title, LV_ALIGN_TOP_MID, 0, 84);
  lit_l1 = new_label(scr_lit, &font_serif_28, COL_HI, 0, 0, "");
  lv_obj_align(lit_l1, LV_ALIGN_TOP_MID, 0, 130);
  lit_l2 = new_label(scr_lit, &font_serif_28, COL_HI, 0, 0, "");
  lv_obj_align(lit_l2, LV_ALIGN_TOP_MID, 0, 176);
  lit_l3 = new_label(scr_lit, &font_serif_28, COL_HI, 0, 0, "");
  lv_obj_align(lit_l3, LV_ALIGN_TOP_MID, 0, 222);
  lit_l4 = new_label(scr_lit, &font_serif_28, COL_HI, 0, 0, "");
  lv_obj_align(lit_l4, LV_ALIGN_TOP_MID, 0, 268);

  /* 印章: 朱红方块 + 黑字 (呼应首页朱红点缀) */
  lv_obj_t* seal = new_rect(scr_lit, 36, 36, 0, 0, COL_LITRED);
  lv_obj_set_style_radius(seal, 4, 0);
  lv_obj_align(seal, LV_ALIGN_TOP_MID, 0, 340);
  lv_obj_t* seal_t = new_label(scr_lit, &font_cjk_20, COL_BG, 0, 0, "摘");
  lv_obj_align(seal_t, LV_ALIGN_TOP_MID, 0, 348);

  /* 农历 + 宜忌 (单行居中, 避开底部指示点) */
  lit_lunar = new_label(scr_lit, &font_cjk_20, COL_MID, 0, 0, "");
  lv_obj_align(lit_lunar, LV_ALIGN_BOTTOM_MID, 0, -44);
  lit_yi = new_label(scr_lit, &font_cjk_20, COL_LITRED, 0, 0, "");
  lv_obj_align(lit_yi, LV_ALIGN_BOTTOM_MID, -84, -14);
  lit_ji = new_label(scr_lit, &font_cjk_20, COL_MID, 0, 0, "");
  lv_obj_align(lit_ji, LV_ALIGN_BOTTOM_MID, 84, -14);

  make_dots(scr_lit, dot_lit, COL_HI, COL_LOW);
}

/* ================= 电池 ================= */
static void updateBattery() {
  int pct = -1;
  bool chg = false;
  if (pmu_ok && pmu.isBatteryConnect()) {
    pct = pmu.getBatteryPercent();
    chg = pmu.isCharging();
  }
  char t[12];
  if (pct < 0)  snprintf(t, sizeof(t), "--");
  else if (chg) snprintf(t, sizeof(t), "%d%%+", pct);
  else          snprintf(t, sizeof(t), "%d%%", pct);
  lv_label_set_text(lbl_batt, t);
  int w = (pct < 0) ? 0 : (17 * pct / 100);
  lv_obj_set_size(batt_fill, w, 6);
  USBSerial.printf("[mem] free heap=%u\n", ESP.getFreeHeap());
}

/* ================= 日期时间 ================= */
static const char* const QUOTES[] = {
  "「凡心所向，素履以往。」",
  "「山不过来，我就过去。」",
  "「慢慢走，欣赏啊。」",
  "「人间有味是清欢。」",
  "「此心安处是吾乡。」",
  "「长风破浪会有时，直挂云帆济沧海。」",
  "「万物皆有裂痕，那是光照进来的地方。」",
  "「闲看庭前花开花落。」",
  "「静水流深。」",
  "「不积跬步，无以至千里。」",
};

/* ================= 24h 趋势绘制 (圆角渐变柱 / 平滑曲线) ================= */
static void updateTrend(int curHour) {
  if (wx.hourlyCount <= 0) {
    for (int i = 0; i < 24; i++) lv_obj_add_flag(dot_bars[i], LV_OBJ_FLAG_HIDDEN);
    if (trend_line) lv_obj_add_flag(trend_line, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  float mn = wx.hourly[0], mx = wx.hourly[0];
  for (int i = 0; i < wx.hourlyCount; i++) {
    if (wx.hourly[i] < mn) mn = wx.hourly[i];
    if (wx.hourly[i] > mx) mx = wx.hourly[i];
  }
  float span = (mx - mn) < 1.0f ? 1.0f : (mx - mn);
  /* 图表区域: x 28..340, y 底 430, 顶 360 (高 70) */
  const int baseY = 430, topY = 360, x0 = 28, x1 = 340;
  const int n = wx.hourlyCount;

  if (trendMode == 0) {
    /* 圆角渐变柱 */
    if (trend_line) lv_obj_add_flag(trend_line, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 24; i++) {
      float v = (i < n) ? wx.hourly[i] : wx.hourly[n - 1];
      int hgt = 8 + (int)((v - mn) / span * (baseY - topY - 8));
      int x = x0 + (int)((float)i / 23.0f * (x1 - x0 - 8));
      lv_obj_set_height(dot_bars[i], hgt);
      lv_obj_set_pos(dot_bars[i], x, baseY - hgt);
      /* 当前小时高亮为亮青, 其余为渐变青 */
      if (i == curHour) {
        lv_obj_set_style_bg_color(dot_bars[i], COL_DOT, 0);
        lv_obj_set_style_bg_grad_color(dot_bars[i], COL_DOTMID, 0);
      } else {
        lv_obj_set_style_bg_color(dot_bars[i], COL_DOTMID, 0);
        lv_obj_set_style_bg_grad_color(dot_bars[i], COL_DOTDIM, 0);
      }
      lv_obj_clear_flag(dot_bars[i], LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    /* 平滑曲线: Catmull-Rom 插值成 96 段 */
    for (int i = 0; i < 24; i++) lv_obj_add_flag(dot_bars[i], LV_OBJ_FLAG_HIDDEN);
    if (!trend_line) return;
    /* 采样点 (含两端重复用于 Catmull-Rom) */
    float py[26];
    for (int i = 0; i < n; i++) py[i] = wx.hourly[i];
    if (n == 1) { py[1] = py[0]; py[2] = py[0]; py[3] = py[0]; }
    else if (n == 2) { py[2] = py[1]; py[3] = py[0]; }
    else { py[n] = py[n - 1]; py[n + 1] = py[n - 2]; }
    int seg = 96;
    for (int s = 0; s <= seg; s++) {
      float t = (float)s / seg * (n - 1);
      int i0 = (int)t;
      float f = t - i0;
      float p0 = py[(i0 - 1 + n) % n];
      float p1 = py[i0 % n];
      float p2 = py[(i0 + 1) % n];
      float p3 = py[(i0 + 2) % n];
      /* Catmull-Rom */
      float v = 0.5f * ((2 * p1) + (-p0 + p2) * f + (2 * p0 - 5 * p1 + 4 * p2 - p3) * f * f
                        + (-p0 + 3 * p1 - 3 * p2 + p3) * f * f * f);
      float x = x0 + (float)s / seg * (x1 - x0);
      float y = baseY - (8 + (v - mn) / span * (baseY - topY - 8));
      trend_pts[s].x = (lv_coord_t)x;
      trend_pts[s].y = (lv_coord_t)y;
    }
    lv_line_set_points(trend_line, trend_pts, seg + 1);
    lv_obj_clear_flag(trend_line, LV_OBJ_FLAG_HIDDEN);
  }
}

static void updateDateTime(bool force) {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  if (t.tm_year + 1900 < 2020) return;

  char hh[4], mm[4];
  snprintf(hh, sizeof(hh), "%02d", t.tm_hour);
  snprintf(mm, sizeof(mm), "%02d", t.tm_min);
  lv_label_set_text(lbl_hh, hh);
  lv_label_set_text(lbl_mm, mm);

  static int last_day = -1;
  if (!force && t.tm_mday == last_day) return;
  last_day = t.tm_mday;

  int y = t.tm_year + 1900, mo = t.tm_mon + 1, d = t.tm_mday;

  set_kicker(lbl_kicker, t.tm_wday);

  static const char* MO[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                             "JUL","AUG","SEP","OCT","NOV","DEC"};
  char meta[48];
  snprintf(meta, sizeof(meta), "%s · %d%s · DAY %d/365",
           MO[t.tm_mon], d, daySuffix(d), t.tm_yday + 1);
  lv_label_set_text(lbl_meta, meta);

  String raildate = cnMonth(mo);
  raildate += "月";
  raildate += cnDay(d);
  raildate += "日";
  lv_label_set_text(lbl_rail_date, verticalize(raildate).c_str());

  String yr = cnYear(y);
  lv_label_set_text(lbl_rail_year, verticalize(yr).c_str());

  lv_label_set_text(lbl_quote, QUOTES[t.tm_yday % 10]);

  /* ---- DOT 页 ---- */
  char b[16];
  if (wx.valid) {
    snprintf(b, sizeof(b), "%.0f°", (double)wx.temp);
    lv_label_set_text(lbl_temp, b);          /* 首页温度 */
    lv_label_set_text(lbl_cond, wxText(wx.code));  /* 首页天气 */
    lv_label_set_text(dot_temp, b);
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(dot_upd, b);
    snprintf(b, sizeof(b), "%d%%", wx.humidity);
    lv_label_set_text(dot_hum, b);
    snprintf(b, sizeof(b), "%.1f", (double)wx.wind);
    lv_label_set_text(dot_wind, b);
    snprintf(b, sizeof(b), "%.0f", (double)wx.pressure);
    lv_label_set_text(dot_pres, b);
    updateTrend(t.tm_hour);
  } else {
    lv_label_set_text(lbl_temp, "--");
    lv_label_set_text(lbl_cond, "-");
    lv_label_set_text(dot_temp, "--");
    lv_label_set_text(dot_upd, "--:--");
    lv_label_set_text(dot_hum, "--");
    lv_label_set_text(dot_wind, "--");
    lv_label_set_text(dot_pres, "--");
    for (int i = 0; i < 24; i++) lv_obj_add_flag(dot_bars[i], LV_OBJ_FLAG_HIDDEN);
    if (trend_line) lv_obj_add_flag(trend_line, LV_OBJ_FLAG_HIDDEN);
  }

  /* ---- LIT 页 ---- */
  int ly, lm, ld, leap;
  solarToLunar(y, mo, d, &ly, &lm, &ld, &leap);
  String lunar = "农历";
  if (leap) lunar += "闰";
  lunar += cnMonth(lm);
  lunar += "月";
  lunar += cnDay(ld);
  int term = termOnDay(y, mo, d);          /* 仅当天恰好是节气日才显示 */
  if (term >= 0) {
    lunar += " · ";
    lunar += TERM_NAMES[term];
  }
  lv_label_set_text(lit_lunar, lunar.c_str());

  char solar[32];
  snprintf(solar, sizeof(solar), "%04d年%02d月%02d日", y, mo, d);
  lv_label_set_text(lit_solar, solar);

  const Excerpt& p = EXCERPTS[t.tm_yday % 12];
  lv_label_set_text(lit_title, p.src);
  lv_label_set_text(lit_l1, p.l1);
  lv_label_set_text(lit_l2, p.l2);
  lv_label_set_text(lit_l3, p.l3);
  lv_label_set_text(lit_l4, p.l4);

  int yi = t.tm_yday % 15, ji = t.tm_yday % 10;
  String yis = String("宜 ") + YI[yi];
  String jis = String("忌 ") + JI[ji];
  lv_label_set_text(lit_yi, yis.c_str());
  lv_label_set_text(lit_ji, jis.c_str());
}

/* ================= 定时器 ================= */
static void timer_1s(lv_timer_t *) {
  updateDateTime(false);
  /* 子页 60s 无操作自动回主页 */
  if (currentPage != PAGE_HOME && (millis() - lastActivity) > 60000UL) {
    switchPage(PAGE_HOME);
  }
  static int cnt = 0;
  if (++cnt >= 30) { cnt = 0; updateBattery(); }
}

static void timer_wx(lv_timer_t *) {
  static int cnt = 0;
  if (wx.valid && ++cnt < 30) return;
  cnt = 0;
  fetchWeather();
  updateDateTime(true);
}

/* ================= 初始化 ================= */
void setup() {
  USBSerial.begin(115200);
  USBSerial.setTxTimeoutMs(0);
  USBSerial.println("\nMONO watchface boot (v1.2: 3-page cards + swipe)");

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
  if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    USBSerial.println("QMI8658 not found (shake-wake disabled)");
  } else {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_500Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
    qmi_ok = true;
    USBSerial.println("QMI8658 ok");
  }

  pmu_ok = pmu.begin(Wire, 0x34, IIC_SDA, IIC_SCL);
  USBSerial.println(pmu_ok ? "AXP2101 ok" : "AXP2101 not found");

  gfx->begin();
  gfx->setBrightness(220);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_WIDTH * LCD_HEIGHT / 10);
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = gfx->width();
  disp_drv.ver_res  = gfx->height();
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  build_home();
  build_dot();
  build_lit();
  lv_scr_load(scr_home);
  updateDots();

  const esp_timer_create_args_t tick_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t tick_timer = NULL;
  esp_timer_create(&tick_args, &tick_timer);
  esp_timer_start_periodic(tick_timer, 2000);

  lv_timer_create(timer_1s, 1000, NULL);
  lv_timer_create(timer_wx, 1800000, NULL);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  USBSerial.print("WiFi connecting");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(50);
    lv_timer_handler();
    delay(450);
    USBSerial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.println(" OK");
    configTime(TIMEZONE_OFFSET, 0, "pool.ntp.org", "time.nist.gov");
  } else {
    USBSerial.println(" FAIL (offline mode)");
  }

  fetchWeather();
  updateDateTime(true);
  updateBattery();

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
    delay(30);
    return;
  }

  lv_timer_handler();
  delay(5);
}
