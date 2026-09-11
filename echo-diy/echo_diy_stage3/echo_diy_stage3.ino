/*
 * Echo DIY — v2.0: 双击唤醒 + 按住说话 (微信式) + WiFi 上传
 * 板载麦克风 → ES8311 ADC → I2S RX → PSRAM → WiFi HTTP POST → 电脑 WAV
 *
 * 交互 (微信语音式):
 *   待机: 屏幕熄灭 + CPU 降频 (省电), 双击屏幕唤醒
 *   就绪: 屏幕显示 "按住说话" 大按钮
 *   录音: 按住按钮一直录 (不写死时长, 上限 60s 防意外), 松手即停
 *   上传: 自动 WiFi 上传, 显示结果 3 秒后回待机
 *
 * 省电设计:
 *   待机: 熄屏 + 80MHz
 *   录音中: 熄屏 (AMOLED 全亮是最大耗电, 录音无需看屏)
 *   上传完成: 自动回待机
 *
 * 串口命令 (测试用):
 *   'r' 触发录音 (等效按住说话)
 *   's' 打印状态
 *
 * 电脑端:
 *   python pc-server/recv_http.py 8080 recordings
 *   python pc-server/transcribe.py recordings   (可选, 自动转写)
 */
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_I2S.h>
#include <memory>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "es8311.h"
#include "pin_config.h"
#include "secrets.h"

#define AUDIO_MCK_IO 16
#define AUDIO_BCK_IO 9
#define AUDIO_WS_IO  45
#define AUDIO_DO_IO  8
#define AUDIO_DI_IO  10   /* 麦克风数据输入 (关键!) */
#define AUDIO_PA_IO  46
#define AUDIO_SAMPLE_RATE 16000

#define MAX_REC_SECONDS 60  /* 录音上限 (防忘记松手), 按住期间一直录 */
#define PCM_BYTES   (AUDIO_SAMPLE_RATE * 2 * MAX_REC_SECONDS)  /* 16bit mono, 1.92MB */

#define MIC_GAIN_SOFT 4   /* 软件增益: ES8311 PGA 增益寄存器实测无效, 录音后放大 4x */

#define BTN_IO 0          /* BOOT 按钮, 备用触发 (按下为 LOW) */

/* 电脑 IP 和端口 (先跑 recv_http.py) */
#define PC_HOST "192.168.3.67"
#define PC_PORT 8080
#define UPLOAD_URL "http://192.168.3.67:8080/upload"

#define WIFI_TIMEOUT_MS 15000

#define BRIGHT_ON 180
#define BRIGHT_OFF 0

I2SClass audio_i2s;

/* ---------------- 屏幕 (Arduino_GFX, 368x448 CO5300 QSPI) ---------------- */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT, 16, 0, 0, 0);

/* ---------------- 触摸 (CST816, I2C) ---------------- */
#define CST816T_DEVICE_ADDRESS 0x15
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> CST816(new Arduino_CST816x(
    IIC_Bus, CST816T_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));
void Arduino_IIC_Touch_Interrupt(void) { }

