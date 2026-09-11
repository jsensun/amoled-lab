/*
 * Echo DIY — Stage 3: 按钮触发录音 + WiFi 上传
 * 板载麦克风 → ES8311 ADC → I2S RX → PSRAM → WiFi HTTP POST → 电脑 WAV
 *
 * 操作:
 *   上电后待机, 按 BOOT 按钮开始录音 (最多 REC_SECONDS 秒)
 *   录音中再按一次 BOOT 可提前停止
 *   录音结束自动 WiFi 上传到电脑 recv_http.py, 完成后回到待机
 *
 * 串口命令 (测试用):
 *   'r' 触发录音 (等效按按钮)
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
#include "Arduino_GFX_Library.h"
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

#define REC_SECONDS 10    /* 最长录音秒数 (再按一次可提前停止) */
#define PCM_BYTES   (AUDIO_SAMPLE_RATE * 2 * REC_SECONDS)  /* 16bit mono */

#define MIC_GAIN_SOFT 8   /* 软件增益: ES8311 PGA 增益寄存器实测无效, 录音后放大 8x (16x 环境噪声放大明显) */

#define BTN_IO 0          /* BOOT 按钮, 按下为 LOW */

/* 电脑 IP 和端口 (先跑 recv_http.py) */
#define PC_HOST "192.168.3.67"
#define PC_PORT 8080
#define UPLOAD_URL "http://192.168.3.67:8080/upload"

#define WIFI_TIMEOUT_MS 15000

I2SClass audio_i2s;

/* ---------------- 屏幕 (Arduino_GFX, 368x448 CO5300 QSPI) ---------------- */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT, 16, 0, 0, 0);
#define BRIGHTNESS 180

/* 状态显示: 标题 + 副标题 + 颜色 (Arduino_GFX 默认字体无中文, 用英文) */
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
static void show_recording(uint32_t sec) {
  char buf[24];
  uint32_t remain = (sec < REC_SECONDS) ? (REC_SECONDS - sec) : 0;  /* 倒数 */
  snprintf(buf, sizeof(buf), "%lu s", remain);
  show_status("RECORDING...", buf, RGB565_RED);
}

/* 用 PSRAM 存 PCM 和完整 WAV (8MB 足够) */
static int16_t* pcm_buf = NULL;
static uint8_t* wav_buf = NULL;

enum State { ST_IDLE, ST_RECORDING, ST_UPLOADING };
static State state = ST_IDLE;
static uint32_t wav_len = 0;      /* 录音完成后组装的总长度, 供上传使用 */
static uint32_t last_rec_ms = 0;  /* 最近一次录音时长 (毫秒), 供结果回显 */

/* ---------- WAV 头 ---------- */
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

/* ---------- 音频初始化 (录音模式) ---------- */
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
  es8311_microphone_gain_set(es, (es8311_mic_gain_t)6);  /* 增益 36dB (默认18dB太轻, Whisper 识别差) */
  es8311_voice_volume_set(es, 60, NULL);
  return true;
}

/* ---------- BOOT 按钮检测 (带消抖, 返回是否按下) ---------- */
static bool btn_pressed() {
  if (digitalRead(BTN_IO) == LOW) {
    delay(30);
    if (digitalRead(BTN_IO) == LOW) {
      /* 等待松开 */
      while (digitalRead(BTN_IO) == LOW) delay(10);
      return true;
    }
  }
  return false;
}

/* ---------- 录音到 PSRAM (可提前停止, 每秒刷新屏幕计时) ---------- */
static uint32_t record_to_ram() {
  uint32_t total = 0;
  uint8_t tmp[2048];
  uint32_t last_disp = 0;
  while (total < PCM_BYTES) {
    /* 录音中再按一次 BOOT → 提前停止 */
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
    /* 每秒刷新屏幕计时 */
    uint32_t sec = total / 32000;
    if (sec != last_disp) {
      last_disp = sec;
      show_recording(sec);
    }
  }
  return total;
}

/* ---------- 组装完整 WAV 到 wav_buf ---------- */
static uint32_t build_wav(uint32_t pcm_len) {
  build_wav_header(wav_buf, pcm_len);
  memcpy(wav_buf + 44, pcm_buf, pcm_len);
  return 44 + pcm_len;
}

/* ---------- WiFi HTTP POST 上传 ---------- */
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

/* ---------- 回退: 通过 USB 串口发 WAV (帧协议) ---------- */
static void send_wav_over_serial(uint32_t wav_len) {
  Serial.write(0xAA); Serial.write(0x55);
  Serial.write(wav_len & 0xff); Serial.write((wav_len>>8)&0xff);
  Serial.write((wav_len>>16)&0xff); Serial.write((wav_len>>24)&0xff);
  Serial.write(wav_buf, wav_len);
  Serial.flush();
  Serial.println();
  Serial.println("SEND_DONE");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Echo DIY Stage3: button-triggered recorder");

  pinMode(BTN_IO, INPUT_PULLUP);

  /* 屏幕初始化 */
  gfx->begin();
  gfx->setBrightness(BRIGHTNESS);
  gfx->setTextWrap(false);
  show_status("Echo DIY", "booting...", RGB565_WHITE);

  pcm_buf = (int16_t*)heap_caps_malloc(PCM_BYTES, MALLOC_CAP_SPIRAM);
  wav_buf = (uint8_t*)heap_caps_malloc(44 + PCM_BYTES, MALLOC_CAP_SPIRAM);
  if (!pcm_buf || !wav_buf) { Serial.println("PSRAM alloc fail"); show_status("ERROR", "PSRAM alloc fail", RGB565_RED); return; }
  Serial.printf("PSRAM buf: %d + %d bytes\n", PCM_BYTES, 44 + PCM_BYTES);

  if (!audio_init()) { Serial.println("audio init fail"); show_status("ERROR", "audio init fail", RGB565_RED); return; }
  Serial.println("audio ok");
  Serial.printf("IDLE: press BOOT to record (max %ds, press again to stop)\n", REC_SECONDS);
  show_status("Press BOOT", "to record", RGB565_WHITE);
}

void loop() {
  switch (state) {
    case ST_IDLE: {
      /* BOOT 按钮 或 串口 'r' 触发 */
      if (btn_pressed() || Serial.read() == 'r') {
        Serial.println("RECORDING...");
        show_recording(0);
        /* 关键: 等按钮释放再开始录音, 否则触发后未松手会被误判为"提前停止" → 空文件 */
        while (digitalRead(BTN_IO) == LOW) delay(10);
        state = ST_RECORDING;
      }
      break;
    }
    case ST_RECORDING: {
      uint32_t got = record_to_ram();
      last_rec_ms = got / 32;   /* 字节 → 毫秒 (16kHz 16bit mono) */
      Serial.printf("recorded %u bytes (%u ms)\n", got, last_rec_ms);
      wav_len = build_wav(got);
      Serial.printf("WAV total %u bytes\n", wav_len);
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
      delay(3000);   /* 结果回显 3 秒, 让用户看清 */
      Serial.println("IDLE: press BOOT to record again");
      show_status("Press BOOT", "to record", RGB565_WHITE);
      state = ST_IDLE;
      break;
    }
  }
  delay(10);
}