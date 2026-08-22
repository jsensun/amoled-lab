/*
 * ============================================================
 *  MONO · EDITORIAL 表盘 —— amoled-lab 阶段 1 · rev4
 *  硬件: 微雪 ESP32-S3-Touch-AMOLED-1.8 V2 (CO5300 + CST820)
 *
 *  rev4 变更:
 *   - 小字 UI 全部提升到 20px (面板可读性临界点以上, 根治"斜体"观感)
 *   - 电量计组左移适配加宽后的文字
 *   注意: 本文件必须保持 UTF-8 编码; 禁止用 PS Get-Content 转存
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
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
LV_FONT_DECLARE(font_ui_20);
LV_FONT_DECLARE(font_cjk_20);

/* ---------------- 设计令牌 ---------------- */
#define COL_BG     lv_color_hex(0x000000)
#define COL_HI     lv_color_hex(0xF5F5F0)
#define COL_MM     lv_color_hex(0xC4C4BE)
#define COL_MID    lv_color_hex(0x9A9A94)
#define COL_LOW    lv_color_hex(0x3A3A38)
#define COL_ACCENT lv_color_hex(0xE8503A)

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

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];

/* ---------------- 电源 ---------------- */
static XPowersAXP2101 pmu;
static bool pmu_ok = false;

/* ---------------- UI 对象 ---------------- */
static lv_obj_t *lbl_kicker, *lbl_batt, *batt_fill;
static lv_obj_t *lbl_hh, *lbl_mm, *lbl_temp, *lbl_cond;
static lv_obj_t *lbl_meta, *lbl_quote;
static lv_obj_t *lbl_rail_year, *lbl_rail_date;

static bool diag_builtin = false;

/* ---------------- 天气 ---------------- */
struct Weather { bool valid; float temp; int code; };
static Weather wx = { false, 0, -1 };

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

static void set_kicker(int wday) {
  static const char* WD[] = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY",
                             "THURSDAY","FRIDAY","SATURDAY"};
  String s;
  for (const char* p = WD[wday]; *p; ++p) {
    s += *p;
    if (*(p + 1)) s += ' ';
  }
  lv_label_set_text(lbl_kicker, s.c_str());
}

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