/* 触摸读取封装 */
static int32_t touch_fingers() {
  return CST816->IIC_Read_Device_Value(
    CST816->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
}
static int32_t touch_x() {
  return CST816->IIC_Read_Device_Value(
    CST816->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
}
static int32_t touch_y() {
  return CST816->IIC_Read_Device_Value(
    CST816->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
}
static String touch_gesture() {
  return CST816->IIC_Read_Device_State(
    CST816->Arduino_IIC_Touch::Status_Information::TOUCH_GESTURE_ID);
}

/* 软件双击检测: 触摸→释放→触摸 且两次间隔 < 400ms 视为双击
 * (不依赖 CST816 手势寄存器, 该寄存器读取后不清零会误报) */
static bool detect_double_tap() {
  static bool in_tap = false;
  static uint32_t last_release_ms = 0;
  static uint32_t tap_count = 0;

  bool touching = (touch_fingers() > 0);
  if (touching && !in_tap) {
    in_tap = true;
    if (millis() - last_release_ms < 400) {
      tap_count++;
    } else {
      tap_count = 1;
    }
    last_release_ms = 0;
  }
  if (!touching && in_tap) {
    in_tap = false;
    last_release_ms = millis();
    if (tap_count >= 2) {
      tap_count = 0;
      return true;
    }
  }
  if (tap_count > 0 && millis() - last_release_ms > 400) {
    tap_count = 0;   /* 超时重置 */
  }
  return false;
}

/* ---------------- 状态机 ---------------- */
enum State { ST_SLEEP, ST_READY, ST_RECORDING, ST_UPLOADING };
static State state = ST_SLEEP;
static bool serial_trigger = false;  /* 串口 'r' 触发时跳过触摸释放检测 */
static uint32_t wav_len = 0;      /* 录音完成后组装的总长度, 供上传使用 */
static uint32_t last_rec_ms = 0;  /* 最近一次录音时长 (毫秒), 供结果回显 */
static uint32_t sleep_entered_ms = 0;  /* 进入 SLEEP 的时刻, 用于跳过手势残留期 */

/* 用 PSRAM 存 PCM 和完整 WAV (8MB 足够) */
static int16_t* pcm_buf = NULL;
static uint8_t* wav_buf = NULL;

/* ---------------- 屏幕显示 ---------------- */
static void show_status(const char* title, const char* sub, uint16_t color) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(color);
  gfx->setTextSize(2);
  gfx->setCursor(30, 190);
  gfx->print(title);
  if (sub) {
    gfx->setCursor(30, 240);
    gfx->print(sub);
  }
}

/* 就绪界面: 中央大圆按钮 "按住说话" */
static void show_ready() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(LCD_WIDTH/2, LCD_HEIGHT/2, 110, RGB565_GREEN);
  gfx->fillCircle(LCD_WIDTH/2, LCD_HEIGHT/2, 100, RGB565_DARKGREEN);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(LCD_WIDTH/2 - 70, LCD_HEIGHT/2 - 12);
  gfx->print("HOLD TO TALK");
  gfx->setTextSize(1);
  gfx->setCursor(LCD_WIDTH/2 - 60, LCD_HEIGHT/2 + 30);
  gfx->print("press & hold to record");
  gfx->setCursor(LCD_WIDTH/2 - 90, LCD_HEIGHT/2 + 55);
  gfx->print("release to send");
}

/* 录音中: 熄屏省电 (触摸检测不依赖屏幕) */
static void show_recording_off() {
  gfx->setBrightness(BRIGHT_OFF);
}

/* 点亮屏幕 */
static void screen_on() {
  gfx->setBrightness(BRIGHT_ON);
}

/* ---------------- WAV 头 ---------------- */
static void build_wav_header(uint8_t* hdr, uint32_t data_len) {
  uint32_t byte_rate = AUDIO_SAMPLE_RATE * 2;  /* 16bit mono */
  uint32_t chunk_size = 36 + data_len;
  hdr[0]='R';hdr[1]='I';hdr[2]='F';hdr[3]='F';
  hdr[4]=chunk_size&0xff;hdr[5]=(chunk_size>>8)&0xff;hdr[6]=(chunk_size>>16)&0xff;hdr[7]=(chunk_size>>24)&0xff;
  hdr[8]='W';hdr[9]='A';hdr[10]='V';hdr[11]='E';
  hdr[12]='f';hdr[13]='m';hdr[14]='t';hdr[15]=' ';
  hdr[16]=16;hdr[17]=0;hdr[18]=0;hdr[19]=0;              /* fmt chunk size */
  hdr[20]=1;hdr[21]=0;                                    /* PCM */
  hdr[22]=1;hdr[23]=0;                                    /* mono */
  hdr[24]=AUDIO_SAMPLE_RATE&0xff;hdr[25]=(AUDIO_SAMPLE_RATE>>8)&0xff;hdr[26]=0;hdr[27]=0;
  hdr[28]=byte_rate&0xff;hdr[29]=(byte_rate>>8)&0xff;hdr[30]=(byte_rate>>16)&0xff;hdr[31]=(byte_rate>>24)&0xff;
  hdr[32]=2;hdr[33]=0;                                    /* block align */
  hdr[34]=16;hdr[35]=0;                                   /* bits per sample */
  hdr[36]='d';hdr[37]='a';hdr[38]='t';hdr[39]='a';
  hdr[40]=data_len&0xff;hdr[41]=(data_len>>8)&0xff;hdr[42]=(data_len>>16)&0xff;hdr[43]=(data_len>>24)&0xff;
}

/* ---------------- 音频初始化 (录音模式) ---------------- */
static bool audio_init() {
  pinMode(AUDIO_PA_IO, OUTPUT);
  digitalWrite(AUDIO_PA_IO, HIGH);

  Wire.begin(IIC_SDA, IIC_SCL);   /* 必须先初始化 I2C, 否则 ES8311 通信失败 */

  audio_i2s.setPins(AUDIO_BCK_IO, AUDIO_WS_IO, AUDIO_DO_IO, AUDIO_DI_IO, AUDIO_MCK_IO);
  if (!audio_i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                       I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("I2S init fail");
    return false;
  }

  es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) { Serial.println("ES8311 create fail"); return false; }
  const es8311_clock_config_t clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = AUDIO_SAMPLE_RATE * 256,
    .sample_frequency = AUDIO_SAMPLE_RATE
  };
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("ES8311 init fail"); return false;
  }
  es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
  es8311_microphone_config(es, false);          /* 模拟麦克风 */
  es8311_microphone_gain_set(es, (es8311_mic_gain_t)6);  /* 增益 36dB */
  es8311_voice_volume_set(es, 60, NULL);
  return true;
}

