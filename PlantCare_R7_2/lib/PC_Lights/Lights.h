#pragma once
// WS2812 灯环 (12 位) + FireBeetle 板载 RGB: 每种颜色一个含义 (缺水黄, 等待蓝, 不缺水绿)
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "pins.h"

enum class LightColor : uint8_t { Off, Yellow, Blue, Green, White, Red };

class Lights {
 public:
  void begin(uint8_t ringBrightness, uint8_t boardBrightness);
  void set(LightColor c);                    // 灯环与板载灯同时显示
  LightColor current() const { return color_; }
  void flash(LightColor c, uint32_t ms);     // 临时闪烁, 到时恢复 set() 的颜色 (需调用 update)
  void flashBoard(LightColor c, uint32_t ms);
  void update(uint32_t now);
  void setRingBrightness(uint8_t b);
  void fillRing(uint8_t r, uint8_t g, uint8_t b);   // 测试用
  void chaseStep(uint32_t color);                    // 测试用: 跑马灯一步

 private:
  static uint32_t rgb(LightColor c);
  void showRing(LightColor c);
  void showBoard(LightColor c);
  Adafruit_NeoPixel ring_{RING_COUNT, PIN_RING, NEO_GRB + NEO_KHZ800};
  Adafruit_NeoPixel board_{1, PIN_BOARD_RGB, NEO_GRB + NEO_KHZ800};
  LightColor color_ = LightColor::Off;
  LightColor flashColor_ = LightColor::Off;
  uint32_t flashUntil_ = 0, flashPeriod_ = 0, flashStart_ = 0;
  bool flashing_ = false, boardOnly_ = false;
  uint8_t chase_ = 0;
};
