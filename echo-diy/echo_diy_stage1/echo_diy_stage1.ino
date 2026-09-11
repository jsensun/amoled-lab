/*
 * Echo DIY — Stage 1: 验证录音
 * 板载麦克风 → ES8311 ADC → ESP32 I2S RX → 内存 → USB 串口 → 电脑 WAV
 *
 * 关键: I2S 数据输入引脚 DI = GPIO10 (官方 15_ES8311 示例确认)
 *       dot_games 里 setPins(..., -1, ...) 的 -1 就是这里要接的 DI
 *
 * 流程:
 *   上电后自动录音 REC_SECONDS 秒 (16kHz/16bit/单声道)
 *   然后通过 USB 串口以帧协议发送 WAV 数据:
 *     [0xAA 0x55] [4字节长度 LE] [WAV 原始字节]
 *   电脑端用 pc-server/recv_serial.py 接收保存为 .wav
 */
#include <Wire.h>
#include <ESP_I2S.h>
#include "es8311.h"
#include "pin_config.h"

#define AUDIO_MCK_IO 16
#define AUDIO_BCK_IO 9
#define AUDIO_WS_IO  45
#define AUDIO_DO_IO  8
#define AUDIO_DI_IO  10   /* 麦克风数据输入 (关键!) */
#define AUDIO_PA_IO  46
#define AUDIO_SAMPLE_RATE 16000

#define REC_SECONDS 5
#define PCM_BYTES   (AUDIO_SAMPLE_RATE * 2 * REC_SECONDS)  /* 16bit mono */

I2SClass audio_i2s;

/* 用 PSRAM 存 PCM (8MB 足够) */
static int16_t* pcm_buf = NULL;

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
  es8311_microphone_gain_set(es, (es8311_mic_gain_t)3);  /* 增益 3/7 */
  es8311_voice_volume_set(es, 60, NULL);
  return true;
}

/* ---------- 录音到 PSRAM ---------- */
static uint32_t record_to_ram() {
  uint32_t total = 0;
  uint8_t tmp[2048];
  while (total < PCM_BYTES) {
    size_t n = audio_i2s.readBytes((char*)tmp, sizeof(tmp));
    if (n == 0) continue;
    /* 立体声 → 取左声道 (单声道) */
    int16_t* st = (int16_t*)tmp;
    int frames = n / 4;
    for (int i = 0; i < frames && total < PCM_BYTES; i++) {
      pcm_buf[total/2] = st[i*2];   /* 左声道 */
      total += 2;
    }
  }
  return total;
}

/* ---------- 通过 USB 串口发 WAV (帧协议) ---------- */
static void send_wav_over_serial(uint32_t pcm_len) {
  uint8_t hdr[44];
  build_wav_header(hdr, pcm_len);
  uint32_t total = 44 + pcm_len;

  Serial.write(0xAA); Serial.write(0x55);
  Serial.write(total & 0xff); Serial.write((total>>8)&0xff);
  Serial.write((total>>16)&0xff); Serial.write((total>>24)&0xff);
  Serial.write(hdr, 44);
  Serial.write((uint8_t*)pcm_buf, pcm_len);
  Serial.flush();
  Serial.println();
  Serial.println("SEND_DONE");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Echo DIY Stage1: record test");

  pcm_buf = (int16_t*)heap_caps_malloc(PCM_BYTES, MALLOC_CAP_SPIRAM);
  if (!pcm_buf) { Serial.println("PSRAM alloc fail"); return; }
  Serial.printf("PSRAM buf: %d bytes\n", PCM_BYTES);

  if (!audio_init()) { Serial.println("audio init fail"); return; }
  Serial.println("audio ok, recording...");

  uint32_t got = record_to_ram();
  Serial.printf("recorded %u bytes (%u ms)\n", got, got/32);
  send_wav_over_serial(got);
}

void loop() {
  /* 完成后空闲 */
}