/* ---------------- 录音到 PSRAM (按住一直录, 松手停止) ----------------
 * 返回录到的 PCM 字节数。停止条件:
 *   1. 触摸释放 (fingers == 0)  → 微信式松手即停 (仅 stop_on_release=true)
 *   2. 达到 MAX_REC_SECONDS 上限 (防意外)
 *   3. BOOT 按钮按下 (备用)
 */
static uint32_t record_to_ram(bool stop_on_release) {
  uint32_t total = 0;
  uint8_t tmp[2048];
  while (total < PCM_BYTES) {
    /* 触摸释放 → 停止 (串口触发时跳过, 否则立即误停) */
    if (stop_on_release && touch_fingers() == 0) {
      Serial.println("release -> stop");
      break;
    }
    /* 串口触发时: 收到任意字符 → 停止 (测试用) */
    if (!stop_on_release && Serial.available() > 0) {
      Serial.read();
      Serial.println("serial stop");
      break;
    }
    /* BOOT 按钮备用停止 */
    if (digitalRead(BTN_IO) == LOW) {
      delay(30);
      if (digitalRead(BTN_IO) == LOW) {
        Serial.println("early stop by button");
        while (digitalRead(BTN_IO) == LOW) delay(10);
        break;
      }
    }
    size_t n = audio_i2s.readBytes((char*)tmp, sizeof(tmp));
    if (n == 0) continue;
    /* 立体声 → 取左声道 (单声道) + 软件增益 */
    int16_t* st = (int16_t*)tmp;
    int frames = n / 4;
    for (int i = 0; i < frames && total < PCM_BYTES; i++) {
      int32_t v = st[i*2];          /* 左声道 */
      v = v * MIC_GAIN_SOFT;        /* 软件放大 */
      if (v > 32767) v = 32767;     /* 防削波 */
      if (v < -32768) v = -32768;
      pcm_buf[total/2] = (int16_t)v;
      total += 2;
    }
  }
  return total;
}

