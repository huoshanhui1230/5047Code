#pragma once
#include "Arduino.h"
typedef uint8_t u8g2_uint_t;
struct u8g2_cb_t { int dummy; };
extern const u8g2_cb_t u8g2_cb_r0;
#define U8G2_R0 (&u8g2_cb_r0)
#define U8X8_PIN_NONE 255
#define U8G2_DRAW_UPPER_RIGHT 0x01
#define U8G2_DRAW_UPPER_LEFT 0x02
#define U8G2_DRAW_LOWER_LEFT 0x04
#define U8G2_DRAW_LOWER_RIGHT 0x08
#define U8G2_DRAW_ALL (U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT)
extern const uint8_t u8g2_font_6x10_tf[];
extern const uint8_t u8g2_font_5x8_tf[];
extern const uint8_t u8g2_font_6x13B_tf[];
class U8G2 {
 public:
  bool begin(void);
  void setI2CAddress(uint8_t adr);
  void setBusClock(uint32_t clock_speed);
  void setContrast(uint8_t value);
  void setPowerSave(uint8_t is_enable);
  void setFontMode(uint8_t is_transparent);
  void clearBuffer(void);
  void sendBuffer(void);
  void setFont(const uint8_t* font);
  u8g2_uint_t drawStr(u8g2_uint_t x, u8g2_uint_t y, const char* s);
  u8g2_uint_t getStrWidth(const char* s);
  void drawCircle(u8g2_uint_t x0, u8g2_uint_t y0, u8g2_uint_t rad, uint8_t opt = U8G2_DRAW_ALL);
  void drawDisc(u8g2_uint_t x0, u8g2_uint_t y0, u8g2_uint_t rad, uint8_t opt = U8G2_DRAW_ALL);
  void drawLine(u8g2_uint_t x1, u8g2_uint_t y1, u8g2_uint_t x2, u8g2_uint_t y2);
  void drawHLine(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w);
  void drawBox(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h);
  void drawRBox(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h, u8g2_uint_t r);
  void drawEllipse(u8g2_uint_t x0, u8g2_uint_t y0, u8g2_uint_t rx, u8g2_uint_t ry, uint8_t opt = U8G2_DRAW_ALL);
  void drawFilledEllipse(u8g2_uint_t x0, u8g2_uint_t y0, u8g2_uint_t rx, u8g2_uint_t ry, uint8_t opt = U8G2_DRAW_ALL);
  void setDrawColor(uint8_t color_index);
  void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
};
class U8G2_SSD1309_128X64_NONAME0_F_HW_I2C : public U8G2 {
 public:
  U8G2_SSD1309_128X64_NONAME0_F_HW_I2C(const u8g2_cb_t* rotation, uint8_t reset = U8X8_PIN_NONE,
                                       uint8_t clock = U8X8_PIN_NONE, uint8_t data = U8X8_PIN_NONE);
};
class U8G2_SSD1309_128X64_NONAME2_F_HW_I2C : public U8G2 {
 public:
  U8G2_SSD1309_128X64_NONAME2_F_HW_I2C(const u8g2_cb_t* rotation, uint8_t reset = U8X8_PIN_NONE,
                                       uint8_t clock = U8X8_PIN_NONE, uint8_t data = U8X8_PIN_NONE);
};
