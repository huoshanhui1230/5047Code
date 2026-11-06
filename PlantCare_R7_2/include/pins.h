#pragma once
// PlantCare R7.2 — FireBeetle 2 ESP32-E (DFR0654) 引脚表
// 与《R7 全流程操作指南 V6》附录引脚表一致（GPIO 编号，不是板上 D 编号）。

#define PIN_PROBE        34   // SEN0308 黄线 AOUT -> GPIO34 (ADC1, 只能输入, 无内部上下拉)
#define PIN_PIR          14   // AM312 OUT
#define PIN_BUTTON       13   // "已补水"按钮另一端接 GND, 内部上拉, 按下为 LOW
#define PIN_I2C_SDA      21   // OLED SDA
#define PIN_I2C_SCL      22   // OLED SCL
#define PIN_DF_RX        16   // ESP32 RX  <- DFPlayer TX
#define PIN_DF_TX        17   // ESP32 TX  -> 1 kΩ -> DFPlayer RX
#define PIN_RING         25   // WS2812 灯环 DIN (经 330 Ω; 灯环 5V 经 1N4007)
#define PIN_BOARD_RGB     5   // FireBeetle 板载 RGB (WS2812)

#define RING_COUNT       12
#define OLED_I2C_ADDR  0x3C

// OLED 驱动型号: 0 = U8G2_SSD1309_128X64_NONAME0, 2 = NONAME2
// 屏幕花屏或整体偏移时改成 2 再烧录 (指南排错表)。
#ifndef OLED_VARIANT
#define OLED_VARIANT      0
#endif