/* ---------------- 自动增益 (AGC): 录音后分析峰值, 小声放大 / 大声不削波 ---------------- */
static void apply_agc(uint32_t samples) {
  int16_t peak = 0;
  for (uint32_t i = 0; i < samples; i++) {
    int16_t v = pcm_buf[i];
    if (v < 0) v = -v;
    if (v > peak) peak = v;
  }
  if (peak == 0) return;
  const float TARGET = 13107.0f;   /* 40% 满幅 */
  float gain = TARGET / peak;
  if (gain > 8.0f) gain = 8.0f;    /* 最大放大 8x */
  if (gain < 1.0f) gain = 1.0f;    /* 不缩小 */
  for (uint32_t i = 0; i < samples; i++) {
    int32_t v = (int32_t)(pcm_buf[i] * gain);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    pcm_buf[i] = (int16_t)v;
  }
  Serial.printf("AGC: peak %d -> gain %.2f\n", peak, gain);
}

/* ---------------- 组装完整 WAV 到 wav_buf ---------------- */
static uint32_t build_wav(uint32_t pcm_len) {
  build_wav_header(wav_buf, pcm_len);
  memcpy(wav_buf + 44, pcm_buf, pcm_len);
  return 44 + pcm_len;
}

/* ---------------- WiFi HTTP POST 上传 ---------------- */
static bool upload_via_wifi(uint32_t wav_len) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi connecting %s ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
      delay(200);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connect FAIL");
      return false;
    }
    Serial.printf("WiFi OK, IP=%s\n", WiFi.localIP().toString().c_str());
  }

  HTTPClient http;
  if (!http.begin(UPLOAD_URL)) {
    Serial.println("http.begin FAIL");
    return false;
  }
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("X-Device", "echo-diy");
  int code = http.POST(wav_buf, wav_len);
  Serial.printf("HTTP POST -> %d\n", code);
  String resp = http.getString();
  if (resp.length() > 0) Serial.println(resp);
  http.end();
  return (code == 200);
}

/* ---------------- 回退: 通过 USB 串口发 WAV (帧协议) ---------------- */
static void send_wav_over_serial(uint32_t wav_len) {
  Serial.write(0xAA); Serial.write(0x55);
  Serial.write(wav_len & 0xff); Serial.write((wav_len>>8)&0xff);
  Serial.write((wav_len>>16)&0xff); Serial.write((wav_len>>24)&0xff);
  Serial.write(wav_buf, wav_len);
  Serial.flush();
  Serial.println();
  Serial.println("SEND_DONE");
}

