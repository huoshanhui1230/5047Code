#pragma once
#include "Arduino.h"
typedef uint16_t neoPixelType;
#define NEO_GRB ((1 << 6) | (1 << 4) | (0 << 2) | (2))
#define NEO_KHZ800 0x0000
class Adafruit_NeoPixel {
 public:
  Adafruit_NeoPixel(uint16_t n, int16_t pin = 6, neoPixelType type = NEO_GRB + NEO_KHZ800);
  void begin(void);
  void show(void);
  void clear(void);
  void setBrightness(uint8_t);
  void fill(uint32_t c = 0, uint16_t first = 0, uint16_t count = 0);
  void setPixelColor(uint16_t n, uint32_t c);
  void setPixelColor(uint16_t n, uint8_t r, uint8_t g, uint8_t b);
  uint16_t numPixels(void) const;
  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
};