static void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) { wx.valid = false; return; }
  HTTPClient http;
  char url[192];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
    "&current=temperature_2m,weather_code&timezone=auto",
    (double)CITY_LAT, (double)CITY_LON);
  http.begin(url);
  http.setTimeout(4000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (!err) {
      wx.temp = doc["current"]["temperature_2m"] | NAN;
      wx.code = doc["current"]["weather_code"] | -1;
      wx.valid = !isnan(wx.temp);
      USBSerial.printf("[wx] json ok temp=%.1f code=%d\n", wx.temp, wx.code);
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

static void applyWeather() {
  if (wx.valid) {
    char b[10];
    snprintf(b, sizeof(b), "%.0f°", (double)wx.temp);
    lv_label_set_text(lbl_temp, b);
    lv_label_set_text(lbl_cond, wxText(wx.code));
  } else {
    lv_label_set_text(lbl_temp, "--");
    lv_label_set_text(lbl_cond, "-");
  }
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

  set_kicker(t.tm_wday);

  static const char* MO[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                             "JUL","AUG","SEP","OCT","NOV","DEC"};
  char meta[48];
  snprintf(meta, sizeof(meta), "%s · %d%s · DAY %d/365",
           MO[t.tm_mon], t.tm_mday, daySuffix(t.tm_mday), t.tm_yday + 1);
  lv_label_set_text(lbl_meta, meta);

  String raildate = cnMonth(t.tm_mon + 1);
  raildate += "月";
  raildate += cnDay(t.tm_mday);
  raildate += "日";
  lv_label_set_text(lbl_rail_date, verticalize(raildate).c_str());

  String yr = cnYear(t.tm_year + 1900);
  lv_label_set_text(lbl_rail_year, verticalize(yr).c_str());

  lv_label_set_text(lbl_quote, QUOTES[t.tm_yday % 10]);
}

/* ================= 定时器 ================= */
static void timer_1s(lv_timer_t *) {
  updateDateTime(false);
  static int cnt = 0;
  if (++cnt >= 30) { cnt = 0; updateBattery(); }
}

static void timer_wx(lv_timer_t *) {
  fetchWeather();
  applyWeather();
}

/* ================= UI 构建 ================= */
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

static void build_ui() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  /* 顶行 */
  lbl_kicker = new_label(scr, &font_ui_20, COL_MID, 32, 26, "");
  lbl_batt   = new_label(scr, &font_ui_20, COL_MID, 226, 26, "--");

  /* 电量仪表 */
  lv_obj_t* shell = lv_obj_create(scr);
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
  new_rect(scr, 2, 4, 217, 34, COL_LOW);

  /* 大号时间堆叠 (118px) */
  lbl_hh = new_label(scr, &font_time_118, COL_HI, 26, 76, "--");
  lbl_mm = new_label(scr, &font_time_118, COL_MM, 26, 192, "--");

  /* 天气块 */
  lbl_temp = new_label(scr, &font_temp_46, COL_HI, 0, 0, "--");
  lv_obj_align(lbl_temp, LV_ALIGN_TOP_RIGHT, -88, 136);
  lbl_cond = new_label(scr, &font_cjk_20, COL_MID, 0, 0, "-");
  lv_obj_align(lbl_cond, LV_ALIGN_TOP_RIGHT, -88, 196);

  /* 版权页小字 */
  lbl_meta = new_label(scr, &font_ui_20, COL_MID, 32, 344, "");

  /* 金句 */
  lbl_quote = new_label(scr, &font_cjk_20, COL_HI, 32, 398, "");
  lv_obj_set_width(lbl_quote, 272);

  /* 右侧竖排栏 */
  new_rect(scr, 1, 392, 304, 28, lv_color_hex(0x161618));
  lbl_rail_year = new_label(scr, &font_cjk_20, COL_MID, 304, 64, "");
  lv_obj_set_width(lbl_rail_year, 64);
  lv_obj_set_style_text_align(lbl_rail_year, LV_TEXT_ALIGN_CENTER, 0);
  new_rect(scr, 7, 7, 333, 205, COL_ACCENT);
  lbl_rail_date = new_label(scr, &font_cjk_20, COL_MID, 304, 240, "");
  lv_obj_set_width(lbl_rail_date, 64);
  lv_obj_set_style_text_align(lbl_rail_date, LV_TEXT_ALIGN_CENTER, 0);
}

/* ================= 诊断: 字体 A/B 对比 ================= */
static void applyDiag() {
  const lv_font_t* f_big = diag_builtin ? &lv_font_montserrat_48 : &font_time_118;
  const lv_font_t* f_mid = diag_builtin ? &lv_font_montserrat_20 : &font_temp_46;
  const lv_font_t* f_sml = diag_builtin ? &lv_font_montserrat_20 : &font_ui_20;
  const lv_font_t* f_cjk = diag_builtin ? &lv_font_montserrat_20 : &font_cjk_20;
  lv_obj_set_style_text_font(lbl_hh,    f_big, 0);
  lv_obj_set_style_text_font(lbl_mm,    f_big, 0);
  lv_obj_set_style_text_font(lbl_temp,  f_mid, 0);
  lv_obj_set_style_text_font(lbl_kicker, f_sml, 0);
  lv_obj_set_style_text_font(lbl_batt,  f_sml, 0);
  lv_obj_set_style_text_font(lbl_meta,  f_sml, 0);
  lv_obj_set_style_text_font(lbl_cond,  f_cjk, 0);
  lv_obj_set_style_text_font(lbl_quote, f_cjk, 0);
  lv_obj_set_style_text_font(lbl_rail_year, f_cjk, 0);
  lv_obj_set_style_text_font(lbl_rail_date, f_cjk, 0);
  USBSerial.printf("[diag] fonts switched to %s\n",
                   diag_builtin ? "BUILTIN montserrat" : "CUSTOM generated");
}

/* ================= 初始化 ================= */
void setup() {
  USBSerial.begin(115200);
  USBSerial.setTxTimeoutMs(0);
  USBSerial.println("\nMONO watchface boot (rev4)");

  pinMode(0, INPUT_PULLUP);   /* BOOT 键 = 字体诊断开关 */

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

  build_ui();

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
  applyWeather();
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
      diag_builtin = !diag_builtin;
      applyDiag();
    }
  }
  lastBoot = b;

  lv_timer_handler();
  delay(5);
}