/* ---------------- 省电 ---------------- */
static void sleep_now() {
  gfx->setBrightness(BRIGHT_OFF);
  setCpuFrequencyMhz(80);
  touch_gesture();   /* 丢弃手势寄存器残留 (CST816 初始化后可能残留 Double Click) */
  sleep_entered_ms = millis();
  state = ST_SLEEP;
  Serial.println("SLEEP (double-tap to wake)");
}
static void wake_up() {
  setCpuFrequencyMhz(240);
  screen_on();
  show_ready();
  state = ST_READY;
  Serial.println("READY (hold button to record)");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Echo DIY v2.0: double-tap wake + hold-to-talk");

  pinMode(BTN_IO, INPUT_PULLUP);

  /* 屏幕初始化 */
  gfx->begin();
  gfx->setTextWrap(false);
  gfx->setBrightness(BRIGHT_ON);
  show_status("Echo DIY", "booting...", RGB565_WHITE);

  /* 触摸初始化 (CST816) */
  bool touch_ok = false;
  for (int i = 0; i < 3 && !touch_ok; i++) {
    touch_ok = CST816->begin();
    if (!touch_ok) { Serial.printf("CST816 retry %d...\n", i+1); delay(200); }
  }
  if (!touch_ok) {
    Serial.println("CST816 init fail (BOOT button only)");
  } else {
    CST816->IIC_Write_Device_State(
      Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
      Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
    Serial.println("CST816 ok");
  }

  /* PSRAM 缓冲 (1.92MB PCM + 1.92MB WAV) */
  pcm_buf = (int16_t*)heap_caps_malloc(PCM_BYTES, MALLOC_CAP_SPIRAM);
  wav_buf = (uint8_t*)heap_caps_malloc(44 + PCM_BYTES, MALLOC_CAP_SPIRAM);
  if (!pcm_buf || !wav_buf) { Serial.println("PSRAM alloc fail"); show_status("ERROR", "PSRAM alloc fail", RGB565_RED); return; }
  Serial.printf("PSRAM buf: %d + %d bytes\n", PCM_BYTES, 44 + PCM_BYTES);

  if (!audio_init()) { Serial.println("audio init fail"); show_status("ERROR", "audio init fail", RGB565_RED); return; }
  Serial.println("audio ok");

  /* 进入待机 (熄屏省电) */
  sleep_now();
}

void loop() {
  switch (state) {
    case ST_SLEEP: {
      /* 双击屏幕唤醒 (每 100ms 轮询, 软件双击检测) */
      static uint32_t lastPoll = 0;
      if (millis() - lastPoll >= 100) {
        lastPoll = millis();
        if (millis() - sleep_entered_ms < 1000) break;  /* 跳过手势残留期 */
        if (detect_double_tap()) {
          Serial.println("double-tap detected");
          wake_up();
        }
      }
      /* 串口 'r' 备用触发 */
      if (Serial.read() == 'r') {
        wake_up();
        serial_trigger = true;
        state = ST_RECORDING;
        Serial.println("RECORDING (serial trigger)...");
        show_recording_off();
      }
      break;
    }
    case ST_READY: {
      /* 串口 'r' 备用触发 */
      if (Serial.read() == 'r') {
        serial_trigger = true;
        state = ST_RECORDING;
        Serial.println("RECORDING (serial trigger)...");
        show_recording_off();
        break;
      }
      /* 触摸按下且在按钮内 (圆心 368/2,448/2, 半径 110) → 开始录音 */
      if (touch_fingers() > 0) {
        int32_t tx = touch_x(), ty = touch_y();
        int dx = tx - LCD_WIDTH/2, dy = ty - LCD_HEIGHT/2;
        if (dx*dx + dy*dy <= 110*110) {
          Serial.printf("hold start (%d,%d)\n", tx, ty);
          state = ST_RECORDING;
          show_recording_off();   /* 录音中熄屏省电 */
        }
      }
      break;
    }
    case ST_RECORDING: {
      uint32_t got = record_to_ram(!serial_trigger);
      serial_trigger = false;
      apply_agc(got / 2);          /* 自动增益: 小声放大, 大声不削波 */
      last_rec_ms = got / 32;   /* 字节 → 毫秒 (16kHz 16bit mono) */
      Serial.printf("recorded %u bytes (%u ms)\n", got, last_rec_ms);
      if (last_rec_ms < 200) {
        /* 太短 (<0.2s) 视为误触, 不发送 */
        Serial.println("too short, discard");
        screen_on();
        show_ready();
        state = ST_READY;
        break;
      }
      wav_len = build_wav(got);
      Serial.printf("WAV total %u bytes\n", wav_len);
      screen_on();
      show_status("UPLOADING...", NULL, RGB565_YELLOW);
      state = ST_UPLOADING;
      break;
    }
    case ST_UPLOADING: {
      if (!upload_via_wifi(wav_len)) {
        Serial.println("WiFi upload failed, fallback to serial...");
        show_status("UPLOAD FAIL", "serial fallback", RGB565_RED);
        send_wav_over_serial(wav_len);
      } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "OK %lu s", last_rec_ms / 1000);
        show_status("UPLOAD OK", buf, RGB565_GREEN);
      }
      Serial.println("UPLOAD_DONE");
      delay(3000);   /* 结果回显 3 秒 */
      sleep_now();   /* 自动回待机 (熄屏省电) */
      break;
    }
  }
  delay(10);
}