#pragma once

// ===================================================================
//  ESP32-S3-Touch-AMOLED-1.8 引脚定义 (V1/V2 通用)
//  官方 v2 包引脚；分辨率 368x448 (以官方 wiki 为准，非 360x360)
//  差异仅在驱动芯片：V2=CO5300+CST816，V1=SH8601+FT3168
// ===================================================================

// 默认编译 V2（最新出厂版本）。要切 V1，注释掉下面这行。
#define BOARD_V2

// ---- 屏幕 QSPI (6 线) ----
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 11
#define LCD_CS   12
#define LCD_WIDTH  368
#define LCD_HEIGHT 448

// ---- 触摸 I2C + 中断 ----
#define IIC_SDA 15
#define IIC_SCL 14
#define TP_INT  21

// ---- XCA9554 触摸电源扩展器 (I2C 地址) ----
#define XCA9554_ADDR 0x20
